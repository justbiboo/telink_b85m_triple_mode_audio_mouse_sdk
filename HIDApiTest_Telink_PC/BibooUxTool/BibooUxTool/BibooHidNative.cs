using System;
using System.Runtime.InteropServices;
using System.Text;

namespace BibooUxTool
{
    /// <summary>
    /// BibooHid.dll 的 P/Invoke 声明（x64，Cdecl）。
    /// 通道：dongle report 0x05，payload 7 字节，双向 EP0 控制传输。
    /// </summary>
    internal static class BibooHidNative
    {
        private const string DllName = "BibooHid.dll";
        private const CallingConvention CC = CallingConvention.Cdecl;

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern IntPtr bh_open_default();

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern void bh_close(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int bh_send(IntPtr h, byte[] payload, int payloadLen);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int bh_read(IntPtr h, byte[] outPayload, int bufLen);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int bh_last_error(byte[] buf, int bufLen);

        [DllImport(DllName, CallingConvention = CC)]
        private static extern IntPtr bh_opened_path();

        /// <summary>最近一次成功打开的设备路径（未连接返回空串）</summary>
        internal static string OpenedPath
        {
            get
            {
                IntPtr p = bh_opened_path();
                if (p == IntPtr.Zero)
                    return string.Empty;
                return Marshal.PtrToStringAnsi(p) ?? string.Empty;
            }
        }

        /// <summary>取 DLL 侧最近一次错误描述（ASCII）</summary>
        internal static string LastError()
        {
            var buf = new byte[256];
            int n = bh_last_error(buf, buf.Length);
            if (n <= 0)
                return string.Empty;
            int len = Math.Min(n, buf.Length - 1);
            return Encoding.ASCII.GetString(buf, 0, len);
        }
    }
}
