using System;
using System.Drawing;
using System.Windows.Forms;

namespace BinaryCalculator.UI
{
    /// <summary>
    /// 컨트롤의 Tag 에 넣는 "글꼴 역할".  MainForm 이 글꼴 크기를 바꿀 때 이 값을 보고 글꼴을 고릅니다.
    /// </summary>
    public static class FontRole
    {
        public const string Normal = "normal";
        public const string Bold = "bold";
        public const string Header = "header";
        public const string Mono = "mono";
        public const string MonoBig = "monoBig";
        public const string Small = "small";
    }

    public static class Palette
    {
        public static readonly Color Header = Color.FromArgb(0, 70, 140);
        public static readonly Color SignBack = Color.FromArgb(255, 222, 222);
        public static readonly Color ExponentBack = Color.FromArgb(214, 245, 214);
        public static readonly Color FractionBack = Color.FromArgb(214, 230, 255);
        public static readonly Color OutputBack = Color.FromArgb(248, 248, 240);
        public static readonly Color ResultBack = Color.FromArgb(255, 250, 205);
        public static readonly Color Error = Color.FromArgb(200, 0, 0);
        public static readonly Color Ok = Color.FromArgb(0, 120, 0);
        public static readonly Color Hint = Color.FromArgb(90, 90, 90);
        public static readonly Color Selected = Color.FromArgb(150, 200, 255);
    }

    /// <summary>
    /// [이름 | 내용 | 버튼] 3열 표 형태의 화면 배치 도우미.
    /// 모든 행은 AutoSize 라서 글꼴이 커지면 자동으로 늘어납니다.
    /// </summary>
    public sealed class FormGrid
    {
        public readonly TableLayoutPanel Table;

        public FormGrid()
        {
            Table = new TableLayoutPanel
            {
                ColumnCount = 3,
                RowCount = 0,
                Dock = DockStyle.Top,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Padding = new Padding(4)
            };
            Table.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            Table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            Table.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        }

        int NewRow()
        {
            Table.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            return Table.RowCount++;
        }

        public Label AddHeader(string text)
        {
            var label = new Label
            {
                Text = text,
                AutoSize = true,
                ForeColor = Palette.Header,
                Margin = new Padding(3, 14, 3, 4),
                Tag = FontRole.Header
            };
            AddFull(label);
            return label;
        }

        public void AddFull(Control c)
        {
            int row = NewRow();
            Table.Controls.Add(c, 0, row);
            Table.SetColumnSpan(c, 3);
        }

        public Label AddRow(string caption, Control main, Control extra = null)
        {
            int row = NewRow();
            var label = Caption(caption);
            Table.Controls.Add(label, 0, row);
            if (extra == null)
            {
                Table.Controls.Add(main, 1, row);
                Table.SetColumnSpan(main, 2);
            }
            else
            {
                Table.Controls.Add(main, 1, row);
                Table.Controls.Add(extra, 2, row);
            }
            return label;
        }

        public static Label Caption(string text)
        {
            return new Label
            {
                Text = text,
                AutoSize = true,
                Anchor = AnchorStyles.Left,
                Margin = new Padding(3, 4, 12, 4),
                Tag = FontRole.Bold
            };
        }

        public static Label Note(string text)
        {
            return new Label
            {
                Text = text,
                AutoSize = true,
                Anchor = AnchorStyles.Left,
                ForeColor = Palette.Hint,
                Margin = new Padding(3, 0, 3, 6),
                Tag = FontRole.Small
            };
        }

        public static TextBox Input()
        {
            return new TextBox
            {
                Anchor = AnchorStyles.Left | AnchorStyles.Right,
                Margin = new Padding(3, 3, 3, 3),
                Tag = FontRole.Mono
            };
        }

        public static TextBox Output(Color back, string role = FontRole.Mono)
        {
            return new TextBox
            {
                ReadOnly = true,
                BackColor = back,
                Anchor = AnchorStyles.Left | AnchorStyles.Right,
                Margin = new Padding(3, 3, 3, 3),
                Tag = role,
                TabStop = false
            };
        }

        public static Button MakeButton(string text, EventHandler onClick)
        {
            var b = new Button
            {
                Text = text,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Padding = new Padding(6, 2, 6, 2),
                Margin = new Padding(3),
                UseVisualStyleBackColor = true,
                Tag = FontRole.Normal
            };
            b.Click += onClick;
            return b;
        }

        public static FlowLayoutPanel Flow()
        {
            return new FlowLayoutPanel
            {
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                WrapContents = true,
                Dock = DockStyle.Fill,
                Margin = new Padding(0)
            };
        }
    }

    /// <summary>
    /// 여러 줄 읽기 전용 텍스트 상자.  글꼴이 바뀌면 VisibleLines 줄이 보이도록 높이를 다시 계산합니다.
    /// </summary>
    public sealed class StepsBox : TextBox
    {
        int visibleLines = 16;

        public StepsBox()
        {
            Multiline = true;
            ReadOnly = true;
            WordWrap = false;
            ScrollBars = ScrollBars.Both;
            AutoSize = false;
            BackColor = Palette.OutputBack;
            Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            Tag = FontRole.Mono;
            UpdateHeight();
        }

        public int VisibleLines
        {
            get { return visibleLines; }
            set { visibleLines = Math.Max(3, value); UpdateHeight(); }
        }

        protected override void OnFontChanged(EventArgs e)
        {
            base.OnFontChanged(e);
            UpdateHeight();
        }

        void UpdateHeight()
        {
            Height = Font.Height * visibleLines + SystemInformation.HorizontalScrollBarHeight + 8;
        }
    }

    /// <summary>스크롤되는 탭 내용 (글꼴을 크게 하면 세로 스크롤바가 생깁니다)</summary>
    public class ScrollPage : UserControl
    {
        public ScrollPage()
        {
            AutoScroll = true;
            Dock = DockStyle.Fill;
            Padding = new Padding(8);
        }
    }
}
