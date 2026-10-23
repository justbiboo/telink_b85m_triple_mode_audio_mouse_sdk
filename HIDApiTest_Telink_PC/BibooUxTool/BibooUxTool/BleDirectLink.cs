using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Storage.Streams;

namespace BibooUxTool
{
    /// <summary>
    /// PC 直连 BLE 引擎（Surface 0：PC ↔ DUT 鼠标），与 DongleLink（Surface 1：PC ↔ dongle HID）平行。
    /// 职责：扫描附近 BLE 外设 / 连接鼠标 / （按需）配对 / 发现 SPP 服务(0x33F3 写, 0x33F4 notify) /
    /// 写帧(0x33F3, write-without-response) 与订阅 notify(0x33F4)。
    ///
    /// 依赖：WinRT Windows.Devices.Bluetooth（net48 桌面需 Microsoft.Windows.SDK.Contracts 包）。
    /// 注意：本类基于 WinRT 异步 API，所有方法均为 async；UI 线程请用 async void 事件处理器调用。
    /// </summary>
    public sealed class BleDirectLink : IDisposable
    {
        private BluetoothLEAdvertisementWatcher _watcher;
        private DeviceWatcher _pairedWatcher;      // 系统已配对设备枚举（AEP watcher，免扫描主入口）
        private readonly Dictionary<string, BleDeviceInfo> _pairedCache = new Dictionary<string, BleDeviceInfo>();
        private Timer _scanTimer;
        private BluetoothLEDevice _device;
        private GattSession _session;            // 保持 BLE 链路（MaintainConnection=true）
        private GattCharacteristic _writeChar;   // 0x33F3 (C2S)
        private GattCharacteristic _notifyChar;  // 0x33F4 (S2C)
        private GattCharacteristic _otaChar;     // Telink OTA write 特征（128-bit，OTA 升级用）
        private Guid _otaCharUuid;               // 实际命中的 UUID 形式（规范/兜底两种之一）
        // 诊断：设备上出现的自定义（非标准 base）128-bit UUID，OTA 特征没找到时打进日志
        private readonly List<string> _gattCustomUuids = new List<string>();
        // 诊断：本次枚举到的服务 UUID 列表（判断「连错设备 / 服务被系统隐藏」用）
        private readonly List<string> _gattServiceUuids = new List<string>();
        private readonly object _sync = new object();

        /// <summary>建链过程中为 true：此时不上报「已连上」，等 SPP 就绪后由 ConnectAsync 统一上报。</summary>
        private volatile bool _settling;

        /// <summary>
        /// 用户主动断开（SoftRelease）标志：置位后 IsConnected 变 false（UI 恢复未连接）、
        /// 活性巡检停走（不读不写计数）、掉线回调只同步 UI 不清理对象 —— 设备/会话/特征
        /// 引用全部保留，供重连时复用（Cached 缓存视图原样，秒级重新接入）。
        /// 重连 AttachAsync 成功或彻底 ReleaseDevice 时复位。
        /// </summary>
        private volatile bool _userReleased;

        /// <summary>等待 SPP 命令回包的一次性令牌。</summary>
        private volatile TaskCompletionSource<byte[]> _pingTcs;

        /// <summary>正在等待回包的命令字（0x78 查鉴权状态 / 0x01 DUT PING）。</summary>
        private volatile byte _pingCmd;

        /// <summary>自动巡检的 Ping 回包不进日志（避免每 2.5s 刷屏），手动点【发送】/【Ping 校验】不受影响。</summary>
        private volatile bool _pingQuiet;

        /// <summary>Ping 并发保护（1=有 Ping 在途）。</summary>
        private int _pingBusy;

        /// <summary>
        /// ATT 活性读探针：优先 0x33F4（SPP notify value，鼠标 ATT 表里是 READ 权限，
        /// 任何固件都允许读），退化时用发现到的第一个可读特征（如电池电量 0x2A19）。
        /// </summary>
        private GattCharacteristic _probeChar;
        private string _probeName;

        /// <summary>
        /// DevInf_aaa 特征（15B，READ）：鼠标把当前版本号写在这里
        /// （固件 vendor/8208_dual_mouse/AAA_app_att.c，挂在 OTA 服务下，handle 0x0010）。
        /// 只读、无需配对加密、不依赖广播 —— 「读当前版本号」走它最可靠。
        /// 见 <see cref="ReadDevInfoAsync"/>。
        /// </summary>
        private GattCharacteristic _devInfoChar;

        /// <summary>
        /// 语音流 notify 特征（my_Data 0xB03E @ service 0xFFF0，vendor/user/voice_dec/ux_voice_dec.c）：
        /// 鼠标把 16kHz/mono PCM 编成 57B mSBC 帧（7.5ms/帧，120 样本）经它 notify 给 PC
        /// （MTU exchange 到 0x009e=158 后单帧一个 notify）。「实时翻译」页的蓝牙语音输入走这里，
        /// 与 SPP 0x33F3/0x33F4、OTA 特征都无关。CCCD = value handle + 1（ux_voice_dec 显式注册）。
        /// 订阅由 <see cref="StartVoiceAsync"/> 显式控制（与 USB 采集二选一，不自动开流）。
        /// </summary>
        private GattCharacteristic _voiceChar;
        private volatile bool _voiceStreaming;

        /// <summary>连续活性校验失败次数，达到 VerifyFailLimit 即判定链路已断。</summary>
        private volatile int _verifyFailStreak;

        /// <summary>连续失败多少次才算「链路真的没了」。2 次 ≈ 5s，兼顾灵敏与抗抖动。</summary>
        private const int VerifyFailLimit = 2;

        /// <summary>最近一次连接的地址与地址类型：断开后回读系统真实链路状态用。</summary>
        private ulong _lastAddress;
        private BluetoothAddressType _lastAddrType;

        // 扫描诊断统计（用于定位「扫不到设备」）
        private int _rawAdvCount;                                   // 收到的广播条数（未过滤）
        private readonly HashSet<ulong> _matched = new HashSet<ulong>();   // 通过过滤的 MAC（去重）
        private readonly HashSet<ulong> _unnamed = new HashSet<ulong>();   // 无名称 MAC（去重，用于统计）
        private readonly Dictionary<ulong, DateTime> _lastRaise =
            new Dictionary<ulong, DateTime>();                      // 每 MAC 的节流时间戳
        private volatile bool _selfStopping;                        // 主动 StopScan 标志

        /// <summary>
        /// 低层链路状态：仅代表 Windows 认为「已连接」。
        /// 注意：WinRT 会在链路其实不可用时也报 Connected（缓存/幽灵条目），
        /// 所以这个属性**不能**用来点亮 UI 的「已连接」。
        /// </summary>
        public bool IsLinkUp
        {
            get
            {
                lock (_sync)
                {
                    return _device != null &&
                           _device.ConnectionStatus == BluetoothConnectionStatus.Connected;
                }
            }
        }

        /// <summary>
        /// 「真连接」判据：链路在 + SPP 特征齐 + ATT 活性读未累计失败。
        /// UI 只应使用本属性，从根上杜绝「显示已连上、实际没连上」。
        /// </summary>
        public bool IsConnected
        {
            get
            {
                lock (_sync)
                {
                    return !_userReleased &&
                           _device != null &&
                           _device.ConnectionStatus == BluetoothConnectionStatus.Connected &&
                           _writeChar != null &&
                           _notifyChar != null &&
                           _verifyFailStreak < VerifyFailLimit;
                }
            }
        }

        /// <summary>
        /// 「OTA 通道就绪」判据：链路在 + OTA 写特征在 + 活性读未累计失败。
        /// 与 <see cref="IsConnected"/> 的唯一区别：**不要求** SPP 0x33F3/0x33F4 ——
        /// OTA 只写 128-bit OTA 特征，SPP 在不在与升级能力无关（8208 量产固件实测
        /// 有时枚举不到 SPP 服务，但那不代表链路不通、更不代表不能升级）。
        /// 「固件升级」页必须用本属性，不能用 IsConnected。
        /// </summary>
        public bool IsOtaReady
        {
            get
            {
                lock (_sync)
                {
                    return !_userReleased &&
                           _device != null &&
                           _device.ConnectionStatus == BluetoothConnectionStatus.Connected &&
                           _otaChar != null &&
                           _verifyFailStreak < VerifyFailLimit;
                }
            }
        }

        /// <summary>
        /// 「链路就绪（不绑定具体特征）」判据：链路在 + ATT 活性读未累计失败。
        /// 与 <see cref="IsConnected"/> 的区别：**不要求** SPP 0x33F3/0x33F4；
        /// 与 <see cref="IsOtaReady"/> 的区别：**不要求** OTA 写特征。
        /// 用于「实时翻译」页的蓝牙输入：后续语音流走 0xFFF0/0xB03E（与 SPP/OTA 都无关），
        /// 连接与否只应看链路活性，不应被某个特征的缺失误判成「未连接」。
        /// </summary>
        public bool IsLinkVerified
        {
            get
            {
                lock (_sync)
                {
                    return !_userReleased &&
                           _device != null &&
                           _device.ConnectionStatus == BluetoothConnectionStatus.Connected &&
                           _verifyFailStreak < VerifyFailLimit;
                }
            }
        }

        /// <summary>链路被判定的断开原因（供 UI 说明「为什么掉了」）。</summary>
        public string LastDropReason { get; private set; }

        /// <summary>是否已发现语音流特征（0xFFF0/0xB03E）。发现即具备启用条件，不等于在收流。</summary>
        public bool HasVoiceChar
        {
            get { lock (_sync) { return _voiceChar != null; } }
        }

        /// <summary>语音 notify（CCCD）当前是否处于订阅态（<see cref="StartVoiceAsync"/> 成功后才为 true）。</summary>
        public bool IsVoiceStreaming { get { return _voiceStreaming; } }

        /// <summary>最近一次连接后的链路活性校验结果：null = 通过；否则为失败原因。</summary>
        public string LastVerifyNote { get; private set; }

        /// <summary>最近一次 ATT 活性读的说明（读的是哪个特征、多少字节）；null = 尚未校验。</summary>
        public string LastLivenessNote { get; private set; }

        /// <summary>最近一次 SPP 命令往返探测结论（仅说明用，不参与「已连接」判定）。</summary>
        public string LastSppCmdNote { get; private set; }

        /// <summary>最近一次 SPP 命令回包的首字节（0x78 / 0x01）。</summary>
        public byte LastSppRspCmd { get; private set; }

        /// <summary>最近一次 Ping 返回的固件版本 / 型号 / 鼠标数据长度。</summary>
        public byte LastPingVersion { get; private set; }
        public byte LastPingModel { get; private set; }
        public byte LastPingMouseLen { get; private set; }

        /// <summary>最近一次读回的滑动参数 valid 标志（1=已配置；0=未配置，固件走编译默认值）。</summary>
        public byte LastSlideValid { get; private set; }

        /// <summary>最近一次读回的滑动参数（12 字节 v2：上下滑动6B+左右滑动6B，各 [fwd_iv_lo,fwd_iv_hi,ret_iv_lo,ret_iv_hi,step_len,step_cnt]）。</summary>
        public byte[] LastSlideParam { get; private set; }

        /// <summary>扫描到设备（同一 MAC 最快 400ms 回调一次）。</summary>
        public event Action<BleDeviceInfo> DeviceDiscovered;

        /// <summary>SPP notify(0x33F4) 收到的帧（≤20B）。</summary>
        public event Action<byte[]> SppReceived;

        /// <summary>
        /// 语音流 notify(0xFFF0/0xB03E) 收到的 mSBC 帧（57B/帧 → 120 样本 @16kHz/mono）。
        /// 订阅由 <see cref="StartVoiceAsync"/> 控制；未订阅时不会触发。
        /// 注意：每个 notify 即一整帧（MTU 158 下单帧 57B 不分片）；长度≠57 的 notify
        /// 视为链路/MTU 异常，调用方应丢弃重新对齐。
        /// </summary>
        public event Action<byte[]> VoiceReceived;

        /// <summary>语音特征/订阅的最近一次说明（未发现特征/订阅失败原因）；null = 正常。</summary>
        public string LastVoiceNote { get; private set; }

        /// <summary>连接状态变化（true=连上 / false=断开）。</summary>
        public event Action<bool> ConnectionChanged;

        /// <summary>扫描结束：(收到的广播条数, 去重后匹配设备数, 因无名称跳过的设备数, 停止原因)。</summary>
        public event Action<int, int, int, string> ScanCompleted;

        // ---------- 扫描 ----------
        /// <summary>开始扫描。nameFilter 为空/空白 = 不过滤；hideUnnamed=true 时丢弃广播名称为空的设备。</summary>
        public void StartScan(string nameFilter = null, int timeoutMs = 10000, bool hideUnnamed = false)
        {
            StopScan();

            _rawAdvCount = 0;
            _matched.Clear();
            _unnamed.Clear();
            _lastRaise.Clear();
            _selfStopping = false;
            _hideUnnamed = hideUnnamed;

            var watcher = new BluetoothLEAdvertisementWatcher();
            watcher.ScanningMode = BluetoothLEScanningMode.Active; // 主动扫描：带回 scan response（含名称）
            watcher.Received += OnAdvertisementReceived;
            watcher.Stopped += OnWatcherStopped;
            _watcher = watcher;
            _nameFilter = string.IsNullOrWhiteSpace(nameFilter) ? null : nameFilter.Trim();

            try
            {
                watcher.Start();
            }
            catch (Exception ex)
            {
                _watcher = null;
                ScanCompleted?.Invoke(0, 0, 0, "启动失败：" + ex.GetType().Name + " " + ex.Message);
                return;
            }

            if (timeoutMs > 0)
                _scanTimer = new Timer(_ => StopScan(), null, timeoutMs, Timeout.Infinite);
        }

        private string _nameFilter;
        private bool _hideUnnamed;

        private void OnAdvertisementReceived(BluetoothLEAdvertisementWatcher sender,
            BluetoothLEAdvertisementReceivedEventArgs a)
        {
            _rawAdvCount++;

            string name = GetLocalName(a.Advertisement);
            string filter = _nameFilter;
            if (filter != null &&
                (name == null || name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) < 0))
                return; // 被名称过滤掉（原始条数仍会计入 _rawAdvCount）

            // 无名称设备：按 MAC 去重计数后丢弃（注意 scan response 可能比 ADV 晚到，
            // 若后续事件带上了名称，该设备仍会被正常列出）。
            if (_hideUnnamed && string.IsNullOrWhiteSpace(name))
            {
                _unnamed.Add(a.BluetoothAddress);
                return;
            }

            _matched.Add(a.BluetoothAddress);

            // 节流：同一 MAC 最快 400ms 回调一次，避免刷爆 UI
            DateTime now = DateTime.Now;
            DateTime last;
            if (_lastRaise.TryGetValue(a.BluetoothAddress, out last) &&
                (now - last).TotalMilliseconds < 400)
                return;
            _lastRaise[a.BluetoothAddress] = now;

            DeviceDiscovered?.Invoke(new BleDeviceInfo
            {
                BluetoothAddress = a.BluetoothAddress,
                Name = name,
                Rssi = a.RawSignalStrengthInDBm,
                LastSeen = now,
                AddressType = a.BluetoothAddressType,
                FwVersion = ParseFwVersion(a.Advertisement)
            });
        }

        /// <summary>
        /// 从广播 Manufacturer Data 解析固件版本（如 "v2.8"）。
        /// 固件广播格式（vendor/8208_dual_mouse/AAA_app.c）：
        ///   AD 0xFF：len=5, type=0xFF, company_id(2B 小端), FW_VER_MAJOR, FW_VER_MINOR
        /// 即 ManufacturerData.Data 恰好 2 字节 = [major, minor]。
        /// 收不到（已配对列表/未广播）返回 null。
        /// </summary>
        private static string ParseFwVersion(BluetoothLEAdvertisement adv)
        {
            if (adv == null)
                return null;
            try
            {
                foreach (var md in adv.ManufacturerData)
                {
                    var dr = DataReader.FromBuffer(md.Data);
                    if (dr.UnconsumedBufferLength < 2)
                        continue;                       // 只要 [major][minor] 两字节
                    byte major = dr.ReadByte();
                    byte minor = dr.ReadByte();
                    if (major > 99 || minor > 99)
                        continue;                       // 明显不是版本号，跳过
                    return "v" + major + "." + minor;
                }
            }
            catch { }
            return null;
        }

        private void OnWatcherStopped(BluetoothLEAdvertisementWatcher sender,
            BluetoothLEAdvertisementWatcherStoppedEventArgs args)
        {
            string reason;
            if (args.Error == BluetoothError.Success)
                reason = _selfStopping ? "正常结束" : "已停止（未主动停止）";
            else if (args.Error == BluetoothError.RadioNotAvailable)
                reason = "蓝牙适配器不可用（Radio 关闭或无机载蓝牙）";
            else
                reason = "系统中止：" + args.Error;

            ScanCompleted?.Invoke(_rawAdvCount, _matched.Count, _unnamed.Count, reason);
        }

        /// <summary>取广播名称：优先 LocalName，为空则手工解析 AD 0x08/0x09。</summary>
        private static string GetLocalName(BluetoothLEAdvertisement adv)
        {
            if (adv == null)
                return null;
            string n = adv.LocalName;
            if (!string.IsNullOrEmpty(n))
                return n;

            foreach (var sec in adv.DataSections)
            {
                if (sec.DataType != 0x08 && sec.DataType != 0x09)
                    continue;
                try
                {
                    var dr = DataReader.FromBuffer(sec.Data);
                    string s = dr.ReadString(sec.Data.Length);
                    if (!string.IsNullOrEmpty(s))
                        return s.Trim('\0', ' ');
                }
                catch { }
            }
            return null;
        }

        public void StopScan()
        {
            var t = _scanTimer;
            if (t != null)
            {
                try { t.Dispose(); } catch { }
                _scanTimer = null;
            }
            var w = _watcher;
            if (w != null)
            {
                _selfStopping = true;   // 让 Stopped 回调知道是主动停止
                _watcher = null;
                try { w.Stop(); } catch { }
            }
        }

        // ---------- 系统已配对设备列表（主入口：免扫描）----------
        /// <summary>已配对设备列表变化类型。</summary>
        public enum PairedChangeKind { Added, Updated, Removed }

        /// <summary>
        /// 已配对设备列表变化。Added = 初始枚举/新配对；Updated = 连接状态等属性变化；Removed = 删除配对。
        /// 注意：watcher 线程池回调，UI 侧需自行 Marshall。
        /// </summary>
        public event Action<BleDeviceInfo, PairedChangeKind> PairedDeviceChanged;

        /// <summary>已配对设备枚举的状态/错误（供 UI 打日志）。</summary>
        public event Action<string> PairedWatchStateChanged;

        private static readonly string[] PairedProps = new string[]
        {
            "System.Devices.Aep.IsConnected",
            "System.Devices.Aep.IsPresent",
            "System.Devices.Aep.DeviceAddress"
        };

        /// <summary>
        /// 开始枚举「系统已配对」BLE 设备并监听连接状态变化。
        /// 这就是 Windows「蓝牙和其他设备」列表的数据源（AEP watcher）——
        /// 主流程不再要求用户先扫描：列表自动加载、连接状态实时刷新、选中即可回连。
        /// </summary>
        public void StartPairedWatch()
        {
            StopPairedWatch();
            lock (_pairedCache) _pairedCache.Clear();

            string selector;
            try
            {
                selector = BluetoothLEDevice.GetDeviceSelectorFromPairingState(true);
            }
            catch (Exception ex)
            {
                PairedWatchStateChanged?.Invoke("获取已配对设备选择器失败（" + ex.GetType().Name + "）：" + ex.Message);
                return;
            }

            try
            {
                var watcher = DeviceInformation.CreateWatcher(selector, PairedProps,
                    DeviceInformationKind.AssociationEndpoint);
                watcher.Added += OnPairedAdded;
                watcher.Updated += OnPairedUpdated;
                watcher.Removed += OnPairedRemoved;
                _pairedWatcher = watcher;
                watcher.Start();
            }
            catch (Exception ex)
            {
                PairedWatchStateChanged?.Invoke("启动已配对设备枚举失败（" + ex.GetType().Name + "）：" + ex.Message);
                return;
            }

            // 兜底快照：个别系统上 watcher 初始枚举可能延迟，用 FindAll 再兜一次
            // （按 DeviceId 去重，重复上报不会产生重复行）。故意不 await：让 watcher 先跑起来。
            _ = LoadPairedSnapshotAsync(selector);
        }

        public void StopPairedWatch()
        {
            var w = _pairedWatcher;
            if (w == null)
                return;
            _pairedWatcher = null;
            try { w.Added -= OnPairedAdded; } catch { }
            try { w.Updated -= OnPairedUpdated; } catch { }
            try { w.Removed -= OnPairedRemoved; } catch { }
            try { w.Stop(); } catch { }
        }

        /// <summary>重启枚举（UI【刷新】用）。</summary>
        public void RestartPairedWatch()
        {
            StartPairedWatch();
        }

        private async Task LoadPairedSnapshotAsync(string selector)
        {
            try
            {
                DeviceInformationCollection dis =
                    await DeviceInformation.FindAllAsync(selector, PairedProps);
                if (dis == null)
                    return;
                foreach (DeviceInformation di in dis)
                    RaisePaired(di, PairedChangeKind.Added);
            }
            catch (Exception ex)
            {
                PairedWatchStateChanged?.Invoke("读取已配对设备列表失败（" + ex.GetType().Name + "）：" + ex.Message);
            }
        }

        private void OnPairedAdded(DeviceWatcher sender, DeviceInformation di)
        {
            RaisePaired(di, PairedChangeKind.Added);
        }

        private void OnPairedUpdated(DeviceWatcher sender, DeviceInformationUpdate du)
        {
            BleDeviceInfo info;
            bool found;
            lock (_pairedCache) { found = _pairedCache.TryGetValue(du.Id, out info); }
            if (!found)
                return;   // 没见过的设备先忽略（等 Added 建立缓存）
            bool conn = ReadBoolProp(du.Properties, "System.Devices.Aep.IsConnected", info.SystemConnected);
            if (conn == info.SystemConnected)
                return;
            info.SystemConnected = conn;
            PairedDeviceChanged?.Invoke(info, PairedChangeKind.Updated);
        }

        private void OnPairedRemoved(DeviceWatcher sender, DeviceInformationUpdate du)
        {
            BleDeviceInfo info;
            bool found;
            lock (_pairedCache)
            {
                found = _pairedCache.TryGetValue(du.Id, out info);
                if (found)
                    _pairedCache.Remove(du.Id);
            }
            if (found)
                PairedDeviceChanged?.Invoke(info, PairedChangeKind.Removed);
        }

        private void RaisePaired(DeviceInformation di, PairedChangeKind kind)
        {
            var info = new BleDeviceInfo
            {
                DeviceId = di.Id,
                Name = string.IsNullOrWhiteSpace(di.Name) ? null : di.Name,
                LastSeen = DateTime.Now
            };
            ulong addr;
            if (TryParsePairedAddress(di, out addr))
                info.BluetoothAddress = addr;
            info.SystemConnected = ReadBoolProp(di.Properties, "System.Devices.Aep.IsConnected", false);

            lock (_pairedCache) { _pairedCache[info.DeviceId] = info; }
            PairedDeviceChanged?.Invoke(info, kind);
        }

        private static bool ReadBoolProp(System.Collections.Generic.IReadOnlyDictionary<string, object> props,
                                         string key, bool fallback)
        {
            object v;
            if (props != null && props.TryGetValue(key, out v) && v is bool)
                return (bool)v;
            return fallback;
        }

        /// <summary>尽力解析已配对设备的 MAC（仅用于显示；连接走 DeviceId，不依赖它）。</summary>
        private static bool TryParsePairedAddress(DeviceInformation di, out ulong address)
        {
            address = 0;
            object v;
            if (di.Properties != null && di.Properties.TryGetValue("System.Devices.Aep.DeviceAddress", out v))
            {
                string s = v as string;
                if (!string.IsNullOrEmpty(s) && TryParseMacTail(s, out address))
                    return true;
            }
            // Id 形如 BluetoothLE#BluetoothLEe4:5f:01:39:a2:b1：取最后 12 个十六进制字符
            return TryParseMacTail(di.Id, out address);
        }

        /// <summary>从字符串尾部收集 12 个十六进制字符（跳过 ':' '-' 等分隔符）解析为 48 位地址。</summary>
        private static bool TryParseMacTail(string s, out ulong address)
        {
            address = 0;
            if (string.IsNullOrEmpty(s))
                return false;
            var sb = new System.Text.StringBuilder(12);
            for (int i = s.Length - 1; i >= 0 && sb.Length < 12; i--)
            {
                char c = s[i];
                if (Uri.IsHexDigit(c))
                    sb.Insert(0, c);
            }
            if (sb.Length != 12)
                return false;
            return ulong.TryParse(sb.ToString(), System.Globalization.NumberStyles.HexNumber,
                System.Globalization.CultureInfo.InvariantCulture, out address);
        }

        /// <summary>
        /// 当前已接入设备的 AEP Id（已配对列表显示「已接入」用）；未接入为 null。
        /// </summary>
        public string ConnectedDeviceId
        {
            get
            {
                lock (_sync)
                {
                    if (_device == null || _device.BluetoothDeviceId == null)
                        return null;
                    return _device.BluetoothDeviceId.Id;
                }
            }
        }

        // ---------- 连接 ----------
        /// <summary>连接指定地址的 BLE 设备并发现 SPP 特征。成功返回 null；失败返回错误描述。</summary>
        /// <param name="requireSpp">
        /// true（默认）= 必须找到 SPP 0x33F3/0x33F4 才算连上（收发调试页）；
        /// false = OTA 专用：没有 SPP 但找到 128-bit OTA 写特征同样算连上。
        /// </param>
        public async Task<string> ConnectAsync(ulong address,
            BluetoothAddressType addrType = BluetoothAddressType.Unspecified,
            bool requireSpp = true)
        {
            BluetoothLEDevice existing;
            lock (_sync) { existing = _device; }
            if (existing != null)
            {
                if (IsConnected)
                    return "已连接";
                // 上一次尝试留下的半成品状态（设备对象在、链路却是断的）：
                // 必须先清理，否则会一直误判为「已连接」而无法重连。
                ReleaseDevice();
            }

            _verifyFailStreak = 0;
            LastDropReason = null;
            // 记住地址：断开后要回读系统真实链路状态，向用户说明「PC 侧释放 ≠ 链路已断」。
            lock (_sync) { _lastAddress = address; _lastAddrType = addrType; }

            BluetoothLEDevice dev;
            try
            {
                // 随机地址（Random）设备必须带上地址类型，否则取到的设备对象可能
                // 指向一个已失效的地址，GATT 操作必然失败。
                dev = await BluetoothLEDevice.FromBluetoothAddressAsync(address, addrType);
            }
            catch (Exception ex)
            {
                return "创建设备对象异常（" + ex.GetType().Name + "）：" + ex.Message;
            }
            if (dev == null)
                return "找不到设备（未配对且不在系统缓存：请先扫描或先配对）";

            dev.ConnectionStatusChanged += OnConnStatusChanged;
            lock (_sync) { _device = dev; }

            // 建链期间屏蔽「已连上」上报：WinRT 在枚举服务的瞬间就会把 ConnectionStatus
            // 置为 Connected，若此时就点亮 UI，用户会看到「显示已连接、其实还没建立完」。
            _settling = true;
            LastVerifyNote = null;

            try
            {
                // 设备对象已就绪 → 走公共 attach 流程（会话保链 → Uncached 枚举 → SPP → notify → 活性读）。
                string err = await AttachAsync(dev, true, requireSpp);
                if (err != null)
                {
                    ReleaseDevice();
                    return err;
                }
            }
            catch (Exception ex)
            {
                ReleaseDevice();
                return "连接异常（" + ex.GetType().Name + "）：" + ex.Message;
            }

            _settling = false;
            ConnectionChanged?.Invoke(true);
            return null;
        }

        /// <summary>
        /// 连接「系统已配对列表」里的设备（UI 主流程：选中 → 连接/回连 → 发现 SPP → 收发）。
        /// 与 <see cref="ConnectAsync"/>（扫描 → 地址直连）的区别：
        /// ① 入口用 AEP DeviceId（FromIdAsync），不需要地址类型、不怕随机地址轮换；
        /// ② 实际建链由 Windows 对已配对设备自动完成，本工具负责等链 + 接入 + 活性校验。
        /// 成功返回 null；失败返回原因。
        /// </summary>
        /// <param name="requireSpp">同 <see cref="ConnectAsync"/>：OTA 专用连接传 false。</param>
        public async Task<string> ConnectByIdAsync(string deviceId, int connectTimeoutMs = 10000,
                                                   bool requireSpp = true)
        {
            if (string.IsNullOrEmpty(deviceId))
                return "设备 Id 为空（该条目不是已配对设备）";

            // 已接入且活性通过 = 目标状态已达成
            BluetoothLEDevice cur;
            lock (_sync) { cur = _device; }
            if (cur != null && cur.BluetoothDeviceId != null &&
                cur.BluetoothDeviceId.Id == deviceId && IsConnected)
                return null;

            // 温和断开（SoftRelease）后重连：设备对象仍保留——若系统链路仍在，直接复用
            // （Cached 缓存视图原样命中，秒级重新接入）；复用失败或链路已断则彻底释放，
            // 继续走下方全新连接。不复用的话，Dispose 过的缓存通道重连会稳定失败
            // （Cached 空 + Uncached 与系统驱动争用 → 「未发现 SPP 特征」）。
            BluetoothLEDevice kept;
            lock (_sync) { kept = _device; }
            if (kept != null && kept.BluetoothDeviceId != null &&
                kept.BluetoothDeviceId.Id == deviceId &&
                kept.ConnectionStatus == BluetoothConnectionStatus.Connected)
            {
                _settling = true;
                _userReleased = false;   // 复用：解除「用户已断开」闸门，恢复巡检/掉线检测
                string reErr = null;
                try { reErr = await ReattachAsync(kept); }
                catch { }
                _settling = false;
                if (reErr == null)
                {
                    ConnectionChanged?.Invoke(true);
                    return null;
                }
                ReleaseDevice();   // 复用失败：彻底释放，fallthrough 全新连接
            }
            else
            {
                ReleaseDevice();
            }

            _verifyFailStreak = 0;
            LastDropReason = null;
            LastVerifyNote = null;

            BluetoothLEDevice dev;
            try
            {
                dev = await BluetoothLEDevice.FromIdAsync(deviceId);
            }
            catch (Exception ex)
            {
                return "创建设备对象异常（" + ex.GetType().Name + "）：" + ex.Message;
            }
            if (dev == null)
                return "找不到设备对象（该设备可能已被删除配对或暂不可用）";

            dev.ConnectionStatusChanged += OnConnStatusChanged;
            lock (_sync) { _device = dev; }
            _settling = true;

            // 等 Windows 把链路连上：已配对设备由系统自动连接，FromIdAsync 本身即会触发建链。
            string waitErr = await WaitConnectedAsync(dev, connectTimeoutMs);
            if (waitErr != null)
            {
                ReleaseDevice();
                return "回连失败：" + waitErr
                     + "（排查：①鼠标是否开机/在范围；②是否已被其它主机占用；③电池电量）";
            }

            try
            {
                // 已配对设备不需要「配对后重试」分支（本就 paired）。
                string err = await AttachAsync(dev, false, requireSpp);
                if (err != null)
                {
                    ReleaseDevice();
                    return err;
                }
            }
            catch (Exception ex)
            {
                ReleaseDevice();
                return "连接异常（" + ex.GetType().Name + "）：" + ex.Message;
            }

            _settling = false;
            ConnectionChanged?.Invoke(true);
            return null;
        }

        /// <summary>
        /// 设备对象就绪后的公共 attach 流程：
        /// ① GattSession 保链 → ② Uncached 枚举服务 → ③（可选）配对后重试 →
        /// ④ 发现 SPP 0x33F3/0x33F4 → ⑤ 订阅 notify → ⑥ ATT 活性读定论 → ⑦ SPP 命令信息性探测。
        /// 返回 null = 成功；否则为错误描述（调用方负责 ReleaseDevice）。
        /// </summary>
        private async Task<string> AttachAsync(BluetoothLEDevice dev, bool allowPairRetry,
                                               bool requireSpp = true)
        {
            // ① 维持链路：GattSession.MaintainConnection = true。
            //    这是「真连接」的关键 —— WinRT 不会因为一次 GATT 操作就长期保持 BLE 链路，
            //    空闲后系统会主动断开，表现为「UI 显示已连、Windows/鼠标侧其实已断」。
            await OpenSessionAsync(dev);

            // ② 枚举服务。系统已连接时链路由 Windows 维持、GATT 数据库已被系统发现，
            //    用 Cached 复用即为真值（此时 Uncached 会与系统 HID 驱动争用而枚举失败，
            //    表现为「服务在列表里却拿不到特征」）；未连接时必须 Uncached 强制真实发现
            //    （Cached 在链路未建立时也会「成功」——那是假连接的来源）。
            bool sysConnected = dev.ConnectionStatus == BluetoothConnectionStatus.Connected;
            GattDeviceServicesResult svcRes = await EnumerateServicesAsync(dev, sysConnected);

            // ③ 失败再尝试配对后重试（仅扫描直连路径需要；已配对设备无需此分支）
            if (allowPairRetry && (svcRes == null || svcRes.Status != GattCommunicationStatus.Success))
            {
                await TryPairAsync(dev);
                svcRes = await EnumerateServicesAsync(dev, sysConnected);
            }

            if (svcRes == null || svcRes.Status != GattCommunicationStatus.Success)
            {
                BluetoothConnectionStatus cs = dev.ConnectionStatus;
                GattCommunicationStatus gs = svcRes == null
                    ? GattCommunicationStatus.Unreachable : svcRes.Status;
                string hint = DescribeConnectFailure(cs, gs);
                return "连接失败：" + gs + "（链路 " + cs + "）" + hint;
            }

            // ④ 找 SPP 特征 0x33F3(写) / 0x33F4(notify)
            //    OTA 页（requireSpp=false）不把 SPP 当硬条件：8208 量产固件实测存在
            //    「枚举不到 SPP 服务、但 OTA 写特征在」的情况，此时链路本身是通的，
            //    只要 OTA 特征命中就应当放行，否则用户会看到「明明连上了却报连接失败」。
            string sppErr = await DiscoverSppAsync(svcRes.Services, sysConnected);

            // OTA 专用兜底：广扫没命中 128-bit OTA 写特征时，按 OTA 服务 UUID 定向再找一次。
            // Windows 对自定义 128-bit 服务有时只在「按服务定向查询」时才返回特征。
            if (!requireSpp && !HasOtaChar)
                await TryFindOtaCharAsync();

            if (sppErr != null)
            {
                if (requireSpp || !HasOtaChar)
                {
                    return requireSpp
                        ? sppErr
                        : "连接失败：" + sppErr + "；且未发现 OTA 写特征。" + GattDiagNote;
                }
                LastSppCmdNote = "该设备未暴露 SPP 特征(0x33F3/0x33F4)：本页只用 OTA 特征，不影响升级";
            }

            // ⑤ 订阅 notify
            if (_notifyChar != null)
            {
                try
                {
                    _notifyChar.ValueChanged += OnValueChanged;
                    GattCommunicationStatus ccc =
                        await _notifyChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                            GattClientCharacteristicConfigurationDescriptorValue.Notify);
                    if (ccc != GattCommunicationStatus.Success)
                        return "订阅 notify(0x33F4) 失败：" + ccc;
                }
                catch (Exception ex)
                {
                    return "订阅 notify 异常：" + ex.Message;
                }
            }

            // ⑤b 语音流特征（0xFFF0/0xB03E）就绪性记录：**不自动订阅** ——
            //     与 USB 音频采集二选一，由调用方在用户启用蓝牙语音时显式 StartVoiceAsync()，
            //     否则鼠标会一直往 PC 推 57B/7.5ms 的 mSBC（白占空口与电量）。
            //     没有该特征不阻断连接（IsLinkVerified 不依赖它），只作说明。
            LastVoiceNote = _voiceChar != null
                ? null
                : "未发现语音流特征(0xFFF0/0xB03E)：该设备/固件不支持 BLE 语音输入";

            // ⑥ 真连接判据（决定性）：ATT 级真实读取。
            //    链路对象存在 / ConnectionStatus=Connected 都不足以证明链路活着
            //    （WinRT 会维持幽灵状态），而一次 Uncached 的 ATT Read 是真实空口往返，
            //    读成功即证明 PC 与鼠标之间此刻真的有一条活链路。
            //    刻意读特征而不是发命令：鼠标 ATT 表里 0x33F4 的值属性是 READ 权限，
            //    不依赖任何固件命令实现（D21 Pro 的 spp_device.c 根本不回命令）。
            LastVerifyNote = await ReadLivenessAsync(1500);
            if (LastVerifyNote != null)
            {
                // 校验不过就不算连上：宁可报未连接，也不给一个假的「已连接」。
                return "链路活性校验未通过（" + LastVerifyNote + "）→ 按「未连接」处理。"
                     + "排查：①该设备是否就是自家鼠标；②设备是否已被其它主机占用或已关机；"
                     + "③设备是否还在范围/已开机。";
            }
            _verifyFailStreak = 0;

            // ⑦ 信息性探测：鼠标到底回不回 SPP 命令（只作说明，不影响上面的判定）。
            //    D21 Pro(8208) 的 spp_device.c 只处理 0x77/0x78，且整段被
            //    #ifdef USER_DATA_AUTHORIZATION 包着；DEBUG_MODE=1 的构建里该宏未定义，
            //    于是任何命令都只被打印后丢弃 —— 这不是链路问题，别误判成假连接。
            //    没有 SPP 特征时无从探测，直接跳过（省 600ms 白等超时）。
            if (_notifyChar != null)
                LastSppCmdNote = await ProbeSppCommandAsync(600);
            return null;
        }

        /// <summary>轮询等待系统把 BLE 链路连上（已配对设备的回连由 Windows 完成，本工具只负责等 + 接入）。</summary>
        private static async Task<string> WaitConnectedAsync(BluetoothLEDevice dev, int timeoutMs)
        {
            DateTime t0 = DateTime.Now;
            while (true)
            {
                if (dev.ConnectionStatus == BluetoothConnectionStatus.Connected)
                    return null;
                if ((DateTime.Now - t0).TotalMilliseconds >= timeoutMs)
                    return "等待 " + timeoutMs + "ms 后系统仍未建立链路";
                await Task.Delay(200);
            }
        }

        /// <summary>建立 GattSession 并开启 MaintainConnection，让系统保持 BLE 链路不被空闲断开。</summary>
        private async Task OpenSessionAsync(BluetoothLEDevice dev)
        {
            try
            {
                GattSession session = await GattSession.FromDeviceIdAsync(dev.BluetoothDeviceId);
                if (session == null)
                    return;

                session.MaintainConnection = true;
                // 先摘后挂：温和释放后重连会重复进入，防同一 handler 重复订阅
                session.SessionStatusChanged -= OnSessionStatusChanged;
                session.SessionStatusChanged += OnSessionStatusChanged;
                lock (_sync) { _session = session; }
            }
            catch
            {
                // 旧系统无 GattSession（< Win10 1703）：退化为原行为，不阻断连接。
            }
        }

        /// <summary>是否已连上且 SPP 特征已就绪。</summary>
        private bool IsSppReady
        {
            get
            {
                lock (_sync) { return _writeChar != null || _notifyChar != null; }
            }
        }

        /// <summary>
        /// 枚举服务：按链路归属选模式。sysConnected=true（链路由 Windows 系统维持，如鼠标
        /// 已被系统蓝牙连接）时优先 Cached —— GATT 数据库已被系统发现，缓存即真值，且能避开
        /// 与系统 HID 驱动的 Uncached 发现争用；失败再回退 Uncached。sysConnected=false 时
        /// 只用 Uncached 强制真实发现（Cached 在链路未建立时也会返回成功，是假连接来源）。
        /// </summary>
        private static async Task<GattDeviceServicesResult> EnumerateServicesAsync(
            BluetoothLEDevice dev, bool sysConnected)
        {
            if (sysConnected)
            {
                foreach (var mode in new[] { BluetoothCacheMode.Cached, BluetoothCacheMode.Uncached })
                {
                    try
                    {
                        var res = await dev.GetGattServicesAsync(mode);
                        // Cached 可能「成功但返回空列表」——断开重连时本进程缓存视图
                        // 已随 dev.Dispose 清理，空列表等同无效，必须回退 Uncached 重新发现。
                        if (res != null && res.Status == GattCommunicationStatus.Success &&
                            res.Services != null && res.Services.Count > 0)
                            return res;
                    }
                    catch { }
                }
                return null;
            }
            try
            {
                return await dev.GetGattServicesAsync(BluetoothCacheMode.Uncached);
            }
            catch
            {
                return null;
            }
        }

        /// <summary>按 GATT/链路状态给出可操作的失败原因。</summary>
        private static string DescribeConnectFailure(BluetoothConnectionStatus cs, GattCommunicationStatus gs)
        {
            if (gs == GattCommunicationStatus.AccessDenied)
                return " → 访问被拒绝：检查 Windows 蓝牙权限，或该设备已被系统/其它程序独占。";
            if (cs == BluetoothConnectionStatus.Disconnected &&
                (gs == GattCommunicationStatus.Unreachable || gs == GattCommunicationStatus.ProtocolError))
                return " → 设备未响应，常见原因：①设备此刻不可连接（已连 Dongle/其它主机，或不在广播态）；"
                     + "②随机地址已轮换（该条目广播过期，请立刻重扫后再点连接）；"
                     + "③距离过远/信号弱；④该条目本身就不是鼠标。";
            return "";
        }

        /// <summary>释放当前设备对象、GattSession 与特征缓存（不触发 ConnectionChanged）。可重入。</summary>
        private void ReleaseDevice()
        {
            GattCharacteristic nc, vc;
            BluetoothLEDevice dev;
            GattSession session;
            lock (_sync)
            {
                nc = _notifyChar;
                vc = _voiceChar;
                dev = _device;
                session = _session;
                _device = null;
                _session = null;
                _writeChar = null;
                _notifyChar = null;
                _voiceChar = null;
                _voiceStreaming = false;
                _probeChar = null;
                _probeName = null;
                _devInfoChar = null;
            }

            _settling = false;
            _pingTcs = null;
            _userReleased = false;   // 彻底重置，SoftRelease 的保留语义一并作废

            if (nc != null)
            {
                try { nc.ValueChanged -= OnValueChanged; } catch { }
            }
            if (vc != null)
            {
                try { vc.ValueChanged -= OnVoiceValueChanged; } catch { }
            }
            if (session != null)
            {
                // 先解除「保持连接」，再销毁会话，链路才会真正释放（鼠标侧可回到广播/配对态）。
                try { session.SessionStatusChanged -= OnSessionStatusChanged; } catch { }
                try { session.MaintainConnection = false; } catch { }
                try { session.Dispose(); } catch { }
            }
            if (dev != null)
            {
                try { dev.ConnectionStatusChanged -= OnConnStatusChanged; } catch { }
                try { dev.Dispose(); } catch { }
            }
        }

        /// <summary>
        /// 「断开」专用温和释放：关 notify、解除保链，但保留 BluetoothLEDevice/GattSession
        /// 对象不 Dispose。原因：复用系统链路的场景下，Dispose 会清掉本进程 GATT 缓存
        /// 通道，重连时 Cached 枚举空 + Uncached 与系统驱动争用，稳定报「未发现 SPP 特征」；
        /// 保留对象则重连时缓存视图原样命中，秒级重新接入。仅 DisconnectAsync 使用；
        /// 连接失败/掉线清理仍走 ReleaseDevice() 彻底释放。
        /// </summary>
        private void SoftRelease()
        {
            // 只置「用户已主动断开」标志：特征/设备/会话引用全部保留。
            // 置位后：IsConnected 变 false（UI 恢复）、活性巡检停走、掉线回调不清理对象，
            // 重连时复用路径 Cached 缓存视图原样命中，秒级重新接入。
            // 若在此销毁或清空引用，活性巡检会因「探针特征为空」误判掉线并彻底释放
            // 保留对象（实测 16:54 日志），重连复用即告失败。
            _settling = false;
            _pingTcs = null;
            _userReleased = true;
        }
        /// <summary>
        /// SoftRelease 后的轻量重接：特征/探针引用全部保留（SoftRelease 刻意不清），
        /// 直接重开会话、重新订阅 notify、做 ATT 活性校验 —— 完全绕过服务/特征枚举。
        /// 必须绕过：断开重连时 Cached 特征缓存已失效、Uncached 与系统 HID 驱动争用，
        /// 重新枚举稳定报「未发现 SPP 特征」（实测连续 3 连败）；复用保留引用才是稳定通道。
        /// </summary>
        private async Task<string> ReattachAsync(BluetoothLEDevice dev)
        {
            string waitErr = await WaitConnectedAsync(dev, 4000);
            if (waitErr != null)
                return "等待链路建立超时：" + waitErr;

            await OpenSessionAsync(dev);   // 幂等：MaintainConnection=true、事件先摘后挂

            GattCharacteristic wc, nc;
            lock (_sync) { wc = _writeChar; nc = _notifyChar; }
            if (wc == null || nc == null)
                return "保留的 SPP 特征引用已失效";

            GattCommunicationStatus st;
            try
            {
                st = await nc.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.Notify);
            }
            catch (Exception ex) { return "重新订阅 notify 异常（" + ex.Message + "）"; }
            if (st != GattCommunicationStatus.Success)
                return "重新订阅 notify 失败（" + st + "）";
            try { nc.ValueChanged -= OnValueChanged; } catch { }
            nc.ValueChanged += OnValueChanged;

            LastVerifyNote = await ReadLivenessAsync(1500);
            if (LastVerifyNote != null)
            {
                return "链路活性校验未通过（" + LastVerifyNote + "）→ 按「未连接」处理，请重试。";
            }
            _verifyFailStreak = 0;
            LastDropReason = null;
            return null;
        }

        /// <summary>
        /// 在服务列表里找 SPP 0x33F3(写)/0x33F4(notify)。
        /// sysConnected=true（系统已连接）时特征枚举主用 Cached（复用系统已发现的数据库），
        /// 失败再退避重试 Uncached；未连接时保持两次 Uncached（原逻辑，防缓存假数据）。
        /// </summary>
        private async Task<string> DiscoverSppAsync(IReadOnlyList<GattDeviceService> services,
                                                    bool sysConnected)
        {
            lock (_sync)
            {
                _writeChar = null;
                _notifyChar = null;
                _otaChar = null;
                _otaCharUuid = Guid.Empty;
                _probeChar = null;
                _probeName = null;
                _devInfoChar = null;
                _voiceChar = null;
                _gattCustomUuids.Clear();
                _gattServiceUuids.Clear();
            }
            _voiceStreaming = false;

            BluetoothCacheMode primary = sysConnected
                ? BluetoothCacheMode.Cached : BluetoothCacheMode.Uncached;

            foreach (var svc in services)
            {
                // 诊断：记下服务 UUID（16-bit 短 UUID 也记），用于判断「是不是连错设备」
                string su = svc.Uuid.ToString();
                if (_gattServiceUuids.Count < 32 && !_gattServiceUuids.Contains(su))
                    _gattServiceUuids.Add(su);

                GattCharacteristicsResult chrRes =
                    await svc.GetCharacteristicsAsync(primary);
                // Cached「成功但 0 特征」= 缓存视图缺失（同服务枚举），按失败处理换模式重试，
                // 否则会把「缓存被清」误判成「该服务没有 SPP 特征」。
                if (chrRes == null || chrRes.Status != GattCommunicationStatus.Success ||
                    chrRes.Characteristics == null || chrRes.Characteristics.Count == 0)
                {
                    // 实测（8208 已配对路径）：链路刚连上、加密尚未就绪时读特征会
                    // AccessDenied/Unreachable。退避换 Uncached 重试再放弃该服务 ——
                    // 不重试就会把「瞬态/缓存缺失」误判成「没有 SPP 特征」。
                    // 断开→立即重连的窗口（supervision timeout 5s + 加密建立）实测
                    // 一次 800ms 不够：自定义服务（SPP/OTA 特征所在）退避多试几轮再放弃。
                    bool custom = !su.EndsWith("-0000-1000-8000-00805f9b34fb",
                                               StringComparison.OrdinalIgnoreCase);
                    int[] backoffs = custom ? new[] { 800, 1600, 2400 } : new[] { 800 };
                    foreach (int delay in backoffs)
                    {
                        await Task.Delay(delay);
                        chrRes = await svc.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);
                        if (chrRes != null && chrRes.Status == GattCommunicationStatus.Success &&
                            chrRes.Characteristics != null && chrRes.Characteristics.Count > 0)
                            break;
                    }
                }
                if (chrRes == null || chrRes.Status != GattCommunicationStatus.Success)
                    continue;

                foreach (var c in chrRes.Characteristics)
                {
                    if (c.Uuid == BleUxProto.SppWriteUuid)
                        _writeChar = c;
                    else if (c.Uuid == BleUxProto.SppNotifyUuid)
                        _notifyChar = c;

                    // 活性读探针：优先 0x33F4 本身（鼠标 ATT 表里它是 READ 权限），
                    // 否则退而求其次取第一个带 Read 属性的特征（电池电量 0x2A19 等）——
                    // 总得有一个能用来做「真实空口读取」的特征。
                    if (_probeChar == null &&
                        (c.CharacteristicProperties & GattCharacteristicProperties.Read) != 0)
                    {
                        _probeChar = c;
                        _probeName = c.Uuid == BleUxProto.SppNotifyUuid ? "0x33F4(SPP)"
                                    : c.Uuid == BleUxProto.BatteryLevelUuid ? "0x2A19(电量)"
                                    : c.Uuid.ToString();
                    }

                    // OTA 写特征（Telink BLE OTA server，128-bit UUID TELINK_SPP_DATA_OTA）。
                    // 与 SPP 发现解耦：没有它只影响 OTA 通道，不影响收发调试。
                    // 规范形式与「wire 字节 memcpy」兜底形式都认（平台差异，见 BleUxProto）。
                    if (c.Uuid == BleUxProto.OtaWriteUuid || c.Uuid == BleUxProto.OtaWriteUuidRaw)
                    {
                        _otaChar = c;
                        _otaCharUuid = c.Uuid;
                    }

                    // DevInf_aaa（当前版本号，只读）：挂在 OTA 服务下、紧跟 OTA 写特征之后，
                    // 属性 CHAR_PROP_READ + ATT_PERMISSIONS_READ，无需配对/加密/广播即可读。
                    // 规范形式与「wire 字节 memcpy」兜底形式都认（见 BleUxProto.DevInfoUuid）。
                    if (c.Uuid == BleUxProto.DevInfoUuid || c.Uuid == BleUxProto.DevInfoUuidRaw)
                        _devInfoChar = c;

                    // 语音流 notify（my_Data 0xB03E @ service 0xFFF0，ux_voice_dec.c）：
                    // 翻译页蓝牙语音输入。与 SPP/OTA 独立发现，订阅延后到用户点【启用语音】。
                    if (c.Uuid == BleUxProto.VoiceCharUuid)
                        _voiceChar = c;

                    // 诊断用：记下自定义 128-bit UUID（标准 base UUID 结尾的 16-bit 特征不算）
                    string ustr = c.Uuid.ToString();
                    if (!ustr.EndsWith("-0000-1000-8000-00805f9b34fb", StringComparison.OrdinalIgnoreCase) &&
                        _gattCustomUuids.Count < 24 && !_gattCustomUuids.Contains(ustr))
                    {
                        _gattCustomUuids.Add(ustr);
                    }
                }
                // 语音特征也找齐才提前收工；设备没有语音服务时自然遍历完全部服务
                // （OTA 页本来就常需全量枚举，多几个服务的代价可接受）。
                if (_writeChar != null && _notifyChar != null && _otaChar != null && _voiceChar != null)
                    break;
            }

            if (_writeChar == null && _notifyChar == null)
                return "未发现 SPP 特征(0x33F3/0x33F4)：该设备可能不是 BIBOO 鼠标的 GATT 服务";
            return null;
        }

        /// <summary>
        /// OTA 写特征定向兜底：按 128-bit OTA 服务 UUID（两种 GUID 形式）直接找特征，
        /// 服务级和特征级都先 Uncached 再 Cached。广扫遍没命中时调用。
        /// 返回 true = 已拿到 _otaChar。
        /// </summary>
        private async Task<bool> TryFindOtaCharAsync()
        {
            if (HasOtaChar)
                return true;

            BluetoothLEDevice dev;
            lock (_sync) { dev = _device; }
            if (dev == null)
                return false;

            foreach (var svcMode in new[] { BluetoothCacheMode.Uncached, BluetoothCacheMode.Cached })
            {
                GattDeviceServicesResult svcRes;
                try { svcRes = await dev.GetGattServicesAsync(svcMode); }
                catch { continue; }
                if (svcRes == null || svcRes.Status != GattCommunicationStatus.Success ||
                    svcRes.Services == null)
                    continue;

                foreach (var svc in svcRes.Services)
                {
                    if (svc.Uuid != BleUxProto.OtaServiceUuid &&
                        svc.Uuid != BleUxProto.OtaServiceUuidRaw)
                        continue;

                    foreach (var chrMode in new[] { BluetoothCacheMode.Uncached, BluetoothCacheMode.Cached })
                    {
                        GattCharacteristicsResult chrRes;
                        try { chrRes = await svc.GetCharacteristicsAsync(chrMode); }
                        catch { continue; }
                        if (chrRes == null || chrRes.Status != GattCommunicationStatus.Success ||
                            chrRes.Characteristics == null)
                            continue;

                        bool foundOta = false;
                        foreach (var c in chrRes.Characteristics)
                        {
                            // 顺便补活性读探针：OTA 服务里有一个 READ 权限的设备信息特征
                            // （TELINK 128-bit DevInf），没有 SPP 时正好用它证明链路活着。
                            if (_probeChar == null &&
                                (c.CharacteristicProperties & GattCharacteristicProperties.Read) != 0)
                            {
                                lock (_sync)
                                {
                                    _probeChar = c;
                                    _probeName = "OTA 服务可读特征 " + c.Uuid;
                                }
                            }

                            if (c.Uuid == BleUxProto.DevInfoUuid || c.Uuid == BleUxProto.DevInfoUuidRaw)
                                _devInfoChar = c;

                            if (c.Uuid != BleUxProto.OtaWriteUuid &&
                                c.Uuid != BleUxProto.OtaWriteUuidRaw)
                                continue;
                            lock (_sync) { _otaChar = c; _otaCharUuid = c.Uuid; }
                            /*不提前 return：DevInf 紧跟在 OTA 写特征之后，
                             *扫完本列表才能把它一起搭上（否则回连补枚举永远拿不到版本）*/
                            foundOta = true;
                        }
                        if (foundOta)
                            return true;
                    }
                }
            }
            return false;
        }

        /// <summary>是否发现了 Telink OTA 写特征（供「固件升级」页判断 BLE 直连 OTA 是否可用）。</summary>
        public bool HasOtaChar
        {
            get { lock (_sync) { return _otaChar != null; } }
        }

        /// <summary>实际命中的 OTA 特征 UUID（未找到时为 Guid.Empty）。</summary>
        public Guid OtaCharUuid
        {
            get { lock (_sync) { return _otaCharUuid; } }
        }

        /// <summary>是否发现了 DevInf_aaa（当前版本号，只读）特征。</summary>
        public bool HasDevInfoChar
        {
            get { lock (_sync) { return _devInfoChar != null; } }
        }

        /// <summary>
        /// 读取鼠标当前固件版本号（DevInf_aaa，15 字节，GATT 只读）。
        /// 该特征无需配对/加密/广播，已连接即可读。
        /// 返回 null = 未找到该特征或读取失败；否则为解析后的 <see cref="BleDevInf"/>。
        /// </summary>
        public async Task<BleDevInf> ReadDevInfoAsync(int timeoutMs = 1200)
        {
            GattCharacteristic dev;
            lock (_sync) { dev = _devInfoChar; }

            if (dev != null)
            {
                BleDevInf di = await ReadDevInfCharAsync(dev, timeoutMs);
                if (di != null)
                {
                    LastDevInfoNote = null;
                    return di;
                }
                /*掉线-回连后保留的特征对象可能绑定已失效的 service session：
                 *Uncached 读恒 AccessDenied/Unreachable，活性探针（0x2A00 标准 GAP
                 *特征）不受影响——必须重新枚举拿新特征对象，否则永远读不到*/
            }

            /*首次无特征，或读到但失败（疑似失效对象）→ 重枚举一次再读*/
            await TryFindOtaCharAsync();
            lock (_sync) { dev = _devInfoChar; }
            if (dev == null)
            {
                LastDevInfoNote = "未发现 DevInf 特征";
                return null;
            }
            BleDevInf di2 = await ReadDevInfCharAsync(dev, timeoutMs);
            LastDevInfoNote = di2 == null ? "DevInf 读失败（" + _lastDevInfoRaw + "）" : null;
            return di2;
        }

        /// <summary>最近一次 ReadDevInfoAsync 失败原因（诊断用）；成功后清空。</summary>
        public string LastDevInfoNote { get; private set; }

        /// <summary>最近一次 DevInf 特征读的原始结果（状态/字节数，诊断用）。</summary>
        private string _lastDevInfoRaw;

        /// <summary>读单个 DevInf 特征并解析；失败原因写 _lastDevInfoRaw。</summary>
        private async Task<BleDevInf> ReadDevInfCharAsync(GattCharacteristic ch, int timeoutMs)
        {
            GattReadResult res = await ReadCharUncachedWithRetryAsync(ch, timeoutMs);
            if (res == null || res.Status != GattCommunicationStatus.Success)
            {
                _lastDevInfoRaw = res == null ? "超时/异常" : res.Status.ToString();
                return null;
            }
            try
            {
                var dr = DataReader.FromBuffer(res.Value);
                int len = (int)dr.UnconsumedBufferLength;
                byte[] buf = new byte[len];
                if (len > 0)
                    dr.ReadBytes(buf);
                BleDevInf di = BleDevInf.Parse(buf);
                _lastDevInfoRaw = len + "B 解析" + (di == null ? "失败" : "成功");
                return di;
            }
            catch (Exception ex)
            {
                _lastDevInfoRaw = "缓冲异常：" + ex.Message;
                return null;
            }
        }

        /// <summary>
        /// Uncached 读一个特征，失败退避 800ms 再试一次。
        /// 回连初期（链路恢复后 1-2s）读特征常见 AccessDenied/Unreachable ——
        /// 与 DiscoverSppAsync 的特征枚举同款退避策略，没有它回连补读版本几乎必失败。
        /// </summary>
        private static async Task<GattReadResult> ReadCharUncachedWithRetryAsync(
            GattCharacteristic ch, int timeoutMs)
        {
            for (int i = 0; i < 2; i++)
            {
                try
                {
                    var cts = new CancellationTokenSource(timeoutMs);
                    Task<GattReadResult> t = ch.ReadValueAsync(BluetoothCacheMode.Uncached).AsTask(cts.Token);
                    Task done = await Task.WhenAny(t, Task.Delay(Timeout.Infinite, cts.Token));
                    if (done == t)
                    {
                        GattReadResult res = await t;
                        if (res != null && res.Status == GattCommunicationStatus.Success)
                            return res;
                    }
                }
                catch (OperationCanceledException) { }
                catch (Exception) { }
                if (i == 0)
                    await Task.Delay(800);
            }
            return null;
        }

        /// <summary>
        /// GATT 级诊断：本次枚举到的全部服务 UUID + 自定义 128-bit 特征 UUID。
        /// 「找不到某个特征」时打进日志，一眼分辨是连错设备、服务被系统缓存挡住、
        /// 还是固件确实没编入该服务。
        /// </summary>
        public string GattDiagNote
        {
            get
            {
                lock (_sync)
                {
                    var sb = new StringBuilder();
                    sb.Append("本次枚举到 ").Append(_gattServiceUuids.Count).Append(" 个服务：");
                    sb.Append(_gattServiceUuids.Count == 0
                        ? "(空 —— 服务枚举失败/被系统缓存挡住)"
                        : string.Join(" / ", _gattServiceUuids.ToArray()));
                    if (_gattCustomUuids.Count > 0)
                    {
                        sb.Append("；自定义 128-bit 特征：");
                        sb.Append(string.Join(" / ", _gattCustomUuids.ToArray()));
                    }
                    return sb.ToString();
                }
            }
        }

        /// <summary>
        /// OTA 特征诊断：设备上出现过的自定义 128-bit UUID 列表。
        /// 未找到 OTA 特征时打进日志，一眼看出「固件里到底有没有 OTA 服务」。
        /// </summary>
        public string OtaDiagNote
        {
            get
            {
                lock (_sync)
                {
                    if (_otaChar != null)
                        return "OTA 写特征已命中：" + _otaCharUuid;
                    if (_gattCustomUuids.Count == 0)
                        return "设备没有暴露任何自定义 128-bit UUID（该固件未编入 OTA 服务，"
                             + "检查 BLE_OTA_SERVER_ENABLE / BOOTLOADER_OTA_ENABLE）。" + GattDiagNote;
                    return "设备自定义 128-bit UUID：" + string.Join(" / ", _gattCustomUuids.ToArray());
                }
            }
        }

        /// <summary>
        /// 写一个 OTA PDU（≤20B）到 Telink OTA 特征（write without response）。
        /// 成功返回 null；未连接/未发现特征/写入失败返回错误描述。
        /// </summary>
        public async Task<string> WriteOtaAsync(byte[] pdu)
        {
            GattCharacteristic c;
            lock (_sync) { c = _otaChar; }
            if (c == null)
                return "未发现 OTA 写特征（" + BleUxProto.OtaWriteUuid + "）；" + OtaDiagNote;
            if (pdu == null || pdu.Length == 0 || pdu.Length > 20)
                return "OTA PDU 长度须为 1..20 字节";

            var dw = new DataWriter();
            dw.WriteBytes(pdu);
            GattCommunicationStatus res =
                await c.WriteValueAsync(dw.DetachBuffer(), GattWriteOption.WriteWithoutResponse);
            return res == GattCommunicationStatus.Success ? null : "写 OTA 特征失败：" + res;
        }

        private static async Task TryPairAsync(BluetoothLEDevice dev)
        {
            try
            {
                DeviceInformationPairing pairing = dev.DeviceInformation?.Pairing;
                if (pairing != null && pairing.CanPair && !pairing.IsPaired)
                {
                    // WinRT 的 PairAsync 无 DevicePairingKinds 重载；用无参版本，
                    // 由系统按设备默认保护级别配对（BLE 多为 ConfirmOnly）。失败不阻断主流程。
                    // 必须带超时：鼠标没进配对模式时 SMP 无人应答，系统要干等约 90s，
                    // 这里 15s 收手（任务遗弃无妨，超时后由调用方按失败处理）。
                    Task pairTask = pairing.PairAsync().AsTask();
                    await Task.WhenAny(pairTask, Task.Delay(15000));
                }
            }
            catch
            {
                // 部分设备无需配对即可用 SPP；配对失败不阻断主流程。
            }
        }

        /// <summary>
        /// 主动配对（UI【配对】按钮用）。成功返回 null；失败返回原因。
        /// 注意：本鼠标 SPP 特征（0x33F3/0x33F4）的 ATT 权限是 open，不配对也能读写，
        /// 配对的唯一好处是让设备出现在 Windows「蓝牙和其他设备」列表里、可在系统中管理/断开。
        /// 代价：鼠标 flash 只存 4 个 bonding 主机（blc_smp_param_setBondingDeviceMaxNumber(4)），
        /// 反复配对会占满，占满后需 UnpairAsync 清理，否则新主机配不上。
        /// **前置条件**：必须先让鼠标进入配对模式，否则 SMP 请求无人应答、只能等系统超时（实测约 90s）。
        /// </summary>
        public async Task<string> PairAsync(ulong address, BluetoothAddressType addrType,
                                            int timeoutMs = 15000)
        {
            BluetoothLEDevice dev = null;
            try
            {
                try
                {
                    dev = await BluetoothLEDevice.FromBluetoothAddressAsync(address, addrType);
                }
                catch (Exception ex)
                {
                    return "创建设备对象异常（" + ex.GetType().Name + "）：" + ex.Message;
                }
                if (dev == null)
                    return "找不到设备对象（未配对且系统无缓存：请先扫描到该设备再配对）";

                DeviceInformationPairing pairing = dev.DeviceInformation == null
                    ? null
                    : dev.DeviceInformation.Pairing;
                if (pairing == null)
                    return "该设备没有 Pairing 信息（DeviceInformation.Pairing 为空）";
                if (!pairing.CanPair)
                    return "该设备声明不可配对（CanPair=false）";
                if (pairing.IsPaired)
                    return null;   // 已配对 = 目标状态已达成

                // 带超时地等 PairAsync：鼠标没进配对模式时 SMP 请求无人应答，
                // 系统要干等约 90s 才放弃，这里提前到 timeoutMs 返回并给出可操作提示。
                DevicePairingResult r = null;
                Task pairTask = pairing.PairAsync().AsTask();
                Task done = await Task.WhenAny(pairTask, Task.Delay(timeoutMs));
                if (done != pairTask)
                {
                    return "等待配对超时（" + timeoutMs + "ms）。鼠标侧未响应 SMP 配对请求。"
                         + "请先在鼠标上【左键 + 电源键】同时长按约 2.8 秒进入配对模式"
                         + "（鼠标会打印 ---ble_start_pair. 并自动重启），再点【配对】。";
                }
                r = await (Task<DevicePairingResult>)pairTask;

                switch (r.Status)
                {
                    case DevicePairingResultStatus.Paired:
                    case DevicePairingResultStatus.AlreadyPaired:
                        // 等一会儿再释放对象：让 Windows 把设备登记进「已配对」列表，
                        // 也给鼠标侧写 flash（FLASH_ADR_SMP_PAIRING）留出时间。
                        await Task.Delay(1500);
                        return null;
                    default:
                        return "配对失败（" + r.Status + "）。"
                             + "常见原因：①鼠标未进入配对模式（需【左键+电源键】长按 2.8 秒）；"
                             + "②鼠标 bonding 表已满（该固件最多存 4 个主机，需先在系统里移除旧配对）；"
                             + "③Windows 弹窗被取消/超时。";
                }
            }
            catch (Exception ex)
            {
                return "配对异常（" + ex.GetType().Name + "）：" + ex.Message;
            }
            finally
            {
                if (dev != null)
                {
                    try { dev.Dispose(); } catch { }
                }
            }
        }

        private void OnValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
        {
            var dr = DataReader.FromBuffer(args.CharacteristicValue);
            byte[] buf = new byte[dr.UnconsumedBufferLength];
            dr.ReadBytes(buf);

            // 有 SPP 命令在等回包：命中即完成（一次性）。
            TaskCompletionSource<byte[]> tcs = _pingTcs;
            if (tcs != null && buf.Length > 0 && buf[0] == _pingCmd)
            {
                _pingTcs = null;
                tcs.TrySetResult(buf);
                if (_pingQuiet)
                    return;   // 自动巡检的回包不进日志，避免每 2.5s 刷屏
            }

            SppReceived?.Invoke(buf);
        }

        /// <summary>
        /// 语音流 notify（0xB03E）：每帧 57B mSBC。与 SPP 的 ping 回包机制完全无关，
        /// 直接分发 <see cref="VoiceReceived"/>（未订阅时不触发）。
        /// </summary>
        private void OnVoiceValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
        {
            var dr = DataReader.FromBuffer(args.CharacteristicValue);
            byte[] buf = new byte[dr.UnconsumedBufferLength];
            dr.ReadBytes(buf);
            VoiceReceived?.Invoke(buf);
        }

        /// <summary>
        /// 订阅语音流 notify（写 0xB03E 的 CCCD）。「实时翻译」页在用户点【启用语音】时调用
        /// （与 USB 音频采集二选一）。返回 null = 成功；否则为失败原因。
        /// 幂等：已订阅时重复调用只是重写一次 CCCD（先摘后挂，防事件重复订阅）。
        /// </summary>
        public async Task<string> StartVoiceAsync()
        {
            GattCharacteristic vc;
            lock (_sync) { vc = _voiceChar; }
            if (vc == null)
                return "未发现语音流特征(0xFFF0/0xB03E)：该设备不支持 BLE 语音输入";

            try
            {
                vc.ValueChanged -= OnVoiceValueChanged;   // 先摘后挂：防重复订阅
                vc.ValueChanged += OnVoiceValueChanged;
                GattCommunicationStatus st =
                    await vc.WriteClientCharacteristicConfigurationDescriptorAsync(
                        GattClientCharacteristicConfigurationDescriptorValue.Notify);
                if (st != GattCommunicationStatus.Success)
                {
                    try { vc.ValueChanged -= OnVoiceValueChanged; } catch { }
                    _voiceStreaming = false;
                    return "订阅语音 notify(0xB03E) 失败：" + st;
                }
                _voiceStreaming = true;
                LastVoiceNote = null;
                return null;
            }
            catch (Exception ex)
            {
                _voiceStreaming = false;
                return "订阅语音 notify 异常：" + ex.Message;
            }
        }

        /// <summary>
        /// 退订语音 notify（鼠标停止向 PC 推 mSBC，省空口与鼠标电量）。幂等；
        /// 未订阅/无特征时直接返回 null。断开流程（<see cref="CloseNotifyAsync"/>）也会调用。
        /// </summary>
        public async Task<string> StopVoiceAsync()
        {
            GattCharacteristic vc;
            lock (_sync) { vc = _voiceChar; }
            _voiceStreaming = false;
            if (vc == null)
                return null;

            try { vc.ValueChanged -= OnVoiceValueChanged; } catch { }
            try
            {
                GattCommunicationStatus st =
                    await vc.WriteClientCharacteristicConfigurationDescriptorAsync(
                        GattClientCharacteristicConfigurationDescriptorValue.None);
                return st == GattCommunicationStatus.Success
                    ? null
                    : "关闭语音 notify 未成功（" + st + "）";
            }
            catch (Exception ex)
            {
                return "关闭语音 notify 异常：" + ex.Message;
            }
        }

        private void OnConnStatusChanged(BluetoothLEDevice sender, object args)
        {
            if (sender.ConnectionStatus == BluetoothConnectionStatus.Connected)
            {
                // 关键：**不再**因为 WinRT 报 Connected 就点亮「已连接」。
                // 该标志可能在链路实际不可用时也为真（幽灵/缓存条目），
                // 「已连接」只能由 ATT 活性读给出（见 ReadLivenessAsync / VerifyAsync）。
                return;
            }

            if (_settling)
                return;   // 建链过程中的抖动由 ConnectAsync 自己收尾

            if (_userReleased)
            {
                // 用户已主动断开（对象保留待重连）：只同步 UI，不动特征引用/失败计数，
                // 否则这里会把复用对象「清理」掉，重连复用即告失败。
                ConnectionChanged?.Invoke(false);
                return;
            }

            lock (_sync) { _writeChar = null; _notifyChar = null; _voiceChar = null; }
            _voiceStreaming = false;
            _verifyFailStreak = VerifyFailLimit;
            LastDropReason = "系统上报 GATT 链路断开（ConnectionStatus=Disconnected）";
            ConnectionChanged?.Invoke(false);
        }

        private void OnSessionStatusChanged(GattSession sender, GattSessionStatusChangedEventArgs args)
        {
            if (args.Status == GattSessionStatus.Active)
                return;   // 同上：交由 ATT 活性读决定

            if (_settling)
                return;

            if (_userReleased)
            {
                // 同上：用户主动断开期间只同步 UI，不清理保留的对象
                ConnectionChanged?.Invoke(false);
                return;
            }

            lock (_sync) { _writeChar = null; _notifyChar = null; _voiceChar = null; }
            _voiceStreaming = false;
            _verifyFailStreak = VerifyFailLimit;
            LastDropReason = "GattSession 失效（链路被系统/对端释放）";
            ConnectionChanged?.Invoke(false);
        }

        // ---------- 链路活性校验（真连接判据）----------
        /// <summary>
        /// 周期巡检：做一次 ATT 级真实读取（Uncached Read）。
        /// 连续 VerifyFailLimit 次失败即判定链路已断（释放设备 + 上报断开），
        /// 用于纠正「系统显示已连、实际早已断」的假连接。返回 true 表示链路仍然活着。
        /// 判据刻意不用 SPP 命令往返：部分固件（如 D21 Pro 的 DEBUG_MODE 构建）
        /// 根本不回任何命令，拿命令当判据会把真连接误判成假连接。
        /// </summary>
        public async Task<bool> VerifyAsync(int timeoutMs = 1200)
        {
            // 用户主动断开（SoftRelease，对象保留待重连）：巡检一律不介入——
            // 不活性读、不写失败计数、不触发掉线清理，保留对象原样等待复用。
            if (_userReleased)
                return true;

            if (!IsLinkUp)
            {
                _verifyFailStreak = VerifyFailLimit;
                LastDropReason = "GATT 链路已不在 Connected 状态";
                ConnectionChanged?.Invoke(false);
                return false;
            }

            bool wasDead;
            lock (_sync) { wasDead = _verifyFailStreak >= VerifyFailLimit; }

            string err = await ReadLivenessAsync(timeoutMs);

            if (err == null)
            {
                _verifyFailStreak = 0;
                if (wasDead)
                    ConnectionChanged?.Invoke(true);
                return true;
            }

            if (++_verifyFailStreak >= VerifyFailLimit)
            {
                ReleaseDevice();
                LastDropReason = "连续 " + VerifyFailLimit + " 次链路活性读失败（" + err + "）";
                ConnectionChanged?.Invoke(false);
                return false;
            }
            return false;
        }

        /// <summary>
        /// ATT 级活性读：对探针特征做一次 Uncached Read。
        /// Uncached 强制走真实空口读取（Cached 会直接返回本地缓存，链路断了也"成功"），
        /// 因此读成功 = PC 与鼠标之间此刻真的有一条活链路。
        /// 返回 null = 链路活着；否则为失败原因（同时把读到的信息写进 LastLivenessNote）。
        /// </summary>
        public async Task<string> ReadLivenessAsync(int timeoutMs = 1200)
        {
            GattCharacteristic probe;
            string probeName;
            lock (_sync) { probe = _probeChar; probeName = _probeName; }
            if (probe == null)
                return "没有可读的探针特征（0x33F4 与电池电量均不可读）";

            GattReadResult res;
            try
            {
                var cts = new CancellationTokenSource(timeoutMs);
                Task<GattReadResult> t = probe.ReadValueAsync(BluetoothCacheMode.Uncached).AsTask(cts.Token);
                Task done = await Task.WhenAny(t, Task.Delay(Timeout.Infinite, cts.Token));
                if (done != t)
                    return "ATT 读取超时（" + timeoutMs + "ms，探针 " + probeName + "）：链路不可达";
                res = await t;
            }
            catch (OperationCanceledException)
            {
                return "ATT 读取超时（" + timeoutMs + "ms，探针 " + probeName + "）：链路不可达";
            }
            catch (Exception ex)
            {
                return "ATT 读取异常（" + ex.GetType().Name + "）：" + ex.Message;
            }

            if (res.Status != GattCommunicationStatus.Success)
                return "ATT 读取失败：" + res.Status + "（探针 " + probeName + "）";

            int len = 0;
            try
            {
                var dr = DataReader.FromBuffer(res.Value);
                len = (int)dr.UnconsumedBufferLength;
            }
            catch { }

            LastLivenessNote = "读 " + probeName + " 成功（" + len + " 字节，真实空口往返）";
            return null;
        }

        /// <summary>
        /// 信息性探测：鼠标是否回 SPP 命令。结论写入 LastSppCmdNote，
        /// **不影响**「已连接」判定（D21 Pro 的 DEBUG_MODE 构建下任何命令都不回包）。
        /// </summary>
        public async Task<string> ProbeSppCommandAsync(int timeoutMs = 600)
        {
            string note = await SppCommandAsync(BleUxProto.GetAuthStateCmd, timeoutMs, true);
            LastSppCmdNote = note == null
                ? "固件支持 SPP 命令回包（0x78 → 0x33F4）"
                : "固件未回 SPP 命令（" + note + "）—— 仅说明，不影响「已连接」判定";
            return LastSppCmdNote;
        }

        /// <summary>
        /// 手动 SPP 命令往返（UI【Ping 校验】用）：依次试 0x78（查鉴权状态，只读）
        /// 与 0x01（DUT PING），任一有回包即算通过。
        /// 这只是「固件是否实现命令回包」的探测，不是连接判据。
        /// </summary>
        public async Task<string> PingAsync(int timeoutMs = 1500)
        {
            if (await SppCommandAsync(BleUxProto.GetAuthStateCmd, timeoutMs) == null)
                return null;
            if (await SppCommandAsync(BleUxProto.DutCmdPing, timeoutMs) == null)
                return null;

            return "SPP 命令无回包（0x78 与 0x01 均超时）。这不代表链路断开 —— "
                 + "该固件可能未实现命令回包（D21 Pro：spp_device.c 只认 0x77/0x78，"
                 + "且被 USER_DATA_AUTHORIZATION 宏关掉；0x01 只属于 8258 产测 DUT 固件）。";
        }

        /// <summary>写 0x33F3 的一个命令字并等 0x33F4 回包（回包首字节须等于该命令字）。</summary>
        private async Task<string> SppCommandAsync(byte cmd, int timeoutMs, bool quiet = false)
        {
            if (Interlocked.CompareExchange(ref _pingBusy, 1, 0) != 0)
                return "上一次命令仍在等待回包";

            try
            {
                if (!IsLinkUp)
                    return "GATT 链路未处于已连接状态";

                GattCharacteristic wc;
                lock (_sync) { wc = _writeChar; }
                if (wc == null)
                    return "未发现 SPP 写特征 0x33F3";

                var tcs = new TaskCompletionSource<byte[]>();
                _pingCmd = cmd;
                _pingTcs = tcs;
                _pingQuiet = quiet;

                byte[] frame = new byte[BleUxProto.SppSize];
                frame[BleUxProto.SppOffsCmd] = cmd;

                string werr = await WriteSppAsync(frame);
                if (werr != null)
                {
                    _pingTcs = null;
                    return "写 0x33F3 失败：" + werr;
                }

                Task done = await Task.WhenAny(tcs.Task, Task.Delay(timeoutMs));
                if (done != tcs.Task)
                {
                    _pingTcs = null;
                    return "0x" + cmd.ToString("X2") + " 无响应（" + timeoutMs + "ms 超时）";
                }

                byte[] rsp = tcs.Task.Result;
                LastSppRspCmd = rsp.Length > 0 ? rsp[0] : (byte)0;
                LastPingVersion = rsp.Length > 1 ? rsp[1] : (byte)0;
                LastPingModel = rsp.Length > 2 ? rsp[2] : (byte)0;
                LastPingMouseLen = rsp.Length > 3 ? rsp[3] : (byte)0;
                return null;
            }
            finally
            {
                _pingTcs = null;
                _pingQuiet = false;
                Interlocked.Exchange(ref _pingBusy, 0);
            }
        }

        /// <summary>通用帧交换：写一帧到 0x33F3，等 0x33F4 回包（回包首字节须等于请求首字节）。
        /// 回包同时走 SppReceived 进日志。失败/超时/占用返回 null。</summary>
        private async Task<byte[]> SppExchangeAsync(byte[] reqFrame, int timeoutMs)
        {
            if (Interlocked.CompareExchange(ref _pingBusy, 1, 0) != 0)
                return null;
            try
            {
                if (!IsLinkUp)
                    return null;

                GattCharacteristic wc;
                lock (_sync) { wc = _writeChar; }
                if (wc == null)
                    return null;

                var tcs = new TaskCompletionSource<byte[]>();
                _pingCmd = reqFrame[0];
                _pingTcs = tcs;
                _pingQuiet = false;   // 滑动参数回包保留 RX 日志（RX 33F4 ...）

                string werr = await WriteSppAsync(reqFrame);
                if (werr != null)
                {
                    _pingTcs = null;
                    return null;
                }

                Task done = await Task.WhenAny(tcs.Task, Task.Delay(timeoutMs));
                if (done != tcs.Task)
                {
                    _pingTcs = null;
                    return null;
                }
                return tcs.Task.Result;
            }
            finally
            {
                _pingTcs = null;
                _pingQuiet = false;
                Interlocked.Exchange(ref _pingBusy, 0);
            }
        }

        /// <summary>读滑动参数：0x80 读 iOS 组 + 0x81 读 Android 组，拼成 24 字节。
        /// 成功返回 null，结果在 LastSlideValid / LastSlideParam（[0..11]=iOS，[12..23]=Android）。</summary>
        public async Task<string> SppSlideParamReadAsync(int timeoutMs = 1500)
        {
            byte[] rspIos = await SppExchangeAsync(new byte[] { BleUxProto.GetSlideParamCmd }, timeoutMs);
            if (rspIos == null)
                return "0x80 无响应（" + timeoutMs + "ms 超时）—— 鼠标固件需为支持 0x79~0x82 的新版本";
            if (rspIos.Length < BleUxProto.SlideParamRspSize)
                return "0x80 回包长度异常：" + rspIos.Length + "（预期 " + BleUxProto.SlideParamRspSize + "）";

            byte[] rspAdr = await SppExchangeAsync(new byte[] { BleUxProto.GetSlideParamAndroidCmd }, timeoutMs);
            if (rspAdr == null)
                return "0x81 无响应（" + timeoutMs + "ms 超时）—— 固件可能仅支持单组（旧版），请重烧新固件";
            if (rspAdr.Length < BleUxProto.SlideParamRspSize)
                return "0x81 回包长度异常：" + rspAdr.Length + "（预期 " + BleUxProto.SlideParamRspSize + "）";

            LastSlideValid = rspIos[2];
            LastSlideParam = new byte[BleUxProto.SlideParamSize];
            Array.Copy(rspIos, 3, LastSlideParam, 0, BleUxProto.SlideParamGroupSize);
            Array.Copy(rspAdr, 3, LastSlideParam, BleUxProto.SlideParamGroupSize, BleUxProto.SlideParamGroupSize);
            return null;
        }

        /// <summary>写滑动参数（0x79 写 iOS 组 + 0x82 写 Android 组）：cfg 须 24 字节
        /// （前 12=iOS 组，后 12=Android 组；每组 3×4B：interval 小端u16/ms、step_len、step_cnt）。
        /// 两组都成功才返回 null；成功后参数写入鼠标 flash，重启保持。</summary>
        public async Task<string> SppSlideParamWriteAsync(byte[] cfg, int timeoutMs = 1500)
        {
            if (cfg == null || cfg.Length != BleUxProto.SlideParamSize)
                return "参数须为 " + BleUxProto.SlideParamSize + " 字节（iOS组12 + Android组12，hex）";

            // 第 1 帧：0x79 写 iOS 组
            var req1 = new byte[2 + BleUxProto.SlideParamGroupSize];
            req1[0] = BleUxProto.SetSlideParamCmd;
            req1[1] = BleUxProto.SlideParamGroupSize;
            Array.Copy(cfg, 0, req1, 2, BleUxProto.SlideParamGroupSize);
            byte[] rsp1 = await SppExchangeAsync(req1, timeoutMs);
            if (rsp1 == null)
                return "0x79 无响应（" + timeoutMs + "ms 超时）—— 鼠标固件需为支持 0x79~0x82 的新版本";
            if (rsp1.Length < 3 || rsp1[2] != 1)
                return "iOS 组被设备拒绝（status≠1）：须满足 间隔1~10000ms、步长1~127、步数1~255";

            // 第 2 帧：0x82 写 Android 组
            var req2 = new byte[2 + BleUxProto.SlideParamGroupSize];
            req2[0] = BleUxProto.SetSlideParamAndroidCmd;
            req2[1] = BleUxProto.SlideParamGroupSize;
            Array.Copy(cfg, BleUxProto.SlideParamGroupSize, req2, 2, BleUxProto.SlideParamGroupSize);
            byte[] rsp2 = await SppExchangeAsync(req2, timeoutMs);
            if (rsp2 == null)
                return "0x82 无响应（" + timeoutMs + "ms 超时）—— iOS 组已写入，Android 组未写";
            if (rsp2.Length < 3 || rsp2[2] != 1)
                return "Android 组被设备拒绝（status≠1）：须满足 间隔1~10000ms、步长1~127、步数1~255";

            return null;
        }

        // ---------- 收发 ----------
        /// <summary>向 0x33F3 写一帧（write without response）。成功返回 null；失败返回错误描述。</summary>
        public async Task<string> WriteSppAsync(byte[] frame)
        {
            GattCharacteristic c;
            lock (_sync) { c = _writeChar; }
            if (c == null)
                return "未连接或未发现 0x33F3 写特征";

            if (frame == null || frame.Length == 0 || frame.Length > BleUxProto.SppSize)
                return "SPP 帧长度须为 1..20 字节";

            var dw = new DataWriter();
            dw.WriteBytes(frame);
            GattCommunicationStatus res =
                await c.WriteValueAsync(dw.DetachBuffer(), GattWriteOption.WriteWithoutResponse);
            return res == GattCommunicationStatus.Success ? null : "写 0x33F3 失败：" + res;
        }

        // ---------- 探索：dump GATT（日常开发用）----------
        public async Task<List<string>> DumpGattAsync()
        {
            BluetoothLEDevice dev;
            lock (_sync) { dev = _device; }
            var lines = new List<string>();
            if (dev == null)
            {
                lines.Add("(未连接)");
                return lines;
            }

            GattDeviceServicesResult svcRes = await dev.GetGattServicesAsync(BluetoothCacheMode.Uncached);
            if (svcRes.Status != GattCommunicationStatus.Success)
            {
                lines.Add("枚举服务失败：" + svcRes.Status);
                return lines;
            }
            foreach (var svc in svcRes.Services)
            {
                lines.Add("SVC " + svc.Uuid);
                GattCharacteristicsResult chrRes = await svc.GetCharacteristicsAsync();
                if (chrRes.Status == GattCommunicationStatus.Success)
                {
                    foreach (var c in chrRes.Characteristics)
                        lines.Add("  CHR " + c.Uuid + " [" + c.CharacteristicProperties + "]");
                }
            }
            return lines;
        }

        // ---------- 断开 ----------
        /// <summary>
        /// 同步版断开（供 Dispose / 兜底用）：关通知 + 释放对象，不做系统状态回读。
        /// </summary>
        public void Disconnect()
        {
            StopScan();
            _ = CloseNotifyAsync();
            ReleaseDevice();
        }

        /// <summary>
        /// 异步版断开（UI【断开】按钮用）：先关 CCCD 通知、释放 PC 侧对象，
        /// 再回读系统真实链路状态，把「到底断没断」如实告诉用户。
        /// 返回可直接打进日志的说明文字。
        /// </summary>
        public async Task<string> DisconnectAsync()
        {
            StopScan();
            string cccdNote = await CloseNotifyAsync();
            SoftRelease();
            // 显式断开必须无条件通知 UI 回到未连接态：系统蓝牙复用场景下只释放本进程
            // 接入，系统链路仍在（ConnectionStatus 保持 Connected），OnConnStatusChanged
            // 不会触发 Disconnected —— 不通知 UI 会卡在「已连接」：连接按钮灰、无法重新接入。
            // 之后系统链路若再断，事件回调会重复通知一次（false），UI 幂等无害。
            ConnectionChanged?.Invoke(false);

            // WinRT 没有「主动断开 BLE」的 API：Dispose 只是释放本进程的引用，
            // 是否真正断链由 Windows 蓝牙栈决定（无人持有时会断），否则要等对端
            // supervision timeout（本鼠标日志里 timeout=500 → 5 秒）。
            // 所以这里回读系统状态，别让用户以为「点了断开 = 链路一定断了」。
            ulong addr;
            BluetoothAddressType addrType;
            lock (_sync) { addr = _lastAddress; addrType = _lastAddrType; }
            if (addr == 0)
                return cccdNote + "PC 侧已释放。";

            // 分两次回读：Windows 断链有延迟，第一次仍连着不代表失败。
            string note = null;
            for (int i = 0; i < 2; i++)
            {
                await Task.Delay(i == 0 ? 1200 : 3000);
                note = await ProbeSystemLinkAsync(addr, addrType);
                if (note != null && note.IndexOf("已确认链路断开", StringComparison.Ordinal) >= 0)
                    return cccdNote + note;
            }
            return cccdNote + note;
        }

        /// <summary>回读系统视角的真实链路状态。断开（或系统无该设备）返回「已确认」开头的说明。</summary>
        private async Task<string> ProbeSystemLinkAsync(ulong addr, BluetoothAddressType addrType)
        {
            try
            {
                var probe = await BluetoothLEDevice.FromBluetoothAddressAsync(addr, addrType);
                if (probe == null)
                    return "系统已无该设备对象（可视为已断开）。";
                try
                {
                    if (probe.ConnectionStatus != BluetoothConnectionStatus.Connected)
                        return "Windows 已确认链路断开。";

                    // 注意：本工具默认是**未配对**的 GATT 直连，设备不会出现在
                    // 「蓝牙和其他设备」列表里，因此没有系统条目可供手动断开 ——
                    // 只能等对端 supervision timeout，或让鼠标重新上电。
                    // 已配对（应用内【配对】或系统设置配对）的设备则可在系统列表里手动断开。
                    return "PC 侧已释放，但 Windows 仍维持着链路。该设备未走系统配对，"
                         + "蓝牙设置里查不到它的条目、也无法在其中手动断开；"
                         + "需等对端 supervision timeout（本鼠标日志 timeout=500 → 5 秒）自然断开。";
                }
                finally { probe.Dispose(); }
            }
            catch (Exception ex)
            {
                return "PC 侧已释放；回读系统状态失败（" + ex.Message + "）。";
            }
        }

        /// <summary>
        /// 关闭 SPP(0x33F4) 与语音(0xB03E) 的 CCCD 通知（礼貌告知对端「PC 不再收 notify」）。
        /// 只解绑事件而不关 CCCD，对端会继续往一个已无人接收的链路上发通知。
        /// </summary>
        private async Task<string> CloseNotifyAsync()
        {
            GattCharacteristic nc, vc;
            lock (_sync) { nc = _notifyChar; vc = _voiceChar; }
            string note = "";

            if (nc != null)
            {
                try { nc.ValueChanged -= OnValueChanged; } catch { }
                try
                {
                    var st = await nc.WriteClientCharacteristicConfigurationDescriptorAsync(
                        GattClientCharacteristicConfigurationDescriptorValue.None);
                    note = st == GattCommunicationStatus.Success
                        ? "已关闭 notify 通知（0x33F4 CCCD）。"
                        : "关闭 notify 未成功（" + st + "）。";
                }
                catch (Exception ex)
                {
                    note = "关闭 notify 异常（" + ex.Message + "）。";
                }
            }

            // 语音 CCCD：断开时一并关掉（用户停用路径也会调 StopVoiceAsync，这里兜底）
            _voiceStreaming = false;
            if (vc != null)
            {
                try { vc.ValueChanged -= OnVoiceValueChanged; } catch { }
                try
                {
                    await vc.WriteClientCharacteristicConfigurationDescriptorAsync(
                        GattClientCharacteristicConfigurationDescriptorValue.None);
                }
                catch { /* 断开竞态：链路可能已不在，忽略 */ }
            }

            return note;
        }

        public void Dispose()
        {
            Disconnect();
        }
    }
}
