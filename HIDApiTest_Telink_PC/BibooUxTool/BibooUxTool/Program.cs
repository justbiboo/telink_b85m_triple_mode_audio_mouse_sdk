using System;
using System.IO;
using System.Windows.Forms;

namespace BibooUxTool
{
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            // 全局异常兜底：未处理异常写入 crash.log（exe 同目录，失败则转 %TEMP%），
            // 并弹窗给出完整堆栈，便于现场反馈定位（如鼠标诊断页异常）。
            Application.ThreadException += (s, e) => HandleCrash("UI 线程", e.Exception);
            AppDomain.CurrentDomain.UnhandledException += (s, e) =>
                HandleCrash("非 UI 线程", e.ExceptionObject as Exception);

            // 启动首页：由用户自行选择要进入的功能模块
            // （收发调试 / 蓝牙连接 / 产线测试 / 鼠标诊断）。
            Application.Run(new HomeForm());
        }

        private static void HandleCrash(string where, Exception ex)
        {
            string text = "[" + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + "] "
                + where + "未处理异常\r\n"
                + (ex == null ? "（异常对象为空，疑似原生代码崩溃）" : ex.ToString())
                + "\r\n\r\n";

            try
            {
                string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "crash.log");
                try
                {
                    File.AppendAllText(path, text);
                }
                catch
                {
                    File.AppendAllText(Path.Combine(Path.GetTempPath(), "BibooOS_crash.log"), text);
                }
            }
            catch
            {
                // 日志也写不进去就放弃，至少还留着弹窗
            }

            MessageBox.Show(text + "（已写入 crash.log，可整段复制发回）",
                "Biboo OS 未处理异常", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
