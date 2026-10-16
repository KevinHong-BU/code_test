using System;
using System.Collections.Generic;
using System.Drawing;
using System.Numerics;
using System.Windows.Forms;
using BinaryCalculator.Core;

namespace BinaryCalculator.UI
{
    /// <summary>
    /// ① 2진 정수 (2의 보수) 계산기
    /// 두 수 A, B 를 2진수 / 10진수 / 16진수 중 아무 칸에나 입력하면 나머지 칸이 자동으로 바뀌고,
    /// 연산 버튼을 누르면 결과와 계산 과정을 보여줍니다.
    /// </summary>
    public sealed class IntegerCalculatorPanel : ScrollPage
    {
        readonly ComboBox cbWidth;
        readonly Label lblRange;
        readonly Operand opA;
        readonly Operand opB;
        readonly Dictionary<IntOperation, Button> opButtons = new Dictionary<IntOperation, Button>();
        readonly Label lblResultTitle;
        readonly TextBox rBin, rDec, rHex, rFlags;
        readonly Label lblResultNote;
        readonly StepsBox steps;

        IntOperation currentOp = IntOperation.Add;
        int bitWidth = 8;
        bool hasResult;
        BigInteger lastResultRaw;

        public IntegerCalculatorPanel()
        {
            var grid = new FormGrid();

            // ---------------- 설정 ----------------
            grid.AddHeader("설정");
            cbWidth = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Tag = FontRole.Mono, Width = 80, Margin = new Padding(3) };
            foreach (int w in TwosComplement.SupportedWidths) cbWidth.Items.Add(w + "비트");
            cbWidth.SelectedIndex = Array.IndexOf(TwosComplement.SupportedWidths, bitWidth);
            cbWidth.SelectedIndexChanged += OnWidthChanged;
            lblRange = new Label { AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(12, 4, 3, 4) };
            var widthFlow = FormGrid.Flow();
            widthFlow.Controls.Add(cbWidth);
            widthFlow.Controls.Add(lblRange);
            grid.AddRow("비트 수", widthFlow);

            // ---------------- 피연산자 ----------------
            opA = new Operand(this, "A", grid);
            opB = new Operand(this, "B", grid);

            // ---------------- 연산 버튼 ----------------
            grid.AddHeader("연산 선택");
            var ops = FormGrid.Flow();
            AddOpButton(ops, IntOperation.Add, "A + B");
            AddOpButton(ops, IntOperation.Subtract, "A − B");
            AddOpButton(ops, IntOperation.Multiply, "A × B");
            AddOpButton(ops, IntOperation.Divide, "A ÷ B (몫)");
            AddOpButton(ops, IntOperation.Remainder, "A % B (나머지)");
            AddOpButton(ops, IntOperation.NegateA, "−A (2의 보수)");
            AddOpButton(ops, IntOperation.NotA, "NOT A (1의 보수)");
            AddOpButton(ops, IntOperation.And, "A AND B");
            AddOpButton(ops, IntOperation.Or, "A OR B");
            AddOpButton(ops, IntOperation.Xor, "A XOR B");
            AddOpButton(ops, IntOperation.ShiftLeft, "A << B");
            AddOpButton(ops, IntOperation.ShiftRightArithmetic, "A >> B (산술)");
            AddOpButton(ops, IntOperation.ShiftRightLogical, "A >>> B (논리)");
            grid.AddFull(ops);

            var tools = FormGrid.Flow();
            tools.Controls.Add(FormGrid.MakeButton("결과 → A", (s, e) => { if (hasResult) opA.SetRaw(lastResultRaw); }));
            tools.Controls.Add(FormGrid.MakeButton("결과 → B", (s, e) => { if (hasResult) opB.SetRaw(lastResultRaw); }));
            tools.Controls.Add(FormGrid.MakeButton("A ↔ B 바꾸기", (s, e) => SwapOperands()));
            tools.Controls.Add(FormGrid.MakeButton("A, B 지우기 (0)", (s, e) => { opA.SetRaw(BigInteger.Zero); opB.SetRaw(BigInteger.Zero); }));
            grid.AddFull(tools);

            // ---------------- 결과 ----------------
            lblResultTitle = grid.AddHeader("결과");
            rBin = FormGrid.Output(Palette.ResultBack, FontRole.MonoBig);
            rDec = FormGrid.Output(Palette.ResultBack, FontRole.MonoBig);
            rHex = FormGrid.Output(Palette.ResultBack, FontRole.MonoBig);
            rFlags = FormGrid.Output(Palette.OutputBack);
            grid.AddRow("2진수", rBin);
            grid.AddRow("10진수", rDec);
            grid.AddRow("16진수", rHex);
            grid.AddRow("플래그", rFlags);
            lblResultNote = new Label { AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 4, 3, 4), Tag = FontRole.Bold };
            grid.AddFull(lblResultNote);

            // ---------------- 계산 과정 ----------------
            grid.AddHeader("계산 과정");
            steps = new StepsBox { VisibleLines = 22 };
            grid.AddFull(steps);

            Controls.Add(grid.Table);

            UpdateRangeLabel();
            opA.SetRaw(TwosComplement.ToRaw(13, bitWidth));
            opB.SetRaw(TwosComplement.ToRaw(-6, bitWidth));
            SelectOperation(IntOperation.Add);
        }

        public int BitWidth { get { return bitWidth; } }

        void AddOpButton(FlowLayoutPanel flow, IntOperation op, string text)
        {
            Button b = FormGrid.MakeButton(text, (s, e) => SelectOperation(op));
            b.Tag = FontRole.Bold;
            opButtons[op] = b;
            flow.Controls.Add(b);
        }

        void SelectOperation(IntOperation op)
        {
            currentOp = op;
            foreach (KeyValuePair<IntOperation, Button> pair in opButtons)
            {
                bool selected = pair.Key == op;
                pair.Value.UseVisualStyleBackColor = !selected;
                pair.Value.BackColor = selected ? Palette.Selected : SystemColors.Control;
            }
            Recalculate();
        }

        void SwapOperands()
        {
            if (!opA.Valid || !opB.Valid) return;
            BigInteger a = opA.Raw;
            opA.SetRaw(opB.Raw);
            opB.SetRaw(a);
        }

        void UpdateRangeLabel()
        {
            lblRange.Text = string.Format("표현 범위: {0}   (부호 없는 정수로는 0 ~ {1})",
                TwosComplement.RangeText(bitWidth), TwosComplement.Mask(bitWidth));
        }

        void OnWidthChanged(object sender, EventArgs e)
        {
            int newWidth = TwosComplement.SupportedWidths[cbWidth.SelectedIndex];
            if (newWidth == bitWidth) return;
            int oldWidth = bitWidth;
            bitWidth = newWidth;
            UpdateRangeLabel();
            opA.ChangeWidth(oldWidth, newWidth);
            opB.ChangeWidth(oldWidth, newWidth);
            Recalculate();
        }

        internal void Recalculate()
        {
            lblResultTitle.Text = "결과:   " + TwosComplement.OperationSymbol(currentOp);
            bool needB = TwosComplement.UsesB(currentOp);
            if (!opA.Valid || (needB && !opB.Valid))
            {
                ClearResult("⚠ 피연산자를 올바르게 입력하세요.");
                return;
            }

            IntResult r = TwosComplement.Compute(currentOp, opA.Raw, opB.Raw, bitWidth);
            if (!r.Success)
            {
                ClearResult("⚠ " + r.Error);
                steps.Text = r.Steps;
                return;
            }

            hasResult = true;
            lastResultRaw = r.Raw;
            BigInteger signed = TwosComplement.ToSigned(r.Raw, bitWidth);
            rBin.Text = TwosComplement.ToBinary(r.Raw, bitWidth, true);
            rDec.Text = signed.ToString();
            rHex.Text = TwosComplement.ToHex(r.Raw, bitWidth, true);

            int n = TwosComplement.BitAt(r.Raw, bitWidth - 1) ? 1 : 0;
            int z = r.Raw.IsZero ? 1 : 0;
            rFlags.Text = string.Format("N(부호)={0}   Z(제로)={1}   C(캐리)={2}   V(오버플로)={3}",
                n, z, r.CarryApplicable ? (r.Carry ? "1" : "0") : "-", r.Overflow ? 1 : 0);

            if (r.Overflow)
            {
                lblResultNote.ForeColor = Palette.Error;
                lblResultNote.Text = string.Format("⚠ 오버플로 발생!  수학적 결과 {0} 은(는) {1}비트 범위({2})를 벗어납니다.",
                    r.MathValue, bitWidth, TwosComplement.RangeText(bitWidth));
            }
            else if (r.HasMathValue)
            {
                lblResultNote.ForeColor = Palette.Ok;
                lblResultNote.Text = "✔ 오버플로 없음 — 수학적 결과와 같습니다.";
            }
            else
            {
                lblResultNote.ForeColor = Palette.Ok;
                lblResultNote.Text = "✔ 비트 연산 결과";
            }
            steps.Text = r.Steps;
        }

        void ClearResult(string message)
        {
            hasResult = false;
            rBin.Text = rDec.Text = rHex.Text = rFlags.Text = string.Empty;
            lblResultNote.ForeColor = Palette.Error;
            lblResultNote.Text = message;
            steps.Text = string.Empty;
        }

        /// <summary>피연산자 하나 (2진수 / 10진수 / 16진수 입력칸 3개가 서로 연동됨)</summary>
        sealed class Operand
        {
            readonly IntegerCalculatorPanel owner;
            readonly TextBox bin, dec, hex;
            readonly Label status;
            bool updating;

            public BigInteger Raw;
            public bool Valid = true;

            public Operand(IntegerCalculatorPanel owner, string name, FormGrid grid)
            {
                this.owner = owner;
                grid.AddHeader("피연산자 " + name);
                bin = FormGrid.Input();
                dec = FormGrid.Input();
                hex = FormGrid.Input();
                grid.AddRow("2진수", bin);
                grid.AddRow("10진수", dec);
                grid.AddRow("16진수", hex);
                status = FormGrid.Note("");
                grid.AddFull(status);

                bin.TextChanged += (s, e) => OnEdited(bin, IntBase.Binary);
                dec.TextChanged += (s, e) => OnEdited(dec, IntBase.Decimal);
                hex.TextChanged += (s, e) => OnEdited(hex, IntBase.Hexadecimal);
                EventHandler reformat = (s, e) => { if (Valid) Show(null); };
                bin.Leave += reformat;
                dec.Leave += reformat;
                hex.Leave += reformat;
            }

            int W { get { return owner.BitWidth; } }

            void OnEdited(TextBox source, IntBase numberBase)
            {
                if (updating) return;
                BigInteger raw;
                string error;
                if (TwosComplement.TryParse(source.Text, numberBase, W, out raw, out error))
                {
                    Raw = raw;
                    Valid = true;
                    Show(source);
                }
                else
                {
                    Valid = false;
                    status.ForeColor = Palette.Error;
                    status.Text = "⚠ " + error;
                }
                owner.Recalculate();
            }

            public void SetRaw(BigInteger raw)
            {
                Raw = raw;
                Valid = true;
                Show(null);
                owner.Recalculate();
            }

            public void ChangeWidth(int oldWidth, int newWidth)
            {
                if (!Valid)
                {
                    Raw = BigInteger.Zero;
                    Valid = true;
                }
                else
                {
                    BigInteger v = TwosComplement.ToSigned(Raw, oldWidth);
                    Raw = TwosComplement.InRange(v, newWidth)
                        ? TwosComplement.ToRaw(v, newWidth)               // 같은 값 유지 (부호 확장)
                        : Raw & TwosComplement.Mask(newWidth);            // 범위를 넘으면 아래쪽 비트만 남김
                }
                Show(null);
            }

            /// <summary>except 를 뺀 나머지 칸을 현재 값으로 채웁니다. (입력 중인 칸은 건드리지 않음)</summary>
            void Show(TextBox except)
            {
                updating = true;
                if (bin != except) bin.Text = TwosComplement.ToBinary(Raw, W, true);
                if (dec != except) dec.Text = TwosComplement.ToSigned(Raw, W).ToString();
                if (hex != except) hex.Text = TwosComplement.ToHex(Raw, W, true);
                updating = false;

                bool negative = TwosComplement.BitAt(Raw, W - 1);
                status.ForeColor = Palette.Hint;
                status.Text = string.Format("부호 비트 = {0} → {1}      부호 없는 정수로 읽으면 {2}",
                    negative ? 1 : 0, negative ? "음수" : "양수(또는 0)", Raw);
            }
        }
    }
}
