#!/usr/bin/env node
/*
 * 레벨 검사기: 모든 레벨이 형식에 맞고, 금을 전부 모은 뒤 맨 윗줄까지 갈 수 있는지 확인한다.
 * 경비병은 무시하고, 파기는 "벽돌 아래로 뚫고 내려가기"로 단순화한다.
 *
 *   node lode-runner/tools/check-levels.js        # 모든 레벨 검사
 *   node lode-runner/tools/check-levels.js 3      # 3번 레벨 맵 출력 + 검사
 */
'use strict';
const path = require('path');
const fs = require('fs');
const vm = require('vm');

const COLS = 28, ROWS = 16;
const src = fs.readFileSync(path.join(__dirname, '..', 'js', 'levels.js'), 'utf8');
const sandbox = { window: {} };
vm.runInNewContext(src, sandbox);
const LEVELS = sandbox.window.LR_LEVELS;

function check(level, idx) {
  const errors = [], warnings = [];
  const m = level.map;
  if (!level.name) warnings.push('이름 없음');
  if (m.length !== ROWS) errors.push(`행 수 ${m.length} (16이어야 함)`);
  m.forEach((row, y) => {
    if (row.length !== COLS) errors.push(`${y}행 길이 ${row.length}`);
    for (const c of row) if (!'#@H-XS$0& '.includes(c)) errors.push(`${y}행 잘못된 문자 '${c}'`);
  });
  const raw = (x, y) => (m[y] || '')[x] || ' ';
  let start = null; const golds = []; let guards = 0;
  for (let y = 0; y < ROWS; y++) for (let x = 0; x < COLS; x++) {
    const c = raw(x, y);
    if (c === '&') { if (start) errors.push('플레이어가 둘 이상'); start = [x, y]; }
    if (c === '$') golds.push([x, y]);
    if (c === '0') guards++;
  }
  if (!start) { errors.push('플레이어 없음'); return { errors, warnings }; }
  if (guards > 5) errors.push(`경비병 ${guards}명 (최대 5)`);

  function tile(x, y, rev) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return '@';
    const c = raw(x, y);
    if (c === 'S') return rev ? 'H' : ' ';
    if ('$0&'.includes(c)) return ' ';
    return c;
  }
  function moves(x, y, rev) {
    let t = tile(x, y, rev);
    if (t === '#' || t === 'X') t = ' '; // 구멍(혹은 함정) 속
    const below = y >= ROWS - 1 ? '@' : tile(x, y + 1, rev);
    const supported = t === 'H' || t === '-' || y >= ROWS - 1 || '#@H'.includes(below);
    if (!supported) return [[x, y + 1]];
    const out = [];
    for (const d of [-1, 1]) {
      const n = tile(x + d, y, rev);
      if (x + d >= 0 && x + d < COLS && !'#@X'.includes(n)) out.push([x + d, y]);
      // 파고 내려가기
      if (y < ROWS - 1 && tile(x + d, y + 1, rev) === '#' && n === ' ' && raw(x + d, y) !== '$') out.push([x + d, y + 1]);
    }
    if (t === 'H' && y > 0 && !'#@X'.includes(tile(x, y - 1, rev))) out.push([x, y - 1]);
    if (y < ROWS - 1 && !'#@'.includes(below) && (t === 'H' || t === '-' || below === 'H')) out.push([x, y + 1]);
    return out;
  }
  function reach(from, rev) {
    const seen = new Set([from.join(',')]);
    const q = [from];
    while (q.length) {
      const [x, y] = q.shift();
      for (const n of moves(x, y, rev)) {
        const k = n.join(',');
        if (!seen.has(k)) { seen.add(k); q.push(n); }
      }
    }
    return seen;
  }
  const canExit = set => [...set].some(k => k.endsWith(',0'));

  const r0 = reach(start, false);
  const missing = golds.filter(g => !r0.has(g.join(',')));
  if (missing.length) errors.push('시작점에서 갈 수 없는 금: ' + missing.map(g => `(${g})`).join(' '));

  // 순서를 찾아 실제로 클리어 가능한지 (DFS)
  const memo = new Set();
  let budget = 200000;
  function solve(pos, mask) {
    if (--budget < 0) return null;
    const key = pos.join(',') + '|' + mask;
    if (memo.has(key)) return false;
    memo.add(key);
    if (mask === (1 << golds.length) - 1) return canExit(reach(pos, true)) ? [] : false;
    const r = reach(pos, false);
    for (let i = 0; i < golds.length; i++) {
      if (mask & (1 << i)) continue;
      if (!r.has(golds[i].join(','))) continue;
      const res = solve(golds[i], mask | (1 << i));
      if (res) return [i, ...res];
      if (res === null) return null;
    }
    return false;
  }
  if (!missing.length && golds.length <= 20) {
    const res = solve(start, 0);
    if (res === false) errors.push('모든 금을 모으고 탈출하는 순서가 없음');
    else if (res === null) warnings.push('탐색 한도 초과 (클리어 가능 여부 미확인)');
  }
  // 함정에 빠지기 쉬운 곳: 각 금 위치에서 나머지 금/탈출로 갈 수 있는지
  golds.forEach((g, i) => {
    const r = reach(g, false);
    const lost = golds.filter((h, j) => j !== i && !r.has(h.join(',')));
    if (lost.length) warnings.push(`금 (${g}) 이후 못 가는 금 ${lost.length}개`);
  });
  if (!golds.length) warnings.push('금 없음');
  return { errors, warnings, golds: golds.length, guards };
}

const only = process.argv[2] ? Number(process.argv[2]) - 1 : null;
let bad = 0;
LEVELS.forEach((lv, i) => {
  if (only !== null && i !== only) return;
  const r = check(lv, i);
  if (only !== null) {
    console.log('    ' + '0123456789'.repeat(3).slice(0, COLS));
    lv.map.forEach((row, y) => console.log(String(y).padStart(2) + ' |' + row + '|'));
  }
  const tag = r.errors.length ? 'FAIL' : r.warnings.length ? 'WARN' : ' OK ';
  console.log(`[${tag}] ${String(i + 1).padStart(2)} ${lv.name}  (gold ${r.golds}, guards ${r.guards})`);
  r.errors.forEach(e => console.log('       error: ' + e));
  r.warnings.forEach(w => console.log('       warn : ' + w));
  if (r.errors.length) bad++;
});
process.exit(bad ? 1 : 0);
