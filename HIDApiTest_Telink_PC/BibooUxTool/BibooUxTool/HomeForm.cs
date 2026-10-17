using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// 启动首页：列出全部功能模块，由用户自行选择进入（客户调试版 customerDebugRelease=true 时只显示蓝牙连接与实时翻译）。
    ///   ① 收发调试      —— 经 USB dongle 的 Host Link HID 通道收发 15 字节报文。
    ///   ② 蓝牙连接        —— PC 直连 BLE（不依赖 dongle），扫描 / 发现 SPP / Ping 校验。
    ///   ③ 实时翻译        —— USB Audio 采集 → VAD 断句 → 翻译引擎 → 实时字幕（框架版，暂不走 BLE）。
    ///   ④ 产线测试        —— 经 dongle 走 UX 产测协议（DUT）。
    /// 各模块各自独立；其中 ① 与 ④ 共用 dongle 的 HID 通道，本页以模态方式逐个打开，
    /// 天然保证两者不会同时占用同一通道。
    /// </summary>
    public sealed class HomeForm : Form
    {
        /// <summary>模式入口卡片高度（与卡片内徽标/文字/按钮的纵向排布配套）。</summary>
        private const int CardHeight = 116;

        /// <summary>三张模块卡片（窗口拉高时整体垂直居中用）。</summary>
        private readonly List<ModeCard> _cards = new List<ModeCard>();

        private Label _lblTitle;
        private Label _lblSub;
        private Label _lblFoot;

        /// <summary>卡片设计宽度（ClientSize 780 - 两侧各 12px 边距）。</summary>
        private const int CardDesignWidth = 756;

        public HomeForm()
        {
            BuildUi();
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            // 首显后整体重绘一次，清除控件初始显示阶段的原生绘制角部残留。
            Refresh();
        }

        // ---------- UI 构建 ----------

        private void BuildUi()
        {
            Text = "Biboo OS";
            BackColor = UiTheme.FormBg;
            Font = UiTheme.FontBody;
            ClientSize = new Size(780, 620);
            MinimumSize = new Size(700, 620);
            StartPosition = FormStartPosition.CenterScreen;

            try
            {
                Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            }
            catch
            {
                // 提取失败不影响主流程。
            }

            _lblTitle = new Label();
            _lblTitle.Text = "Biboo OS";
            _lblTitle.Font = new Font("Microsoft YaHei UI", 15F, FontStyle.Bold);
            _lblTitle.ForeColor = UiTheme.TextMain;
            _lblTitle.SetBounds(22, 20, 600, 34);
            _lblTitle.AutoSize = false;
            _lblTitle.Anchor = AnchorStyles.Top | AnchorStyles.Left;   // 宽度由 LayoutContent 管
            _lblTitle.TextAlign = ContentAlignment.MiddleLeft;

            _lblSub = new Label();
            // 客户调试版：只保留蓝牙连接与实时翻译入口（功能代码保留，开关改回 false 即恢复全部模块首页）。
            // 注意：不能声明为 const —— const 会让两个 if 分支被编译器判定为死代码（CS0162 警告）。
            bool customerDebugRelease = true;
            _lblSub.Text = customerDebugRelease
                ? "调试版本：可进入蓝牙连接（读写滑动参数配置）或实时翻译（USB Audio 字幕框架）"
                : "请选择要进入的功能模块（各模块相互独立，可分别单独使用）";
            _lblSub.ForeColor = UiTheme.TextSub;
            _lblSub.SetBounds(24, 56, 700, 20);
            _lblSub.AutoSize = false;
            _lblSub.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _lblSub.TextAlign = ContentAlignment.MiddleLeft;

            Controls.Add(_lblTitle);
            Controls.Add(_lblSub);

            int y = 92;
            if (!customerDebugRelease)
                Controls.Add(MakeCard(ref y, "HID", UiTheme.Primary,
                    "收发调试",
                    "经 USB dongle 的 Host Link HID 通道直接收发：下行 0x09 Feature（15 字节）、"
                    + "上行 0x08 Input（15 字节），带实时收发日志与链路断开提示。",
                    OnOpenDongle));

            Controls.Add(MakeCard(ref y, "BLE", UiTheme.Accent,
                "蓝牙连接",
                "不依赖 dongle：直接列出系统已配对设备并实时显示连接状态，选中即回连收发；"
                + "也可扫描附近设备直连。已连接以 ATT 级真实读取校验，杜绝幽灵连接。",
                OnOpenBle));

            // 实时翻译：不受 customerDebugRelease 限制（新功能主线，始终展示）。
            // 框架阶段音频走 USB Audio（标准 WaveIn），不走 BLE；引擎为桩实现。
            Controls.Add(MakeCard(ref y, "TR", UiTheme.Primary2,
                "实时翻译",
                "USB Audio 采集（16kHz 单声道）→ VAD 断句 → 翻译引擎 → 实时字幕。"
                + "当前为框架版：引擎为桩实现（不加载模型），先把采集与字幕链路跑通，"
                + "后续接 Whisper.net（识别）+ Argos（翻译）。",
                OnOpenTranslator));

            // 产线测试：解除 customerDebugRelease 屏蔽（用户要求打开），与收发调试解耦单独显示。
            // 保留 customerDebugRelease 仅控制「收发调试」入口与底部提示。
            if (true)
                Controls.Add(MakeCard(ref y, "FT", UiTheme.Warn,
                    "产线测试",
                    "经 dongle 走 UX 产测协议（DUT）：固件版本 / 型号 / 鼠标报告 LED / 鉴权往返等。"
                    + "与收发调试共用 HID 通道，两者不可同时打开。",
                    OnOpenFactory));

            _lblFoot = new Label();
            _lblFoot.Text = "提示：收发调试与产线测试共用 dongle 的 HID 通道，关闭其中一个后再进入另一个；"
                          + "蓝牙连接与实时翻译相互独立，可随时进入。";
            _lblFoot.Visible = !customerDebugRelease;
            _lblFoot.ForeColor = UiTheme.TextSub;
            _lblFoot.SetBounds(22, ClientSize.Height - 40, 736, 20);
            _lblFoot.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;   // 宽度由 LayoutContent 管
            _lblFoot.AutoSize = false;
            _lblFoot.TextAlign = ContentAlignment.MiddleLeft;

            Controls.Add(_lblFoot);

            LayoutContent();   // 初始定位（默认窗口尺寸）
        }

        /// <summary>
        /// 内容列居中：窗口拉宽时标题/三张卡片/页脚保持设计宽度（756）整体居中，
        /// 两侧留白，不再拉伸成「左边内容、右边一大片空白」的长条；
        /// 窗口比设计窄时全部等比收窄（最小 240px）。
        /// 卡片不设左右锚定（锚定会强制拉伸），位置/宽度由此统一管理。
        /// </summary>
        private void LayoutContent()
        {
            const int margin = 12;
            int avail = ClientSize.Width - margin * 2;
            if (avail < 240)
                avail = 240;
            int w = Math.Min(CardDesignWidth, avail);
            int x = (ClientSize.Width - w) / 2;

            if (_lblTitle != null)
                _lblTitle.SetBounds(x + 10, _lblTitle.Top, w - 20, _lblTitle.Height);
            if (_lblSub != null)
                _lblSub.SetBounds(x + 12, _lblSub.Top, w - 24, _lblSub.Height);
            if (_lblFoot != null)
                _lblFoot.SetBounds(x + 10, ClientSize.Height - 40, w - 20, _lblFoot.Height);
            foreach (var card in _cards)
                card.SetBounds(x, card.Top, w, CardHeight);
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            LayoutContent();
            LayoutHome();
        }

        /// <summary>
        /// 窗口拉高时把三张卡片整体垂直居中：全屏/最大化后不会上面挤、下面空一大片。
        /// 空间不富余（extra &lt; 24）时保持默认排布，避免最小尺寸附近抖动。
        /// </summary>
        private void LayoutHome()
        {
            if (_cards.Count == 0)
                return;
            const int top = 92;        // 标题区占位
            const int gap = 16;        // 卡片间距
            const int footZone = 48;   // 底部提示预留
            int block = _cards.Count * CardHeight + (_cards.Count - 1) * gap;
            int extra = ClientSize.Height - top - footZone - block;
            if (extra < 24)
                return;
            int y = top + extra / 2;
            foreach (var card in _cards)
            {
                card.SetBounds(card.Left, y, card.Width, CardHeight);
                y += CardHeight + gap;
            }
        }

        private ModeCard MakeCard(ref int y, string badge, Color badgeColor,
                                  string title, string desc, EventHandler onEnter)
        {
            var card = new ModeCard(badge, badgeColor, title, desc);
            card.SetBounds(12, y, CardDesignWidth, CardHeight);
            // 不设左右锚定：宽度/水平位置由 LayoutContent 统一管理（限宽 + 居中）
            card.Anchor = AnchorStyles.Top;
            card.EnterRequested += onEnter;
            _cards.Add(card);
            y += CardHeight + 16;
            return card;
        }

        // ---------- 模块入口 ----------

        private void OnOpenDongle(object sender, EventArgs e)
        {
            using (var f = new DongleForm())
            {
                f.ShowDialog(this);
            }
        }

        private void OnOpenBle(object sender, EventArgs e)
        {
            using (var f = new BleScannerForm())
            {
                f.ShowDialog(this);
            }
        }

        private void OnOpenTranslator(object sender, EventArgs e)
        {
            using (var f = new TranslatorForm())
            {
                f.ShowDialog(this);
            }
        }

        private void OnOpenFactory(object sender, EventArgs e)
        {
            using (var f = new FactoryTestForm())
            {
                f.ShowDialog(this);
            }
        }
    }

    /// <summary>
    /// 首页的模式入口卡片：左侧圆角色块徽标 + 标题 + 说明 + 右侧【进入】。
    /// 整张卡片可点，鼠标悬停时描边高亮、按钮变为主色渐变样式。
    /// </summary>
    internal sealed class ModeCard : RoundedPanel
    {
        private const int BadgeSize = 60;
        private const int BadgeLeft = 20;
        private const int TextLeft = 98;

        private readonly string _badgeText;
        private readonly Color _badgeColor;
        private readonly GradientButton _btnGo;
        private bool _hover;

        /// <summary>点击卡片 / 【进入】按钮 / 卡片内任意文字时触发。（不用 Enter：与 Control.Enter 焦点事件同名）</summary>
        public event EventHandler EnterRequested;

        public ModeCard(string badgeText, Color badgeColor, string title, string desc)
        {
            _badgeText = badgeText;
            _badgeColor = badgeColor;
            Cursor = Cursors.Hand;

            var lblTitle = new Label();
            lblTitle.Text = title;
            lblTitle.Font = new Font("Microsoft YaHei UI", 10.5F, FontStyle.Bold);
            lblTitle.ForeColor = UiTheme.TextMain;
            lblTitle.SetBounds(TextLeft, 24, 420, 24);
            lblTitle.AutoSize = false;
            lblTitle.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            lblTitle.TextAlign = ContentAlignment.MiddleLeft;
            lblTitle.Cursor = Cursors.Hand;

            var lblDesc = new Label();
            lblDesc.Text = desc;
            lblDesc.ForeColor = UiTheme.TextSub;
            lblDesc.SetBounds(TextLeft, 50, 520, 56);
            lblDesc.AutoSize = false;   // 关掉自动宽高：否则卡片拉宽后说明文字不重新折行
            lblDesc.TextAlign = ContentAlignment.TopLeft;
            lblDesc.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            lblDesc.Cursor = Cursors.Hand;

            _btnGo = new GradientButton();
            _btnGo.Text = "进入";
            _btnGo.SetBounds(636, 42, 96, 32);
            _btnGo.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnGo.IsPrimary = false;
            _btnGo.Click += RaiseEnterRequested;

            Controls.AddRange(new Control[] { lblTitle, lblDesc, _btnGo });

            Click += RaiseEnterRequested;
            lblTitle.Click += RaiseEnterRequested;
            lblDesc.Click += RaiseEnterRequested;

            MouseEnter += OnHoverIn;
            MouseLeave += OnHoverOut;
            lblTitle.MouseEnter += OnHoverIn;
            lblTitle.MouseLeave += OnHoverOut;
            lblDesc.MouseEnter += OnHoverIn;
            lblDesc.MouseLeave += OnHoverOut;
            _btnGo.MouseEnter += OnHoverIn;
            _btnGo.MouseLeave += OnHoverOut;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);

            // 徽标：圆角方块 + 白色粗体缩写（HID / BLE / FT）。
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            var rect = new Rectangle(BadgeLeft, (Height - BadgeSize) / 2, BadgeSize, BadgeSize);
            using (var path = RoundedRectPath(rect, 12))
            using (var brush = new SolidBrush(_badgeColor))
            {
                g.FillPath(brush, path);
            }

            using (var font = new Font("Consolas", 12F, FontStyle.Bold))
            {
                TextRenderer.DrawText(g, _badgeText, font, rect, Color.White,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter |
                    TextFormatFlags.SingleLine);
            }
        }

        private void SetHover(bool on)
        {
            if (_hover == on)
                return;

            _hover = on;
            BorderColor = on ? UiTheme.Primary : UiTheme.CardBorder;
            _btnGo.IsPrimary = on;
            Invalidate();
        }

        private void OnHoverIn(object sender, EventArgs e)
        {
            SetHover(true);
        }

        private void OnHoverOut(object sender, EventArgs e)
        {
            // 鼠标从卡片移入子控件也会触发父控件的 MouseLeave，
            // 因此必须复查指针是否仍在卡片矩形内，避免悬停效果闪烁。
            if (!ClientRectangle.Contains(PointToClient(MousePosition)))
                SetHover(false);
        }

        private void RaiseEnterRequested(object sender, EventArgs e)
        {
            var handler = EnterRequested;
            if (handler != null)
                handler(this, EventArgs.Empty);
        }
    }
}
