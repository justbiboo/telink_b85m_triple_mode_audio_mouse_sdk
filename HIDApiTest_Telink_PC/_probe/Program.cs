using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

internal static class Probe
{
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);

    private const uint PwClientOnly = 1;

    [STAThread]
    private static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        var f = new BibooUxTool.TranslatorForm();
        f.StartPosition = FormStartPosition.Manual;
        f.Location = new Point(-4000, -4000);   // 屏外：不打扰用户
        int w = 820, h = 740;
        if (args.Length >= 3)
        {
            int.TryParse(args[1], out w);
            int.TryParse(args[2], out h);
        }
        f.ClientSize = new Size(w, h);
        f.Show();

        // 等布局 + 首帧绘制稳定（OnShown 里的模型预热是后台线程，不阻塞）
        for (int i = 0; i < 40; i++)
        {
            Application.DoEvents();
            Thread.Sleep(50);
        }

        Size cs = f.ClientSize;
        using (var bmp = new Bitmap(cs.Width, cs.Height, PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(bmp))
        {
            IntPtr hdc = g.GetHdc();
            try { PrintWindow(f.Handle, hdc, PwClientOnly); }
            finally { g.ReleaseHdc(hdc); }
            string outPath = args.Length > 0 ? args[0] : "probe_translator.png";
            bmp.Save(outPath, ImageFormat.Png);
            Console.WriteLine("saved: " + outPath + "  client=" + cs.Width + "x" + cs.Height);
        }
        f.Close();
        Environment.Exit(0);
    }
}
