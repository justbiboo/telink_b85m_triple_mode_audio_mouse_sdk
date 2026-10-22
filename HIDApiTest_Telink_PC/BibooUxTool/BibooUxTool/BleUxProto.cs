using System;
using System.Text;
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

        // OTA（Telink BLE OTA server，stack/ble/service/uuid.h）——两个都是 128-bit UUID：
        //   TELINK_OTA_UUID_SERVICE = 12 19 0D 0C 0B 0A 09 08 07 06 05 04 03 02 01 00（wire，LSB first）
        //   TELINK_SPP_DATA_OTA     = 12 2B 0D 0C 0B 0A 09 08 07 06 05 04 03 02 01 00（wire，写特征）
        // 规范文本形式（nRF Connect / 安卓见到的样子）：
        //   service = 00010203-0405-0607-0809-0a0b0c0d1912
        //   write   = 00010203-0405-0607-0809-0a0b0c0d2b12
        // 属性 READ | WRITE_WITHOUT_RSP，写回调 user_ota_write → otaWrite()。
        // 见 8208 量产 SDK：vendor/8208_dual_mouse/AAA_app_att.c:502-507（特征定义）、661-668（ATT 表），
        // 条件 BLE_OTA_SERVER_ENABLE||BOOTLOADER_OTA_ENABLE（512K flash 时 BLE_OTA_SERVER_ENABLE=1）。
        // ⚠ 这两个是 128-bit UUID（不是 16-bit 短 UUID），不能走 FromShort。
        // ⚠ 历史 bug（2026-10-20 修）：原实现把 Data1/2/3 写成 0C0D0B0A/0908/0706，算出的
        //   GUID 是 0c0d0b0a-0908-0706-0504-03020100122b —— 与设备上的 00010203-0405-0607-…
        //   完全不同，导致「未发现 OTA 写特征」，BLE OTA 永远走不通。
        public static Guid OtaServiceUuid => MakeTelinkUuid(0x1912);
        public static Guid OtaWriteUuid   => MakeTelinkUuid(0x2B12);

        /// <summary>
        /// 兜底写法：若某平台直接把 16B wire 字节 memcpy 进 GUID（不做 128-bit 数值换算），
        /// 呈现出来的就是这一形式（Data1/2/3 由 wire 前 8 字节按小端读出）。
        /// 枚举时两种都认，避免平台差异导致又搜不到。
        /// </summary>
        public static Guid OtaServiceUuidRaw => MakeTelinkUuidRaw(0x1912);
        public static Guid OtaWriteUuidRaw   => MakeTelinkUuidRaw(0x2B12);

        // ================= 设备信息特征 DevInf_aaa（当前版本号）=================
        // 固件：vendor/8208_dual_mouse/AAA_app_att.c:27（数组）/ 509-516（特征）/ 667-668（ATT 表）
        //   my_DevInfUUID（wire，LSB first，即源码里的 16 个字节原样）：
        //     79 61 6E 67 6B 65 71 69 79 61 6E 67 6B 65 71 69
        //   属性 CHAR_PROP_READ + ATT_PERMISSIONS_READ，长度 15；
        //   挂在 OTA 服务（TELINK_OTA_UUID_SERVICE）下、紧跟 OTA 写特征之后
        //   → handle = OTA_CMD_INF_DP_H = 0x0010（0x000E = OTA 写特征，已实测确认）。
        // 结论：**只读、不需要配对/加密、不需要广播**，已配对但没广播的设备也能读。
        // 规范文本形式（= wire 逆序）：6971656b-676e-6179-6971-656b676e6179
        public static Guid DevInfoUuid => new Guid(0x6971656B, 0x676E, 0x6179,
            0x69, 0x71, 0x65, 0x6B, 0x67, 0x6E, 0x61, 0x79);

        /// <summary>DevInf 特征的 wire-memcpy 兜底形式（见 <see cref="OtaWriteUuidRaw"/> 的说明）。</summary>
        public static Guid DevInfoUuidRaw => new Guid(0x676E6179, 0x656B, 0x6971,
            0x79, 0x61, 0x6E, 0x67, 0x6B, 0x65, 0x71, 0x69);

        /// <summary>
        /// Telink 128-bit UUID 的规范文本形式：00010203-0405-0607-0809-0A0B0C0D&lt;t1&gt;&lt;t2&gt;。
        /// tail 传 wire 字节按小端读出的 u16（例：wire 的 "12 2B" → 0x2B12 → 文本尾部 "2b12"）。
        /// </summary>
        public static Guid MakeTelinkUuid(ushort tail)
        {
            return new Guid(
                0x00010203,
                0x0405,
                0x0607,
                0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                (byte)(tail >> 8), (byte)(tail & 0xFF));
        }

        /// <summary>wire 字节原样 memcpy 进 GUID 时的写法（见 <see cref="OtaWriteUuidRaw"/>）。</summary>
        public static Guid MakeTelinkUuidRaw(ushort tail)
        {
            return new Guid(
                (int)(0x0C0D0000u | tail),
                0x0A0B,
                0x0809,
                0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00);
        }

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

        /// <summary>
        /// 固件版本（如 "v2.8"），从广播 Manufacturer Data 解析：
        /// 固件 AAA_app.c 广播 `05 FF <company_lo> <company_hi> FW_VER_MAJOR FW_VER_MINOR`。
        /// 已配对列表来自系统 AEP，没有广播数据，需另跑一次短扫描才能补上。
        /// </summary>
        public string FwVersion { get; set; }

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
            string ver = string.IsNullOrEmpty(FwVersion) ? "" : "  " + FwVersion;
            if (IsPairedItem)
            {
                // 已配对条目：一眼看出「连没连、本工具接没接入」
                string state = Attached ? "● 已连接（已接入）"
                             : SystemConnected ? "● 系统已连接（未接入）"
                             : "○ 未连接";
                return string.Format("{0}  {1}  {2}{3}", state, Name ?? "(未命名设备)", Mac, ver);
            }
            return string.Format("{0}  {1}{2}  {3}dBm{4}",
                Name ?? "(无名称)", Mac, AddrTag, Rssi, ver);
        }
    }

    /// <summary>
    /// DevInf_aaa（15 字节，GATT 只读）解析结果 —— 鼠标的「当前版本号」。
    ///
    /// 字节位置由**量产固件二进制实测**确定（不是照抄注译）：
    ///   00 00 00 00 04 00 03 03 01 01 01 00 02 08 00
    ///   idx 0-3   sensor information（固件里恒 0）
    ///   idx 4     MOUSE_DATA_LEN_AAA（4；老仓鼠 dongle 兼容模式为 6）
    ///   idx 5     project name（0）
    ///   idx 6-8   IC lib SDK 版本      → 3.3.1
    ///   idx 9-11  鼠标 SDK 版本        → 1.1.0
    ///   idx 12-14 用户固件版本 FW_VER_MAJOR/MINOR/PATCH → 2.8.0
    ///
    /// ⚠ Telink 模板 AAA_app_att.c 里这三行注释写的是「12,13,14 ic / 9,10,11 mouse /
    ///   6,7,8 user fw」，与 C 的顺序初始化不符（照它读会把 IC SDK 版本当固件版本）。
    ///   已用 8208_dual_mouse.bin 反查 + GATT 读回双重确认：固件版本在 12-14。
    /// </summary>
    public sealed class BleDevInf
    {
        public byte[] Raw { get; set; }

        public int MouseDataLen { get; set; }
        public int ProjectName { get; set; }

        public int IcSdkMajor { get; set; }
        public int IcSdkMinor { get; set; }
        public int IcSdkPatch { get; set; }

        public int MouseSdkMajor { get; set; }
        public int MouseSdkMinor { get; set; }
        public int MouseSdkPatch { get; set; }

        public int FwMajor { get; set; }
        public int FwMinor { get; set; }
        public int FwPatch { get; set; }

        /// <summary>与广播/文件名习惯一致的两段式版本，如 "v2.8"。</summary>
        public string FwText
        {
            get { return "v" + FwMajor + "." + FwMinor; }
        }

        public string FwFullText
        {
            get { return FwMajor + "." + FwMinor + "." + FwPatch; }
        }

        public string IcSdkText
        {
            get { return IcSdkMajor + "." + IcSdkMinor + "." + IcSdkPatch; }
        }

        public string MouseSdkText
        {
            get { return MouseSdkMajor + "." + MouseSdkMinor + "." + MouseSdkPatch; }
        }

        public string RawHex
        {
            get
            {
                if (Raw == null || Raw.Length == 0)
                    return "";
                var sb = new StringBuilder(Raw.Length * 3);
                for (int i = 0; i < Raw.Length; i++)
                {
                    if (i > 0)
                        sb.Append(' ');
                    sb.Append(Raw[i].ToString("X2"));
                }
                return sb.ToString();
            }
        }

        /// <summary>一行摘要，给状态栏 / 日志用。</summary>
        public string Summary
        {
            get
            {
                return "固件 " + FwText + " · 鼠标 SDK " + MouseSdkText + " · IC SDK " + IcSdkText;
            }
        }

        /// <summary>解析 DevInf_aaa 原始字节。长度不足 15 返回 null。</summary>
        public static BleDevInf Parse(byte[] d)
        {
            if (d == null || d.Length < 15)
                return null;
            return new BleDevInf
            {
                Raw = d,
                MouseDataLen = d[4],
                ProjectName = d[5],
                IcSdkMajor = d[6],
                IcSdkMinor = d[7],
                IcSdkPatch = d[8],
                MouseSdkMajor = d[9],
                MouseSdkMinor = d[10],
                MouseSdkPatch = d[11],
                FwMajor = d[12],
                FwMinor = d[13],
                FwPatch = d[14]
            };
        }
    }

    /// <summary>MAC 地址 <-> WinRT BluetoothAddress(ulong) 互转。</summary>
    internal static class BleMac
    {        // WinRT 的 BluetoothAddress 为 48 位整数，首字节为 MAC 最高位。
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
