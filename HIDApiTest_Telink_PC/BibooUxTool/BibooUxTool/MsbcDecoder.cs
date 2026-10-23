#pragma warning disable 0219
using System;

namespace BibooUxTool
{
    /// <summary>
    /// mSBC 解码器（57B 帧 -> 240B PCM：120 样本 x 16bit LE @ 16kHz mono）。
    /// 移植自 ble_msbc_to_wav_v1/src/msbc_decode.c 的生效实现（#else 分支, line 1320-2616，
    /// 即 BlueZ sbc.c 的 mSBC 定制版）；常量表由脚本从 C 源程序化提取。
    ///
    /// 运算保真说明：C 原版用 MULA(a,b,res)=a*b+res 的嵌套链做矩阵乘，int32 可能回绕。
    /// 由于 Z/2^32 环上加减乘满足结合/交换律，此处改用等价循环求和（回绕结果逐位一致），
    /// 默认 unchecked 语义即 x86 32 位回绕行为。
    ///
    /// 协议口径（与 SDK ux_mic_stream / ux_voice_dec 对齐）：
    ///   - 固定参数：syncword 0xAD / 16kHz / mono / 8 子带 / 15 块 / bitpool 26 / LOUDNESS
    ///   - data[3] 为 CRC8（覆盖 data[1..2] + 8 个 4bit scale factor 共 48 bit），失败返回 -3
    ///   - 合成滤波器历史 V[170] 与 offset[16] 跨帧保持：一个实例对应一路语音会话，
    ///     新会话（鼠标重连）必须 Reset()，否则音质撕裂。
    /// 已验证：test_mic_demo/exe/enc.bin（1013 帧）解码结果与 pcm.bin 字节级一致。
    /// </summary>
    public sealed class MsbcDecoder
    {
        public const int FrameLen = 57;            // mSBC 帧字节数
        public const int PcmSamplesPerFrame = 120; // 15 块 x 8 子带
        public const int PcmBytesPerFrame = 240;   // 120 样本 x 2B

        private const int MSBC_SYNCWORD = 0xAD;
        private const int MSBC_CHANNELS = 1;
        private const int MSBC_SUBBANDS = 8;
        private const int MSBC_BLOCKS = 15;
        private const int MSBC_BITPOOL = 26;
        private const int SBC_FREQ_16000 = 0;      // sbc_offset8 行索引
        private const int SBCDEC_FIXED_EXTRA_BITS = 2;

        // ---- 常量表（SS8/SN8 = ASR(val,14)，已由脚本展开） ----
        private static readonly int[] Synmatrix8 = new int[]
        {
        5792, -5793, -5793, 5792, 5792, -5793, -5793, 5792,
        4551, -8035, 1598, 6811, -6812, -1599, 8034, -4552,
        3134, -7569, 7568, -3135, -3135, 7568, -7569, 3134,
        1598, -4552, 6811, -8035, 8034, -6812, 4551, -1599,
        0, 0, 0, 0, 0, 0, 0, 0,
        -1599, 4551, -6812, 8034, -8035, 6811, -4552, 1598,
        -3135, 7568, -7569, 3134, 3134, -7569, 7568, -3135,
        -4552, 8034, -1599, -6812, 6811, 1598, -8035, 4551,
        -5793, 5792, 5792, -5793, -5793, 5792, 5792, -5793,
        -6812, 1598, 8034, 4551, -4552, -8035, -1599, 6811,
        -7569, -3135, 3134, 7568, 7568, 3134, -3135, -7569,
        -8035, -6812, -4552, -1599, 1598, 4551, 6811, 8034,
        -8192, -8192, -8192, -8192, -8192, -8192, -8192, -8192,
        -8035, -6812, -4552, -1599, 1598, 4551, 6811, 8034,
        -7569, -3135, 3134, 7568, 7568, 3134, -3135, -7569,
        -6812, 1598, 8034, 4551, -4552, -8035, -1599, 6811,
        };

        private static readonly int[] Proto80M0 = new int[]
        {
        0, -1484, -17826, 17825, 1483, -42, -2105, -21754,
        13942, 916, -90, -2742, -25579, 10243, 432, -146,
        -3342, -29150, 6844, 46, -216, -3842, -32314, 3837,
        -237, -299, -4170, -34935, 1288, -424, -388, -4253,
        -36898, -767, -523, -468, -4016, -38114, -2322, -552,
        };

        private static readonly int[] Proto80M1 = new int[]
        {
        -528, -3392, -38524, -3392, -528, -552, -2322, -38114,
        -4016, -468, -523, -767, -36898, -4253, -388, -424,
        1288, -34935, -4170, -299, -237, 3837, -32314, -3842,
        -216, 46, 6844, -29150, -3342, -146, 432, 10243,
        -25579, -2742, -90, 916, 13942, -21754, -2105, -42,
        };

        private static readonly byte[] CrcTable = new byte[]
        {
        0x00, 0x1D, 0x3A, 0x27, 0x74, 0x69, 0x4E, 0x53, 0xE8, 0xF5, 0xD2, 0xCF, 0x9C, 0x81, 0xA6, 0xBB,
        0xCD, 0xD0, 0xF7, 0xEA, 0xB9, 0xA4, 0x83, 0x9E, 0x25, 0x38, 0x1F, 0x02, 0x51, 0x4C, 0x6B, 0x76,
        0x87, 0x9A, 0xBD, 0xA0, 0xF3, 0xEE, 0xC9, 0xD4, 0x6F, 0x72, 0x55, 0x48, 0x1B, 0x06, 0x21, 0x3C,
        0x4A, 0x57, 0x70, 0x6D, 0x3E, 0x23, 0x04, 0x19, 0xA2, 0xBF, 0x98, 0x85, 0xD6, 0xCB, 0xEC, 0xF1,
        0x13, 0x0E, 0x29, 0x34, 0x67, 0x7A, 0x5D, 0x40, 0xFB, 0xE6, 0xC1, 0xDC, 0x8F, 0x92, 0xB5, 0xA8,
        0xDE, 0xC3, 0xE4, 0xF9, 0xAA, 0xB7, 0x90, 0x8D, 0x36, 0x2B, 0x0C, 0x11, 0x42, 0x5F, 0x78, 0x65,
        0x94, 0x89, 0xAE, 0xB3, 0xE0, 0xFD, 0xDA, 0xC7, 0x7C, 0x61, 0x46, 0x5B, 0x08, 0x15, 0x32, 0x2F,
        0x59, 0x44, 0x63, 0x7E, 0x2D, 0x30, 0x17, 0x0A, 0xB1, 0xAC, 0x8B, 0x96, 0xC5, 0xD8, 0xFF, 0xE2,
        0x26, 0x3B, 0x1C, 0x01, 0x52, 0x4F, 0x68, 0x75, 0xCE, 0xD3, 0xF4, 0xE9, 0xBA, 0xA7, 0x80, 0x9D,
        0xEB, 0xF6, 0xD1, 0xCC, 0x9F, 0x82, 0xA5, 0xB8, 0x03, 0x1E, 0x39, 0x24, 0x77, 0x6A, 0x4D, 0x50,
        0xA1, 0xBC, 0x9B, 0x86, 0xD5, 0xC8, 0xEF, 0xF2, 0x49, 0x54, 0x73, 0x6E, 0x3D, 0x20, 0x07, 0x1A,
        0x6C, 0x71, 0x56, 0x4B, 0x18, 0x05, 0x22, 0x3F, 0x84, 0x99, 0xBE, 0xA3, 0xF0, 0xED, 0xCA, 0xD7,
        0x35, 0x28, 0x0F, 0x12, 0x41, 0x5C, 0x7B, 0x66, 0xDD, 0xC0, 0xE7, 0xFA, 0xA9, 0xB4, 0x93, 0x8E,
        0xF8, 0xE5, 0xC2, 0xDF, 0x8C, 0x91, 0xB6, 0xAB, 0x10, 0x0D, 0x2A, 0x37, 0x64, 0x79, 0x5E, 0x43,
        0xB2, 0xAF, 0x88, 0x95, 0xC6, 0xDB, 0xFC, 0xE1, 0x5A, 0x47, 0x60, 0x7D, 0x2E, 0x33, 0x14, 0x09,
        0x7F, 0x62, 0x45, 0x58, 0x0B, 0x16, 0x31, 0x2C, 0x97, 0x8A, 0xAD, 0xB0, 0xE3, 0xFE, 0xD9, 0xC4,
        };

        private static readonly int[,] SbcOffset8 = new int[,]
        {
        { -2, 0, 0, 0, 0, 0, 0, 1 },
        { -3, 0, 0, 0, 0, 0, 1, 2 },
        { -4, 0, 0, 0, 0, 0, 1, 2 },
        { -4, 0, 0, 0, 0, 0, 1, 2 },
        };

        // ---- 解码器状态（对应 C 的 static sbc_de_ctx，跨帧保持） ----
        private readonly int[] _v = new int[170];
        private readonly int[] _offset = new int[16];
        private bool _init;

        // 帧解析暂存（随帧覆盖）
        private readonly uint[] _scaleFactor = new uint[MSBC_CHANNELS * MSBC_SUBBANDS];
        private readonly int[] _sbSample = new int[MSBC_BLOCKS * MSBC_CHANNELS * MSBC_SUBBANDS];
        private readonly short[] _pcmSample = new short[MSBC_CHANNELS * MSBC_BLOCKS * MSBC_SUBBANDS];

        /// <summary>最近一次 Decode 实际写出的 PCM 字节数（对应 C 的 *written）。</summary>
        public int LastWritten { get; private set; }

        /// <summary>重置合成滤波器历史（等价 msbc_release_ctx + 下次首帧重新 init）。</summary>
        public void Reset()
        {
            _init = false;
            Array.Clear(_v, 0, _v.Length);
            Array.Clear(_offset, 0, _offset.Length);
            LastWritten = 0;
        }

        /// <summary>
        /// 解一帧 mSBC。返回帧长(57)或负错误码（-1 数据过短 / -2 sync 错 / -3 CRC 错），
        /// 与 C 版 sbc_decode 语义一致；输出为小端 16bit PCM。
        /// </summary>
        public int Decode(byte[] input, int inOff, int inputLen, byte[] output, int outOff, int outputLen)
        {
            if (input == null)
                return -1;

            int framelen = MsbcUnpackFrame(input, inOff, inputLen);

            if (!_init)
            {
                // sbc_decoder_init：首帧清零历史并初始化 offset
                Array.Clear(_v, 0, _v.Length);
                for (int i = 0; i < MSBC_SUBBANDS * 2; i++)
                    _offset[i] = 10 * i + 10;
                _init = true;
            }

            if (output == null)
                return framelen;

            LastWritten = 0;
            if (framelen <= 0)
                return framelen;

            int samples = SynthesizeAudio();   // 120

            if (outputLen < samples * MSBC_CHANNELS * 2)
                samples = outputLen / (MSBC_CHANNELS * 2);

            int wpos = 0;
            for (int i = 0; i < samples; i++)
            {
                for (int ch = 0; ch < MSBC_CHANNELS; ch++)
                {
                    short s = _pcmSample[ch * (MSBC_BLOCKS * MSBC_SUBBANDS) + i];
                    output[outOff + wpos++] = (byte)(s & 0x00ff);
                    output[outOff + wpos++] = (byte)((s & 0xff00) >> 8);
                }
            }

            LastWritten = wpos;
            return framelen;
        }

        /// <summary>便捷封装：一帧 57B -> 240B PCM。成功返回 true。</summary>
        public bool TryDecodeFrame(byte[] frame, int frameOff, byte[] pcmOut, int pcmOff)
        {
            int r = Decode(frame, frameOff, FrameLen, pcmOut, pcmOff, PcmBytesPerFrame);
            return r > 0 && LastWritten == PcmBytesPerFrame;
        }

        // ---- msbc_unpack_frame ----
        private int MsbcUnpackFrame(byte[] data, int off, int len)
        {
            if (len < 4)
                return -1;
            if (data[off] != MSBC_SYNCWORD)
                return -2;
            // mSBC 参数固定：16kHz / mono / 8 子带 / 15 块 / bitpool 26 / LOUDNESS
            return UnpackFrameInternal(data, off, len);
        }

        // ---- sbc_unpack_frame_internal ----
        private int UnpackFrameInternal(byte[] data, int off, int len)
        {
            int consumed = 32;
            byte[] crcHeader = new byte[11];
            crcHeader[0] = data[off + 1];
            crcHeader[1] = data[off + 2];
            int crcPos = 16;

            if (len * 8 < consumed + (4 * MSBC_SUBBANDS * MSBC_CHANNELS))
                return -1;

            for (int sb = 0; sb < MSBC_SUBBANDS; sb++)
            {
                uint sf = (uint)((data[off + (consumed >> 3)] >> (4 - (consumed & 0x7))) & 0x0F);
                _scaleFactor[sb] = sf;
                crcHeader[crcPos >> 3] |= (byte)(sf << (4 - (crcPos & 0x7)));
                consumed += 4;
                crcPos += 4;
            }

            if (data[off + 3] != SbcCrc8(crcHeader, crcPos))
                return -3;

            int[,] bits = new int[MSBC_CHANNELS, MSBC_SUBBANDS];
            CalculateBits(bits);

            uint[,] levels = new uint[MSBC_CHANNELS, MSBC_SUBBANDS];
            for (int ch = 0; ch < MSBC_CHANNELS; ch++)
                for (int sb = 0; sb < MSBC_SUBBANDS; sb++)
                    levels[ch, sb] = (1u << bits[ch, sb]) - 1u;

            for (int blk = 0; blk < MSBC_BLOCKS; blk++)
            {
                for (int ch = 0; ch < MSBC_CHANNELS; ch++)
                {
                    for (int sb = 0; sb < MSBC_SUBBANDS; sb++)
                    {
                        if (levels[ch, sb] == 0)
                        {
                            _sbSample[(blk * MSBC_CHANNELS + ch) * MSBC_SUBBANDS + sb] = 0;
                            continue;
                        }

                        int shift = (int)_scaleFactor[ch * MSBC_SUBBANDS + sb] + 1 + SBCDEC_FIXED_EXTRA_BITS;

                        uint audioSample = 0;
                        for (int bit = 0; bit < bits[ch, sb]; bit++)
                        {
                            if (consumed > len * 8)
                                return -1;
                            if (((data[off + (consumed >> 3)] >> (7 - (consumed & 0x7))) & 0x01) != 0)
                                audioSample |= 1u << (bits[ch, sb] - bit - 1);
                            consumed++;
                        }

                        // C: (((audio_sample << 1) | 1) << shift) / levels - (1 << shift)
                        //    （C 经 shift_left64/div64 实现，值域内与 long 直算等价）
                        long tmp = (long)(((ulong)audioSample << 1) | 1UL) << shift;
                        tmp /= levels[ch, sb];
                        _sbSample[(blk * MSBC_CHANNELS + ch) * MSBC_SUBBANDS + sb] =
                            (int)(tmp - (1L << shift));
                    }
                }
            }

            if ((consumed & 0x7) != 0)
                consumed += 8 - (consumed & 0x7);

            return consumed >> 3;
        }

        // ---- sbc_calculate_bits_internal（MONO + LOUDNESS 路径） ----
        private void CalculateBits(int[,] bits)
        {
            int[,] bitneed = new int[MSBC_CHANNELS, MSBC_SUBBANDS];
            int sf = SBC_FREQ_16000;

            for (int ch = 0; ch < MSBC_CHANNELS; ch++)
            {
                int maxBitneed = 0;
                for (int sb = 0; sb < MSBC_SUBBANDS; sb++)
                {
                    if (_scaleFactor[ch * MSBC_SUBBANDS + sb] == 0)
                        bitneed[ch, sb] = -5;
                    else
                    {
                        int loudness = (int)_scaleFactor[ch * MSBC_SUBBANDS + sb] - SbcOffset8[sf, sb];
                        bitneed[ch, sb] = loudness > 0 ? loudness / 2 : loudness;
                    }
                    if (bitneed[ch, sb] > maxBitneed)
                        maxBitneed = bitneed[ch, sb];
                }

                int bitcount = 0;
                int slicecount = 0;
                int bitslice = maxBitneed + 1;

                do
                {
                    bitslice--;
                    bitcount += slicecount;
                    slicecount = 0;
                    for (int sb = 0; sb < MSBC_SUBBANDS; sb++)
                    {
                        if ((bitneed[ch, sb] > bitslice + 1) && (bitneed[ch, sb] < bitslice + 16))
                            slicecount++;
                        else if (bitneed[ch, sb] == bitslice + 1)
                            slicecount += 2;
                    }
                } while (bitcount + slicecount < MSBC_BITPOOL);

                if (bitcount + slicecount == MSBC_BITPOOL)
                {
                    bitcount += slicecount;
                    bitslice--;
                }

                for (int sb = 0; sb < MSBC_SUBBANDS; sb++)
                {
                    if (bitneed[ch, sb] < bitslice + 2)
                        bits[ch, sb] = 0;
                    else
                    {
                        bits[ch, sb] = bitneed[ch, sb] - bitslice;
                        if (bits[ch, sb] > 16)
                            bits[ch, sb] = 16;
                    }
                }

                for (int sb = 0; bitcount < MSBC_BITPOOL && sb < MSBC_SUBBANDS; sb++)
                {
                    if ((bits[ch, sb] >= 2) && (bits[ch, sb] < 16))
                    {
                        bits[ch, sb]++;
                        bitcount++;
                    }
                    else if ((bitneed[ch, sb] == bitslice + 1) && (MSBC_BITPOOL > bitcount + 1))
                    {
                        bits[ch, sb] = 2;
                        bitcount += 2;
                    }
                }

                for (int sb = 0; bitcount < MSBC_BITPOOL && sb < MSBC_SUBBANDS; sb++)
                {
                    if (bits[ch, sb] < 16)
                    {
                        bits[ch, sb]++;
                        bitcount++;
                    }
                }
            }
        }

        // ---- sbc_crc8（init 0x0f，len 以 bit 计；mSBC 恒为 48bit 整字节） ----
        private static byte SbcCrc8(byte[] data, int lenBits)
        {
            byte crc = 0x0f;
            int i = 0;
            for (; i < lenBits / 8; i++)
                crc = CrcTable[crc ^ data[i]];

            if (lenBits % 8 > 0)
            {
                byte octet = data[i];
                for (int b = 0; b < lenBits % 8; b++)
                {
                    int bit = ((octet ^ crc) & 0x80) >> 7;
                    crc = (byte)(((crc & 0x7f) << 1) ^ (bit != 0 ? 0x1d : 0x00));
                    octet = (byte)(octet << 1);
                }
            }
            return crc;
        }

        // ---- sbc_synthesize_eight ----
        // SCALE8_STAGED1(x) = ASR(x, 15)
        private void SynthesizeEight(int ch, int blk)
        {
            // 1) 环形缓冲移位 + 矩阵乘写入新子带样本
            for (int i = 0; i < 16; i++)
            {
                _offset[i]--;
                if (_offset[i] < 0)
                {
                    _offset[i] = 159;
                    for (int j = 0; j < 9; j++)
                        _v[j + 160] = _v[j];
                }

                unchecked
                {
                    int sum = 0;
                    for (int j = 0; j < MSBC_SUBBANDS; j++)
                        sum += Synmatrix8[i * MSBC_SUBBANDS + j] * SbSample(blk, ch, j);
                    _v[_offset[i]] = sum >> 15;   // SCALE8_STAGED1
                }
            }

            // 2) 原型滤波合成 8 个 PCM 样本（Q0 输出）
            unchecked
            {
                for (int idx = 0, i = 0; i < 8; i++, idx += 5)
                {
                    int k = (i + 8) & 0xf;
                    int sum = 0;
                    sum += _v[_offset[i] + 0] * Proto80M0[idx + 0];
                    sum += _v[_offset[k] + 1] * Proto80M1[idx + 0];
                    sum += _v[_offset[i] + 2] * Proto80M0[idx + 1];
                    sum += _v[_offset[k] + 3] * Proto80M1[idx + 1];
                    sum += _v[_offset[i] + 4] * Proto80M0[idx + 2];
                    sum += _v[_offset[k] + 5] * Proto80M1[idx + 2];
                    sum += _v[_offset[i] + 6] * Proto80M0[idx + 3];
                    sum += _v[_offset[k] + 7] * Proto80M1[idx + 3];
                    sum += _v[_offset[i] + 8] * Proto80M0[idx + 4];
                    sum += _v[_offset[k] + 9] * Proto80M1[idx + 4];
                    _pcmSample[ch * (MSBC_BLOCKS * MSBC_SUBBANDS) + blk * 8 + i] =
                        Clip16(sum >> 15);   // SCALE8_STAGED1
                }
            }
        }

        private static short Clip16(int s)
        {
            if (s > 0x7FFF) return 0x7FFF;
            if (s < -0x8000) return -0x8000;
            return (short)s;
        }

        private int SbSample(int blk, int ch, int sb)
        {
            return _sbSample[(blk * MSBC_CHANNELS + ch) * MSBC_SUBBANDS + sb];
        }

        // ---- sbc_synthesize_audio ----
        private int SynthesizeAudio()
        {
            for (int ch = 0; ch < MSBC_CHANNELS; ch++)
                for (int blk = 0; blk < MSBC_BLOCKS; blk++)
                    SynthesizeEight(ch, blk);
            return MSBC_BLOCKS * 8;
        }
    }
}
