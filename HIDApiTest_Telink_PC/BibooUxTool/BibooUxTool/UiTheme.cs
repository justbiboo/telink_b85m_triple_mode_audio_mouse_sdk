using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// 浅色蓝紫主题配色与字体规范（首页与各功能模块共用）。
    /// </summary>
    internal static class UiTheme
    {
        public static readonly Color FormBg    = Color.FromArgb(0xF4, 0xF7, 0xFE); // 窗体背景：浅蓝白
        public static readonly Color CardBorder = Color.FromArgb(0xE3, 0xE9, 0xF5); // 卡片描边
        public static readonly Color Primary   = Color.FromArgb(0x4A, 0x7C, 0xFF); // 主色（渐变左 / 描边 / 文字）
        public static readonly Color Primary2  = Color.FromArgb(0x7B, 0x5C, 0xFF); // 渐变右
        public static readonly Color PrimaryHover  = Color.FromArgb(0x60, 0x8E, 0xFF); // Hover 渐变左
        public static readonly Color PrimaryHover2 = Color.FromArgb(0x91, 0x7A, 0xFF); // Hover 渐变右
        public static readonly Color TextMain  = Color.FromArgb(0x1F, 0x2D, 0x3D); // 主文字
        public static readonly Color TextSub   = Color.FromArgb(0x7A, 0x86, 0x9A); // 次要说明
        public static readonly Color Success   = Color.FromArgb(0x34, 0xC7, 0x7B); // 成功状态
        public static readonly Color Danger    = Color.FromArgb(0xF5, 0x6C, 0x6C); // 错误状态
        public static readonly Color InputBg   = Color.FromArgb(0xFB, 0xFC, 0xFE); // 输入框 / 日志框底色
        public static readonly Color Timestamp = Color.FromArgb(0x9A, 0xA7, 0xBD); // 日志时间戳
        public static readonly Color Rx        = Color.FromArgb(0x00, 0xA8, 0xA8); // 接收日志
        public static readonly Color Warn      = Color.FromArgb(0xF2, 0x9D, 0x38); // 警告状态（如链路存疑）
        public static readonly Color Accent    = Color.FromArgb(0x2E, 0xB8, 0xA6); // 辅助强调色（BLE 徽标等）

        public static readonly Font FontBody  = new Font("Microsoft YaHei UI", 9F);
        public static readonly Font FontTitle = new Font("Microsoft YaHei UI", 9F, FontStyle.Bold);
    }

    /// <summary>
    /// 圆角白底卡片：白底 + 浅蓝描边，替代默认 GroupBox 的硬边框。
    /// </summary>
    internal class RoundedPanel : Panel
    {
        public int Radius { get; set; }
        public Color BorderColor { get; set; }

        public RoundedPanel()
        {
            Radius = 10;
            BorderColor = UiTheme.CardBorder;
            BackColor = Color.White;
            DoubleBuffered = true;
            // 必须开 ResizeRedraw：窗口拉大时 LayoutContent 会重设卡片宽度，没有本样式时
            // 只有扩展条带会被失效重绘，卡片内锚定（Top|Right / Left|Right）移动的子控件
            // 旧位置留在原处不清除，越拉越多形成虚线残影（GradientButton 已开，此处对齐）。
            ResizeRedraw = true;
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            // 先按父容器背景色铺满整个矩形：圆角路径外的像素若保持窗口初始
            // 内容会显示为黑点，描边抗锯齿与黑色混合后更明显。
            // 同样要沿父链跳过透明层（Color.Transparent 铺上去就是透明黑）。
            Color parentBg = UiTheme.FormBg;
            for (Control p = Parent; p != null; p = p.Parent)
            {
                if (p.BackColor.A != 0)
                {
                    parentBg = p.BackColor;
                    break;
                }
            }
            using (var brush = new SolidBrush(parentBg))
            {
                e.Graphics.FillRectangle(brush, ClientRectangle);
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            using (var path = RoundedRectPath(ClientRectangle, Radius))
            using (var fill = new SolidBrush(BackColor))
            {
                g.FillPath(fill, path);
            }

            var borderRect = ClientRectangle;
            borderRect.Inflate(-1, -1);
            using (var path = RoundedRectPath(borderRect, Radius))
            using (var pen = new Pen(BorderColor))
            {
                g.DrawPath(pen, path);
            }

            base.OnPaint(e);
        }

        internal static GraphicsPath RoundedRectPath(Rectangle rect, int radius)
        {
            int r = radius * 2;
            if (r > rect.Width) r = rect.Width;
            if (r > rect.Height) r = rect.Height;
            if (r < 2) r = 2;

            var path = new GraphicsPath();
            path.AddArc(rect.X, rect.Y, r, r, 180, 90);
            path.AddArc(rect.Right - r, rect.Y, r, r, 270, 90);
            path.AddArc(rect.Right - r, rect.Bottom - r, r, r, 0, 90);
            path.AddArc(rect.X, rect.Bottom - r, r, r, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    /// <summary>
    /// 圆角渐变按钮：主按钮为蓝紫渐变白字；IsPrimary=false 时为白底蓝字描边（次要操作）。
    /// </summary>
    internal sealed class GradientButton : Button
    {
        private bool _hover;
        private bool _primary = true;

        /// <summary>切换主/次样式会立即重绘（属性直接赋值不会触发 Invalidate）。</summary>
        public bool IsPrimary
        {
            get { return _primary; }
            set
            {
                if (_primary == value)
                    return;
                _primary = value;
                Invalidate();
            }
        }

        public int Radius { get; set; }

        public GradientButton()
        {
            Radius = 8;
            Font = UiTheme.FontBody;
            Cursor = Cursors.Hand;
            TabStop = true;

            // 完全扁平：阻止原生 BUTTON 窗口类绘制默认边框 / 主题背景，
            // 否则其主题位图的黑色角像素会从自绘圆角外透出形成黑点。
            FlatStyle = FlatStyle.Flat;
            FlatAppearance.BorderSize = 0;
            FlatAppearance.BorderColor = Color.FromArgb(0, 255, 255, 255);

            SetStyle(ControlStyles.UserPaint |
                     ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.ResizeRedraw |
                     ControlStyles.Opaque, true);

            MouseEnter += delegate { _hover = true; Invalidate(); };
            MouseLeave += delegate { _hover = false; Invalidate(); };
        }

        protected override CreateParams CreateParams
        {
            get
            {
                // 不使用原生 BUTTON 窗口类（改为 WinForms 通用窗口），
                // 杜绝系统在焦点变化等时机绘制原生按钮边框。
                CreateParams cp = base.CreateParams;
                cp.ClassName = null;
                return cp;
            }
        }

        protected override void OnEnabledChanged(EventArgs e)
        {
            base.OnEnabledChanged(e);
            Invalidate();
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            // 窗口创建/首次显示时系统原生绘制可能留下角部残留，
            // 句柄就绪后补一次重绘将其抹掉。
            BeginInvoke(new MethodInvoker(Invalidate));
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            // Opaque 样式：系统不再擦除背景，整个客户区由 OnPaint 负责绘制，
            // 避免 UserPaint 管线下 OnPaintBackground 不生效导致的圆角黑点。
        }

        /// <summary>
        /// 取绘制底色：沿父链向上找第一个 Alpha≠0 的 BackColor。
        /// 中间层常为 Color.Transparent（透明子面板），必须跳过，否则
        /// Clear(Transparent) 会在圆角外留下透明黑，形成视觉上的黑边。
        /// </summary>
        private Color ResolveBackColor()
        {
            for (Control p = Parent; p != null; p = p.Parent)
            {
                Color c = p.BackColor;
                if (c.A != 0)
                    return c;
            }
            return UiTheme.FormBg;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;

            // 1) 先用父容器背景色彻底铺满客户区（Clear 不受抗锯齿影响，
            //    不会与缓冲位图初始黑色混合产生暗边 / 角部黑点）。
            //    ⚠ 必须沿父链找到第一个**不透明** BackColor：子面板常设成
            //    Color.Transparent（如 OtaForm._pnlBle/_pnlUsb），直接 Clear 透明色
            //    等于在圆角外铺一层透明黑，抗锯齿过渡区会显出黑边。
            g.Clear(ResolveBackColor());

            // 2) 圆角主体绘制需要平滑边缘，此处再开启抗锯齿。
            g.SmoothingMode = SmoothingMode.AntiAlias;

            var rect = new Rectangle(0, 0, Width - 1, Height - 1);
            Color textColor;

            using (var path = RoundedPanel.RoundedRectPath(rect, Radius))
            {
                if (IsPrimary)
                {
                    Color c1, c2;
                    if (!Enabled)
                    {
                        c1 = Color.FromArgb(0xD9, 0xE1, 0xF2);
                        c2 = Color.FromArgb(0xD9, 0xE1, 0xF2);
                        textColor = Color.FromArgb(0xA6, 0xB1, 0xC6);
                    }
                    else
                    {
                        c1 = _hover ? UiTheme.PrimaryHover : UiTheme.Primary;
                        c2 = _hover ? UiTheme.PrimaryHover2 : UiTheme.Primary2;
                        textColor = Color.White;
                    }

                    using (var brush = new LinearGradientBrush(rect, c1, c2, LinearGradientMode.Horizontal))
                    {
                        g.FillPath(brush, path);
                    }
                }
                else
                {
                    Color fill = Enabled ? Color.White : Color.FromArgb(0xF2, 0xF4, 0xFA);
                    Color border = Enabled ? UiTheme.Primary : UiTheme.CardBorder;
                    textColor = Enabled ? UiTheme.Primary : UiTheme.TextSub;

                    using (var brush = new SolidBrush(fill))
                    {
                        g.FillPath(brush, path);
                    }
                    using (var pen = new Pen(border))
                    {
                        g.DrawPath(pen, path);
                    }
                }
            }

            TextRenderer.DrawText(g, Text ?? string.Empty, Font, ClientRectangle, textColor,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter |
                TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
        }
    }
}
