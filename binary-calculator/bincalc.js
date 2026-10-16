/*
 * 2진 데이터 계산·변환 핵심 로직.
 * 모든 정수 연산은 BigInt로 처리하며, 지정한 비트 폭(bits) 안에서
 * 2의 보수(signed) 또는 부호 없는(unsigned) 정수로 해석합니다.
 */
(function (root) {
  'use strict';

  const BASES = { 2: 'bin', 8: 'oct', 10: 'dec', 16: 'hex' };
  const DIGITS = {
    2: /^[01]+$/,
    8: /^[0-7]+$/,
    10: /^[0-9]+$/,
    16: /^[0-9a-f]+$/,
  };
  const PREFIX = { '0b': 2, '0o': 8, '0x': 16 };

  function mask(bits) {
    return (1n << BigInt(bits)) - 1n;
  }

  function range(bits, signed) {
    const b = BigInt(bits);
    return signed
      ? { min: -(1n << (b - 1n)), max: (1n << (b - 1n)) - 1n }
      : { min: 0n, max: (1n << b) - 1n };
  }

  // 문자열을 BigInt로 파싱합니다. 부호, 0b/0o/0x 접두사, 공백과 '_' 구분자를 허용합니다.
  function parseValue(input, base) {
    let s = String(input).trim().toLowerCase().replace(/[\s_]/g, '');
    if (s === '') throw new Error('값을 입력하세요.');
    let negative = false;
    if (s[0] === '-' || s[0] === '+') {
      negative = s[0] === '-';
      s = s.slice(1);
    }
    const prefixBase = PREFIX[s.slice(0, 2)];
    if (prefixBase) {
      if (base !== undefined && base !== prefixBase && base !== 10) {
        throw new Error(`접두사 ${s.slice(0, 2)}는 ${base}진수 입력에 쓸 수 없습니다.`);
      }
      base = prefixBase;
      s = s.slice(2);
    }
    base = base || 10;
    if (!DIGITS[base]) throw new Error(`지원하지 않는 진법입니다: ${base}`);
    if (!DIGITS[base].test(s)) {
      throw new Error(`${base}진수에 쓸 수 없는 문자가 있습니다.`);
    }
    let v = 0n;
    const B = BigInt(base);
    for (const ch of s) v = v * B + BigInt(parseInt(ch, 16));
    return negative ? -v : v;
  }

  // 임의의 정수를 bits 폭의 비트 패턴(0 ~ 2^bits-1)으로 자릅니다.
  function toUnsigned(value, bits) {
    return BigInt.asUintN(bits, value);
  }

  // 비트 패턴을 signed/unsigned 정수로 해석합니다.
  function interpret(value, bits, signed) {
    return signed ? BigInt.asIntN(bits, value) : BigInt.asUintN(bits, value);
  }

  // 값을 bits 폭에 맞추고, 원래 값이 표현 범위를 벗어났는지 알려 줍니다.
  function fit(value, bits, signed) {
    const { min, max } = range(bits, signed);
    return { value: interpret(value, bits, signed), overflow: value < min || value > max };
  }

  function group(str, size, sep) {
    const out = [];
    for (let i = str.length; i > 0; i -= size) out.unshift(str.slice(Math.max(0, i - size), i));
    return out.join(sep);
  }

  // 값을 진법별 문자열로 만듭니다. 2/8/16진수는 비트 패턴(2의 보수)을, 10진수는 해석된 값을 보여 줍니다.
  function format(value, base, bits, signed, opts) {
    opts = opts || {};
    if (base === 10) {
      const s = interpret(value, bits, signed).toString(10);
      if (!opts.group) return s;
      const neg = s[0] === '-';
      return (neg ? '-' : '') + group(neg ? s.slice(1) : s, 3, ',');
    }
    const u = toUnsigned(value, bits);
    let s = u.toString(base).toUpperCase();
    if (base === 2) s = s.padStart(bits, '0');
    if (base === 16) s = s.padStart(Math.ceil(bits / 4), '0');
    if (base === 8) s = s.padStart(Math.ceil(bits / 3), '0');
    if (opts.group) s = group(s, base === 8 ? 3 : 4, ' ');
    return s;
  }

  function popcount(value, bits) {
    let u = toUnsigned(value, bits);
    let n = 0;
    while (u) {
      n += Number(u & 1n);
      u >>= 1n;
    }
    return n;
  }

  // 각 비트를 LSB(0번)부터 담은 배열.
  function bitArray(value, bits) {
    const u = toUnsigned(value, bits);
    const out = [];
    for (let i = 0; i < bits; i++) out.push(Number((u >> BigInt(i)) & 1n));
    return out;
  }

  function toggleBit(value, bits, signed, index) {
    return interpret(toUnsigned(value, bits) ^ (1n << BigInt(index)), bits, signed);
  }

  const OPS = {
    add: { label: 'A + B', symbol: '+', arity: 2 },
    sub: { label: 'A − B', symbol: '−', arity: 2 },
    mul: { label: 'A × B', symbol: '×', arity: 2 },
    div: { label: 'A ÷ B', symbol: '÷', arity: 2 },
    mod: { label: 'A mod B', symbol: 'mod', arity: 2 },
    and: { label: 'A AND B', symbol: 'AND', arity: 2 },
    or: { label: 'A OR B', symbol: 'OR', arity: 2 },
    xor: { label: 'A XOR B', symbol: 'XOR', arity: 2 },
    nand: { label: 'A NAND B', symbol: 'NAND', arity: 2 },
    nor: { label: 'A NOR B', symbol: 'NOR', arity: 2 },
    not: { label: 'NOT A', symbol: 'NOT', arity: 1 },
    neg: { label: '−A (2의 보수)', symbol: 'NEG', arity: 1 },
    shl: { label: 'A << B', symbol: '<<', arity: 2 },
    shr: { label: 'A >> B (산술)', symbol: '>>', arity: 2 },
    lsr: { label: 'A >>> B (논리)', symbol: '>>>', arity: 2 },
    rol: { label: 'A ROL B', symbol: 'ROL', arity: 2 },
    ror: { label: 'A ROR B', symbol: 'ROR', arity: 2 },
  };

  function shiftCount(b) {
    if (b < 0n) throw new Error('시프트/회전 횟수는 0 이상이어야 합니다.');
    return b;
  }

  // a, b는 이미 bits 폭으로 해석된 값이어야 합니다.
  // 반환: { value, overflow } (value는 bits 폭으로 해석된 결과)
  function calculate(op, a, b, bits, signed) {
    const B = BigInt(bits);
    const ua = toUnsigned(a, bits);
    const ub = toUnsigned(b, bits);
    const m = mask(bits);
    switch (op) {
      case 'add': return fit(a + b, bits, signed);
      case 'sub': return fit(a - b, bits, signed);
      case 'mul': return fit(a * b, bits, signed);
      case 'div':
        if (b === 0n) throw new Error('0으로 나눌 수 없습니다.');
        return fit(a / b, bits, signed); // 0 방향으로 버림
      case 'mod':
        if (b === 0n) throw new Error('0으로 나눌 수 없습니다.');
        return fit(a % b, bits, signed); // 나머지 부호는 A를 따름
      case 'and': return fit(interpret(ua & ub, bits, signed), bits, signed);
      case 'or': return fit(interpret(ua | ub, bits, signed), bits, signed);
      case 'xor': return fit(interpret(ua ^ ub, bits, signed), bits, signed);
      case 'nand': return fit(interpret(~(ua & ub) & m, bits, signed), bits, signed);
      case 'nor': return fit(interpret(~(ua | ub) & m, bits, signed), bits, signed);
      case 'not': return fit(interpret(~ua & m, bits, signed), bits, signed);
      case 'neg': return fit(-a, bits, signed);
      case 'shl': {
        const n = shiftCount(b);
        const r = n >= B ? 0n : (ua << n) & m;
        // 밀려난 비트가 있거나 부호가 바뀌면 오버플로
        const exact = a * (1n << n);
        return { value: interpret(r, bits, signed), overflow: fit(exact, bits, signed).overflow };
      }
      case 'shr': {
        const n = shiftCount(b);
        const sa = BigInt.asIntN(bits, ua);
        const r = signed ? sa >> (n >= B ? B - 1n : n) : n >= B ? 0n : ua >> n;
        return { value: interpret(r, bits, signed), overflow: false };
      }
      case 'lsr': {
        const n = shiftCount(b);
        const r = n >= B ? 0n : ua >> n;
        return { value: interpret(r, bits, signed), overflow: false };
      }
      case 'rol':
      case 'ror': {
        let n = shiftCount(b) % B;
        if (op === 'ror') n = (B - n) % B;
        const r = ((ua << n) | (ua >> (B - n))) & m;
        return { value: interpret(r, bits, signed), overflow: false };
      }
      default:
        throw new Error(`알 수 없는 연산: ${op}`);
    }
  }

  // ---- 텍스트 ↔ 2진수 (UTF-8) ----

  function textToBytes(text) {
    return Array.from(new TextEncoder().encode(text));
  }

  function bytesToText(bytes) {
    return new TextDecoder('utf-8', { fatal: true }).decode(new Uint8Array(bytes));
  }

  function formatBytes(bytes, base) {
    const width = { 2: 8, 8: 3, 10: 0, 16: 2 }[base];
    return bytes.map((b) => b.toString(base).toUpperCase().padStart(width, '0')).join(' ');
  }

  // 공백으로 구분된 바이트 목록을 파싱합니다. 2/16진수는 구분 없이 붙여 써도 됩니다.
  function parseBytes(input, base) {
    let s = String(input).trim().replace(/,/g, ' ');
    if (s === '') return [];
    let tokens = s.split(/\s+/);
    const width = { 2: 8, 16: 2 }[base];
    if (width && tokens.length === 1 && s.length > width) {
      if (s.length % width !== 0) {
        throw new Error(`${base}진수 바이트는 ${width}자리씩 끊어져야 합니다.`);
      }
      tokens = s.match(new RegExp(`.{${width}}`, 'g'));
    }
    return tokens.map((t) => {
      const v = parseValue(t, base);
      if (v < 0n || v > 255n) throw new Error(`바이트 범위(0–255)를 벗어났습니다: ${t}`);
      return Number(v);
    });
  }

  // ---- IEEE 754 부동소수점 ----

  function floatToBits(num, bits) {
    const buf = new DataView(new ArrayBuffer(8));
    if (bits === 32) {
      buf.setFloat32(0, num);
      return BigInt(buf.getUint32(0));
    }
    buf.setFloat64(0, num);
    return buf.getBigUint64(0);
  }

  function bitsToFloat(pattern, bits) {
    const buf = new DataView(new ArrayBuffer(8));
    if (bits === 32) {
      buf.setUint32(0, Number(toUnsigned(pattern, 32)));
      return buf.getFloat32(0);
    }
    buf.setBigUint64(0, toUnsigned(pattern, 64));
    return buf.getFloat64(0);
  }

  // 부호/지수/가수 필드를 나눠 돌려줍니다.
  function floatFields(pattern, bits) {
    const [eBits, mBits] = bits === 32 ? [8, 23] : [11, 52];
    const u = toUnsigned(pattern, bits);
    const mant = u & mask(mBits);
    const exp = (u >> BigInt(mBits)) & mask(eBits);
    const sign = Number(u >> BigInt(bits - 1));
    const bias = (1 << (eBits - 1)) - 1;
    let kind = 'normal';
    if (exp === mask(eBits)) kind = mant === 0n ? 'infinity' : 'nan';
    else if (exp === 0n) kind = mant === 0n ? 'zero' : 'subnormal';
    return {
      sign,
      exponent: exp,
      mantissa: mant,
      eBits,
      mBits,
      bias,
      unbiased: kind === 'subnormal' ? 1 - bias : Number(exp) - bias,
      kind,
    };
  }

  const api = {
    BASES, OPS, mask, range, parseValue, toUnsigned, interpret, fit, format, popcount,
    bitArray, toggleBit, calculate, textToBytes, bytesToText, formatBytes, parseBytes,
    floatToBits, bitsToFloat, floatFields,
  };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.BinCalc = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
