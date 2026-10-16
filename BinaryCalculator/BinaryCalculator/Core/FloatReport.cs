using System;
using System.Globalization;
using System.Numerics;
using System.Text;

namespace BinaryCalculator.Core
{
    /// <summary>
    /// 화면에 보여줄 IEEE 754 변환 결과 (모든 문자열을 여기서 만듭니다).
    /// </summary>
    public sealed class FloatReport
    {
        /// <summary>|값| 이 이 범위 밖이면 "크거나 작은 값" 으로 보고 지수 형태로 보여줍니다.</summary>
        public const int LargeExponent = 7;    // |x| ≥ 10^7
        public const int SmallExponent = -4;   // |x| < 10^-4

        public string Sign = "";
        public string Exponent = "";
        public string Fraction = "";
        public string AllBits = "";
        public string Hex = "";
        public string Kind = "";

        public string Decimal = "";      // 대표 10진수 값 (보통 크기 → 일반 실수, 크거나 작음 → 지수 형태)
        public string Scientific = "";   // 지수 표기
        public string Exact = "";        // 실제로 저장된 정확한 값
        public string FractionForm = ""; // 분수
        public string Error = "";        // 입력값과의 오차
        public string Steps = "";

        /// <summary>10진수 입력창에 다시 넣어줄 문자열</summary>
        public string DecimalForInput = "";

        static string I(BigInteger x) { return x.ToString(CultureInfo.InvariantCulture); }
        static string I(int x) { return x.ToString(CultureInfo.InvariantCulture); }

        public static FloatReport Build(FloatValue v, ParsedNumber input)
        {
            FloatFormat f = v.Format;
            var r = new FloatReport();
            string signChar = v.Negative ? "−" : "";
            string signAscii = v.Negative ? "-" : "";

            r.Sign = string.Format("{0}      →  {1}", v.SignBitText, v.Negative ? "음수 (−)" : "양수 (+)");
            r.Exponent = string.Format("{0}      = {1}", NumberText.GroupFromRight(v.ExponentBitsText, 4), v.ExponentField);
            r.Fraction = NumberText.GroupFromLeft(v.FractionBitsText, 4);
            r.AllBits = v.SignBitText + " | " + v.ExponentBitsText + " | " + v.FractionBitsText;
            r.Hex = v.HexText;

            switch (v.Kind)
            {
                case FloatKind.Zero:
                    r.Kind = "0 (영)  — 지수 비트와 가수 비트가 모두 0";
                    break;
                case FloatKind.Subnormal:
                    r.Kind = string.Format("비정규화 수 (Subnormal)  — 지수 비트가 모두 0, 실제 지수 = {0}, 숨은 비트 = 0", f.MinExponent);
                    break;
                case FloatKind.Normal:
                    r.Kind = string.Format("정규화 수 (Normal)  — 실제 지수 = {0} − {1} = {2}, 숨은 비트 = 1",
                        v.ExponentField, f.Bias, v.UnbiasedExponent);
                    break;
                case FloatKind.Infinity:
                    r.Kind = "무한대 (Infinity)  — 지수 비트가 모두 1, 가수 비트가 모두 0";
                    break;
                case FloatKind.NaN:
                    r.Kind = string.Format("NaN (Not a Number, 숫자가 아님)  — 지수 비트가 모두 1, 가수 ≠ 0  ({0})",
                        ((v.Fraction >> (f.FractionBits - 1)) & 1UL) == 1UL ? "조용한 NaN" : "신호 NaN");
                    break;
            }

            var steps = new StringBuilder();
            steps.AppendLine(string.Format("■ 형식: {0}  =  부호 1비트 + 지수 {1}비트 + 가수 {2}비트,   바이어스(bias) = {3}",
                f.Name, f.ExponentBits, f.FractionBits, f.Bias));
            steps.AppendLine(string.Format("   값 = (−1)^S × 1.M(2) × 2^(E − {0})        (정규화 수)", f.Bias));
            steps.AppendLine(string.Format("   값 = (−1)^S × 0.M(2) × 2^({0})          (비정규화 수, E = 0)", f.MinExponent));
            steps.AppendLine();

            if (v.Kind == FloatKind.NaN)
            {
                r.Decimal = "NaN (숫자가 아님)";
                r.Scientific = r.Exact = r.FractionForm = "—";
                r.DecimalForInput = "NaN";
                r.Error = "—";
                steps.AppendLine("■ 지수 비트가 모두 1 이고 가수가 0 이 아니면 NaN 입니다.");
                steps.AppendLine("   0/0, ∞−∞, √(−1) 처럼 정의할 수 없는 계산의 결과를 나타냅니다.");
                r.Steps = steps.ToString();
                return r;
            }

            if (v.Kind == FloatKind.Infinity)
            {
                r.Decimal = signChar + "∞  (" + (v.Negative ? "음의" : "양의") + " 무한대)";
                r.Scientific = r.Exact = r.FractionForm = "—";
                r.DecimalForInput = signAscii + "Infinity";
                if (input != null && input.IsFinite)
                {
                    BigInteger maxNum, maxDen;
                    FloatValue max = FloatValue.FromBits((((ulong)f.MaxExponentField - 1) << f.FractionBits) | ((1UL << f.FractionBits) - 1), f);
                    max.GetExactRational(out maxNum, out maxDen);
                    int me;
                    bool ex;
                    string mm = NumberText.ToScientific(maxNum, maxDen, f.MaxSignificantDigits, out me, out ex);
                    r.Error = "범위 초과 (오버플로) → 무한대";
                    AppendInputSteps(steps, input, v);
                    steps.AppendLine(string.Format("■ 입력값이 {0} 의 최댓값 ≈ {1} 보다 커서 표현할 수 없습니다.", f.Name, NumberText.TimesTen(mm, me)));
                    steps.AppendLine("   → 지수 비트를 모두 1, 가수 비트를 모두 0 으로 하여 무한대(∞)로 저장합니다.");
                }
                else
                {
                    r.Error = "—";
                    steps.AppendLine("■ 지수 비트가 모두 1 이고 가수가 0 이면 무한대(∞)입니다.");
                }
                r.Steps = steps.ToString();
                return r;
            }

            if (v.Kind == FloatKind.Zero)
            {
                r.Decimal = signChar + "0";
                r.Scientific = signChar + "0 × 10^0";
                r.Exact = signChar + "0";
                r.FractionForm = signChar + "0";
                r.DecimalForInput = signAscii + "0";
                if (input != null && input.IsFinite)
                {
                    AppendInputSteps(steps, input, v);
                    if (input.Num.IsZero)
                    {
                        r.Error = "0  (정확하게 표현됨)";
                        steps.AppendLine("■ 0 은 지수 비트와 가수 비트를 모두 0 으로 하여 저장합니다.");
                        if (v.Negative) steps.AppendLine("   부호 비트만 1 인 값은 −0 입니다. (−0 == +0 으로 비교됨)");
                    }
                    else
                    {
                        r.Error = "너무 작아서 0 이 됨 (언더플로)  " + ErrorText(v, input);
                        steps.AppendLine("■ 입력값이 가장 작은 비정규화 수의 절반보다 작아서 0 으로 반올림되었습니다. (언더플로)");
                    }
                }
                else
                {
                    r.Error = "—";
                    steps.AppendLine("■ 지수 비트와 가수 비트가 모두 0 이면 값은 0 입니다.");
                }
                r.Steps = steps.ToString();
                return r;
            }

            // ---------------- 유한한 0 이 아닌 값 ----------------
            BigInteger num, den;
            v.GetExactRational(out num, out den);

            // 가장 짧은 10진수 표현 (예: 0.1f → "0.1")
            ParsedNumber shortest;
            string err;
            NumberText.TryParseNumber(v.ShortestDecimal(), out shortest, out err);
            int sexp;
            bool sExact;
            string smant = NumberText.ToScientific(shortest.Num, shortest.Den, f.MaxSignificantDigits, out sexp, out sExact);
            bool ordinary = sexp >= SmallExponent && sexp < LargeExponent;

            bool fixedExact;
            string shortestFixed = NumberText.ToFixed(shortest.Num, shortest.Den, 400, out fixedExact);

            if (ordinary)
            {
                r.Decimal = signChar + shortestFixed;
                r.DecimalForInput = signAscii + shortestFixed;
            }
            else
            {
                r.Decimal = signChar + NumberText.TimesTen(smant, sexp) + "     (" + signAscii + NumberText.ENotation(smant, sexp) + ")";
                r.DecimalForInput = signAscii + NumberText.ENotation(smant, sexp);
            }
            r.Scientific = signChar + NumberText.TimesTen(smant, sexp) + "     (" + signAscii + NumberText.ENotation(smant, sexp) + ")";

            // 실제 저장된 정확한 값
            bool exact;
            string exactFixed = NumberText.ToFixed(num, den, 2000, out exact);
            if (ordinary)
            {
                r.Exact = signChar + exactFixed;
            }
            else
            {
                int eexp;
                bool eex;
                string emant = NumberText.ToScientific(num, den, 30, out eexp, out eex);
                int fracDigits = exactFixed.Contains(".") ? exactFixed.Length - exactFixed.IndexOf('.') - 1 : 0;
                r.Exact = signChar + NumberText.TimesTen(emant, eexp) + (eex ? "" : "…") +
                          (fracDigits > 0
                              ? string.Format("   (유효숫자 30자리까지 표시, 정확한 값은 소수점 아래 {0}자리)", fracDigits)
                              : string.Format("   (정확한 값은 {0}자리 정수)", exactFixed.Length));
            }

            // 분수
            int denPow = NumberText.BitLength(den) - 1; // den = 2^denPow
            if (den.IsOne)
            {
                r.FractionForm = ordinary
                    ? signChar + I(num) + "  (정수)"
                    : signChar + I(v.Significand) + " × 2^" + I(v.Exponent2) + "  (정수)";
                if (!ordinary)
                {
                    // Significand 를 홀수가 될 때까지 줄여 간단하게
                    BigInteger m = v.Significand;
                    int k = v.Exponent2;
                    while (!m.IsZero && m.IsEven) { m >>= 1; k++; }
                    r.FractionForm = signChar + I(m) + " × 2^" + I(k) + "  (정수)";
                }
            }
            else if (ordinary)
            {
                var sb = new StringBuilder();
                sb.Append(signChar).Append(I(num)).Append(" / ").Append(I(den));
                sb.Append("   ( = ").Append(signChar).Append(I(num)).Append(" / 2^").Append(I(denPow)).Append(" )");
                if (num > den)
                {
                    BigInteger rem;
                    BigInteger whole = BigInteger.DivRem(num, den, out rem);
                    sb.Append("   = ").Append(signChar).Append(I(whole)).Append(" 와 ").Append(I(rem)).Append("/").Append(I(den));
                }
                r.FractionForm = sb.ToString();
            }
            else
            {
                r.FractionForm = signChar + I(num) + " / 2^" + I(denPow) + "     (분모가 매우 커서 2의 거듭제곱으로 표시)";
            }

            r.Error = input != null && input.IsFinite ? ErrorText(v, input) : "— (2진수에서 변환한 값은 그 자체가 정확한 값입니다)";

            // ---------------- 변환 과정 ----------------
            if (input != null && input.IsFinite)
            {
                AppendInputSteps(steps, input, v);
                AppendEncodeSteps(steps, v);
                steps.AppendLine();
            }
            AppendDecodeSteps(steps, v, r.Decimal);
            r.Steps = steps.ToString();
            return r;
        }

        static string ErrorText(FloatValue v, ParsedNumber input)
        {
            // 저장된 값 − 입력값  (부호 포함)
            BigInteger sn, sd;
            if (v.Kind == FloatKind.Zero) { sn = BigInteger.Zero; sd = BigInteger.One; }
            else v.GetExactRational(out sn, out sd);
            if (v.Negative) sn = -sn;
            BigInteger inNum = input.Negative ? -input.Num : input.Num;

            BigInteger diffNum = sn * input.Den - inNum * sd;
            BigInteger diffDen = sd * input.Den;
            if (diffNum.IsZero) return "0  (입력값을 정확하게 표현함)";

            int e;
            bool ex;
            string m = NumberText.ToScientific(BigInteger.Abs(diffNum), diffDen, 6, out e, out ex);
            string text = (diffNum.Sign < 0 ? "−" : "+") + NumberText.TimesTen(m, e) + "   (저장된 값 − 입력값)";
            if (!input.Num.IsZero)
            {
                int re;
                string rm = NumberText.ToScientific(BigInteger.Abs(diffNum) * input.Den, diffDen * input.Num, 3, out re, out ex);
                text += ",   상대오차 " + NumberText.TimesTen(rm, re);
            }
            return text;
        }

        /// <summary>① 입력값을 2진수로 바꾸는 과정 (정수부 / 소수부 ×2 반복)</summary>
        static void AppendInputSteps(StringBuilder sb, ParsedNumber input, FloatValue v)
        {
            sb.AppendLine("■ [10진수 → 2진수]");
            bool fe;
            string inputText = NumberText.ToFixed(input.Num, input.Den, 30, out fe) + (fe ? "" : "…");
            if (NumberText.DigitCount(input.Num / input.Den) > 40)
            {
                int ie;
                bool ix;
                inputText = NumberText.TimesTen(NumberText.ToScientific(input.Num, input.Den, 20, out ie, out ix), ie);
            }
            sb.AppendLine(string.Format("① 부호: {0}{1}  →  S = {2}", input.Negative ? "−" : "+", inputText, input.Negative ? 1 : 0));

            BigInteger rem;
            BigInteger intPart = BigInteger.DivRem(input.Num, input.Den, out rem);
            string intBin = NumberText.ToBinary(intPart);
            sb.AppendLine("② 절댓값을 2진수로 바꿉니다.");
            if (intBin.Length <= 64)
                sb.AppendLine(string.Format("   정수부 {0} → {1}(2)", I(intPart), intBin));
            else
                sb.AppendLine(string.Format("   정수부 → {0}…(2)   ({1}비트 정수)", intBin.Substring(0, 24), intBin.Length));

            string fracBin = "";
            if (!rem.IsZero)
            {
                sb.AppendLine("   소수부 → 2를 곱해서 나오는 정수부(0 또는 1)를 차례로 적습니다.");
                BigInteger frac = rem;
                var bits = new StringBuilder();
                const int maxLines = 12;
                int line = 0;
                while (!frac.IsZero && line < maxLines)
                {
                    bool e1;
                    string before = "0" + NumberText.ToFixed(frac, input.Den, 12, out e1).Substring(1) + (e1 ? "" : "…");
                    BigInteger doubled = frac * 2;
                    int bit = doubled >= input.Den ? 1 : 0;
                    BigInteger after = doubled - (bit == 1 ? input.Den : BigInteger.Zero);
                    bool e2;
                    string doubledText = NumberText.ToFixed(doubled, input.Den, 12, out e2) + (e2 ? "" : "…");
                    sb.AppendLine(string.Format("      {0,-16} × 2 = {1,-16} → {2}", before, doubledText, bit));
                    bits.Append(bit);
                    frac = after;
                    line++;
                }
                if (!frac.IsZero)
                {
                    sb.AppendLine("      …  (끝나지 않고 계속됩니다 → 정확히 표현할 수 없어 반올림)");
                    fracBin = bits + "…";
                }
                else
                {
                    fracBin = bits.ToString();
                }
                if (intBin.Length <= 64)
                    sb.AppendLine(string.Format("   ∴ {0} = {1}.{2}(2)", inputText, intBin, fracBin));
            }
        }

        /// <summary>② 정규화 → 지수/가수 비트 만들기</summary>
        static void AppendEncodeSteps(StringBuilder sb, FloatValue v)
        {
            FloatFormat f = v.Format;
            if (v.Kind == FloatKind.Normal)
            {
                sb.AppendLine(string.Format("③ 정규화: 1.xxx × 2^지수 꼴로 소수점을 옮깁니다.   →   {0}(2) × 2^{1}",
                    v.SignificandBinaryText, v.UnbiasedExponent));
                if (v.Rounded)
                    sb.AppendLine(string.Format("   (가수는 소수점 아래 {0}비트까지만 저장되므로 가장 가까운 값으로 반올림됩니다)", f.FractionBits));
                sb.AppendLine(string.Format("④ 지수: {0} + {1}(bias) = {2}  →  {3}(2)",
                    v.UnbiasedExponent, f.Bias, v.ExponentField, v.ExponentBitsText));
                sb.AppendLine(string.Format("⑤ 가수: 맨 앞의 1 (숨은 비트)은 저장하지 않고, 소수점 아래 {0}비트를 저장", f.FractionBits));
                sb.AppendLine("         " + NumberText.GroupFromLeft(v.FractionBitsText, 4));
            }
            else if (v.Kind == FloatKind.Subnormal)
            {
                sb.AppendLine(string.Format("③ 값이 너무 작아 정규화하면 지수가 {0} 보다 작아집니다.", f.MinExponent));
                sb.AppendLine(string.Format("   → 비정규화 수: {0}(2) × 2^{1}  (숨은 비트 0)", v.SignificandBinaryText, f.MinExponent));
                sb.AppendLine("④ 지수: 비정규화 수는 지수 비트를 모두 0 으로 저장  →  " + v.ExponentBitsText);
                sb.AppendLine("⑤ 가수: " + NumberText.GroupFromLeft(v.FractionBitsText, 4));
            }
            sb.AppendLine(string.Format("⑥ 결과: {0} | {1} | {2}  =  {3}",
                v.SignBitText, v.ExponentBitsText, v.FractionBitsText, v.HexText));
        }

        /// <summary>비트 → 10진수 값 계산 과정</summary>
        static void AppendDecodeSteps(StringBuilder sb, FloatValue v, string decimalText)
        {
            FloatFormat f = v.Format;
            sb.AppendLine("■ [2진수 → 10진수]");
            sb.AppendLine(string.Format("① 부호 S = {0}  →  {1}", v.SignBitText, v.Negative ? "음수 (−1)" : "양수 (+1)"));
            if (v.Kind == FloatKind.Normal)
            {
                sb.AppendLine(string.Format("② 지수 E = {0}(2) = {1}  →  실제 지수 = {1} − {2} = {3}",
                    v.ExponentBitsText, v.ExponentField, f.Bias, v.UnbiasedExponent));
            }
            else
            {
                sb.AppendLine(string.Format("② 지수 E = {0}(2) = 0  →  비정규화 수, 실제 지수 = 1 − {1} = {2}",
                    v.ExponentBitsText, f.Bias, f.MinExponent));
            }

            BigInteger sigDen = BigInteger.One << f.FractionBits;
            bool ex;
            string sigDec = NumberText.ToFixed(v.Significand, sigDen, 80, out ex);
            sb.AppendLine(string.Format("③ 가수 = {0}(2) {1}= {2}",
                v.SignificandBinaryText, v.Kind == FloatKind.Normal ? "(앞의 1. 은 숨은 비트) " : "", sigDec));

            int e = v.UnbiasedExponent;
            string pow = "2^" + I(e);
            if (Math.Abs(e) <= 20)
            {
                bool pe;
                string powValue = e >= 0
                    ? I(BigInteger.One << e)
                    : NumberText.ToFixed(BigInteger.One, BigInteger.One << -e, 40, out pe);
                pow += " (= " + powValue + ")";
            }
            sb.AppendLine(string.Format("④ 값 = {0} × {1} × {2}", v.Negative ? "(−1)" : "(+1)", sigDec, pow));
            sb.AppendLine("       = " + decimalText);
        }
    }
}
