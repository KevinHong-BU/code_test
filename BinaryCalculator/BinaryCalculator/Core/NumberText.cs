using System;
using System.Globalization;
using System.Numerics;
using System.Text;
using System.Text.RegularExpressions;

namespace BinaryCalculator.Core
{
    /// <summary>
    /// 사용자가 입력한 10진수(또는 분수)를 정확한 유리수로 표현한 값.
    /// |값| = Num / Den  (Den &gt; 0)
    /// </summary>
    public sealed class ParsedNumber
    {
        public bool Negative;
        public bool IsInfinity;
        public bool IsNaN;
        public BigInteger Num = BigInteger.Zero;
        public BigInteger Den = BigInteger.One;

        public bool IsFinite { get { return !IsInfinity && !IsNaN; } }
    }

    /// <summary>
    /// 정확한(무한 정밀도) 10진수 파싱과 표시를 위한 도우미.
    /// 모든 계산은 BigInteger 로 하므로 반올림 오차가 생기지 않습니다.
    /// </summary>
    public static class NumberText
    {
        static readonly Regex DecimalRegex = new Regex(
            @"^([+-])?(\d*)(?:\.(\d*))?(?:[eE]([+-]?\d+))?$",
            RegexOptions.CultureInvariant);

        public static BigInteger Pow10(int n)
        {
            return BigInteger.Pow(10, n);
        }

        public static int DigitCount(BigInteger x)
        {
            if (x.Sign < 0) x = -x;
            return x.IsZero ? 1 : x.ToString(CultureInfo.InvariantCulture).Length;
        }

        public static int BitLength(BigInteger x)
        {
            if (x.Sign < 0) x = -x;
            if (x.IsZero) return 0;
            byte[] bytes = x.ToByteArray();
            int len = bytes.Length;
            while (len > 1 && bytes[len - 1] == 0) len--;
            int bits = (len - 1) * 8;
            int top = bytes[len - 1];
            while (top > 0) { bits++; top >>= 1; }
            return bits;
        }

        /// <summary>10진수 문자열(예: 12.375, -0.1, 1.5e-3), 분수(1/3), inf, nan 을 해석합니다.</summary>
        public static bool TryParseNumber(string text, out ParsedNumber result, out string error)
        {
            result = null;
            error = null;
            string s = (text ?? string.Empty).Trim()
                .Replace(" ", string.Empty)
                .Replace("_", string.Empty)
                .Replace(",", string.Empty)
                .Replace('−', '-');
            if (s.Length == 0)
            {
                error = "10진수 값을 입력하세요.";
                return false;
            }

            string lower = s.ToLowerInvariant();
            bool neg = false;
            string body = lower;
            if (body.StartsWith("+") || body.StartsWith("-"))
            {
                neg = body[0] == '-';
                body = body.Substring(1);
            }
            if (body == "inf" || body == "infinity" || body == "∞")
            {
                result = new ParsedNumber { Negative = neg, IsInfinity = true };
                return true;
            }
            if (body == "nan")
            {
                result = new ParsedNumber { Negative = neg, IsNaN = true };
                return true;
            }

            bool n1, n2;
            BigInteger num1, den1, num2, den2;
            int slash = s.IndexOf('/');
            if (slash >= 0)
            {
                if (!TryParseDecimal(s.Substring(0, slash), out n1, out num1, out den1, out error)) return false;
                if (!TryParseDecimal(s.Substring(slash + 1), out n2, out num2, out den2, out error)) return false;
                if (num2.IsZero)
                {
                    error = "분모가 0 일 수 없습니다.";
                    return false;
                }
                result = new ParsedNumber { Negative = n1 ^ n2, Num = num1 * den2, Den = den1 * num2 };
            }
            else
            {
                if (!TryParseDecimal(s, out n1, out num1, out den1, out error)) return false;
                result = new ParsedNumber { Negative = n1, Num = num1, Den = den1 };
            }

            BigInteger g = BigInteger.GreatestCommonDivisor(result.Num, result.Den);
            if (!g.IsZero && !g.IsOne)
            {
                result.Num /= g;
                result.Den /= g;
            }
            return true;
        }

        public static bool TryParseDecimal(string s, out bool negative, out BigInteger num, out BigInteger den, out string error)
        {
            negative = false;
            num = BigInteger.Zero;
            den = BigInteger.One;
            error = null;

            Match m = DecimalRegex.Match(s ?? string.Empty);
            string intPart = m.Success ? m.Groups[2].Value : string.Empty;
            string fracPart = m.Success ? m.Groups[3].Value : string.Empty;
            if (!m.Success || intPart.Length + fracPart.Length == 0)
            {
                error = "올바른 숫자 형식이 아닙니다.  예) 12.375   -0.1   1.5e-3   1/3   inf   nan";
                return false;
            }

            int exp = 0;
            if (m.Groups[4].Success &&
                (!int.TryParse(m.Groups[4].Value, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out exp)
                 || Math.Abs(exp) > 5000))
            {
                error = "지수(e 뒤의 수)가 너무 큽니다. (-5000 ~ 5000)";
                return false;
            }

            negative = m.Groups[1].Value == "-";
            string digits = intPart + fracPart;
            num = BigInteger.Parse(digits.Length == 0 ? "0" : digits, CultureInfo.InvariantCulture);
            int scale = fracPart.Length - exp;
            if (scale >= 0)
            {
                den = Pow10(scale);
            }
            else
            {
                num *= Pow10(-scale);
                den = BigInteger.One;
            }
            return true;
        }

        /// <summary>
        /// num/den (num ≥ 0) 을 고정 소수점 10진수 문자열로 만듭니다.
        /// 소수점 아래 maxFracDigits 자리 안에 끝나지 않으면 exact = false (잘라냄).
        /// </summary>
        public static string ToFixed(BigInteger num, BigInteger den, int maxFracDigits, out bool exact)
        {
            BigInteger rem;
            BigInteger intPart = BigInteger.DivRem(num, den, out rem);
            var sb = new StringBuilder(intPart.ToString(CultureInfo.InvariantCulture));
            if (rem.IsZero)
            {
                exact = true;
                return sb.ToString();
            }
            sb.Append('.');
            int count = 0;
            while (!rem.IsZero && count < maxFracDigits)
            {
                rem *= 10;
                BigInteger digit = BigInteger.DivRem(rem, den, out rem);
                sb.Append((char)('0' + (int)digit));
                count++;
            }
            exact = rem.IsZero;
            return sb.ToString();
        }

        /// <summary>
        /// num/den (num &gt; 0) 을 유효숫자 sig 자리의 지수 형태로 만듭니다.
        /// 반환값은 가수 문자열(예: "1.2375"), exp10 은 10의 지수.
        /// </summary>
        public static string ToScientific(BigInteger num, BigInteger den, int sig, out int exp10, out bool exact)
        {
            int e = DigitCount(num) - DigitCount(den);
            BigInteger a, b;
            ScaleByPow10(num, den, e, out a, out b);
            if (a < b)
            {
                e--;
                ScaleByPow10(num, den, e, out a, out b);
            }

            BigInteger p = Pow10(sig - 1);
            BigInteger rem;
            BigInteger n = BigInteger.DivRem(a * p, b, out rem);
            exact = rem.IsZero;
            if (rem * 2 >= b) n += 1;
            if (n >= p * 10)
            {
                n /= 10;
                e++;
            }

            string d = n.ToString(CultureInfo.InvariantCulture);
            string mant = d.Substring(0, 1);
            string rest = d.Substring(1).TrimEnd('0');
            if (rest.Length > 0) mant += "." + rest;
            exp10 = e;
            return mant;
        }

        static void ScaleByPow10(BigInteger num, BigInteger den, int e, out BigInteger a, out BigInteger b)
        {
            if (e >= 0)
            {
                a = num;
                b = den * Pow10(e);
            }
            else
            {
                a = num * Pow10(-e);
                b = den;
            }
        }

        /// <summary>"1.2375 × 10^1" 형태</summary>
        public static string TimesTen(string mant, int exp10)
        {
            return mant + " × 10^" + exp10.ToString(CultureInfo.InvariantCulture);
        }

        /// <summary>"1.2375E+1" 형태 (프로그래밍 언어/계산기의 E 표기)</summary>
        public static string ENotation(string mant, int exp10)
        {
            return mant + "E" + (exp10 >= 0 ? "+" : "") + exp10.ToString(CultureInfo.InvariantCulture);
        }

        public static string ToBinary(BigInteger x)
        {
            if (x.Sign < 0) x = -x;
            if (x.IsZero) return "0";
            var sb = new StringBuilder();
            while (!x.IsZero)
            {
                sb.Insert(0, x.IsEven ? '0' : '1');
                x >>= 1;
            }
            return sb.ToString();
        }

        /// <summary>오른쪽부터 group 자리마다 공백을 넣습니다. (예: 10000010 → 1000 0010)</summary>
        public static string GroupFromRight(string s, int group)
        {
            if (s.Length <= group) return s;
            var sb = new StringBuilder();
            int first = s.Length % group;
            if (first > 0) sb.Append(s, 0, first);
            for (int i = first; i < s.Length; i += group)
            {
                if (sb.Length > 0) sb.Append(' ');
                sb.Append(s, i, group);
            }
            return sb.ToString();
        }

        /// <summary>왼쪽부터 group 자리마다 공백을 넣습니다. (가수 비트용)</summary>
        public static string GroupFromLeft(string s, int group)
        {
            var sb = new StringBuilder();
            for (int i = 0; i < s.Length; i += group)
            {
                if (i > 0) sb.Append(' ');
                sb.Append(s, i, Math.Min(group, s.Length - i));
            }
            return sb.ToString();
        }
    }
}
