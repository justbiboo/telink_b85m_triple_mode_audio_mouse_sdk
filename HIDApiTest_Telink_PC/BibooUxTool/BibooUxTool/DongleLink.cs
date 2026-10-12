using System;

namespace BibooUxTool
{
    /// <summary>
    /// dongle HID 通道连接管理。
    /// payload 固定 7 字节；下行使用 Feature Report 0x09，上行使用 Input Report 0x08。
    /// </summary>
    public sealed class DongleLink : IDisposable
    {
        private IntPtr _h;
        private readonly object _sync = new object();

        public bool IsConnected
        {
            get { lock (_sync) { return _h != IntPtr.Zero; } }
        }

        /// <summary>
        /// 枚举并打开 dongle 的独立 Host Link HID Interface。
        /// 成功返回 null；失败返回错误描述。
        /// </summary>
        public string Connect()
        {
            lock (_sync)
            {
                if (_h != IntPtr.Zero)
                    return null;

                IntPtr h = BibooHidNative.bh_open_default();
                if (h == IntPtr.Zero)
                {
                    string err = BibooHidNative.LastError();
                    return string.IsNullOrEmpty(err) ? "bh_open_default 失败" : err;
                }
                _h = h;
                return null;
            }
        }

        public void Disconnect()
        {
            lock (_sync)
            {
                if (_h != IntPtr.Zero)
                {
                    BibooHidNative.bh_close(_h);
                    _h = IntPtr.Zero;
                }
            }
        }

        /// <summary>发送 7 字节 payload。成功返回 null；失败返回错误描述。</summary>
        public string Send(byte[] payload7)
        {
            lock (_sync)
            {
                if (_h == IntPtr.Zero)
                    return "未连接";
                if (payload7 == null || payload7.Length != 7)
                    return "payload 必须为 7 字节";

                int r = BibooHidNative.bh_send(_h, payload7, payload7.Length);
                if (r != 0)
                {
                    string err = BibooHidNative.LastError();
                    return string.IsNullOrEmpty(err) ? "bh_send 失败" : err;
                }
                return null;
            }
        }

        /// <summary>阻塞等待设备主动发送的 Input Report。返回 0 成功 / -1 失败。</summary>
        public int TryRead(out byte[] payload7, out string err)
        {
            lock (_sync)
            {
                payload7 = new byte[7];
                err = null;
                if (_h == IntPtr.Zero)
                {
                    err = "未连接";
                    return -1;
                }

                int r = BibooHidNative.bh_read(_h, payload7, payload7.Length);
                if (r < 0)
                {
                    err = BibooHidNative.LastError();
                    if (string.IsNullOrEmpty(err))
                        err = "bh_read 失败";
                    return -1;
                }
                return 0;
            }
        }

        /// <summary>最多等待 timeoutMs 毫秒。返回 1=收到，0=超时，-1=失败。</summary>
        public int TryReadTimeout(int timeoutMs, out byte[] payload7, out string err)
        {
            lock (_sync)
            {
                payload7 = new byte[7];
                err = null;
                if (_h == IntPtr.Zero)
                {
                    err = "未连接";
                    return -1;
                }

                int r = BibooHidNative.bh_read_timeout(_h, payload7, payload7.Length, timeoutMs);
                if (r == 0)
                    return 0;
                if (r < 0)
                {
                    err = BibooHidNative.LastError();
                    if (string.IsNullOrEmpty(err))
                        err = "bh_read_timeout 失败";
                    return -1;
                }
                return 1;
            }
        }

        public string GetOpenedPath()
        {
            return BibooHidNative.OpenedPath;
        }

        public void Dispose()
        {
            Disconnect();
        }
    }
}
