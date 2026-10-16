using System;
using System.Globalization;
using System.Numerics;
using System.Text;

namespace BinaryCalculator.Core
{
    public enum IntBase
    {
        Binary,
        Decimal,
        Hexadecimal
    }

    public enum IntOperation
    {
        Add,
        Subtract,
        Multiply,
        Divide,
        Remainder,
        And,
        Or,
        Xor,
        NotA,
        NegateA,
        ShiftLeft,
        ShiftRightArithmetic,
        ShiftRightLogical
    }

    public sealed class IntResult
    {
        public bool Success = true;
        public string Error;

        /// <summary>결과 비트 패턴 (0 ≤ Raw &lt; 2^w)</summary>
        public BigInteger Raw;

        public bool CarryApplicable;
        public bool Carry;
        public bool Overflow;

        /// <summary>비트 수 제한이 없을 때의 수학적 결과</summary>
        public bool HasMathValue;
        public BigInteger MathValue;

        public string Steps = string.Empty;
    }

    /// <summary>
    /// w비트 2의 보수 정수 계산.
    /// 값은 항상 "비트 패턴(raw, 0 ~ 2^w-1)" 으로 보관하고, 필요할 때 부호 있는 값으로 해석합니다.
    /// </summary>
    public static class TwosComplement
    {
        public static readonly int[] SupportedWidths = { 4, 8, 16, 32, 64 };

        public static BigInteger Modulus(int w) { return BigInteger.One << w; }
        public static BigInteger Mask(int w) { return Modulus(w) - 1; }
        public static BigInteger MinValue(int w) { return -(BigInteger.One << (w - 1)); }
        public static BigInteger MaxValue(int w) { return (BigInteger.One << (w - 1)) - 1; }

        public static bool InRange(BigInteger v, int w)
        {
            return v >= MinValue(w) && v <= MaxValue(w);
        }

        public static BigInteger ToSigned(BigInteger raw, int w)
        {
            return raw > MaxValue(w) ? raw - Modulus(w) : raw;
        }

        public static BigInteger ToRaw(BigInteger value, int w)
        {
            BigInteger m = Modulus(w);
            BigInteger r = BigInteger.Remainder(value, m);
            if (r.Sign < 0) r += m;
            return r;
        }

        public static bool BitAt(BigInteger raw, int i)
        {
            return !((raw >> i) & BigInteger.One).IsZero;
        }

        public static string ToBinary(BigInteger raw, int w, bool grouped)
        {
            var sb = new StringBuilder();
            for (int i = w - 1; i >= 0; i--)
            {
                sb.Append(BitAt(raw, i) ? '1' : '0');
                if (grouped && i > 0 && i % 4 == 0) sb.Append(' ');
            }
            return sb.ToString();
        }

        public static string ToHex(BigInteger raw, int w, bool grouped)
        {
            const string digitsText = "0123456789ABCDEF";
            int digits = (w + 3) / 4;
            var sb = new StringBuilder();
            for (int i = digits - 1; i >= 0; i--)
            {
                int d = (int)((raw >> (4 * i)) & 15);
                sb.Append(digitsText[d]);
                if (grouped && i > 0 && i % 4 == 0) sb.Append(' ');
            }
            return sb.ToString();
        }

        public static string RangeText(int w)
        {
            return string.Format(CultureInfo.InvariantCulture, "{0} ~ {1}", MinValue(w), MaxValue(w));
        }

        // ------------------------------------------------------------------
        // 입력 해석
        // ------------------------------------------------------------------

        /// <summary>
        /// 2진수/16진수: 입력한 비트 패턴 그대로 사용 (자리수가 모자라면 앞을 0으로 채움).
        ///               앞에 '-' 를 붙이면 그 크기의 음수로 해석.
        /// 10진수: 부호 있는 정수 (w비트 범위 안이어야 함).
        /// </summary>
        public static bool TryParse(string text, IntBase numberBase, int w, out BigInteger raw, out string error)
        {
            raw = BigInteger.Zero;
            error = null;
            string s = (text ?? string.Empty).Trim()
                .Replace(" ", string.Empty)
                .Replace("_", string.Empty)
                .Replace(",", string.Empty)
                .Replace('−', '-');
            if (s.Length == 0)
            {
                error = "값을 입력하세요.";
                return false;
            }

            bool neg = false;
            if (s[0] == '-' || s[0] == '+')
            {
                neg = s[0] == '-';
                s = s.Substring(1);
            }

            if (numberBase == IntBase.Decimal)
            {
                if (s.Length == 0 || !AllDigits(s, 10))
                {
                    error = "10진수는 0~9 숫자(와 앞의 - 부호)만 입력할 수 있습니다.";
                    return false;
                }
                BigInteger v = BigInteger.Parse(s, CultureInfo.InvariantCulture);
                if (neg) v = -v;
                if (!InRange(v, w))
                {
                    error = string.Format("{0}비트 2의 보수 범위({1})를 벗어났습니다.", w, RangeText(w));
                    return false;
                }
                raw = ToRaw(v, w);
                return true;
            }

            int radix = numberBase == IntBase.Binary ? 2 : 16;
            string lower = s.ToLowerInvariant();
            if (radix == 2 && lower.StartsWith("0b")) s = s.Substring(2);
            if (radix == 16 && lower.StartsWith("0x")) s = s.Substring(2);
            if (s.Length == 0 || !AllDigits(s, radix))
            {
                error = radix == 2
                    ? "2진수는 0과 1만 입력할 수 있습니다."
                    : "16진수는 0~9, A~F 만 입력할 수 있습니다.";
                return false;
            }

            BigInteger mag = BigInteger.Zero;
            foreach (char c in s)
            {
                mag = mag * radix + HexValue(c);
            }

            if (neg)
            {
                if (mag > (BigInteger.One << (w - 1)))
                {
                    error = string.Format("{0}비트 2의 보수 범위({1})를 벗어났습니다.", w, RangeText(w));
                    return false;
                }
                raw = ToRaw(-mag, w);
            }
            else
            {
                if (mag > Mask(w))
                {
                    error = radix == 2
                        ? string.Format("{0}비트보다 깁니다. (최대 {0}개의 비트)", w)
                        : string.Format("{0}비트보다 깁니다. (16진수 최대 {1}자리)", w, (w + 3) / 4);
                    return false;
                }
                raw = mag;
            }
            return true;
        }

        static bool AllDigits(string s, int radix)
        {
            foreach (char c in s)
            {
                int v = HexValue(c);
                if (v < 0 || v >= radix) return false;
            }
            return true;
        }

        static int HexValue(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        // ------------------------------------------------------------------
        // 설명 문자열
        // ------------------------------------------------------------------

        public static string OperationSymbol(IntOperation op)
        {
            switch (op)
            {
                case IntOperation.Add: return "A + B";
                case IntOperation.Subtract: return "A − B";
                case IntOperation.Multiply: return "A × B";
                case IntOperation.Divide: return "A ÷ B";
                case IntOperation.Remainder: return "A % B";
                case IntOperation.And: return "A AND B";
                case IntOperation.Or: return "A OR B";
                case IntOperation.Xor: return "A XOR B";
                case IntOperation.NotA: return "NOT A";
                case IntOperation.NegateA: return "−A";
                case IntOperation.ShiftLeft: return "A << B";
                case IntOperation.ShiftRightArithmetic: return "A >> B";
                case IntOperation.ShiftRightLogical: return "A >>> B";
                default: return op.ToString();
            }
        }

        public static bool UsesB(IntOperation op)
        {
            return op != IntOperation.NotA && op != IntOperation.NegateA;
        }

        /// <summary>피연산자가 2의 보수로 어떻게 표현되는지 설명합니다.</summary>
        public static string DescribeOperand(string name, BigInteger raw, int w)
        {
            var sb = new StringBuilder();
            BigInteger v = ToSigned(raw, w);
            sb.AppendLine(string.Format("[{0}] = {1}  (10진수)   =   {2}  (2진수)   =   0x{3}  (16진수)",
                name, v, ToBinary(raw, w, true), ToHex(raw, w, false)));
            if (v.Sign >= 0)
            {
                sb.AppendLine("   · 부호 비트(맨 왼쪽) = 0  →  양수: 2진수 값을 그대로 저장합니다.");
            }
            else
            {
                BigInteger mag = -v;
                BigInteger ones = ToRaw(~mag, w);
                sb.AppendLine("   · 부호 비트(맨 왼쪽) = 1  →  음수: 2의 보수로 저장합니다.");
                sb.AppendLine(string.Format("       |{0}| = {1,-6}         : {2}", name, mag, ToBinary(ToRaw(mag, w), w, true)));
                sb.AppendLine(string.Format("       비트 반전 (1의 보수)  : {0}", ToBinary(ones, w, true)));
                sb.AppendLine(string.Format("       + 1      (2의 보수)  : {0}", ToBinary(raw, w, true)));
                sb.AppendLine(string.Format("   · 부호 없는 정수로 읽으면 {0}  ( = 2^{1} − {2} )", raw, w, mag));
            }
            return sb.ToString();
        }

        // ------------------------------------------------------------------
        // 연산
        // ------------------------------------------------------------------

        public static IntResult Compute(IntOperation op, BigInteger rawA, BigInteger rawB, int w)
        {
            var r = new IntResult();
            BigInteger a = ToSigned(rawA, w);
            BigInteger b = ToSigned(rawB, w);
            BigInteger mask = Mask(w);
            var sb = new StringBuilder();

            sb.AppendLine(string.Format("■ {0}비트 2의 보수  (표현 범위 {1})", w, RangeText(w)));
            sb.AppendLine();
            sb.Append(DescribeOperand("A", rawA, w));
            if (UsesB(op))
            {
                sb.AppendLine();
                sb.Append(DescribeOperand("B", rawB, w));
            }
            sb.AppendLine();
            sb.AppendLine("■ 계산: " + OperationSymbol(op));

            switch (op)
            {
                case IntOperation.Add:
                    {
                        sb.AppendLine("   2진수끼리 오른쪽 비트부터 더하고, 넘치는 올림(캐리)은 버립니다.");
                        sb.AppendLine();
                        bool[] carries;
                        r.Raw = RippleAdd(rawA, rawB, false, w, out carries);
                        AppendAdditionDiagram(sb, rawA, rawB, r.Raw, carries, w, "+", a, b);
                        SetArithmeticFlags(r, a + b, carries, w);
                        break;
                    }
                case IntOperation.Subtract:
                    {
                        BigInteger notB = (~rawB) & mask;
                        sb.AppendLine("   뺄셈은 더하기로 바꿉니다:  A − B = A + (−B) = A + (B의 비트 반전) + 1");
                        sb.AppendLine("   (+1 은 맨 오른쪽 비트로 들어가는 캐리(carry-in) 1 로 넣습니다)");
                        sb.AppendLine();
                        sb.AppendLine(string.Format("   B          : {0}", ToBinary(rawB, w, true)));
                        sb.AppendLine(string.Format("   ~B (반전)  : {0}", ToBinary(notB, w, true)));
                        sb.AppendLine();
                        bool[] carries;
                        r.Raw = RippleAdd(rawA, notB, true, w, out carries);
                        AppendAdditionDiagram(sb, rawA, notB, r.Raw, carries, w, "+~", a, ToSigned(notB, w));
                        SetArithmeticFlags(r, a - b, carries, w);
                        break;
                    }
                case IntOperation.Multiply:
                    {
                        BigInteger product = a * b;
                        r.HasMathValue = true;
                        r.MathValue = product;
                        r.Raw = ToRaw(product, w);
                        r.Overflow = !InRange(product, w);
                        sb.AppendLine(string.Format("   수학적 곱      : {0} × {1} = {2}", a, b, product));
                        sb.AppendLine(string.Format("   {0}비트로 쓴 곱 : {1}", 2 * w, ToBinary(ToRaw(product, 2 * w), 2 * w, true)));
                        sb.AppendLine(string.Format("   하위 {0}비트만 저장: {1}  →  {2}", w, ToBinary(r.Raw, w, true), ToSigned(r.Raw, w)));
                        if (r.Overflow)
                            sb.AppendLine("   ※ 상위 비트가 잘려 나가 값이 달라졌습니다 → 오버플로");
                        break;
                    }
                case IntOperation.Divide:
                case IntOperation.Remainder:
                    {
                        if (b.IsZero)
                        {
                            r.Success = false;
                            r.Error = "0으로 나눌 수 없습니다. (B = 0)";
                            sb.AppendLine("   " + r.Error);
                            break;
                        }
                        BigInteger rem;
                        BigInteger q = BigInteger.DivRem(a, b, out rem);
                        sb.AppendLine("   정수 나눗셈: 몫은 0 쪽으로 버림 (C, C#, Java 와 같은 방식)");
                        sb.AppendLine("               나머지의 부호는 나뉘는 수(A)의 부호를 따름");
                        sb.AppendLine(string.Format("   {0} ÷ {1} = 몫 {2}, 나머지 {3}     ( {1} × {2} + {3} = {0} )", a, b, q, rem));
                        BigInteger value = op == IntOperation.Divide ? q : rem;
                        r.HasMathValue = true;
                        r.MathValue = value;
                        r.Raw = ToRaw(value, w);
                        r.Overflow = !InRange(value, w);
                        if (r.Overflow)
                            sb.AppendLine(string.Format("   ※ {0} ÷ (−1) = {1} 은 양수 최댓값 {2} 보다 커서 오버플로", a, value, MaxValue(w)));
                        break;
                    }
                case IntOperation.And:
                case IntOperation.Or:
                case IntOperation.Xor:
                    {
                        string name = op == IntOperation.And ? "AND" : op == IntOperation.Or ? "OR " : "XOR";
                        if (op == IntOperation.And) { r.Raw = rawA & rawB; sb.AppendLine("   AND: 두 비트가 모두 1 일 때만 1"); }
                        else if (op == IntOperation.Or) { r.Raw = rawA | rawB; sb.AppendLine("   OR : 두 비트 중 하나라도 1 이면 1"); }
                        else { r.Raw = rawA ^ rawB; sb.AppendLine("   XOR: 두 비트가 서로 다를 때 1"); }
                        sb.AppendLine();
                        sb.AppendLine(string.Format("        A : {0}   ({1})", ToBinary(rawA, w, true), a));
                        sb.AppendLine(string.Format("    {0} B : {1}   ({2})", name, ToBinary(rawB, w, true), b));
                        sb.AppendLine("   " + new string('-', ToBinary(0, w, true).Length + 8));
                        sb.AppendLine(string.Format("        = : {0}   ({1})", ToBinary(r.Raw, w, true), ToSigned(r.Raw, w)));
                        break;
                    }
                case IntOperation.NotA:
                    {
                        r.Raw = (~rawA) & mask;
                        sb.AppendLine("   NOT: 모든 비트를 반전 (= 1의 보수).  2의 보수에서 NOT A = −A − 1");
                        sb.AppendLine();
                        sb.AppendLine(string.Format("       A : {0}   ({1})", ToBinary(rawA, w, true), a));
                        sb.AppendLine(string.Format("   NOT A : {0}   ({1})", ToBinary(r.Raw, w, true), ToSigned(r.Raw, w)));
                        break;
                    }
                case IntOperation.NegateA:
                    {
                        BigInteger inverted = (~rawA) & mask;
                        r.Raw = ToRaw(-a, w);
                        r.HasMathValue = true;
                        r.MathValue = -a;
                        r.Overflow = !InRange(-a, w);
                        sb.AppendLine("   2의 보수 구하기: 모든 비트를 반전한 뒤 1을 더합니다.");
                        sb.AppendLine();
                        sb.AppendLine(string.Format("   A            : {0}   ({1})", ToBinary(rawA, w, true), a));
                        sb.AppendLine(string.Format("   비트 반전    : {0}", ToBinary(inverted, w, true)));
                        sb.AppendLine(string.Format("   + 1  =  −A   : {0}   ({1})", ToBinary(r.Raw, w, true), ToSigned(r.Raw, w)));
                        if (r.Overflow)
                            sb.AppendLine(string.Format("   ※ 최솟값 {0} 의 부호를 바꾼 {1} 은 표현할 수 없음 → 오버플로", a, -a));
                        break;
                    }
                case IntOperation.ShiftLeft:
                case IntOperation.ShiftRightArithmetic:
                case IntOperation.ShiftRightLogical:
                    {
                        if (b.Sign < 0 || b > w)
                        {
                            r.Success = false;
                            r.Error = string.Format("이동할 칸 수 B 는 0 ~ {0} 사이여야 합니다.", w);
                            sb.AppendLine("   " + r.Error);
                            break;
                        }
                        int n = (int)b;
                        r.CarryApplicable = n > 0;
                        if (op == IntOperation.ShiftLeft)
                        {
                            r.Raw = (rawA << n) & mask;
                            r.Carry = n > 0 && BitAt(rawA, w - n);
                            BigInteger math = a * BigInteger.Pow(2, n);
                            r.HasMathValue = true;
                            r.MathValue = math;
                            r.Overflow = !InRange(math, w);
                            sb.AppendLine(string.Format("   왼쪽으로 {0}칸 이동, 오른쪽은 0으로 채움  (= A × 2^{0})", n));
                        }
                        else if (op == IntOperation.ShiftRightArithmetic)
                        {
                            r.Raw = ToRaw(a >> n, w);
                            r.Carry = n > 0 && BitAt(rawA, n - 1);
                            r.HasMathValue = true;
                            r.MathValue = a >> n;
                            sb.AppendLine(string.Format("   산술 시프트: 오른쪽으로 {0}칸 이동, 왼쪽은 부호 비트로 채움  (= A ÷ 2^{0}, 내림)", n));
                        }
                        else
                        {
                            r.Raw = rawA >> n;
                            r.Carry = n > 0 && BitAt(rawA, n - 1);
                            sb.AppendLine(string.Format("   논리 시프트: 오른쪽으로 {0}칸 이동, 왼쪽은 0으로 채움 (부호 없는 수처럼 동작)", n));
                        }
                        sb.AppendLine();
                        sb.AppendLine(string.Format("   A      : {0}   ({1})", ToBinary(rawA, w, true), a));
                        sb.AppendLine(string.Format("   결과   : {0}   ({1})", ToBinary(r.Raw, w, true), ToSigned(r.Raw, w)));
                        if (n > 0)
                            sb.AppendLine(string.Format("   마지막으로 밀려난 비트(캐리) = {0}", r.Carry ? 1 : 0));
                        if (r.Overflow)
                            sb.AppendLine("   ※ 부호 비트가 바뀌거나 1 이 밀려나 값이 달라졌습니다 → 오버플로");
                        break;
                    }
            }

            if (r.Success)
            {
                BigInteger result = ToSigned(r.Raw, w);
                sb.AppendLine();
                sb.AppendLine(string.Format("■ 결과 = {0}  (10진수)   =   {1}  (2진수)   =   0x{2}  (16진수)",
                    result, ToBinary(r.Raw, w, true), ToHex(r.Raw, w, false)));
                if (r.HasMathValue)
                {
                    if (r.Overflow)
                        sb.AppendLine(string.Format("   ⚠ 수학적 결과 {0} 은(는) {1}비트 범위({2})를 벗어나 {3} 이(가) 되었습니다.  (오버플로)",
                            r.MathValue, w, RangeText(w), result));
                    else
                        sb.AppendLine("   ✔ 범위 안의 값이므로 수학적 결과와 같습니다.");
                }
            }

            r.Steps = sb.ToString();
            return r;
        }

        /// <summary>리플 캐리 가산기. carries[i] = i번째 비트로 들어가는 캐리, carries[w] = 최종 캐리 출력</summary>
        static BigInteger RippleAdd(BigInteger x, BigInteger y, bool carryIn, int w, out bool[] carries)
        {
            carries = new bool[w + 1];
            carries[0] = carryIn;
            BigInteger sum = BigInteger.Zero;
            for (int i = 0; i < w; i++)
            {
                bool xi = BitAt(x, i), yi = BitAt(y, i), ci = carries[i];
                if (xi ^ yi ^ ci) sum |= BigInteger.One << i;
                carries[i + 1] = (xi && yi) || (xi && ci) || (yi && ci);
            }
            return sum;
        }

        static void SetArithmeticFlags(IntResult r, BigInteger math, bool[] carries, int w)
        {
            r.CarryApplicable = true;
            r.Carry = carries[w];
            r.HasMathValue = true;
            r.MathValue = math;
            r.Overflow = !InRange(math, w);
        }

        static void AppendAdditionDiagram(StringBuilder sb, BigInteger x, BigInteger y, BigInteger sum,
            bool[] carries, int w, string op, BigInteger xv, BigInteger yv)
        {
            // 캐리 줄: 맨 앞 칸 = 최종 캐리 출력, 나머지 = 각 비트로 들어가는 캐리
            var carryLine = new StringBuilder();
            for (int i = w - 1; i >= 0; i--)
            {
                carryLine.Append(carries[i] ? '1' : ' ');
                if (i > 0 && i % 4 == 0) carryLine.Append(' ');
            }
            string cout = carries[w] ? "1" : " ";
            string pad = op.Length == 1 ? " " : "";

            sb.AppendLine(string.Format("   carry  {0} {1}", cout, carryLine));
            sb.AppendLine(string.Format("          {0} {1}   ({2})", " ", ToBinary(x, w, true), xv));
            sb.AppendLine(string.Format("   {0}{1}     {2} {3}   ({4})", op, pad, " ", ToBinary(y, w, true), yv));
            sb.AppendLine("   " + new string('-', ToBinary(0, w, true).Length + 8));
            sb.AppendLine(string.Format("          {0} {1}   ({2})", cout, ToBinary(sum, w, true), ToSigned(sum, w)));
            sb.AppendLine();
            if (carries[w])
                sb.AppendLine(string.Format("   · 맨 왼쪽으로 넘친 캐리 1 은 {0}비트 밖이므로 버립니다.  (C = 1)", w));
            else
                sb.AppendLine("   · 맨 왼쪽으로 넘친 캐리가 없습니다.  (C = 0)");

            bool cin = carries[w - 1], coutBit = carries[w];
            sb.AppendLine(string.Format("   · 오버플로 판정: 부호 비트로 들어온 캐리({0}) XOR 나간 캐리({1}) = {2}  →  {3}",
                cin ? 1 : 0, coutBit ? 1 : 0, (cin ^ coutBit) ? 1 : 0,
                (cin ^ coutBit) ? "오버플로 발생 (V = 1)" : "오버플로 없음 (V = 0)"));
            if (cin ^ coutBit)
                sb.AppendLine(BitAt(sum, w - 1)
                    ? "     (양수 + 양수 인데 결과의 부호 비트가 1(음수)이 됨)"
                    : "     (음수 + 음수 인데 결과의 부호 비트가 0(양수)이 됨)");
        }
    }
}
