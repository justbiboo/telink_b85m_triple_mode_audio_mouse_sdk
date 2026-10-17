using System;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// 滑动参数设置对话框（客户向）：参数分 iOS / Android 两组，每组 2 行 ——
    /// 上下滑动 / 左右滑动，列 = 前进间隔(ms) / 回位间隔(ms) / 发送步长 / 发送步数。
    /// 每行控制对应方向滑动整段：步长与步数为前进/回位两段共用（位移天然配平，光标必回原点），
    /// 前进段与回位段的发送间隔各自独立可调。
    /// 输入后实时汇总成 24 字节参数（iOS 12B + Android 12B，v2 布局：2方向×6B）并预览两组写参数帧
    /// （0x79 写 iOS、0x82 写 Android）。窗口内【读参数】从鼠标 flash 读回填充，
    /// 【写参数】直接写入鼠标 flash；【确定】把 24 字节带回主界面参数框。
    /// 运行时鼠标按当前连接系统选用对应组。
    /// 固件约束：前进/回位间隔均 1~100ms（小端 u16，实际需求不超过 100ms）、步长 1~127（HID 有符号 s8）、
    /// 步数 1~255。需 v2 固件配套（flash 魔数 0xA7），工具与固件须配对更新。
    /// </summary>
    public class SlideParamDialog : Form
    {
        private readonly NumericUpDown[] _fwdIv = new NumericUpDown[4];   // 前进段发送间隔 ms
        private readonly NumericUpDown[] _retIv = new NumericUpDown[4];   // 回位段发送间隔 ms
        private readonly NumericUpDown[] _len   = new NumericUpDown[4];   // 发送步长（两段共用）
        private readonly NumericUpDown[] _cnt   = new NumericUpDown[4];   // 发送步数（两段共用）
        private TextBox _txtCmdIos;
        private TextBox _txtCmdAndroid;
        private readonly BleDirectLink _link;   // 供窗口内直接读/写鼠标参数（可空）
        private Button _btnRead;
        private Button _btnWrite;
        private Label _lblStatus;

        // 行名与固件参数方向对应：上下滑动(前6B) / 左右滑动(后6B)；
        // [0..1]=iOS 组，[2..3]=Android 组。
        private static readonly string[] RowNames =
            { "上下滑动", "左右滑动" };

        // 固件编译默认值（valid=0 时实际生效值）：步长4、前进/回位间隔均 3ms，
        // 上下滑动 iOS/Android 均 30 步；左右滑动 iOS 35 步(70/2)、Android 60 步(120/2)。
        private static readonly decimal[][] Def =
        {
            new decimal[] { 3, 3, 4, 30 },      // iOS 上下滑动
            new decimal[] { 3, 3, 4, 35 },      // iOS 左右滑动（70/2）
            new decimal[] { 3, 3, 4, 30 },      // Android 上下滑动
            new decimal[] { 3, 3, 4, 60 },      // Android 左右滑动（120/2）
        };

        // 表格列 x 坐标（组内）：行标签列右对齐到 168，四列 NUD 左对齐列头
        private const int ColFwdIv = 180;   // 前进间隔 (ms)
        private const int ColRetIv = 286;   // 回位间隔 (ms)
        private const int ColLen   = 392;   // 步长
        private const int ColCnt   = 452;   // 步数

        /// <summary>确定后返回的 24 字节参数（iOS组12 + Android组12；每组 上下滑动6B + 左右滑动6B）。</summary>
        public byte[] ResultParam { get; private set; }

        public SlideParamDialog(byte[] initial, BleDirectLink link)
        {
            Text = "滑动参数设置（iOS / Android 两组）";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterParent;
            ClientSize = new Size(664, 376);
            BackColor = Color.White;
            _link = link;

            BuildUi();
            LoadParam(initial);
            RefreshPreview();
        }

        private void BuildUi()
        {
            for (int os = 0; os < 2; os++)
            {
                var grp = new GroupBox();
                grp.Text = os == 0 ? "iOS 组（读 0x80 / 写 0x79）" : "Android 组（读 0x81 / 写 0x82）";
                grp.SetBounds(12, 12 + os * 105, 640, 97);

                AddHead(grp, ColFwdIv, "前进间隔 (ms)", 100);
                AddHead(grp, ColRetIv, "回位间隔 (ms)", 100);
                AddHead(grp, ColLen, "步长", 52);
                AddHead(grp, ColCnt, "步数", 52);
                for (int r = 0; r < 2; r++)
                    AddGridRow(grp, os, r);
                Controls.Add(grp);
            }

            var lbl1 = new Label();
            lbl1.Text = "命令预览（0x79 写 iOS 组）：";
            lbl1.SetBounds(12, 222, 400, 16);
            Controls.Add(lbl1);

            _txtCmdIos = MakePreviewBox(12, 240);
            Controls.Add(_txtCmdIos);

            var lbl2 = new Label();
            lbl2.Text = "命令预览（0x82 写 Android 组）：约束 前进/回位间隔均1~100ms、步长1~127、步数1~255";
            lbl2.SetBounds(12, 268, 620, 16);
            Controls.Add(lbl2);

            _txtCmdAndroid = MakePreviewBox(12, 286);
            Controls.Add(_txtCmdAndroid);

            _btnRead = new Button();
            _btnRead.Text = "读参数";
            _btnRead.SetBounds(12, 320, 92, 28);
            _btnRead.Enabled = _link != null && _link.IsConnected;
            _btnRead.Click += OnReadParam;
            Controls.Add(_btnRead);

            _btnWrite = new Button();
            _btnWrite.Text = "写参数";
            _btnWrite.SetBounds(112, 320, 92, 28);
            _btnWrite.Enabled = _btnRead.Enabled;
            _btnWrite.Click += OnWriteParam;
            Controls.Add(_btnWrite);

            _lblStatus = new Label();
            _lblStatus.SetBounds(216, 326, 224, 16);
            _lblStatus.ForeColor = Color.Firebrick;
            Controls.Add(_lblStatus);

            var ok = new Button();
            ok.Text = "确定";
            ok.DialogResult = DialogResult.OK;
            ok.SetBounds(456, 320, 92, 28);
            ok.Click += OnOk;
            Controls.Add(ok);

            var cancel = new Button();
            cancel.Text = "取消";
            cancel.DialogResult = DialogResult.Cancel;
            cancel.SetBounds(560, 320, 92, 28);
            Controls.Add(cancel);

            var tip = new Label();
            tip.Text = "【读参数】从鼠标 flash 读回填充；【写参数】写入鼠标 flash，重启后保持；鼠标运行时按连接系统选用对应组。";
            tip.SetBounds(12, 354, 640, 16);
            tip.ForeColor = Color.Firebrick;
            Controls.Add(tip);

            // 不设 AcceptButton：Enter 绝不触发关窗——这是"输入数字按 Enter 窗口
            // 就退出"的根源（Enter 被【确定】按钮拦截）。数据提交由 ProcessKeyPreview
            // 拦截完成，确认只能点【确定】按钮，取消可用 Esc。
            CancelButton = cancel;
        }

        private static void AddHead(GroupBox grp, int x, string text, int w)
        {
            var lbl = new Label();
            lbl.Text = text;
            lbl.TextAlign = ContentAlignment.MiddleLeft;
            lbl.SetBounds(x, 20, w, 16);
            grp.Controls.Add(lbl);
        }

        private void AddGridRow(GroupBox grp, int os, int row)
        {
            int idx = os * 2 + row;
            int y = 42 + row * 27;

            var lbl = new Label();
            lbl.Text = RowNames[row];
            lbl.TextAlign = ContentAlignment.MiddleRight;
            lbl.SetBounds(8, y, 164, 23);
            grp.Controls.Add(lbl);

            _fwdIv[idx] = MakeNud(1, 100);
            _retIv[idx] = MakeNud(1, 100);
            _len[idx]   = MakeNud(1, 127);
            _cnt[idx]   = MakeNud(1, 255);
            PlaceNud(grp, _fwdIv[idx], ColFwdIv, y, 66);
            PlaceNud(grp, _retIv[idx], ColRetIv, y, 66);
            PlaceNud(grp, _len[idx],   ColLen,   y, 52);
            PlaceNud(grp, _cnt[idx],   ColCnt,   y, 52);
        }

        private void PlaceNud(GroupBox grp, NumericUpDown nud, int x, int y, int w)
        {
            nud.SetBounds(x, y, w, 23);
            nud.ValueChanged += delegate { RefreshPreview(); };
            grp.Controls.Add(nud);
        }

        // Enter 提交：NumericUpDown 的键入焦点在其内部编辑控件上，KeyDown 事件
        // 不会冒泡到 NumericUpDown 本体，挂在它上面收不到——改在窗体级拦截：
        // 解析文本、钳制到合法范围、刷新预览。注意 Form.ActiveControl 只返回直接
        // 子容器（GroupBox），拿不到内部编辑框，必须递归找 Focused 控件再回溯
        // 其父级 NumericUpDown。本对话框不设 AcceptButton，Enter 只提交不关窗。
        protected override bool ProcessKeyPreview(ref Message m)
        {
            if (m.Msg == 0x100 /*WM_KEYDOWN*/ && (Keys)m.WParam.ToInt32() == Keys.Return)
            {
                var focused = FindFocusedControl(this);
                var nud = focused as NumericUpDown
                          ?? focused?.Parent as NumericUpDown;
                if (nud != null)
                    CommitNud(nud);
                return true;
            }
            return base.ProcessKeyPreview(ref m);
        }

        private static Control FindFocusedControl(Control parent)
        {
            foreach (Control c in parent.Controls)
            {
                if (c.Focused)
                    return c;
                Control deep = FindFocusedControl(c);
                if (deep != null)
                    return deep;
            }
            return null;
        }

        private void CommitNud(NumericUpDown nud)
        {
            if (decimal.TryParse(nud.Text, out decimal v))
            {
                v = Math.Min(nud.Maximum, Math.Max(nud.Minimum, v));
                nud.Value = v;
            }
            else
            {
                nud.Text = nud.Value.ToString();   // 非法输入回退当前值
            }
            RefreshPreview();
        }

        // 读参数：从鼠标 flash 读回 iOS/Android 两组 → 填充界面。
        // valid=0 时设备回包为编译默认值（或全 0），按规范改填固件默认值。
        private async void OnReadParam(object sender, EventArgs e)
        {
            if (!EnsureLink())
                return;
            SetBusy(true, "正在读参数…");
            string err = await _link.SppSlideParamReadAsync();
            SetBusy(false);
            if (err != null)
            {
                ShowStatus("读参数失败：" + err);
                return;
            }
            if (_link.LastSlideValid == 1)
            {
                LoadParam(_link.LastSlideParam);
                RefreshPreview();
                ShowStatus("读参数成功（valid=1）");
            }
            else
            {
                LoadParam(null);
                RefreshPreview();
                ShowStatus("设备参数无效(valid=0)，已填编译默认值");
            }
        }

        // 写参数：把当前界面两组 24 字节直接写入鼠标 flash。
        private async void OnWriteParam(object sender, EventArgs e)
        {
            if (!EnsureLink())
                return;
            SetBusy(true, "正在写参数…");
            string err = await _link.SppSlideParamWriteAsync(BuildParam());
            SetBusy(false);
            if (err != null)
            {
                ShowStatus("写参数失败：" + err);
                return;
            }
            ShowStatus("写参数成功，重启后保持");
        }

        private bool EnsureLink()
        {
            if (_link == null || !_link.IsConnected)
            {
                ShowStatus("未接入鼠标，请先连接再读写");
                return false;
            }
            return true;
        }

        private void SetBusy(bool busy, string msg = null)
        {
            _btnRead.Enabled  = !busy && _link != null && _link.IsConnected;
            _btnWrite.Enabled = _btnRead.Enabled;
            ShowStatus(msg ?? "");
        }

        private void ShowStatus(string msg)
        {
            _lblStatus.Text = msg;
        }

        private static TextBox MakePreviewBox(int x, int y)
        {
            var box = new TextBox();
            box.SetBounds(x, y, 640, 22);
            box.ReadOnly = true;
            box.Font = new Font("Consolas", 9f);
            box.BackColor = SystemColors.Control;
            return box;
        }

        private static NumericUpDown MakeNud(int min, int max)
        {
            var nud = new NumericUpDown();
            nud.Minimum = min;
            nud.Maximum = max;
            return nud;
        }

        // 值变化 → 实时重新汇总命令预览
        private void RefreshPreview()
        {
            if (_txtCmdIos == null)
                return;
            byte[] p = BuildParam();
            _txtCmdIos.Text = BuildFrameText(BleUxProto.SetSlideParamCmd, p, 0);
            _txtCmdAndroid.Text = BuildFrameText(BleUxProto.SetSlideParamAndroidCmd, p, BleUxProto.SlideParamGroupSize);
        }

        private static string BuildFrameText(byte cmd, byte[] p, int offs)
        {
            var sb = new StringBuilder(cmd.ToString("X2")).Append(" 0C");
            for (int i = 0; i < BleUxProto.SlideParamGroupSize; i++)
                sb.Append(' ').Append(p[offs + i].ToString("X2"));
            return sb.ToString();
        }

        // 4 个界面值 → 24 字节（iOS组12 + Android组12；每组 2方向×6B
        // [fwd_lo, fwd_hi, ret_lo, ret_hi, len, cnt]，间隔小端 u16）
        private byte[] BuildParam()
        {
            byte[] p = new byte[BleUxProto.SlideParamSize];
            for (int g = 0; g < 2; g++)          // g: 0=iOS 组, 1=Android 组
            {
                int ui = g * 2;                  // 界面行索引（每组 2 行）
                int dst = g * BleUxProto.SlideParamGroupSize;
                WriteSlot(p, dst,     ui);       // 前6B 上下滑动
                WriteSlot(p, dst + 6, ui + 1);   // 后6B 左右滑动
            }
            return p;
        }

        private void WriteSlot(byte[] p, int offs, int uiRow)
        {
            int fwd = (int)_fwdIv[uiRow].Value;
            int ret = (int)_retIv[uiRow].Value;
            p[offs]     = (byte)(fwd & 0xFF);
            p[offs + 1] = (byte)(fwd >> 8);
            p[offs + 2] = (byte)(ret & 0xFF);
            p[offs + 3] = (byte)(ret >> 8);
            p[offs + 4] = (byte)_len[uiRow].Value;
            p[offs + 5] = (byte)_cnt[uiRow].Value;
        }

        private void LoadParam(byte[] p)
        {
            if (p == null || p.Length != BleUxProto.SlideParamSize)
            {
                // 无有效初值：用固件默认参数（iOS/Android 两组）
                for (int g = 0; g < 4; g++)
                {
                    _fwdIv[g].Value = Def[g][0];
                    _retIv[g].Value = Def[g][1];
                    _len[g].Value   = Def[g][2];
                    _cnt[g].Value   = Def[g][3];
                }
                return;
            }
            for (int g = 0; g < 2; g++)
            {
                int ui = g * 2;
                int src = g * BleUxProto.SlideParamGroupSize;
                ReadSlot(p, src,     ui);        // 前6B 上下滑动
                ReadSlot(p, src + 6, ui + 1);    // 后6B 左右滑动
            }
        }

        private void ReadSlot(byte[] p, int offs, int uiRow)
        {
            _fwdIv[uiRow].Value = p[offs] | (p[offs + 1] << 8);
            _retIv[uiRow].Value = p[offs + 2] | (p[offs + 3] << 8);
            _len[uiRow].Value   = p[offs + 4];
            _cnt[uiRow].Value   = p[offs + 5];
        }

        private void OnOk(object sender, EventArgs e)
        {
            ResultParam = BuildParam();
        }
    }
}
