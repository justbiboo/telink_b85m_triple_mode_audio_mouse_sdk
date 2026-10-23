using System;
using System.Diagnostics;
using System.IO;
using BibooUxTool;

// mSBC 解码器字节级验证：enc.bin(64B/记录, mSBC 在 +3, 57B) -> 与 pcm.bin 逐字节比对
internal static class MsbcCheck
{
    private static int Main(string[] args)
    {
        string dir = args.Length > 0 ? args[0]
            : @"E:\BibooFamily\fnos\Telink\Telink_8258\audio_mouse\test_mic_demo\exe";

        byte[] enc = File.ReadAllBytes(Path.Combine(dir, "enc.bin"));
        byte[] refPcm = File.ReadAllBytes(Path.Combine(dir, "pcm.bin"));

        int rec = enc.Length / 64;
        Console.WriteLine("enc.bin: {0} bytes -> {1} frames (record=64B, mSBC@+3, len=57)", enc.Length, rec);
        Console.WriteLine("pcm.bin: {0} bytes (expect {1})", refPcm.Length, rec * 240);
        if (enc.Length % 64 != 0) { Console.WriteLine("ENC SIZE MISMATCH"); return 2; }
        if (refPcm.Length != rec * 240) { Console.WriteLine("REF SIZE MISMATCH"); return 2; }

        var dec = new MsbcDecoder();
        byte[] pcm = new byte[rec * 240];
        byte[] frame = new byte[240];

        var sw = Stopwatch.StartNew();
        for (int i = 0; i < rec; i++)
        {
            int r = dec.Decode(enc, i * 64 + 3, MsbcDecoder.FrameLen, frame, 0, MsbcDecoder.PcmBytesPerFrame);
            if (r != MsbcDecoder.FrameLen || dec.LastWritten != MsbcDecoder.PcmBytesPerFrame)
            {
                Console.WriteLine("FAIL: frame {0} decode error r={1} written={2}", i, r, dec.LastWritten);
                return 3;
            }
            Buffer.BlockCopy(frame, 0, pcm, i * 240, 240);
        }
        sw.Stop();

        int bad = 0, firstBad = -1;
        for (int i = 0; i < pcm.Length; i++)
        {
            if (pcm[i] != refPcm[i]) { bad++; if (firstBad < 0) firstBad = i; }
        }

        Console.WriteLine("decoded {0} frames in {1} ms ({2:F3} ms/frame)", rec, sw.ElapsedMilliseconds,
            (double)sw.ElapsedMilliseconds / rec);

        if (bad == 0)
        {
            Console.WriteLine("PASS: byte-exact match with pcm.bin ({0} bytes, {1} frames)", pcm.Length, rec);
            return 0;
        }

        Console.WriteLine("FAIL: {0}/{1} differing bytes; first at offset {2} (frame {3}, byte {4})",
            bad, pcm.Length, firstBad, firstBad / 240, firstBad % 240);
        int shown = 0;
        for (int i = 0; i < pcm.Length && shown < 10; i++)
        {
            if (pcm[i] != refPcm[i])
            {
                Console.WriteLine("  off {0} (frame {1}, sample {2}): got {3}, want {4}",
                    i, i / 240, (i % 240) / 2, pcm[i], refPcm[i]);
                shown++;
            }
        }
        return 1;
    }
}
