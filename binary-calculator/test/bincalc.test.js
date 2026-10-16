const test = require('node:test');
const assert = require('node:assert/strict');
const C = require('../bincalc.js');

test('parseValue: 진법, 접두사, 구분자, 부호', () => {
  assert.equal(C.parseValue('1010', 2), 10n);
  assert.equal(C.parseValue('0b1010_1010'), 170n);
  assert.equal(C.parseValue('0xFF'), 255n);
  assert.equal(C.parseValue('ff', 16), 255n);
  assert.equal(C.parseValue('0o17'), 15n);
  assert.equal(C.parseValue('-128', 10), -128n);
  assert.equal(C.parseValue('1111 0000', 2), 240n);
  assert.throws(() => C.parseValue('102', 2), /2진수/);
  assert.throws(() => C.parseValue('', 10), /입력/);
  assert.throws(() => C.parseValue('0x10', 2), /접두사/);
});

test('format: 2의 보수 비트 패턴과 그룹', () => {
  assert.equal(C.format(-1n, 2, 8, true), '11111111');
  assert.equal(C.format(-1n, 16, 16, true), 'FFFF');
  assert.equal(C.format(255n, 10, 8, true), '-1');
  assert.equal(C.format(255n, 10, 8, false), '255');
  assert.equal(C.format(5n, 2, 8, false, { group: true }), '0000 0101');
  assert.equal(C.format(1234567n, 10, 32, false, { group: true }), '1,234,567');
  assert.equal(C.format(-1234567n, 10, 32, true, { group: true }), '-1,234,567');
  assert.equal(C.format(8n, 8, 8, false), '010');
});

test('fit: 범위와 오버플로', () => {
  assert.deepEqual(C.fit(128n, 8, true), { value: -128n, overflow: true });
  assert.deepEqual(C.fit(127n, 8, true), { value: 127n, overflow: false });
  assert.deepEqual(C.fit(-1n, 8, false), { value: 255n, overflow: true });
});

test('calculate: 산술', () => {
  assert.deepEqual(C.calculate('add', 127n, 1n, 8, true), { value: -128n, overflow: true });
  assert.deepEqual(C.calculate('add', 200n, 100n, 8, false), { value: 44n, overflow: true });
  assert.deepEqual(C.calculate('sub', 0n, 1n, 8, false), { value: 255n, overflow: true });
  assert.deepEqual(C.calculate('mul', 16n, 16n, 16, false), { value: 256n, overflow: false });
  assert.deepEqual(C.calculate('div', -7n, 2n, 8, true), { value: -3n, overflow: false });
  assert.deepEqual(C.calculate('mod', -7n, 2n, 8, true), { value: -1n, overflow: false });
  assert.deepEqual(C.calculate('div', -128n, -1n, 8, true), { value: -128n, overflow: true });
  assert.throws(() => C.calculate('div', 1n, 0n, 8, true), /0으로/);
});

test('calculate: 논리', () => {
  assert.equal(C.calculate('and', 0b1100n, 0b1010n, 8, false).value, 0b1000n);
  assert.equal(C.calculate('or', 0b1100n, 0b1010n, 8, false).value, 0b1110n);
  assert.equal(C.calculate('xor', 0b1100n, 0b1010n, 8, false).value, 0b0110n);
  assert.equal(C.calculate('nand', 0b1100n, 0b1010n, 8, false).value, 0b11110111n);
  assert.equal(C.calculate('nor', 0b1100n, 0b1010n, 8, false).value, 0b11110001n);
  assert.equal(C.calculate('not', 0n, 0n, 8, true).value, -1n);
  assert.equal(C.calculate('neg', 5n, 0n, 8, false).value, 251n);
});

test('calculate: 시프트와 회전', () => {
  assert.deepEqual(C.calculate('shl', 0b01000000n, 1n, 8, true), { value: -128n, overflow: true });
  assert.deepEqual(C.calculate('shl', 3n, 2n, 8, false), { value: 12n, overflow: false });
  assert.equal(C.calculate('shl', 1n, 9n, 8, false).value, 0n);
  assert.equal(C.calculate('shr', -16n, 2n, 8, true).value, -4n);
  assert.equal(C.calculate('shr', -16n, 20n, 8, true).value, -1n);
  assert.equal(C.calculate('lsr', -16n, 2n, 8, true).value, 0b00111100n);
  assert.equal(C.calculate('rol', 0b10000001n, 1n, 8, false).value, 0b00000011n);
  assert.equal(C.calculate('ror', 0b10000001n, 1n, 8, false).value, 0b11000000n);
  assert.equal(C.calculate('rol', 0b10000001n, 9n, 8, false).value, 0b00000011n);
  assert.equal(C.calculate('ror', 0b1n, 0n, 8, false).value, 1n);
  assert.throws(() => C.calculate('shl', 1n, -1n, 8, false), /0 이상/);
});

test('비트 유틸', () => {
  assert.equal(C.popcount(-1n, 32), 32);
  assert.deepEqual(C.bitArray(5n, 4), [1, 0, 1, 0]);
  assert.equal(C.toggleBit(0n, 8, true, 7), -128n);
});

test('텍스트 ↔ 바이트 (UTF-8)', () => {
  assert.deepEqual(C.textToBytes('A가'), [0x41, 0xea, 0xb0, 0x80]);
  assert.equal(C.formatBytes([0x41], 2), '01000001');
  assert.equal(C.formatBytes([0x41, 0xea], 16), '41 EA');
  assert.deepEqual(C.parseBytes('0100000101000010', 2), [65, 66]);
  assert.deepEqual(C.parseBytes('48 69', 16), [0x48, 0x69]);
  assert.equal(C.bytesToText(C.parseBytes('EAB080', 16)), '가');
  assert.throws(() => C.parseBytes('256', 10), /범위/);
  assert.throws(() => C.parseBytes('01000001010', 2), /8자리/);
  assert.throws(() => C.bytesToText([0xff]));
});

test('IEEE 754', () => {
  assert.equal(C.floatToBits(1, 32), 0x3f800000n);
  assert.equal(C.floatToBits(-2.5, 64), 0xc004000000000000n);
  assert.equal(C.bitsToFloat(0x40490fdbn, 32), Math.fround(Math.PI));
  const f = C.floatFields(0x3f800000n, 32);
  assert.equal(f.sign, 0);
  assert.equal(f.exponent, 127n);
  assert.equal(f.unbiased, 0);
  assert.equal(f.kind, 'normal');
  assert.equal(C.floatFields(0x7fc00000n, 32).kind, 'nan');
  assert.equal(C.floatFields(0x00000001n, 32).kind, 'subnormal');
});
