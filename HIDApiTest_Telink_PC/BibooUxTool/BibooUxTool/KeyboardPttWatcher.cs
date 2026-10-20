using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// 监听 USB HID 键盘的「Ctrl+Win 按下 / 修饰符释放后的 Enter」，翻译成语音键的开始/结束事件。
    ///
    /// 对应固件行为（vendor/8258_dual_mouse/AAA_Public.c，BIBOO_UX_K5_CTRL_WIN_MIC）：
    ///   K5 按下 → 键盘报告 ctrl_key=0x09（左 Ctrl | 左 Win，无普通键码）+ 开麦推流；
    ///   K5 松开 → 先发全零报告释放修饰符，再发 Enter 按下(0x28)、Enter 释放。
    /// dongle 把这些报告原样转发到 USB HID 键盘，所以上位机只需监听键盘事件。
    ///
    /// 用 Raw Input（RIDEV_INPUTSINK）而不是全局键盘钩子：
    ///   ① 窗口不在前台也能收到（用户可以在微信/游戏等目标应用里说话）；
    ///   ② 能拿到设备句柄，只认「发出 Ctrl+Win 的同一台设备」的 Enter，
    ///      外接键盘/输入法的 Enter 不会误停录音；
    ///   ③ 不需要注入标记过滤，也不拦截任何按键，不影响正常输入。
    /// </summary>
    internal sealed class KeyboardPttWatcher : IDisposable
    {
        /// <summary>WM_INPUT（窗体 WndProc 用它分流）。</summary>
        public const int WM_INPUT = 0x00FF;

        private const uint RID_INPUT = 0x10000003;
        private const uint RIDI_DEVICENAME = 0x20000007;
        private const uint RIM_TYPEKEYBOARD = 1;
        private const uint RIDEV_INPUTSINK = 0x00000100;

        private const int WM_KEYDOWN = 0x0100;
        private const int WM_KEYUP = 0x0101;
        private const int WM_SYSKEYDOWN = 0x0104;
        private const int WM_SYSKEYUP = 0x0105;

        private const int VK_CONTROL = 0x11;
        private const int VK_LCONTROL = 0xA2;
        private const int VK_RCONTROL = 0xA3;
        private const int VK_LWIN = 0x5B;
        private const int VK_RWIN = 0x5C;
        private const int VK_RETURN = 0x0D;

        /// <summary>Enter 兜底等待：修饰符已释放但 Enter 报告迟迟不到（BLE 断链/丢包）时，
        /// 超过该时间就当作“结束”，避免录音一直挂到 300s 上限。</summary>
        private const int EnterFallbackMs = 400;

        /// <summary>Ctrl+Win 组合成立（开始录音）。</summary>
        public event Action Pressed;

        /// <summary>Ctrl+Win 释放后的 Enter（结束录音）；Enter 丢包时由兜底定时器触发。</summary>
        public event Action Released;

        /// <summary>Raw Input 是否注册成功（远程桌面等场景可能失败）。</summary>
        public bool IsRegistered { get; private set; }

        /// <summary>注册失败原因（已本地化描述），成功时为空。</summary>
        public string LastError { get; private set; }

        /// <summary>最近一次触发组合键的设备路径（\\?\HID#VID_xxxx&PID_xxxx...），仅用于日志。</summary>
        public string LastDeviceName { get; private set; }

        public bool Enabled
        {
            get { return _enabled; }
            set
            {
                if (_enabled == value)
                    return;
                _enabled = value;
                if (!value)
                    Reset();
            }
        }

        private readonly Timer _releaseTimer;
        private bool _ctrlDown;
        private bool _winDown;
        private bool _active;           // Ctrl+Win 手势已成立，尚未收到结束 Enter
        private IntPtr _armedDevice;    // 发出 Ctrl+Win 的设备；只接受同一设备的 Enter
        private int _releaseSeenAt;     // 修饰符全部释放的时刻（TickCount），0 = 未释放
        private bool _enabled = true;
        private bool _disposed;

        public KeyboardPttWatcher(IntPtr hwnd)
        {
            _releaseTimer = new Timer();
            _releaseTimer.Interval = 120;
            _releaseTimer.Tick += OnReleaseTimerTick;

            if (hwnd == IntPtr.Zero)
            {
                LastError = "窗口句柄无效";
                return;
            }

            var device = new RAWINPUTDEVICE();
            device.usUsagePage = 0x01;      // Generic Desktop
            device.usUsage = 0x06;          // Keyboard
            device.dwFlags = RIDEV_INPUTSINK;   // 后台也接收（不要求前台）
            device.hwndTarget = hwnd;

            if (!RegisterRawInputDevices(new[] { device }, 1, (uint)Marshal.SizeOf(typeof(RAWINPUTDEVICE))))
            {
                LastError = "RegisterRawInputDevices 失败（Win32 " + Marshal.GetLastWin32Error() + "）";
                return;
            }
            IsRegistered = true;
        }

        /// <summary>
        /// 处理 WM_INPUT（由窗体 WndProc 调用）。解析失败一律静默，绝不影响 UI 线程。
        /// Windows 在 DefWindowProc 里会清理 raw input 缓冲，所以调用方仍需 base.WndProc。
        /// </summary>
        public void ProcessMessage(IntPtr hRawInput)
        {
            if (!_enabled || _disposed || !IsRegistered || hRawInput == IntPtr.Zero)
                return;

            uint size = 0;
            uint headerSize = (uint)Marshal.SizeOf(typeof(RAWINPUTHEADER));
            uint res = GetRawInputData(hRawInput, RID_INPUT, IntPtr.Zero, ref size, headerSize);
            if (res == unchecked((uint)-1) || size == 0 || size > 1024)
                return;

            IntPtr buf = Marshal.AllocHGlobal((int)size);
            try
            {
                res = GetRawInputData(hRawInput, RID_INPUT, buf, ref size, headerSize);
                if (res == unchecked((uint)-1) || res == 0)
                    return;

                var header = (RAWINPUTHEADER)Marshal.PtrToStructure(buf, typeof(RAWINPUTHEADER));
                if (header.dwType != RIM_TYPEKEYBOARD)
                    return;

                var raw = (RAWINPUT)Marshal.PtrToStructure(buf, typeof(RAWINPUT));
                HandleKey(header.hDevice, raw.keyboard);
            }
            catch
            {
                // Raw Input 结构解析异常：丢掉这一帧即可。
            }
            finally
            {
                Marshal.FreeHGlobal(buf);
            }
        }

        private void HandleKey(IntPtr device, RAWKEYBOARD kb)
        {
            bool down = kb.Message == WM_KEYDOWN || kb.Message == WM_SYSKEYDOWN;
            bool up = kb.Message == WM_KEYUP || kb.Message == WM_SYSKEYUP;
            if (!down && !up)
                return;

            int vk = kb.VKey;

            if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL)
            {
                _ctrlDown = down;
                if (down) TryArm(device);
                else OnModifierReleased();
                return;
            }
            if (vk == VK_LWIN || vk == VK_RWIN)
            {
                _winDown = down;
                if (down) TryArm(device);
                else OnModifierReleased();
                return;
            }

            if (vk == VK_RETURN && down)
            {
                // 严格条件：组合键成立 + 修饰符已全部释放 + Enter 来自同一设备。
                // 三点同时满足才是固件发的那一发结束 Enter，其它 Enter 一律不碰。
                if (_active && !_ctrlDown && !_winDown && device == _armedDevice)
                {
                    FinishGesture();
                    Released?.Invoke();
                }
            }
        }

        /// <summary>Ctrl 与 Win 都处于按下状态时建立手势（两者通常在同一条 HID 报告里）。</summary>
        private void TryArm(IntPtr device)
        {
            if (!_enabled || _active || !_ctrlDown || !_winDown)
                return;

            _active = true;
            _armedDevice = device;
            _releaseSeenAt = 0;
            _releaseTimer.Stop();
            LastDeviceName = QueryDeviceName(device);
            Pressed?.Invoke();
        }

        /// <summary>修饰符释放：开始等 Enter；若 Enter 报告丢了，兜底定时器结束这段录音。</summary>
        private void OnModifierReleased()
        {
            if (!_active || _ctrlDown || _winDown)
                return;
            _releaseSeenAt = Environment.TickCount;
            _releaseTimer.Start();
        }

        private void OnReleaseTimerTick(object sender, EventArgs e)
        {
            if (!_active || _releaseSeenAt == 0)
            {
                _releaseTimer.Stop();
                return;
            }

            int dt = unchecked(Environment.TickCount - _releaseSeenAt);
            if (dt < 0)
                dt = EnterFallbackMs;   // TickCount 溢出边界：直接按超时处理
            if (dt < EnterFallbackMs)
                return;

            // 修饰符早已释放却迟迟没有 Enter：按“结束”兜底，避免录音一直挂到上限。
            FinishGesture();
            Released?.Invoke();
        }

        private void FinishGesture()
        {
            _active = false;
            _armedDevice = IntPtr.Zero;
            _releaseSeenAt = 0;
            _releaseTimer.Stop();
        }

        private void Reset()
        {
            _ctrlDown = false;
            _winDown = false;
            FinishGesture();
        }

        public void Dispose()
        {
            if (_disposed)
                return;
            _disposed = true;
            _enabled = false;
            Reset();
            try
            {
                _releaseTimer.Tick -= OnReleaseTimerTick;
                _releaseTimer.Stop();
                _releaseTimer.Dispose();
            }
            catch
            {
                // 关闭竞态：忽略。
            }
            Pressed = null;
            Released = null;
        }

        /// <summary>取设备接口路径（\\?\HID#VID_xxxx&PID_xxxx...），失败返回空串。</summary>
        private static string QueryDeviceName(IntPtr device)
        {
            if (device == IntPtr.Zero)
                return string.Empty;
            try
            {
                uint size = 0;
                uint res = GetRawInputDeviceInfo(device, RIDI_DEVICENAME, IntPtr.Zero, ref size);
                if (res == unchecked((uint)-1) || size == 0 || size > 4096)
                    return string.Empty;

                IntPtr buf = Marshal.AllocHGlobal((int)size * 2);
                try
                {
                    res = GetRawInputDeviceInfo(device, RIDI_DEVICENAME, buf, ref size);
                    if (res == unchecked((uint)-1))
                        return string.Empty;
                    return Marshal.PtrToStringUni(buf) ?? string.Empty;
                }
                finally
                {
                    Marshal.FreeHGlobal(buf);
                }
            }
            catch
            {
                return string.Empty;
            }
        }

        // ---------- Win32 ----------

        [StructLayout(LayoutKind.Sequential)]
        private struct RAWINPUTDEVICE
        {
            public ushort usUsagePage;
            public ushort usUsage;
            public uint dwFlags;
            public IntPtr hwndTarget;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RAWINPUTHEADER
        {
            public uint dwType;
            public uint dwSize;
            public IntPtr hDevice;
            public IntPtr wParam;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RAWKEYBOARD
        {
            public ushort MakeCode;
            public ushort Flags;
            public ushort Reserved;
            public ushort VKey;
            public uint Message;
            public IntPtr ExtraInformation;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RAWINPUT
        {
            public RAWINPUTHEADER header;
            public RAWKEYBOARD keyboard;
        }

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool RegisterRawInputDevices(
            [In] RAWINPUTDEVICE[] pRawInputDevices, uint uiNumDevices, uint cbSize);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern uint GetRawInputData(
            IntPtr hRawInput, uint uiCommand, IntPtr pData, ref uint pcbSize, uint cbSizeHeader);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern uint GetRawInputDeviceInfo(
            IntPtr hDevice, uint uiCommand, IntPtr pData, ref uint pcbSize);
    }
}
