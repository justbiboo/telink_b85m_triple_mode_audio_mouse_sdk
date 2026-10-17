using System;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;

namespace BibooUxTool
{
    /// <summary>
    /// PC ↔ DUT 直连 BLE 通道（Surface 0）的协议常量与工具。
    /// 与 dongle 链路（Surface 1：PC ↔ dongle HID 0x09/0x08）平行；
    /// 这里 PC 直接以 BLE central 连接鼠标，走鼠标 GATT 上的 SPP 服务
    /// （同 ux_spp.h / ux_ft_proto.h，固件端由 ux_spp_init 注册）。
    /// </summary>
    internal static class BleUxProto
    {
        // ===== GATT UUID（16-bit → 标准 Bluetooth base UUID）=====
        // 鼠标 SPP 数据通道（vendor/user/spp/ux_spp.h）：
        public const ushort SppWriteUuid16  = 0x33F3; // C2S：PC → 鼠标（write without response，20B 帧）
        public const ushort SppNotifyUuid16 = 0x33F4; // S2C：鼠标 → PC（notify）

        // 语音流通道（vendor/user/voice_dec/ux_voice_dec.h）：service 0xFFF0，char 0xB03E（my_Data）
        public const ushort VoiceServiceUuid16 = 0xFFF0;
        public const ushort VoiceCharUuid16    = 0xB03E;

        // 电池电量（标准 GATT Battery Service 0x180F / Battery Level 0x2A19）：
        // 作为「链路活性读」的兜底探针（当 0x33F4 不可读时）。
        public const ushort BatteryLevelUuid16 = 0x2A19;
        public static Guid BatteryLevelUuid => FromShort(BatteryLevelUuid16);

        public static Guid SppWriteUuid   => FromShort(SppWriteUuid16);
        public static Guid SppNotifyUuid  => FromShort(SppNotifyUuid16);
        public static Guid VoiceServiceUuid => FromShort(VoiceServiceUuid16);
        public static Guid VoiceCharUuid    => FromShort(VoiceCharUuid16);

        // 16-bit UUID → 128-bit：0000XXXX-0000-1000-8000-00805F9B34FB
        public static Guid FromShort(ushort uuid16)
        {
            return new Guid(
                (int)uuid16,                 // Data1
                0x0000,                       // Data2
                0x1000,                       // Data3
                0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB); // Data4
        }

        // ===== SPP 20B 帧布局（与 UxFtProto.Spp* 一致）=====
        public const int SppSize      = 20;
        public const int SppOffsCmd   = 0; // [0] command
        public const int SppOffsArg   = 1; // [1] argument（如 auth Tick）
        public const int SppOffsResult = 2; // [2] result / state in responses

        // DUT 命令（与 UxFtProto.Dut* / ux_spp.h 一致）
        // 注意：0x01 PING 只属于 8258 产测 DUT 固件（ux_ft_dut.c）；
        // D21 Pro(8208) 鼠标固件的 spp_device.c 只认 0x77/0x78，且被
        // USER_DATA_AUTHORIZATION 宏包着 —— 不能拿它当连接判据。
        public const byte DutCmdPing      = 0x01;
        public const byte DutCmdAesEnc    = 0x10;
        public const byte DutCmdAesDec    = 0x11;
        public const byte DutCmdAuth      = 0x12;
        public const byte DutCmdTouchStart = 0x20;
        public const byte DutCmdTouchStop  = 0x21;
        public const byte AuthCmd         = 0x77; // [1]=Tick → [2]=AuthRspResult_e（会写flash，勿当ping频繁发）
        public const byte GetAuthStateCmd = 0x78; // → [2]=auth_status_t（只读，可作通道验证）

        // 滑动配置参数（D21 Pro 8208 固件 spp_device.c，2026-10 新增；存 flash 重启保持）
        // 参数分 iOS / Android 两组，各 12 字节；BLE MTU=23 → notify 单包上限 20B，
        // 故两组分四个命令收发（0x80/0x81 读、0x79/0x82 写），单包均 ≤15 字节。
        public const byte SetSlideParamCmd = 0x79; // 写iOS组：[79][0C]+12B → 回[0]=79 [1]=3 [2]=status(1成功)
        public const byte GetSlideParamCmd = 0x80; // 读iOS组：[80] → 回[0]=80 [1]=0F [2]=valid [3..14]=12B参数
        public const byte GetSlideParamAndroidCmd = 0x81; // 读Android组：回包格式同 0x80
        public const byte SetSlideParamAndroidCmd = 0x82; // 写Android组：[82][0C]+12B → 回[0]=82 [1]=3 [2]=status
        public const int  SlideParamGroupSize = 12;  // 单OS组 v2：2方向×6B [fwd_lo,fwd_hi,ret_lo,ret_hi,step_len,step_cnt]
                                                   // 方向：0=上下滑动 1=左右滑动；前进/回位间隔=小端u16(ms)，各1~100ms（与固件校验一致）
        public const int  SlideParamSize      = 24;  // 完整参数 = iOS组12 + Android组12
        public const int  SlideParamRspSize   = 15;  // 单组回包总长 = 3 + 12

        // SPP 鉴权结果（ux_spp.h AuthRspResult_e）
        public const byte AuthRspFail    = 0;
        public const byte AuthRspAlready = 1;
        public const byte AuthRspSuccess = 2;
    }

    /// <summary>BLE 设备条目：既用于广播扫描结果（DeviceId 为空），也用于「系统已配对列表」（DeviceId 非空）。</summary>
    public sealed class BleDeviceInfo
    {
        private ulong _address;

        public ulong BluetoothAddress
        {
            get { return _address; }
            set { _address = value; AddressKnown = true; }
        }

        /// <summary>地址是否已知（已配对条目从 AEP 属性解析失败时为 false，显示 ??）。</summary>
        public bool AddressKnown { get; set; }

        public string Name { get; set; }
        public short Rssi { get; set; }
        public DateTime LastSeen { get; set; }

        /// <summary>地址类型（Public/Random）。随机地址设备必须带上它才连得上。</summary>
        public BluetoothAddressType AddressType { get; set; }

        /// <summary>
        /// 系统已配对设备的 AEP Id（DeviceInformation.Id）。非空 = 来自「系统已配对列表」，
        /// 连接走 FromIdAsync（不怕随机地址轮换，已配对设备的 RPA 由系统解析）；为空 = 来自广播扫描。
        /// </summary>
        public string DeviceId { get; set; }

        /// <summary>系统视角是否已连接（已配对列表实时刷新用）。</summary>
        public bool SystemConnected { get; set; }

        /// <summary>本工具是否已接入（GattSession + SPP 特征 + 活性校验通过）。</summary>
        public bool Attached { get; set; }

        public bool IsPairedItem => !string.IsNullOrEmpty(DeviceId);

        public string Mac => AddressKnown ? BleMac.ToMacString(_address) : "??:??:??:??:??:??";

        /// <summary>随机地址在列表里加 [R] 标记，便于排查「连不上」。</summary>
        public string AddrTag => AddressType == BluetoothAddressType.Random ? " [R]" : "";

        public override string ToString()
        {
            if (IsPairedItem)
            {
                // 已配对条目：一眼看出「连没连、本工具接没接入」
                string state = Attached ? "● 已连接（已接入）"
                             : SystemConnected ? "● 系统已连接（未接入）"
                             : "○ 未连接";
                return string.Format("{0}  {1}  {2}", state, Name ?? "(未命名设备)", Mac);
            }
            return string.Format("{0}  {1}{2}  {3}dBm",
                Name ?? "(无名称)", Mac, AddrTag, Rssi);
        }
    }

    /// <summary>MAC 地址 <-> WinRT BluetoothAddress(ulong) 互转。</summary>
    internal static class BleMac
    {
        // WinRT 的 BluetoothAddress 为 48 位整数，首字节为 MAC 最高位。
        public static string ToMacString(ulong address)
        {
            return string.Format("{0:X2}:{1:X2}:{2:X2}:{3:X2}:{4:X2}:{5:X2}",
                (byte)(address >> 40), (byte)(address >> 32), (byte)(address >> 24),
                (byte)(address >> 16), (byte)(address >> 8), (byte)address);
        }

        public static bool TryParse(string mac, out ulong address)
        {
            address = 0;
            if (string.IsNullOrWhiteSpace(mac))
                return false;
            var parts = mac.Split(new char[] { ':', '-', ' ' },
                StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length != 6)
                return false;
            ulong v = 0;
            foreach (var p in parts)
            {
                if (!byte.TryParse(p, System.Globalization.NumberStyles.HexNumber,
                        System.Globalization.CultureInfo.InvariantCulture, out byte b))
                    return false;
                v = (v << 8) | b;
            }
            address = v;
            return true;
        }
    }
}
