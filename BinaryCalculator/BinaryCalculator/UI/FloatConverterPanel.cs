using System;
using System.Windows.Forms;
using BinaryCalculator.Core;

namespace BinaryCalculator.UI
{
    /// <summary>
    /// ② IEEE 754 실수 변환기
    /// 10진수 → 2진수(부호/지수/가수), 2진수 → 10진수 를 양방향으로 변환합니다.
    /// </summary>
    public sealed class FloatConverterPanel : ScrollPage
    {
        readonly RadioButton rbSingle, rbDouble;
        readonly TextBox tbDecimalIn, tbBinaryIn;
        readonly Label lblStatus;
        readonly Label lblSign, lblExponent, lblFraction;
        readonly TextBox oSign, oExponent, oFraction, oAllBits, oHex, oKind;
        readonly TextBox oDecimal, oScientific, oExact, oFractionForm, oError;
        readonly StepsBox steps;

        bool updating;

        public FloatConverterPanel()
        {
            var grid = new FormGrid();

            // ---------------- 정밀도 ----------------
            grid.AddHeader("정밀도 선택");
            rbSingle = new RadioButton { Text = "단정밀도 32비트 (float)  =  부호 1 + 지수 8 + 가수 23,  bias 127", AutoSize = true, Checked = true, Margin = new Padding(3, 3, 20, 3) };
            rbDouble = new RadioButton { Text = "배정밀도 64비트 (double)  =  부호 1 + 지수 11 + 가수 52,  bias 1023", AutoSize = true, Margin = new Padding(3) };
            rbSingle.CheckedChanged += (s, e) => { if (rbSingle.Checked) OnPrecisionChanged(); };
            rbDouble.CheckedChanged += (s, e) => { if (rbDouble.Checked) OnPrecisionChanged(); };
            var precision = FormGrid.Flow();
            precision.Controls.Add(rbSingle);
            precision.Controls.Add(rbDouble);
            grid.AddFull(precision);

            // ---------------- 입력 ----------------
            grid.AddHeader("입력  (어느 쪽에 입력해도 바로 변환됩니다)");
            tbDecimalIn = FormGrid.Input();
            tbDecimalIn.Tag = FontRole.MonoBig;
            grid.AddRow("10진수 → 2진수", tbDecimalIn, FormGrid.MakeButton("변환 ▶", (s, e) => ConvertFromDecimal()));
            grid.AddFull(FormGrid.Note("예)  12.375    -0.1    0.15625    3.4e38    1e-45    1/3    inf    nan"));

            tbBinaryIn = FormGrid.Input();
            tbBinaryIn.Tag = FontRole.MonoBig;
            grid.AddRow("2진수 → 10진수", tbBinaryIn, FormGrid.MakeButton("변환 ▶", (s, e) => ConvertFromBinary()));
            grid.AddFull(FormGrid.Note("예)  0 10000010 10001100000000000000000    (공백은 무시)     또는 16진수  0x41460000"));

            lblStatus = new Label { AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 2, 3, 2), Tag = FontRole.Bold };
            grid.AddFull(lblStatus);

            tbDecimalIn.TextChanged += (s, e) => { if (!updating) ConvertFromDecimal(); };
            tbBinaryIn.TextChanged += (s, e) => { if (!updating) ConvertFromBinary(); };

            // ---------------- 비트 구조 ----------------
            grid.AddHeader("IEEE 754 비트 구조");
            oSign = FormGrid.Output(Palette.SignBack, FontRole.MonoBig);
            oExponent = FormGrid.Output(Palette.ExponentBack, FontRole.MonoBig);
            oFraction = FormGrid.Output(Palette.FractionBack, FontRole.MonoBig);
            oAllBits = FormGrid.Output(Palette.OutputBack);
            oHex = FormGrid.Output(Palette.OutputBack);
            oKind = FormGrid.Output(Palette.OutputBack, FontRole.Normal);
            lblSign = grid.AddRow("부호 S", oSign);
            lblExponent = grid.AddRow("지수 E", oExponent);
            lblFraction = grid.AddRow("가수 M", oFraction);
            grid.AddRow("전체 비트", oAllBits);
            grid.AddRow("16진수", oHex);
            grid.AddRow("종류", oKind);

            // ---------------- 10진수 값 ----------------
            grid.AddHeader("10진수 값");
            oDecimal = FormGrid.Output(Palette.ResultBack, FontRole.MonoBig);
            oScientific = FormGrid.Output(Palette.OutputBack);
            oExact = FormGrid.Output(Palette.OutputBack);
            oFractionForm = FormGrid.Output(Palette.OutputBack);
            oError = FormGrid.Output(Palette.OutputBack);
            grid.AddRow("10진수", oDecimal);
            grid.AddRow("지수 표기", oScientific);
            grid.AddRow("저장된 정확한 값", oExact);
            grid.AddRow("분수", oFractionForm);
            grid.AddRow("변환 오차", oError);
            grid.AddFull(FormGrid.Note(string.Format(
                "※ |값| ≥ 10^{0} 이거나 |값| < 10^{1} 이면 지수 형태로, 그 사이의 값은 일반 실수와 분수로 보여줍니다.",
                FloatReport.LargeExponent, FloatReport.SmallExponent)));

            // ---------------- 변환 과정 ----------------
            grid.AddHeader("변환 과정");
            steps = new StepsBox { VisibleLines = 26 };
            grid.AddFull(steps);

            Controls.Add(grid.Table);

            UpdateFieldLabels();
            tbDecimalIn.Text = "12.375";
        }

        FloatFormat Format { get { return rbSingle.Checked ? FloatFormat.Single : FloatFormat.Double; } }

        void UpdateFieldLabels()
        {
            FloatFormat f = Format;
            lblSign.Text = "부호 S (1비트)";
            lblExponent.Text = string.Format("지수 E ({0}비트)", f.ExponentBits);
            lblFraction.Text = string.Format("가수 M ({0}비트)", f.FractionBits);
        }

        void OnPrecisionChanged()
        {
            UpdateFieldLabels();
            // 10진수 입력칸에는 항상 현재 값이 들어 있으므로 그것을 새 정밀도로 다시 변환합니다.
            ConvertFromDecimal();
        }

        void ConvertFromDecimal()
        {
            ParsedNumber input;
            string error;
            if (!NumberText.TryParseNumber(tbDecimalIn.Text, out input, out error))
            {
                ShowStatus("⚠ " + error, true);
                return;
            }

            FloatFormat f = Format;
            FloatValue v;
            if (input.IsNaN) v = FloatValue.NaN(input.Negative, f);
            else if (input.IsInfinity) v = FloatValue.Infinity(input.Negative, f);
            else v = FloatValue.FromRational(input.Negative, input.Num, input.Den, f);

            FloatReport report = FloatReport.Build(v, input);
            ShowReport(report);
            SetTextSilently(tbBinaryIn, v.SignBitText + " " + v.ExponentBitsText + " " + v.FractionBitsText);

            if (v.OverflowedToInfinity) ShowStatus("⚠ 표현할 수 있는 범위를 넘어 무한대가 되었습니다. (오버플로)", true);
            else if (v.UnderflowedToZero) ShowStatus("⚠ 너무 작아서 0 이 되었습니다. (언더플로)", true);
            else if (v.Rounded) ShowStatus("✔ 변환 완료 — 2진수로 정확히 표현할 수 없어 가장 가까운 값으로 반올림했습니다.", false);
            else ShowStatus("✔ 변환 완료 — 오차 없이 정확하게 표현됩니다.", false);
        }

        void ConvertFromBinary()
        {
            ulong bits;
            string note, error;
            if (!FloatValue.TryParseBits(tbBinaryIn.Text, Format, out bits, out note, out error))
            {
                ShowStatus("⚠ " + error, true);
                return;
            }
            FloatValue v = FloatValue.FromBits(bits, Format);
            FloatReport report = FloatReport.Build(v, null);
            ShowReport(report);
            SetTextSilently(tbDecimalIn, report.DecimalForInput);
            ShowStatus("✔ 변환 완료" + (note != null ? " — " + note : ""), false);
        }

        void SetTextSilently(TextBox box, string text)
        {
            updating = true;
            box.Text = text;
            updating = false;
        }

        void ShowStatus(string text, bool isError)
        {
            lblStatus.Text = text;
            lblStatus.ForeColor = isError ? Palette.Error : Palette.Ok;
        }

        void ShowReport(FloatReport r)
        {
            oSign.Text = r.Sign;
            oExponent.Text = r.Exponent;
            oFraction.Text = r.Fraction;
            oAllBits.Text = r.AllBits;
            oHex.Text = r.Hex;
            oKind.Text = r.Kind;
            oDecimal.Text = r.Decimal;
            oScientific.Text = r.Scientific;
            oExact.Text = r.Exact;
            oFractionForm.Text = r.FractionForm;
            oError.Text = r.Error;
            steps.Text = r.Steps;
        }
    }
}
