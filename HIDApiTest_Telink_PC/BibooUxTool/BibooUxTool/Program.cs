using System;
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
            // 启动首页：由用户自行选择要进入的功能模块
            // （收发调试 / 蓝牙连接 / 产线测试）。
            Application.Run(new HomeForm());
        }
    }
}
