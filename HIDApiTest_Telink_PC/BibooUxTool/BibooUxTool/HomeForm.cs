using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// 启动首页：列出功能模块，由用户自行选择进入。当前为全模块版（ShowAllModules=true），
    /// 六张卡全部显示；把 ShowAllModules 改回 false 即回到精简版（实时翻译 + 固件升级两张），
    /// 窗口高度、副标题、页脚提示都会跟着自动还原，不用再动别处。
    ///   ① 收发调试      —— 经 USB dongle 的 Host Link HID 通道收发 15 字节报文（客户调试版隐藏）。
    ///   ② 蓝牙连接        —— PC 直连 BLE（不依赖 dongle），扫描 / 发现 SPP / Ping 校验。
    ///   ③ 实时翻译        —— USB Audio 采集 → VAD 断句 → 本地识别（SenseVoice）→ 离线翻译（混元）→ 字幕与朗读。
    ///   ④ 产线测试        —— 经 dongle 走 UX 产测协议（DUT）。
    ///   ⑤ 鼠标诊断        —— 经 dongle 读鼠标链路丢帧计数器（MSTAT 0x03）+ 0xE1 间隔监视。
    ///   ⑥ 固件升级        —— 给鼠标刷 Telink bin（USB dongle 透传 SPP / BLE 直连写 OTA 特征），两种模式都显示。
    /// 各模块各自独立；其中 ① / ④ / ⑤ / ⑥(dongle 通道) 共用 dongle 的 HID 通道，本页以模态方式逐个打开，
    /// 天然保证它们不会同时占用同一通道。
    /// </summary>
    public sealed class HomeForm : Form
    {
        /// <summary>模式入口卡片高度（与卡片内徽标/文字/按钮的纵向排布配套）。</summary>
        private const int CardHeight = 116;

        /// <summary>
        /// 是否显示「实时翻译」以外的模块入口（收发调试 / 蓝牙连接 / 产线测试 / 鼠标诊断；
        /// 固件升级恒显，不受此开关控制）。
        /// 现为全模块版：六张卡全部显示；改回 false 即回到精简版（实时翻译 + 固件升级两张），
        /// 窗口高度、副标题、页脚提示都会跟着自动还原，不用再动别处。
        /// ⚠ 不能用 const —— const 会让下面的 if 分支被编译器判成死代码（CS0162）。
        /// </summary>
        private static readonly bool ShowAllModules = true;

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

            // 客户调试版开关：true 时隐藏「收发调试」入口（功能代码保留）；当前 false = 全功能版。
            // 注意：不能声明为 const —— const 会让两个 if 分支被编译器判定为死代码（CS0162 警告）。
            bool customerDebugRelease = false;

            // 可见卡片数（必须与下面实际 Add 的卡片一一对应，改任一支勿忘同步此式）：
            //   全模块 = 收发调试(仅非客户版) + 蓝牙连接 + 实时翻译 + 固件升级(恒显) + 产线测试 + 鼠标诊断
            //   → customerDebugRelease=true 时 5 张，false 时 6 张
            //   精简版固定两张：实时翻译 + 固件升级(OTA)
            int cardCount = ShowAllModules ? (customerDebugRelease ? 5 : 6) : 2;
            int clientHeight = 92 + cardCount * CardHeight + (cardCount - 1) * 16 + 72;
            if (clientHeight < 300)
                clientHeight = 300;          // 单卡时也留出标题与页脚区域，不要压成一条

            ClientSize = new Size(780, clientHeight);
            MinimumSize = new Size(700, clientHeight);
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
            if (!ShowAllModules)
                _lblSub.Text = "调试版本：实时翻译（USB Audio 采集 → 本地识别 → 离线翻译 → 字幕与朗读）与固件升级（OTA）";
            else
                _lblSub.Text = customerDebugRelease
                    ? "调试版本：可进入蓝牙连接、实时翻译、固件升级、产线测试或鼠标诊断（收发调试已隐藏）"
                    : "请选择要进入的功能模块（各模块相互独立，可分别单独使用）";
            _lblSub.ForeColor = UiTheme.TextSub;
            _lblSub.SetBounds(24, 56, 700, 20);
            _lblSub.AutoSize = false;
            _lblSub.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _lblSub.TextAlign = ContentAlignment.MiddleLeft;

            Controls.Add(_lblTitle);
            Controls.Add(_lblSub);

            int y = 92;
            if (ShowAllModules && !customerDebugRelease)
                Controls.Add(MakeCard(ref y, "HID", UiTheme.Primary,
                    "收发调试",
                    "经 USB Dongle 的 Host Link HID 通道直接收发：下行 0x09 Feature（15 字节）、"
                    + "上行 0x08 Input（15 字节），带实时收发日志与链路断开提示。",
                    OnOpenDongle));

            if (ShowAllModules)
                Controls.Add(MakeCard(ref y, "BLE", UiTheme.Accent,
                    "蓝牙连接",
                    "不依赖 Dongle：直接列出系统已配对设备并实时显示连接状态，选中即回连收发；"
                    + "也可扫描附近设备直连。已连接以 ATT 级真实读取校验，杜绝幽灵连接。",
                    OnOpenBle));

            // 实时翻译：不受 customerDebugRelease 限制（新功能主线，始终展示）。
            // 音频走 USB Audio（标准 WaveIn），识别 = SenseVoice，翻译 = 混元 Hy-MT2，全程离线。
            Controls.Add(MakeCard(ref y, "TR", UiTheme.Primary2,
                "实时翻译",
                "USB Audio 采集（16kHz 单声道）→ 自动断句 → 本地识别（SenseVoice）→ 离线翻译"
                + "（混元 Hy-MT2，38 语种）→ 实时字幕，并可把译文朗读出来。全程离线、不联网。",
                OnOpenTranslator));

            // 固件升级（OTA）：给鼠标刷 Telink bin。USB dongle 通道需先补固件命令，
            // BLE 直连通道走鼠标自带 OTA 特征（BLE_OTA_SERVER_ENABLE=1）。默认模拟模式。
            Controls.Add(MakeCard(ref y, "OTA", UiTheme.Accent,
                "固件升级",
                "选择 Telink bin（自动读出版本/大小/CRC），经 USB Dongle 或 BLE 直连写给鼠标："
                + "CRC16 分片校验、进度/速率/剩余时间、失败可定位到具体 PDU。",
                OnOpenOta));

            // 产线测试：解除 customerDebugRelease 屏蔽（用户要求打开），与收发调试解耦单独显示。
            // 保留 customerDebugRelease 仅控制「收发调试」入口与底部提示。
            if (ShowAllModules)
                Controls.Add(MakeCard(ref y, "FT", UiTheme.Warn,
                    "产线测试",
                    "经 Dongle 走 UX 产测协议（DUT）：固件版本 / 型号 / 鼠标报告 LED / 鉴权往返等。"
                    + "与收发调试共用 HID 通道，两者不可同时打开。",
                    OnOpenFactory));

            // 鼠标诊断：与产线测试/收发调试共用 dongle 的 HID 通道（模态独占，天然互斥）。
            if (ShowAllModules)
                Controls.Add(MakeCard(ref y, "DG", UiTheme.Primary2,
                    "鼠标诊断",
                    "定位鼠标画线卡顿/断线发生在哪一环：读取 Dongle 的 BLE 收帧 / USB 发出 / "
                    + "ring、fifo 两处缓冲丢帧计数器（MSTAT 0x03），并可在产测模式下监视 0xE1 "
                    + "报告流间隔，区分 BLE 上游与 USB/PC 下游。",
                    OnOpenDiag));

            _lblFoot = new Label();
            _lblFoot.Text = "提示：收发调试 / 产线测试 / 鼠标诊断 / 固件升级（USB Dongle 通道）共用 Dongle 的 HID 通道，"
                          + "四者同时只能打开一个；蓝牙连接与实时翻译相互独立，可随时进入。";
            _lblFoot.Visible = ShowAllModules && !customerDebugRelease;   // 单模块版不显示多模块互斥提示
            _lblFoot.ForeColor = UiTheme.TextSub;
            _lblFoot.SetBounds(22, ClientSize.Height - 40, 736, 20);
            _lblFoot.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;   // 宽度由 LayoutContent 管
            _lblFoot.AutoSize = false;
            _lblFoot.TextAlign = ContentAlignment.MiddleLeft;

            Controls.Add(_lblFoot);

            LayoutContent();   // 初始定位（默认窗口尺寸）
        }

        /// <summary>
        /// 内容列自适应：标题/卡片/页脚一律铺满可用宽度（最大化时同步放大，
        /// 不再保持 756 设计宽度并在两侧留白）；窗口过窄时按最小宽度 240px 兜底。
        /// 卡片不设左右锚定（锚定会强制拉伸），位置/宽度由此统一管理；
        /// 卡片内部控件靠各自锚点跟随卡片宽度（ModeCard 的标题/说明/【进入】均已锚定）。
        /// </summary>
        private void LayoutContent()
        {
            const int margin = 12;
            int avail = ClientSize.Width - margin * 2;
            if (avail < 240)
                avail = 240;
            int w = avail;                 // 铺满：最大化时与翻译页一致地整体放大
            int x = margin;

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

        private void OnOpenOta(object sender, EventArgs e)
        {
            using (var f = new OtaForm())
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

        private void OnOpenDiag(object sender, EventArgs e)
        {
            using (var f = new MouseDiagForm())
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
        private readonly Label _lblTitle;
        private readonly Label _lblDesc;
        private readonly GradientButton _btnGo;
        private bool _hover;

        /// <summary>点击卡片 / 【进入】按钮 / 卡片内任意文字时触发。（不用 Enter：与 Control.Enter 焦点事件同名）</summary>
        public event EventHandler EnterRequested;

        public ModeCard(string badgeText, Color badgeColor, string title, string desc)
        {
            _badgeText = badgeText;
            _badgeColor = badgeColor;
            Cursor = Cursors.Hand;

            _lblTitle = new Label();
            _lblTitle.Text = title;
            _lblTitle.Font = new Font("Microsoft YaHei UI", 10.5F, FontStyle.Bold);
            _lblTitle.ForeColor = UiTheme.TextMain;
            _lblTitle.AutoSize = false;
            _lblTitle.TextAlign = ContentAlignment.MiddleLeft;
            _lblTitle.Cursor = Cursors.Hand;

            _lblDesc = new Label();
            _lblDesc.Text = desc;
            _lblDesc.ForeColor = UiTheme.TextSub;
            _lblDesc.AutoSize = false;   // 关掉自动宽高：否则卡片拉宽后说明文字不重新折行
            _lblDesc.TextAlign = ContentAlignment.TopLeft;
            _lblDesc.Cursor = Cursors.Hand;

            _btnGo = new GradientButton();
            _btnGo.Text = "进入";
            _btnGo.IsPrimary = false;
            _btnGo.Click += RaiseEnterRequested;

            // ⚠ 三个子控件都不设 Anchor，改由 OnResize 显式排布。
            // 曾经用 Top|Left|Right 锚定，但卡片构造时自身还是默认 200px 宽，
            // 右锚距被算成负值；之后卡片被 SetBounds 拉宽到 756，锚定按负偏移复算，
            // 直接把标题/说明拉成 976/1076 宽、【进入】推到 x=1192 飞出卡片（首页一直看不到按钮）。
            Controls.AddRange(new Control[] { _lblTitle, _lblDesc, _btnGo });

            Click += RaiseEnterRequested;
            _lblTitle.Click += RaiseEnterRequested;
            _lblDesc.Click += RaiseEnterRequested;

            MouseEnter += OnHoverIn;
            MouseLeave += OnHoverOut;
            _lblTitle.MouseEnter += OnHoverIn;
            _lblTitle.MouseLeave += OnHoverOut;
            _lblDesc.MouseEnter += OnHoverIn;
            _lblDesc.MouseLeave += OnHoverOut;
            _btnGo.MouseEnter += OnHoverIn;
            _btnGo.MouseLeave += OnHoverOut;

            LayoutChildren();
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            LayoutChildren();
        }

        /// <summary>按当前卡宽排布：文字列铺到【进入】按钮左侧，按钮右对齐留 24px 边距。</summary>
        private void LayoutChildren()
        {
            const int btnW = 96;
            const int btnRightMargin = 24;
            int textW = Width - btnRightMargin - btnW - 18 - TextLeft;   // 与按钮之间留 18px
            if (textW < 80)
                textW = 80;                                             // 卡片极窄时兜底，宁可截断也不重叠
            if (_lblTitle != null)
                _lblTitle.SetBounds(TextLeft, 24, textW, 24);
            if (_lblDesc != null)
                _lblDesc.SetBounds(TextLeft, 50, textW, 56);
            if (_btnGo != null)
                _btnGo.SetBounds(Width - btnRightMargin - btnW, 42, btnW, 32);
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
