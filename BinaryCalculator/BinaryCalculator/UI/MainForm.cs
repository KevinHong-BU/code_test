using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Text;
using System.Windows.Forms;

namespace BinaryCalculator.UI
{
    /// <summary>
    /// 메인 창: 위쪽 글꼴 크기 도구막대 + 두 개의 탭(정수 계산기 / 실수 변환기)
    /// 글꼴 크기: 버튼, 숫자 입력, Ctrl + '+' / Ctrl + '−' / Ctrl + 0, Ctrl + 마우스 휠
    /// </summary>
    public sealed class MainForm : Form, IMessageFilter
    {
        public const int DefaultFontSize = 14;
        const int MinFontSize = 8;
        const int MaxFontSize = 48;
        const int WM_MOUSEWHEEL = 0x020A;

        readonly NumericUpDown nudFontSize;
        readonly TabControl tabs;
        readonly string uiFamily;
        readonly string monoFamily;
        List<Font> currentFonts = new List<Font>();

        public MainForm()
        {
            Text = "2진 정수 계산기 · IEEE 754 실수 변환기";
            AutoScaleMode = AutoScaleMode.None;
            StartPosition = FormStartPosition.CenterScreen;
            Size = new Size(1280, 900);
            MinimumSize = new Size(640, 480);
            KeyPreview = true;

            uiFamily = PickFamily(new[] { "맑은 고딕", "Malgun Gothic", "Segoe UI" }, SystemFonts.MessageBoxFont.FontFamily.Name);
            monoFamily = PickFamily(new[] { "Consolas", "D2Coding", "Cascadia Mono", "Courier New" }, FontFamily.GenericMonospace.Name);

            // ---------------- 글꼴 크기 도구막대 ----------------
            var bar = new FlowLayoutPanel
            {
                Dock = DockStyle.Top,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                WrapContents = true,
                Padding = new Padding(6, 4, 6, 4),
                BackColor = Color.FromArgb(230, 238, 250)
            };
            bar.Controls.Add(new Label { Text = "글꼴 크기", AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 8, 6, 3), Tag = FontRole.Bold });
            bar.Controls.Add(MakeBarButton("가−  작게", (s, e) => StepFontSize(-2)));
            nudFontSize = new NumericUpDown
            {
                Minimum = MinFontSize,
                Maximum = MaxFontSize,
                Value = DefaultFontSize,
                Anchor = AnchorStyles.Left,
                Margin = new Padding(3, 5, 3, 3),
                TextAlign = HorizontalAlignment.Center,
                Tag = FontRole.Bold
            };
            nudFontSize.ValueChanged += (s, e) => ApplyFontSize((int)nudFontSize.Value);
            bar.Controls.Add(nudFontSize);
            bar.Controls.Add(new Label { Text = "pt", AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(0, 8, 6, 3) });
            bar.Controls.Add(MakeBarButton("가+  크게", (s, e) => StepFontSize(+2)));
            bar.Controls.Add(MakeBarButton("기본 크기", (s, e) => nudFontSize.Value = DefaultFontSize));
            bar.Controls.Add(MakeBarButton("수업용 (24pt)", (s, e) => nudFontSize.Value = 24));
            bar.Controls.Add(new Label
            {
                Text = "단축키: Ctrl + '+' / Ctrl + '−' / Ctrl + 0,  Ctrl + 마우스 휠",
                AutoSize = true,
                Anchor = AnchorStyles.Left,
                ForeColor = Palette.Hint,
                Margin = new Padding(16, 8, 3, 3),
                Tag = FontRole.Small
            });

            // ---------------- 탭 ----------------
            tabs = new TabControl { Dock = DockStyle.Fill, Padding = new Point(14, 6) };
            var intPage = new TabPage("① 2진 정수 계산기 (2의 보수)");
            intPage.Controls.Add(new IntegerCalculatorPanel());
            var floatPage = new TabPage("② 실수 변환기 (IEEE 754)");
            floatPage.Controls.Add(new FloatConverterPanel());
            tabs.TabPages.Add(intPage);
            tabs.TabPages.Add(floatPage);

            // 도킹 순서: 나중에 추가한 컨트롤이 먼저 배치됨 → bar 가 위, tabs 가 나머지를 채움
            Controls.Add(tabs);
            Controls.Add(bar);

            ApplyFontSize(DefaultFontSize);
            Application.AddMessageFilter(this);
        }

        Button MakeBarButton(string text, EventHandler onClick)
        {
            Button b = FormGrid.MakeButton(text, onClick);
            b.TabStop = false;
            return b;
        }

        static string PickFamily(string[] candidates, string fallback)
        {
            using (var installed = new InstalledFontCollection())
            {
                var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (FontFamily family in installed.Families) names.Add(family.Name);
                foreach (string c in candidates)
                {
                    if (names.Contains(c)) return c;
                }
            }
            return fallback;
        }

        void StepFontSize(int delta)
        {
            int size = (int)nudFontSize.Value + delta;
            size = Math.Max(MinFontSize, Math.Min(MaxFontSize, size));
            nudFontSize.Value = size;
        }

        /// <summary>모든 컨트롤의 글꼴을 역할(Tag)에 맞게 새 크기로 바꿉니다.</summary>
        void ApplyFontSize(int size)
        {
            var fonts = new Dictionary<string, Font>
            {
                { FontRole.Normal, new Font(uiFamily, size, FontStyle.Regular) },
                { FontRole.Bold, new Font(uiFamily, size, FontStyle.Bold) },
                { FontRole.Header, new Font(uiFamily, size * 1.2f, FontStyle.Bold) },
                { FontRole.Small, new Font(uiFamily, Math.Max(MinFontSize, size * 0.85f), FontStyle.Regular) },
                { FontRole.Mono, new Font(monoFamily, size, FontStyle.Regular) },
                { FontRole.MonoBig, new Font(monoFamily, size * 1.2f, FontStyle.Bold) }
            };

            SuspendLayout();
            Font = fonts[FontRole.Normal];
            ApplyFontRecursive(this, fonts);
            ResumeLayout(true);
            PerformLayout();

            foreach (Font old in currentFonts) old.Dispose();
            currentFonts = new List<Font>(fonts.Values);
        }

        static void ApplyFontRecursive(Control parent, Dictionary<string, Font> fonts)
        {
            foreach (Control c in parent.Controls)
            {
                string role = c.Tag as string;
                Font font;
                if (role == null || !fonts.TryGetValue(role, out font)) font = fonts[FontRole.Normal];
                c.Font = font;

                // 자동으로 너비가 바뀌지 않는 컨트롤은 직접 맞춰 줌
                var combo = c as ComboBox;
                if (combo != null)
                {
                    int widest = 0;
                    foreach (object item in combo.Items)
                        widest = Math.Max(widest, TextRenderer.MeasureText(item.ToString(), font).Width);
                    combo.Width = widest + SystemInformation.VerticalScrollBarWidth + 16;
                }
                var updown = c as NumericUpDown;
                if (updown != null)
                    updown.Width = TextRenderer.MeasureText("888", font).Width + SystemInformation.VerticalScrollBarWidth + 16;

                ApplyFontRecursive(c, fonts);
            }
        }

        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            switch (keyData)
            {
                case Keys.Control | Keys.Oemplus:
                case Keys.Control | Keys.Shift | Keys.Oemplus:
                case Keys.Control | Keys.Add:
                    StepFontSize(+1);
                    return true;
                case Keys.Control | Keys.OemMinus:
                case Keys.Control | Keys.Subtract:
                    StepFontSize(-1);
                    return true;
                case Keys.Control | Keys.D0:
                case Keys.Control | Keys.NumPad0:
                    nudFontSize.Value = DefaultFontSize;
                    return true;
            }
            return base.ProcessCmdKey(ref msg, keyData);
        }

        /// <summary>Ctrl + 마우스 휠로 글꼴 크기 조절 (어느 컨트롤 위에 있든 동작)</summary>
        public bool PreFilterMessage(ref Message m)
        {
            if (m.Msg == WM_MOUSEWHEEL && (ModifierKeys & Keys.Control) == Keys.Control && ContainsFocus)
            {
                int delta = (short)((m.WParam.ToInt64() >> 16) & 0xFFFF);
                StepFontSize(delta > 0 ? +1 : -1);
                return true;
            }
            return false;
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            Application.RemoveMessageFilter(this);
            base.OnFormClosed(e);
        }
    }
}
