using System;
using System.Globalization;
using System.Numerics;
using System.Text;

namespace BinaryCalculator.Core
{
    public enum FloatKind
    {
        Zero,
        Subnormal,
        Normal,
        Infinity,
        NaN
    }

    /// <summary>IEEE 754 이진 부동소수점 형식 (단정밀도 / 배정밀도)</summary>
    public sealed class FloatFormat
    {
        public static readonly FloatFormat Single = new FloatFormat("단정밀도 (32비트, float)", 32, 8, 23, 9);
        public static readonly FloatFormat Double = new FloatFormat("배정밀도 (64비트, double)", 64, 11, 52, 17);

        public readonly string Name;
        public readonly int TotalBits;
        public readonly int ExponentBits;
        public readonly int FractionBits;
        /// <summary>왕복 변환(round-trip)에 필요한 최대 유효숫자 수</summary>
        public readonly int MaxSignificantDigits;

        FloatFormat(string name, int total, int exp, int frac, int sig)
        {
            Name = name;
            TotalBits = total;
            ExponentBits = exp;
            FractionBits = frac;
            MaxSignificantDigits = sig;
        }

        public int Bias { get { return (1 << (ExponentBits - 1)) - 1; } }
        public int MaxExponentField { get { return (1 << ExponentBits) - 1; } }
        /// <summary>정규화 수의 최소 지수 (1 − bias)</summary>
        public int MinExponent { get { return 1 - Bias; } }
        public int MaxExponent { get { return Bias; } }
    }

    /// <summary>IEEE 754 값 하나를 비트 단위로 분해한 결과</summary>
    public sealed class FloatValue
    {
        public FloatFormat Format;
        public ulong Bits;
        public bool Negative;
        public int ExponentField;
        public ulong Fraction;
        public FloatKind Kind;

        /// <summary>|값| = Significand × 2^Exponent2  (Significand 는 숨은 비트를 포함한 정수)</summary>
        public BigInteger Significand;
        public int Exponent2;

        /// <summary>10진수 → 2진수 변환에서 반올림이 일어났는지</summary>
        public bool Rounded;
        /// <summary>범위를 넘어 무한대가 되었는지</summary>
        public bool OverflowedToInfinity;
        /// <summary>너무 작아 0 이 되었는지</summary>
        public bool UnderflowedToZero;

        public bool IsFinite { get { return Kind != FloatKind.Infinity && Kind != FloatKind.NaN; } }

        /// <summary>정규화 수는 E − bias, 비정규화 수는 1 − bias</summary>
        public int UnbiasedExponent
        {
            get { return Kind == FloatKind.Normal ? ExponentField - Format.Bias : Format.MinExponent; }
        }

        public static FloatValue FromBits(ulong bits, FloatFormat f)
        {
            if (f.TotalBits < 64) bits &= (1UL << f.TotalBits) - 1;
            var v = new FloatValue { Format = f, Bits = bits };
            v.Negative = ((bits >> (f.TotalBits - 1)) & 1UL) == 1UL;
            v.ExponentField = (int)((bits >> f.FractionBits) & (ulong)f.MaxExponentField);
            v.Fraction = bits & ((1UL << f.FractionBits) - 1);

            if (v.ExponentField == f.MaxExponentField)
            {
                v.Kind = v.Fraction == 0 ? FloatKind.Infinity : FloatKind.NaN;
            }
            else if (v.ExponentField == 0)
            {
                v.Kind = v.Fraction == 0 ? FloatKind.Zero : FloatKind.Subnormal;
                v.Significand = new BigInteger(v.Fraction);
                v.Exponent2 = f.MinExponent - f.FractionBits;
            }
            else
            {
                v.Kind = FloatKind.Normal;
                v.Significand = new BigInteger(v.Fraction) + (BigInteger.One << f.FractionBits);
                v.Exponent2 = v.ExponentField - f.Bias - f.FractionBits;
            }
            return v;
        }

        static ulong Compose(bool negative, ulong expField, ulong fraction, FloatFormat f)
        {
            ulong sign = negative ? 1UL << (f.TotalBits - 1) : 0UL;
            return sign | (expField << f.FractionBits) | fraction;
        }

        public static FloatValue Infinity(bool negative, FloatFormat f)
        {
            return FromBits(Compose(negative, (ulong)f.MaxExponentField, 0, f), f);
        }

        public static FloatValue NaN(bool negative, FloatFormat f)
        {
            // 조용한 NaN (가수의 최상위 비트 = 1)
            return FromBits(Compose(negative, (ulong)f.MaxExponentField, 1UL << (f.FractionBits - 1), f), f);
        }

        /// <summary>
        /// 정확한 유리수 num/den (num ≥ 0, den &gt; 0) 을 가장 가까운 부동소수점 값으로 변환합니다.
        /// (IEEE 754 기본 반올림: 가장 가까운 값, 정확히 중간이면 짝수 쪽)
        /// </summary>
        public static FloatValue FromRational(bool negative, BigInteger num, BigInteger den, FloatFormat f)
        {
            if (num.IsZero)
                return FromBits(Compose(negative, 0, 0, f), f);

            // 2^e ≤ num/den < 2^(e+1) 인 e 찾기
            int e = NumberText.BitLength(num) - NumberText.BitLength(den);
            if (CompareWithPow2(num, den, e) < 0) e--;

            if (e > f.MaxExponent)
            {
                FloatValue inf = Infinity(negative, f);
                inf.OverflowedToInfinity = true;
                inf.Rounded = true;
                return inf;
            }

            int scaleExp = e < f.MinExponent ? f.MinExponent : e; // 너무 작으면 비정규화 수
            int shift = f.FractionBits - scaleExp;
            BigInteger a = num, b = den;
            if (shift >= 0) a <<= shift; else b <<= -shift;

            BigInteger rem;
            BigInteger n = BigInteger.DivRem(a, b, out rem);
            int cmp = (rem * 2).CompareTo(b);
            if (cmp > 0 || (cmp == 0 && !n.IsEven)) n += 1;

            BigInteger hidden = BigInteger.One << f.FractionBits;
            if (n >= hidden << 1)
            {
                n >>= 1;
                scaleExp++;
            }
            if (scaleExp > f.MaxExponent)
            {
                FloatValue inf = Infinity(negative, f);
                inf.OverflowedToInfinity = true;
                inf.Rounded = true;
                return inf;
            }

            ulong expField, fraction;
            if (n >= hidden)
            {
                expField = (ulong)(scaleExp + f.Bias);
                fraction = (ulong)(n - hidden);
            }
            else
            {
                expField = 0;
                fraction = (ulong)n;
            }

            FloatValue v = FromBits(Compose(negative, expField, fraction, f), f);
            v.Rounded = !rem.IsZero;
            v.UnderflowedToZero = v.Kind == FloatKind.Zero;
            return v;
        }

        static int CompareWithPow2(BigInteger num, BigInteger den, int e)
        {
            return e >= 0 ? num.CompareTo(den << e) : (num << -e).CompareTo(den);
        }

        /// <summary>|값| 을 기약분수 num/den 으로 (유한한 값만)</summary>
        public void GetExactRational(out BigInteger num, out BigInteger den)
        {
            if (Exponent2 >= 0)
            {
                num = Significand << Exponent2;
                den = BigInteger.One;
            }
            else
            {
                num = Significand;
                den = BigInteger.One << -Exponent2;
            }
            BigInteger g = BigInteger.GreatestCommonDivisor(num, den);
            if (!g.IsZero && !g.IsOne)
            {
                num /= g;
                den /= g;
            }
        }

        /// <summary>
        /// 다시 같은 비트로 되돌아오는 가장 짧은 10진수 (부호 제외, E 표기 문자열).
        /// 예: 0.1f 의 실제 값은 0.100000001490116... 이지만 "1E-1" 을 돌려줍니다.
        /// 런타임의 double.Parse 에 의존하지 않도록 BigInteger 로 직접 확인합니다.
        /// </summary>
        public string ShortestDecimal()
        {
            if (!IsFinite) return Kind == FloatKind.NaN ? "NaN" : "Infinity";
            if (Kind == FloatKind.Zero) return "0";

            BigInteger num, den;
            GetExactRational(out num, out den);
            ulong absBits = Bits & ~(1UL << (Format.TotalBits - 1));
            string candidate = null;
            for (int p = 1; p <= Format.MaxSignificantDigits; p++)
            {
                int e;
                bool exact;
                candidate = NumberText.ENotation(NumberText.ToScientific(num, den, p, out e, out exact), e);
                ParsedNumber q;
                string error;
                if (NumberText.TryParseNumber(candidate, out q, out error) &&
                    FromRational(false, q.Num, q.Den, Format).Bits == absBits)
                    break;
            }
            return candidate;
        }

        // ------------------------------------------------------------------
        // 비트 문자열
        // ------------------------------------------------------------------

        public string SignBitText { get { return Negative ? "1" : "0"; } }

        public string ExponentBitsText
        {
            get { return Convert.ToString(ExponentField, 2).PadLeft(Format.ExponentBits, '0'); }
        }

        public string FractionBitsText
        {
            get
            {
                var sb = new StringBuilder();
                for (int i = Format.FractionBits - 1; i >= 0; i--)
                    sb.Append(((Fraction >> i) & 1UL) == 1UL ? '1' : '0');
                return sb.ToString();
            }
        }

        public string AllBitsText
        {
            get { return SignBitText + ExponentBitsText + FractionBitsText; }
        }

        public string HexText
        {
            get { return "0x" + Bits.ToString(Format.TotalBits == 32 ? "X8" : "X16", CultureInfo.InvariantCulture); }
        }

        /// <summary>"1.100011" 같은 (숨은 비트 포함) 가수의 2진 표현. 끝의 0 은 생략.</summary>
        public string SignificandBinaryText
        {
            get
            {
                string frac = FractionBitsText.TrimEnd('0');
                if (frac.Length == 0) frac = "0";
                return (Kind == FloatKind.Normal ? "1." : "0.") + frac;
            }
        }

        /// <summary>
        /// 2진수 → 10진수 변환.
        /// "0x" 로 시작하면 16진수, 아니면 0/1 비트열. 자리가 모자라면 앞을 0 으로 채웁니다.
        /// </summary>
        public static bool TryParseBits(string text, FloatFormat f, out ulong bits, out string note, out string error)
        {
            bits = 0;
            note = null;
            error = null;
            string s = (text ?? string.Empty).Trim()
                .Replace(" ", string.Empty)
                .Replace("_", string.Empty)
                .Replace("|", string.Empty)
                .ToLowerInvariant();
            if (s.Length == 0)
            {
                error = "2진수(0과 1) 또는 0x 로 시작하는 16진수를 입력하세요.";
                return false;
            }

            if (s.StartsWith("0x"))
            {
                s = s.Substring(2);
                int maxDigits = f.TotalBits / 4;
                foreach (char c in s)
                {
                    if (!Uri.IsHexDigit(c))
                    {
                        error = "16진수는 0~9, A~F 만 입력할 수 있습니다.";
                        return false;
                    }
                }
                if (s.Length == 0 || s.Length > maxDigits)
                {
                    error = string.Format("{0}비트 형식의 16진수는 최대 {1}자리입니다.", f.TotalBits, maxDigits);
                    return false;
                }
                bits = Convert.ToUInt64(s, 16);
                if (s.Length < maxDigits) note = string.Format("16진수가 {0}자리보다 짧아서 앞쪽을 0으로 채웠습니다.", maxDigits);
                return true;
            }

            foreach (char c in s)
            {
                if (c != '0' && c != '1')
                {
                    error = "2진수는 0과 1만 입력할 수 있습니다. (16진수는 0x 를 앞에 붙이세요)";
                    return false;
                }
            }
            if (s.Length > f.TotalBits)
            {
                error = string.Format("{0}비트 형식인데 {1}개의 비트를 입력했습니다.", f.TotalBits, s.Length);
                return false;
            }
            bits = Convert.ToUInt64(s, 2);
            if (s.Length < f.TotalBits)
                note = string.Format("입력한 비트가 {0}개라서 앞쪽을 0으로 채워 {1}비트로 만들었습니다.", s.Length, f.TotalBits);
            return true;
        }
    }
}
