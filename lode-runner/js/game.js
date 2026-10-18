/*
 * Lode Runner — Apple II 스타일 웹 에디션
 *
 * 화면: 280x192 (Apple II 고해상도), 플레이필드 28x16 타일, 타일 10x11 픽셀.
 * 이동: 타일 하나를 5단계(offset -2..2)로 나눠 움직인다 (원작과 같은 방식).
 */
(() => {
'use strict';

// ---------------------------------------------------------------------------
// 상수
// ---------------------------------------------------------------------------
const COLS = 28, ROWS = 16, TW = 10, TH = 11;
const SW = 280, SH = 192;

const EMPTY = 0, BRICK = 1, SOLID = 2, LADDER = 3, ROPE = 4, TRAP = 5, HLADDER = 6;
const CHAR_TO_TILE = { ' ': EMPTY, '#': BRICK, '@': SOLID, 'H': LADDER, '-': ROPE, 'X': TRAP, 'S': HLADDER };

const DIG_TICKS = 12;          // 구멍을 파는 데 걸리는 틱
const HOLE_OPEN_TICKS = 170;   // 구멍이 열려 있는 시간
const HOLE_FILL_TICKS = 18;    // 벽돌이 다시 차오르는 애니메이션 시간
const GUARD_TRAP_STEPS = 72;   // 경비병이 구멍에 갇혀 있는 시간 (경비병 이동 단위)
const GUARD_RESPAWN_STEPS = 26;
const GUARD_RATE = 0.66;       // 러너 대비 경비병 속도
const MAX_GUARDS = 5;          // 원작과 같은 최대 경비병 수
const START_LIVES = 5;
const SPEEDS = [90, 72, 58, 48, 40, 33]; // 틱 간격(ms)

const SCORE_GOLD = 250, SCORE_TRAP = 75, SCORE_KILL = 75, SCORE_LEVEL = 1500;

const HI_KEY = 'lr.hiscore.v1';
const CUSTOM_KEY = 'lr.custom.v1';
const SPEED_KEY = 'lr.speed.v1';

// Apple II 고해상도 팔레트 근사값
const C = {
  white: '#ffffff', orange: '#ff6a3c', violet: '#d36bff', blue: '#14cffd',
  green: '#14f53c', black: '#000000', dim: '#5a2a18', grid: '#161616'
};

// ---------------------------------------------------------------------------
// 스프라이트 (10x11). '#' = 주색, 'o' = 보조색
// ---------------------------------------------------------------------------
const FRAMES = {
  stand: [
    '....##....',
    '....##....',
    '...####...',
    '..#.##.#..',
    '..#.##.#..',
    '....##....',
    '....##....',
    '...#..#...',
    '...#..#...',
    '..#....#..',
    '.##....##.'],
  run1: [
    '.....##...',
    '.....##...',
    '....###...',
    '...#.###..',
    '..#..##.#.',
    '.....##...',
    '....#.#...',
    '...#...#..',
    '..#.....#.',
    '.#.......#',
    '.#........'],
  run2: [
    '.....##...',
    '.....##...',
    '....###...',
    '....####..',
    '....###...',
    '.....##...',
    '.....##...',
    '.....#.#..',
    '....#..#..',
    '....#..#..',
    '...##..##.'],
  run3: [
    '.....##...',
    '.....##...',
    '....###...',
    '...####...',
    '..#.##.#..',
    '.....##...',
    '....#..#..',
    '...#....#.',
    '..#.....#.',
    '.##......#',
    '.........#'],
  climb: [
    '.#..##....',
    '.#..##..#.',
    '..######..',
    '....##..#.',
    '....##....',
    '....##....',
    '...####...',
    '...#..#...',
    '...#..#...',
    '...#..##..',
    '..##......'],
  rope1: [
    '.#......#.',
    '.#..##..#.',
    '..#.##.#..',
    '...####...',
    '....##....',
    '....##....',
    '....##....',
    '...#..#...',
    '...#..#...',
    '...#..#...',
    '..........'],
  rope2: [
    '........#.',
    '.#..##..#.',
    '.#..##.#..',
    '..#####...',
    '....##....',
    '....##....',
    '....##....',
    '....#.#...',
    '...#..#...',
    '...#...#..',
    '..........'],
  fall: [
    '.#......#.',
    '.#..##..#.',
    '..#.##.#..',
    '...####...',
    '....##....',
    '....##....',
    '...####...',
    '..#....#..',
    '..#....#..',
    '..#....#..',
    '.##....##.'],
  dig: [
    '....##....',
    '....##....',
    '...####...',
    '..#.##.#..',
    '..#.##..#.',
    '....##...#',
    '....##....',
    '...#..#...',
    '...#..#...',
    '..#....#..',
    '.##....##.']
};

const TILES = {
  brick: [
    '#######.##',
    '#######.##',
    '#######.##',
    '..........',
    '###.######',
    '###.######',
    '###.######',
    '..........',
    '#######.##',
    '#######.##',
    '..........'],
  solid: [
    '#########.',
    '#########.',
    '#########.',
    '#########.',
    '#########.',
    '#########.',
    '#########.',
    '#########.',
    '#########.',
    '#########.',
    '..........'],
  ladder: [
    '.#......#.',
    '.########.',
    '.#......#.',
    '.#......#.',
    '.#......#.',
    '.########.',
    '.#......#.',
    '.#......#.',
    '.#......#.',
    '.########.',
    '.#......#.'],
  rope: [
    '..........',
    '##########',
    '..........',
    '..........',
    '..........',
    '..........',
    '..........',
    '..........',
    '..........',
    '..........',
    '..........'],
  gold: [
    '..........',
    '..........',
    '..........',
    '..........',
    '..........',
    '...####...',
    '..#oooo#..',
    '.########.',
    '.#oo##oo#.',
    '.#oooooo#.',
    '.########.']
};

// 경비병은 모자(넓은 머리)로 러너와 실루엣을 구분한다
function guardify(rows) {
  const out = rows.slice();
  const r0 = rows[0].split('');
  const wide = r0.slice();
  for (let i = 0; i < r0.length; i++) {
    if (r0[i] === '#') { if (i > 0) wide[i - 1] = '#'; if (i < r0.length - 1) wide[i + 1] = '#'; }
  }
  out[0] = wide.join('');
  return out;
}

function makeSprite(rows, colors) {
  const h = rows.length, w = rows[0].length;
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  const x = c.getContext('2d');
  for (let yy = 0; yy < h; yy++) {
    for (let xx = 0; xx < w; xx++) {
      const col = colors[rows[yy][xx]];
      if (col) { x.fillStyle = col; x.fillRect(xx, yy, 1, 1); }
    }
  }
  return c;
}

function flipSprite(src) {
  const c = document.createElement('canvas');
  c.width = src.width; c.height = src.height;
  const x = c.getContext('2d');
  x.translate(src.width, 0); x.scale(-1, 1); x.drawImage(src, 0, 0);
  return c;
}

const SPR = { runner: {}, guard: {}, runnerAlt: {}, tile: {} };
function buildSprites() {
  for (const name of Object.keys(FRAMES)) {
    const r = makeSprite(FRAMES[name], { '#': C.white });
    SPR.runner[name] = { r, l: flipSprite(r) };
    const a = makeSprite(FRAMES[name], { '#': C.orange });
    SPR.runnerAlt[name] = { r: a, l: flipSprite(a) };
    const g = makeSprite(guardify(FRAMES[name]), { '#': C.violet });
    SPR.guard[name] = { r: g, l: flipSprite(g) };
  }
  SPR.tile.brick = makeSprite(TILES.brick, { '#': C.orange });
  SPR.tile.solid = makeSprite(TILES.solid, { '#': C.orange });
  SPR.tile.ladder = makeSprite(TILES.ladder, { '#': C.white });
  SPR.tile.rope = makeSprite(TILES.rope, { '#': C.white });
  SPR.tile.gold = makeSprite(TILES.gold, { '#': C.white, 'o': C.orange });
  // 에디터 전용 표시
  SPR.tile.trapEdit = makeSprite(TILES.brick, { '#': C.violet });
  SPR.tile.hladderEdit = makeSprite(TILES.ladder, { '#': C.green });
}

// ---------------------------------------------------------------------------
// 5x7 비트맵 폰트
// ---------------------------------------------------------------------------
const FONT_SRC = {
  'A': '01110,10001,10001,11111,10001,10001,10001', 'B': '11110,10001,10001,11110,10001,10001,11110',
  'C': '01110,10001,10000,10000,10000,10001,01110', 'D': '11110,10001,10001,10001,10001,10001,11110',
  'E': '11111,10000,10000,11110,10000,10000,11111', 'F': '11111,10000,10000,11110,10000,10000,10000',
  'G': '01110,10001,10000,10111,10001,10001,01111', 'H': '10001,10001,10001,11111,10001,10001,10001',
  'I': '01110,00100,00100,00100,00100,00100,01110', 'J': '00111,00010,00010,00010,00010,10010,01100',
  'K': '10001,10010,10100,11000,10100,10010,10001', 'L': '10000,10000,10000,10000,10000,10000,11111',
  'M': '10001,11011,10101,10101,10001,10001,10001', 'N': '10001,10001,11001,10101,10011,10001,10001',
  'O': '01110,10001,10001,10001,10001,10001,01110', 'P': '11110,10001,10001,11110,10000,10000,10000',
  'Q': '01110,10001,10001,10001,10101,10010,01101', 'R': '11110,10001,10001,11110,10100,10010,10001',
  'S': '01111,10000,10000,01110,00001,00001,11110', 'T': '11111,00100,00100,00100,00100,00100,00100',
  'U': '10001,10001,10001,10001,10001,10001,01110', 'V': '10001,10001,10001,10001,10001,01010,00100',
  'W': '10001,10001,10001,10101,10101,10101,01010', 'X': '10001,10001,01010,00100,01010,10001,10001',
  'Y': '10001,10001,10001,01010,00100,00100,00100', 'Z': '11111,00001,00010,00100,01000,10000,11111',
  '0': '01110,10001,10011,10101,11001,10001,01110', '1': '00100,01100,00100,00100,00100,00100,01110',
  '2': '01110,10001,00001,00010,00100,01000,11111', '3': '11111,00010,00100,00010,00001,10001,01110',
  '4': '00010,00110,01010,10010,11111,00010,00010', '5': '11111,10000,11110,00001,00001,10001,01110',
  '6': '00110,01000,10000,11110,10001,10001,01110', '7': '11111,00001,00010,00100,01000,01000,01000',
  '8': '01110,10001,10001,01110,10001,10001,01110', '9': '01110,10001,10001,01111,00001,00010,01100',
  '.': '00000,00000,00000,00000,00000,01100,01100', ',': '00000,00000,00000,00000,01100,00100,01000',
  '!': '00100,00100,00100,00100,00100,00000,00100', '?': '01110,10001,00001,00010,00100,00000,00100',
  '-': '00000,00000,00000,11111,00000,00000,00000', ':': '00000,01100,01100,00000,01100,01100,00000',
  '/': '00000,00001,00010,00100,01000,10000,00000', "'": '01100,00100,01000,00000,00000,00000,00000',
  '(': '00010,00100,01000,01000,01000,00100,00010', ')': '01000,00100,00010,00010,00010,00100,01000',
  '<': '00010,00100,01000,10000,01000,00100,00010', '>': '01000,00100,00010,00001,00010,00100,01000',
  '=': '00000,00000,11111,00000,11111,00000,00000', '+': '00000,00100,00100,11111,00100,00100,00000',
  '*': '00000,10101,01110,11111,01110,10101,00000', ' ': '00000,00000,00000,00000,00000,00000,00000'
};
const FONT = {};
for (const k of Object.keys(FONT_SRC)) FONT[k] = FONT_SRC[k].split(',');

// ---------------------------------------------------------------------------
// 캔버스
// ---------------------------------------------------------------------------
const canvas = document.getElementById('screen');
const ctx = canvas.getContext('2d');
canvas.width = SW; canvas.height = SH;
ctx.imageSmoothingEnabled = false;

function drawText(s, x, y, color, scale = 1, adv = 6) {
  ctx.fillStyle = color;
  s = String(s).toUpperCase();
  for (const ch of s) {
    const g = FONT[ch] || FONT['?'];
    for (let r = 0; r < 7; r++) {
      const row = g[r];
      for (let c = 0; c < 5; c++) {
        if (row[c] === '1') ctx.fillRect(x + c * scale, y + r * scale, scale, scale);
      }
    }
    x += adv * scale;
  }
}
function textWidth(s, scale = 1, adv = 6) { return s.length * adv * scale - (adv - 5) * scale; }
function drawTextC(s, y, color, scale = 1, adv = 6) { drawText(s, Math.round((SW - textWidth(s, scale, adv)) / 2), y, color, scale, adv); }
function pad(n, w) { return String(Math.max(0, Math.floor(n))).padStart(w, '0').slice(-w); }

// ---------------------------------------------------------------------------
// 사운드 (Apple II 스피커 느낌의 사각파)
// ---------------------------------------------------------------------------
const Sound = {
  ctx: null, master: null, muted: false, fallOsc: null, fallGain: null,
  init() {
    if (this.ctx) { if (this.ctx.state === 'suspended') this.ctx.resume(); return; }
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    try {
      this.ctx = new AC();
      this.master = this.ctx.createGain();
      this.master.gain.value = 0.12;
      this.master.connect(this.ctx.destination);
    } catch (e) { this.ctx = null; }
  },
  tone(f, d, o = {}) {
    if (this.muted || !this.ctx) return;
    const t0 = this.ctx.currentTime + (o.delay || 0);
    const osc = this.ctx.createOscillator();
    const g = this.ctx.createGain();
    osc.type = o.type || 'square';
    osc.frequency.setValueAtTime(f, t0);
    if (o.to) osc.frequency.exponentialRampToValueAtTime(o.to, t0 + d);
    const v = o.vol == null ? 0.5 : o.vol;
    g.gain.setValueAtTime(v, t0);
    g.gain.setValueAtTime(v, t0 + d * 0.75);
    g.gain.linearRampToValueAtTime(0, t0 + d);
    osc.connect(g); g.connect(this.master);
    osc.start(t0); osc.stop(t0 + d + 0.02);
  },
  gold() { [988, 1319, 1760].forEach((f, i) => this.tone(f, 0.05, { delay: i * 0.045 })); },
  dig() { this.tone(260, 0.4, { to: 60, vol: 0.35 }); },
  trap() { this.tone(700, 0.09, { to: 350, vol: 0.35 }); },
  kill() { this.tone(400, 0.25, { to: 50, vol: 0.4 }); },
  reveal() { [523, 659, 784, 1047, 784, 1047, 1319].forEach((f, i) => this.tone(f, 0.07, { delay: i * 0.07 })); },
  death() {
    this.fallStop();
    this.tone(1000, 1.0, { to: 60, vol: 0.45 });
    this.tone(500, 1.0, { to: 40, vol: 0.25, type: 'sawtooth' });
  },
  tally() { this.tone(1400 + Math.random() * 300, 0.025, { vol: 0.3 }); },
  start() { [392, 523, 659, 784].forEach((f, i) => this.tone(f, 0.08, { delay: i * 0.08 })); },
  fallStart() {
    if (this.muted || !this.ctx || this.fallOsc) return;
    const t = this.ctx.currentTime;
    const o = this.ctx.createOscillator(), g = this.ctx.createGain();
    o.type = 'square';
    o.frequency.setValueAtTime(1500, t);
    o.frequency.exponentialRampToValueAtTime(180, t + 2.5);
    g.gain.value = 0.16;
    o.connect(g); g.connect(this.master);
    o.start(t);
    this.fallOsc = o; this.fallGain = g;
  },
  fallStop() {
    if (!this.fallOsc) return;
    try { this.fallGain.gain.setValueAtTime(0, this.ctx.currentTime); this.fallOsc.stop(this.ctx.currentTime + 0.02); } catch (e) { /* ignore */ }
    this.fallOsc = null; this.fallGain = null;
  }
};

// ---------------------------------------------------------------------------
// 저장소 (브라우저 저장소가 막혀 있어도 게임은 동작해야 한다)
// ---------------------------------------------------------------------------
function storeGet(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
function storeSet(k, v) { try { localStorage.setItem(k, v); } catch (e) { /* ignore */ } }

// ---------------------------------------------------------------------------
// 게임 상태
// ---------------------------------------------------------------------------
const MAIN_LEVELS = (window.LR_LEVELS || []).map(l => ({ name: l.name, map: l.map }));

const G = {
  mode: 'title', frame: 0, speed: 3,
  score: 0, lives: START_LIVES, hiscore: Number(storeGet(HI_KEY)) || 0,
  set: 'main', levels: MAIN_LEVELS, levelIdx: 0, startLevel: 0,
  base: null, gold: null, holes: null, holeList: [], guards: [], runner: null,
  goldLeft: 0, revealed: false, guardAcc: 0, timer: 0, paused: false, tally: 0,
  banner: 0
};
{
  const sp = Number(storeGet(SPEED_KEY));
  if (sp >= 0 && sp < SPEEDS.length && storeGet(SPEED_KEY) !== null) G.speed = sp;
}

function newEntity(x, y, isGuard) {
  return { x, y, xo: 0, yo: 0, dir: 1, anim: 0, isGuard, moving: false, falling: false, dig: null, pose: 'stand' };
}
function newGuard(x, y) {
  return Object.assign(newEntity(x, y, true), {
    gold: 0, goldTimer: 0, trapped: 0, climbing: false, holeX: 0, holeY: 0, ignoreHole: null, respawn: 0
  });
}

function loadLevel(def) {
  G.base = []; G.gold = []; G.holes = []; G.holeList = []; G.guards = [];
  G.runner = null; G.goldLeft = 0; G.revealed = false; G.guardAcc = 0;
  const rows = def.map;
  for (let y = 0; y < ROWS; y++) {
    G.base.push(new Array(COLS).fill(EMPTY));
    G.gold.push(new Array(COLS).fill(false));
    G.holes.push(new Array(COLS).fill(null));
    const line = rows[y] || '';
    for (let x = 0; x < COLS; x++) {
      const ch = line[x] || ' ';
      if (ch in CHAR_TO_TILE) G.base[y][x] = CHAR_TO_TILE[ch];
      else if (ch === '$') { G.gold[y][x] = true; G.goldLeft++; }
      else if (ch === '0') { if (G.guards.length < MAX_GUARDS) G.guards.push(newGuard(x, y)); }
      else if (ch === '&') { if (!G.runner) G.runner = newEntity(x, y, false); }
    }
  }
  if (!G.runner) G.runner = newEntity(0, ROWS - 1, false);
  if (G.goldLeft === 0) G.revealed = true;
}

// 현재 효과적으로 작용하는 타일 (구멍, 숨은 사다리 반영)
function act(x, y) {
  if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return SOLID;
  const b = G.base[y][x];
  if (b === BRICK) {
    const h = G.holes[y][x];
    if (h && h.phase !== 'dig') return EMPTY;
    return BRICK;
  }
  if (b === HLADDER) return G.revealed ? LADDER : EMPTY;
  return b;
}
// 방금 빠져나온 구멍은 그 경비병에게 벽돌처럼 보인다 (바로 다시 빠지지 않도록)
function entAct(e, x, y) {
  if (e.isGuard && e.ignoreHole && e.ignoreHole.x === x && e.ignoreHole.y === y) return BRICK;
  return act(x, y);
}
function blocksSide(t) { return t === BRICK || t === SOLID || t === TRAP; }
function isFloor(t) { return t === BRICK || t === SOLID || t === LADDER; }

function guardAt(x, y, except) {
  for (const g of G.guards) {
    if (g !== except && g.respawn === 0 && g.x === x && g.y === y) return g;
  }
  return null;
}

function centerX(e) { if (e.xo > 0) e.xo--; else if (e.xo < 0) e.xo++; }
function centerY(e) { if (e.yo > 0) e.yo--; else if (e.yo < 0) e.yo++; }

function isFalling(e) {
  const { x, y } = e;
  const t = entAct(e, x, y);
  if (t === LADDER) return false;
  if (t === ROPE && e.yo === 0) return false;
  if (e.yo < 0) return true;
  if (y >= ROWS - 1) return false;
  if (isFloor(entAct(e, x, y + 1))) return false;
  if (guardAt(x, y + 1, e)) return false; // 경비병 머리 위에 설 수 있다
  return true;
}

function fallStep(e) {
  centerX(e);
  e.yo++;
  if (e.yo > 2) { e.y++; e.yo = -2; }
}

function moveSide(e, d) {
  const { x, y } = e;
  if (d < 0 ? e.xo <= 0 : e.xo >= 0) {
    const nx = x + d;
    if (nx < 0 || nx >= COLS) return false;
    if (blocksSide(entAct(e, nx, y))) return false;
    if (e.isGuard && guardAt(nx, y, e)) return false;
  }
  centerY(e);
  e.xo += d;
  if (e.xo < -2) { e.x--; e.xo = 2; }
  else if (e.xo > 2) { e.x++; e.xo = -2; }
  e.dir = d;
  return true;
}

function moveUp(e) {
  const { x, y } = e;
  const t = entAct(e, x, y);
  if (e.yo <= 0) {
    if (t !== LADDER) return false;
    if (e.yo === 0) {
      if (y === 0) return false;
      if (blocksSide(entAct(e, x, y - 1))) return false;
      if (e.isGuard && guardAt(x, y - 1, e)) return false;
    }
  } else if (t !== LADDER && !(y < ROWS - 1 && entAct(e, x, y + 1) === LADDER)) {
    return false;
  }
  centerX(e);
  e.yo--;
  if (e.yo < -2) { e.y--; e.yo = 2; }
  return true;
}

function moveDown(e) {
  const { x, y } = e;
  const t = entAct(e, x, y);
  if (e.yo >= 0) {
    if (y >= ROWS - 1) return false;
    const b = entAct(e, x, y + 1);
    if (b === BRICK || b === SOLID) return false;
    if (!(t === LADDER || b === LADDER || t === ROPE)) return false;
    if (guardAt(x, y + 1, e)) return false;
  }
  centerX(e);
  e.yo++;
  if (e.yo > 2) { e.y++; e.yo = -2; }
  return true;
}

function nearCenter(e) { return Math.abs(e.xo) <= 1 && Math.abs(e.yo) <= 1; }

// ---------------------------------------------------------------------------
// 러너
// ---------------------------------------------------------------------------
function runnerPickGold() {
  const r = G.runner;
  if (G.gold[r.y][r.x] && nearCenter(r)) {
    G.gold[r.y][r.x] = false;
    G.goldLeft--;
    G.score += SCORE_GOLD;
    Sound.gold();
    if (G.goldLeft <= 0) revealLadders();
  }
}

function revealLadders() {
  if (G.revealed) return;
  G.revealed = true;
  G.goldLeft = 0;
  Sound.reveal();
}

function startDig(d) {
  const r = G.runner;
  const tx = r.x + d, ty = r.y + 1;
  if (ty >= ROWS || tx < 0 || tx >= COLS) return false;
  if (G.base[ty][tx] !== BRICK || G.holes[ty][tx]) return false;
  if (act(tx, r.y) !== EMPTY || G.gold[r.y][tx]) return false;
  if (guardAt(tx, r.y)) return false;
  r.xo = 0; r.yo = 0;
  r.dir = d;
  r.dig = { tx, ty, t: 0 };
  const h = { x: tx, y: ty, phase: 'dig', t: 0 };
  G.holes[ty][tx] = h;
  G.holeList.push(h);
  Sound.dig();
  return true;
}

function removeHole(h) {
  G.holes[h.y][h.x] = null;
  const i = G.holeList.indexOf(h);
  if (i >= 0) G.holeList.splice(i, 1);
}

function digTick() {
  const r = G.runner, dg = r.dig;
  const h = G.holes[dg.ty][dg.tx];
  dg.t++;
  if (h) h.t = dg.t;
  if (!h || guardAt(dg.tx, r.y)) {
    if (h) removeHole(h);
    r.dig = null;
    return;
  }
  if (dg.t >= DIG_TICKS) {
    h.phase = 'open';
    h.t = HOLE_OPEN_TICKS;
    r.dig = null;
  }
}

function updateRunner() {
  const r = G.runner;
  r.moving = false;
  if (r.dig) { digTick(); r.pose = 'dig'; return; }
  runnerPickGold();
  if (isFalling(r)) {
    if (!r.falling) { r.falling = true; Sound.fallStart(); }
    fallStep(r);
    r.pose = 'fall';
    runnerPickGold();
    return;
  }
  if (r.falling) { r.falling = false; Sound.fallStop(); }

  if (Input.held.DL && startDig(-1)) { r.pose = 'dig'; return; }
  if (Input.held.DR && startDig(1)) { r.pose = 'dig'; return; }

  const d = Input.dir();
  let moved = false;
  if (d === 'L') moved = moveSide(r, -1);
  else if (d === 'R') moved = moveSide(r, 1);
  else if (d === 'U') moved = moveUp(r);
  else if (d === 'D') moved = moveDown(r);
  if (moved) { r.moving = true; r.anim++; }
  r.pose = poseFor(r, d, moved);
  runnerPickGold();
}

function poseFor(e, d, moved) {
  const t = act(e.x, e.y);
  if (t === ROPE && e.yo === 0) return 'rope';
  if (t === LADDER && (d === 'U' || d === 'D' || !moved || e.yo !== 0)) {
    if (moved && (d === 'L' || d === 'R') && e.yo === 0) return 'run';
    return 'climb';
  }
  if (e.yo !== 0 && (d === 'U' || d === 'D')) return 'climb';
  return moved ? 'run' : (e.pose === 'climb' || e.pose === 'rope' ? e.pose : 'stand');
}

// ---------------------------------------------------------------------------
// 경비병 AI (원작 알고리즘을 따른다: 같은 층이면 직진, 아니면 층 스캔)
// ---------------------------------------------------------------------------
function bestMove(g) {
  const r = G.runner;
  const gx = g.x, gy = g.y, rx = r.x, ry = r.y;
  const A = (x, y) => entAct(g, x, y);

  if (gy === ry) {
    let x = gx, ok = true;
    while (x !== rx) {
      const cur = A(x, gy);
      const below = gy < ROWS - 1 ? A(x, gy + 1) : SOLID;
      if (cur === LADDER || cur === ROPE || isFloor(below) || gy === ROWS - 1 || guardAt(x, gy + 1, g)) {
        x += x < rx ? 1 : -1;
        if (blocksSide(A(x, gy))) { ok = false; break; }
      } else { ok = false; break; }
    }
    if (ok) {
      if (gx < rx) return 'R';
      if (gx > rx) return 'L';
      return g.xo < r.xo ? 'R' : (g.xo > r.xo ? 'L' : 'S');
    }
  }
  return scanFloor(g);
}

function scanFloor(g) {
  const x = g.x, y = g.y, ry = G.runner.y;
  const A = (xx, yy) => entAct(g, xx, yy);
  const floorAt = xx => y >= ROWS - 1 || isFloor(A(xx, y + 1));
  const canDown = xx => y < ROWS - 1 && A(xx, y + 1) !== BRICK && A(xx, y + 1) !== SOLID;

  let lx = x;
  while (lx > 0) {
    const t = A(lx - 1, y);
    if (blocksSide(t)) break;
    if (t === LADDER || t === ROPE || floorAt(lx - 1)) lx--;
    else { lx--; break; }
  }
  let rxe = x;
  while (rxe < COLS - 1) {
    const t = A(rxe + 1, y);
    if (blocksSide(t)) break;
    if (t === LADDER || t === ROPE || floorAt(rxe + 1)) rxe++;
    else { rxe++; break; }
  }

  let bestRating = 255, bestPath = 'S';
  const rate = (endY, cx) => {
    if (endY === ry) return Math.abs(x - cx);
    if (endY > ry) return endY - ry + 200;
    return ry - endY + 100;
  };
  const sideExit = (cx, yy) => {
    for (const nx of [cx - 1, cx + 1]) {
      if (nx < 0 || nx >= COLS) continue;
      const nb = yy < ROWS - 1 ? A(nx, yy + 1) : SOLID;
      if (isFloor(nb) || A(nx, yy) === ROPE) return true;
    }
    return false;
  };
  const scanDown = (cx, path) => {
    let yy = y;
    while (yy < ROWS - 1) {
      const b = A(cx, yy + 1);
      if (b === BRICK || b === SOLID) break;
      if (A(cx, yy) !== EMPTY && yy >= ry && sideExit(cx, yy)) break;
      yy++;
    }
    const rt = rate(yy, cx);
    if (rt < bestRating) { bestRating = rt; bestPath = path; }
  };
  const scanUp = (cx, path) => {
    let yy = y;
    while (yy > 0 && A(cx, yy) === LADDER) {
      yy--;
      if (yy <= ry && sideExit(cx, yy)) break;
    }
    const rt = rate(yy, cx);
    if (rt < bestRating) { bestRating = rt; bestPath = path; }
  };

  if (canDown(x)) scanDown(x, 'D');
  if (A(x, y) === LADDER) scanUp(x, 'U');
  for (let cx = lx; cx < x; cx++) {
    if (canDown(cx)) scanDown(cx, 'L');
    if (A(cx, y) === LADDER) scanUp(cx, 'L');
  }
  for (let cx = rxe; cx > x; cx--) {
    if (canDown(cx)) scanDown(cx, 'R');
    if (A(cx, y) === LADDER) scanUp(cx, 'R');
  }
  return bestPath;
}

function trapGuard(g) {
  g.trapped = GUARD_TRAP_STEPS;
  g.holeX = g.x; g.holeY = g.y;
  g.xo = 0; g.yo = 0;
  g.falling = false;
  G.score += SCORE_TRAP;
  Sound.trap();
  if (g.gold) {
    g.gold = 0;
    const ax = g.x, ay = g.y - 1;
    if (ay >= 0 && act(ax, ay) === EMPTY && G.base[ay][ax] === EMPTY && !G.gold[ay][ax]) {
      G.gold[ay][ax] = true;
    } else {
      // 금을 놓을 곳이 없으면 사라진 것으로 처리 (레벨이 막히지 않도록)
      G.goldLeft--;
      if (G.goldLeft <= 0) revealLadders();
    }
  }
}

function killGuard(g) {
  G.score += SCORE_KILL;
  Sound.kill();
  if (g.gold) {
    g.gold = 0;
    G.goldLeft--;
    if (G.goldLeft <= 0) revealLadders();
  }
  Object.assign(g, { trapped: 0, climbing: false, ignoreHole: null, falling: false, xo: 0, yo: 0, respawn: GUARD_RESPAWN_STEPS });
  // 맨 윗줄부터 빈 칸을 무작위로 찾아 다시 등장
  for (let y = 0; y < ROWS; y++) {
    const start = Math.floor(Math.random() * COLS);
    for (let i = 0; i < COLS; i++) {
      const x = (start + i) % COLS;
      if (act(x, y) === EMPTY && G.base[y][x] === EMPTY && !G.gold[y][x] && !guardAt(x, y, g) &&
          !(G.runner.x === x && G.runner.y === y)) {
        g.x = x; g.y = y;
        return;
      }
    }
  }
}

function guardStep(g) {
  if (g.respawn > 0) { g.respawn--; return; }
  if (g.ignoreHole && !(g.x === g.ignoreHole.x && g.y === g.ignoreHole.y - 1)) g.ignoreHole = null;

  if (g.trapped > 0) {
    g.trapped--;
    g.pose = 'fall';
    if (g.trapped === 0) g.climbing = true;
    return;
  }
  if (g.climbing) {
    if (g.y === g.holeY - 1 && g.yo === 0) {
      g.climbing = false;
      g.ignoreHole = { x: g.holeX, y: g.holeY };
    } else {
      if (g.y === g.holeY && g.yo === 0) {
        const above = act(g.x, g.y - 1);
        if (blocksSide(above) || guardAt(g.x, g.y - 1, g)) return; // 위가 막혀 있으면 대기
      }
      g.yo--;
      if (g.yo < -2) { g.y--; g.yo = 2; }
      g.anim++;
      g.pose = 'climb';
      return;
    }
  }

  // 열린 구멍 한가운데 도착하면 갇힌다
  const h = G.holes[g.y][g.x];
  if (h && h.phase !== 'dig' && g.yo === 0 && !g.ignoreHole) {
    trapGuard(g);
    return;
  }

  if (isFalling(g)) {
    fallStep(g);
    g.falling = true;
    g.pose = 'fall';
    return;
  }
  g.falling = false;

  // 금 줍기 / 내려놓기
  if (!g.gold && G.gold[g.y][g.x] && nearCenter(g)) {
    G.gold[g.y][g.x] = false;
    g.gold = 1;
    g.goldTimer = 20 + Math.floor(Math.random() * 60);
  } else if (g.gold) {
    g.goldTimer--;
    if (g.goldTimer <= 0 && g.xo === 0 && g.yo === 0 && G.base[g.y][g.x] === EMPTY && !G.gold[g.y][g.x] &&
        (g.y === ROWS - 1 || isFloor(act(g.x, g.y + 1)))) {
      G.gold[g.y][g.x] = true;
      g.gold = 0;
    }
  }

  const m = bestMove(g);
  let moved = false;
  if (m === 'L') moved = moveSide(g, -1);
  else if (m === 'R') moved = moveSide(g, 1);
  else if (m === 'U') moved = moveUp(g);
  else if (m === 'D') moved = moveDown(g);
  if (moved) g.anim++;
  g.pose = poseFor(g, m, moved);
}

// ---------------------------------------------------------------------------
// 구멍, 충돌, 진행
// ---------------------------------------------------------------------------
function updateHoles() {
  for (const h of G.holeList.slice()) {
    if (h.phase === 'open') {
      h.t--;
      if (h.t <= 0) { h.phase = 'fill'; h.t = 0; }
    } else if (h.phase === 'fill') {
      h.t++;
      if (h.t >= HOLE_FILL_TICKS) {
        removeHole(h);
        const r = G.runner;
        if (r.x === h.x && r.y === h.y) killRunner();
        for (const g of G.guards) {
          if (g.respawn === 0 && g.x === h.x && g.y === h.y) killGuard(g);
          if (g.ignoreHole && g.ignoreHole.x === h.x && g.ignoreHole.y === h.y) g.ignoreHole = null;
        }
      }
    }
  }
}

function entPx(e) { return e.x * TW + e.xo * 2; }
function entPy(e) { return e.y * TH + Math.round(e.yo * TH / 5); }

function checkCollisions() {
  const r = G.runner;
  const rx = entPx(r), ry = entPy(r);
  for (const g of G.guards) {
    if (g.respawn > 0) continue;
    if (Math.abs(entPx(g) - rx) < 7 && Math.abs(entPy(g) - ry) < 8) { killRunner(); return; }
  }
}

function killRunner() {
  if (G.mode !== 'play') return;
  G.mode = 'dying';
  G.timer = 48;
  G.runner.dig = null;
  Sound.death();
}

function playTick() {
  updateRunner();
  G.guardAcc += GUARD_RATE;
  while (G.guardAcc >= 1) {
    G.guardAcc -= 1;
    for (const g of G.guards) guardStep(g);
  }
  updateHoles();
  if (G.mode !== 'play') return;
  checkCollisions();
  if (G.mode !== 'play') return;
  const r = G.runner;
  if (G.revealed && r.y === 0 && r.yo === 0 && !r.falling) levelComplete();
}

function levelComplete() {
  Sound.fallStop();
  G.mode = 'complete';
  G.tally = SCORE_LEVEL;
  G.timer = 0;
}

function completeTick() {
  G.timer++;
  if (G.tally > 0) {
    if (G.timer % 2 === 0) {
      G.tally -= 100;
      G.score += 100;
      Sound.tally();
    }
    return;
  }
  if (G.timer < 40) return;
  if (G.set === 'test') { enterEditor(); return; }
  G.lives = Math.min(255, G.lives + 1); // 원작처럼 레벨을 깰 때마다 한 명 추가
  G.levelIdx++;
  if (G.levelIdx >= G.levels.length) {
    G.levelIdx = 0;
    G.mode = 'victory';
    G.timer = 220;
    Sound.reveal();
    return;
  }
  startLevel();
}

function afterDeath() {
  if (G.set === 'test') { startLevel(); return; }
  G.lives--;
  if (G.lives <= 0) {
    G.mode = 'gameover';
    G.timer = 200;
    saveHi();
    return;
  }
  startLevel();
}

function saveHi() {
  if (G.score > G.hiscore) {
    G.hiscore = G.score;
    storeSet(HI_KEY, String(G.hiscore));
  }
}

function startLevel() {
  loadLevel(G.levels[G.levelIdx]);
  G.mode = 'ready';
  G.paused = false;
  G.banner = 70;
  Input.fresh = false;
  Sound.fallStop();
}

function newGame(set, idx) {
  G.set = set;
  G.levels = set === 'main' ? MAIN_LEVELS : set === 'custom' ? ED.levels : [ED.levels[ED.idx]];
  if (!G.levels.length) return;
  G.levelIdx = Math.max(0, Math.min(idx, G.levels.length - 1));
  G.score = 0;
  G.lives = START_LIVES;
  hideEditorPanel();
  Sound.start();
  startLevel();
}

function goTitle() {
  saveHi();
  Sound.fallStop();
  G.mode = 'title';
  G.paused = false;
  G.levels = MAIN_LEVELS;
  hideEditorPanel();
}

function tick() {
  G.frame++;
  if (G.paused) return;
  if (G.banner > 0) G.banner--;
  switch (G.mode) {
    case 'ready':
      if (Input.fresh) { G.mode = 'play'; playTick(); }
      break;
    case 'play': playTick(); break;
    case 'dying': if (--G.timer <= 0) afterDeath(); break;
    case 'complete': completeTick(); break;
    case 'gameover': if (--G.timer <= 0) goTitle(); break;
    case 'victory': if (--G.timer <= 0) startLevel(); break;
  }
}

// ---------------------------------------------------------------------------
// 렌더링
// ---------------------------------------------------------------------------
function drawTile(spr, x, y) { ctx.drawImage(spr, x * TW, y * TH); }

function renderField() {
  for (let y = 0; y < ROWS; y++) {
    for (let x = 0; x < COLS; x++) {
      const b = G.base[y][x];
      const px = x * TW, py = y * TH;
      if (b === BRICK) {
        const h = G.holes[y][x];
        if (!h) drawTile(SPR.tile.brick, x, y);
        else if (h.phase === 'dig') {
          const k = Math.min(TH, Math.floor(h.t / DIG_TICKS * TH));
          if (k < TH) ctx.drawImage(SPR.tile.brick, 0, k, TW, TH - k, px, py + k, TW, TH - k);
          // 파편
          ctx.fillStyle = C.orange;
          for (let i = 0; i < 4; i++) {
            const sx = (i * 3 + h.t * 7) % TW;
            const sy = TH - 1 - ((i * 5 + h.t * 3) % 6);
            ctx.fillRect(px + sx, py - TH + sy, 1, 1);
          }
        } else if (h.phase === 'fill') {
          const k = Math.max(1, Math.ceil(h.t / HOLE_FILL_TICKS * TH));
          ctx.drawImage(SPR.tile.brick, 0, TH - k, TW, k, px, py + TH - k, TW, k);
        }
      } else if (b === SOLID) drawTile(SPR.tile.solid, x, y);
      else if (b === LADDER) drawTile(SPR.tile.ladder, x, y);
      else if (b === ROPE) drawTile(SPR.tile.rope, x, y);
      else if (b === TRAP) drawTile(SPR.tile.brick, x, y);
      else if (b === HLADDER && G.revealed) drawTile(SPR.tile.ladder, x, y);
      if (G.gold[y][x]) drawTile(SPR.tile.gold, x, y);
    }
  }
  // 바닥선
  ctx.fillStyle = C.orange;
  ctx.fillRect(0, ROWS * TH + 1, SW, 2);
}

function spriteFor(set, e) {
  let name = 'stand';
  switch (e.pose) {
    case 'run': name = ['run1', 'run2', 'run3', 'run2'][e.anim % 4]; break;
    case 'climb': name = 'climb'; break;
    case 'rope': name = e.anim % 2 ? 'rope2' : 'rope1'; break;
    case 'fall': name = 'fall'; break;
    case 'dig': name = 'dig'; break;
    default: name = 'stand';
  }
  const fr = set[name];
  if (name === 'climb') return (e.anim >> 1) % 2 ? fr.l : fr.r;
  return e.dir < 0 ? fr.l : fr.r;
}

function renderEntities() {
  for (const g of G.guards) {
    if (g.respawn > 0) {
      if ((G.frame >> 1) % 2) ctx.drawImage(SPR.guard.stand.r, g.x * TW, g.y * TH);
      continue;
    }
    let px = entPx(g);
    if (g.trapped > 0 && g.trapped < 18) px += (G.frame % 2) ? 1 : -1;
    ctx.drawImage(spriteFor(SPR.guard, g), px, entPy(g));
  }
  const r = G.runner;
  if (G.mode === 'ready' && (G.frame >> 2) % 2) return;
  if (G.mode === 'dying') {
    const set = (G.frame >> 1) % 2 ? SPR.runnerAlt : SPR.runner;
    if (G.timer > 8 || G.frame % 2) ctx.drawImage(spriteFor(set, r), entPx(r), entPy(r));
    return;
  }
  ctx.drawImage(spriteFor(SPR.runner, r), entPx(r), entPy(r));
}

function renderStatus() {
  const lv = G.set === 'test' ? 'TEST' : pad(G.levelIdx + 1, 3);
  const men = G.set === 'test' ? '---' : pad(G.lives, 3);
  const s = `SCORE${pad(G.score, 7)} MEN${men} LEVEL${lv}`;
  drawText(s, Math.round((SW - textWidth(s, 1, 7)) / 2), 183, C.white, 1, 7);
}

function box(lines, color = C.white) {
  const w = Math.max(...lines.map(l => textWidth(l))) + 16;
  const h = lines.length * 10 + 10;
  const x = Math.round((SW - w) / 2), y = Math.round((ROWS * TH - h) / 2);
  ctx.fillStyle = C.black; ctx.fillRect(x, y, w, h);
  ctx.fillStyle = C.orange; ctx.fillRect(x, y, w, 1); ctx.fillRect(x, y + h - 1, w, 1);
  ctx.fillRect(x, y, 1, h); ctx.fillRect(x + w - 1, y, 1, h);
  lines.forEach((l, i) => drawTextC(l, y + 6 + i * 10, i === 0 ? color : C.white));
}

function renderTitle() {
  // 벽돌 테두리
  for (let x = 0; x < COLS; x++) { drawTile(SPR.tile.brick, x, 0); drawTile(SPR.tile.brick, x, 15); }
  drawTextC('LODE RUNNER', 22, C.orange, 3);
  drawTextC('APPLE II STYLE  WEB EDITION', 50, C.white);
  // 장식: 사다리, 밧줄, 러너, 경비병, 금
  for (let y = 6; y <= 8; y++) drawTile(SPR.tile.ladder, 4, y);
  for (let x = 5; x <= 22; x++) drawTile(SPR.tile.rope, x, 6);
  for (let x = 5; x <= 23; x++) drawTile(SPR.tile.brick, x, 9);
  drawTile(SPR.tile.solid, 3, 9); drawTile(SPR.tile.solid, 24, 9);
  const t = G.frame;
  const rx = 50 + ((t * 2) % 150);
  ctx.drawImage(SPR.runner[['run1', 'run2', 'run3', 'run2'][(t >> 1) % 4]].r, rx, 8 * TH);
  ctx.drawImage(SPR.guard[['run1', 'run2', 'run3', 'run2'][(t >> 1) % 4]].r, rx - 30, 8 * TH);
  if (rx < 190) ctx.drawImage(SPR.tile.gold, 200, 8 * TH);

  const n = MAIN_LEVELS.length;
  const sel = `< LEVEL ${pad(G.startLevel + 1, 3)}/${pad(n, 3)} >`;
  drawTextC(sel, 112, C.white);
  if ((t >> 3) % 2) drawTextC('PRESS ENTER TO START', 124, C.orange);
  drawTextC(ED.levels.length ? `C: CUSTOM LEVELS (${ED.levels.length})   E: EDITOR` : 'E: LEVEL EDITOR', 138, C.white);
  drawTextC(`HIGH SCORE ${pad(G.hiscore, 7)}`, 152, C.blue);
  drawTextC(`SPEED ${G.speed + 1}/${SPEEDS.length}   M: SOUND ${Sound.muted ? 'OFF' : 'ON'}`, 183, C.white);
}

function renderEditor() {
  ctx.fillStyle = C.grid;
  for (let x = 1; x < COLS; x++) ctx.fillRect(x * TW, 0, 1, ROWS * TH);
  for (let y = 1; y < ROWS; y++) ctx.fillRect(0, y * TH, SW, 1);
  const map = ED.levels[ED.idx].map;
  for (let y = 0; y < ROWS; y++) {
    for (let x = 0; x < COLS; x++) {
      const ch = map[y][x];
      const spr = {
        '#': SPR.tile.brick, '@': SPR.tile.solid, 'H': SPR.tile.ladder, '-': SPR.tile.rope,
        'X': SPR.tile.trapEdit, 'S': SPR.tile.hladderEdit, '$': SPR.tile.gold,
        '0': SPR.guard.stand.r, '&': SPR.runner.stand.r
      }[ch];
      if (spr) drawTile(spr, x, y);
    }
  }
  if (ED.hover) {
    ctx.strokeStyle = C.blue;
    ctx.lineWidth = 1;
    ctx.strokeRect(ED.hover.x * TW + 0.5, ED.hover.y * TH + 0.5, TW - 1, TH - 1);
  }
  ctx.fillStyle = C.orange;
  ctx.fillRect(0, ROWS * TH + 1, SW, 2);
  const tool = TOOLS.find(t => t.ch === ED.tool);
  drawText(`EDIT ${pad(ED.idx + 1, 2)}/${pad(ED.levels.length, 2)}  TOOL:${tool ? tool.en : ''}`, 4, 183, C.white);
}

function render() {
  ctx.fillStyle = C.black;
  ctx.fillRect(0, 0, SW, SH);
  if (G.mode === 'title') { renderTitle(); return; }
  if (G.mode === 'editor') { renderEditor(); return; }
  renderField();
  renderEntities();
  renderStatus();
  if (G.mode === 'ready' && G.banner > 0) {
    const lv = G.levels[G.levelIdx];
    const name = (lv && lv.name) ? lv.name.toUpperCase().slice(0, 30) : '';
    box([G.set === 'test' ? 'TEST PLAY' : `LEVEL ${pad(G.levelIdx + 1, 3)}`, name].filter(Boolean), C.orange);
  }
  if (G.paused) box(['PAUSED', 'P: RESUME   Q: QUIT'], C.orange);
  else if (G.mode === 'gameover') box(['GAME OVER', `SCORE ${pad(G.score, 7)}`], C.orange);
  else if (G.mode === 'victory') box(['CONGRATULATIONS!', 'ALL LEVELS CLEARED', 'BACK TO LEVEL 001'], C.orange);
}

// ---------------------------------------------------------------------------
// 입력
// ---------------------------------------------------------------------------
const KEYMAP = {
  ArrowLeft: 'L', KeyJ: 'L', ArrowRight: 'R', KeyL: 'R', ArrowUp: 'U', KeyI: 'U', ArrowDown: 'D', KeyK: 'D',
  KeyZ: 'DL', KeyU: 'DL', KeyX: 'DR', KeyO: 'DR'
};
const Input = {
  held: { L: false, R: false, U: false, D: false, DL: false, DR: false },
  order: [],
  fresh: false, // 레벨 시작 후 조작 키가 눌렸는지
  press(k) {
    if (!this.held[k] && ['L', 'R', 'U', 'D'].includes(k)) this.order.push(k);
    this.held[k] = true;
    this.fresh = true;
  },
  release(k) {
    this.held[k] = false;
    this.order = this.order.filter(o => o !== k);
  },
  clear() { for (const k of Object.keys(this.held)) this.held[k] = false; this.order = []; },
  dir() {
    for (let i = this.order.length - 1; i >= 0; i--) if (this.held[this.order[i]]) return this.order[i];
    return null;
  }
};

function changeSpeed(d) {
  G.speed = Math.max(0, Math.min(SPEEDS.length - 1, G.speed + d));
  storeSet(SPEED_KEY, String(G.speed));
  updateHud();
}
function toggleSound() {
  Sound.muted = !Sound.muted;
  if (Sound.muted) Sound.fallStop();
  updateHud();
}
function togglePause() {
  if (!['ready', 'play'].includes(G.mode)) return;
  G.paused = !G.paused;
  if (G.paused) { Sound.fallStop(); Input.clear(); }
}
function abortLife() {
  if (G.mode === 'play' || G.mode === 'ready') { G.mode = 'play'; G.paused = false; killRunner(); }
}
function quitPlay() {
  G.paused = false;
  if (G.set === 'test') enterEditor(); else goTitle();
}

function command(cmd) {
  Sound.init();
  switch (cmd) {
    case 'start':
      if (G.mode === 'title') newGame('main', G.startLevel);
      else if (G.mode === 'gameover') goTitle();
      else if (G.paused) togglePause();
      break;
    case 'pause': togglePause(); break;
    case 'abort': abortLife(); break;
    case 'quit': if (G.paused || G.mode === 'gameover') quitPlay(); break;
    case 'faster': changeSpeed(1); break;
    case 'slower': changeSpeed(-1); break;
    case 'sound': toggleSound(); break;
    case 'editor': if (G.mode === 'title') enterEditor(); break;
    case 'custom': if (G.mode === 'title' && ED.levels.length) newGame('custom', 0); break;
    case 'prevLevel': if (G.mode === 'title') G.startLevel = (G.startLevel + MAIN_LEVELS.length - 1) % MAIN_LEVELS.length; break;
    case 'nextLevel': if (G.mode === 'title') G.startLevel = (G.startLevel + 1) % MAIN_LEVELS.length; break;
  }
}

function isTyping(e) {
  const t = e.target;
  return t && (t.tagName === 'TEXTAREA' || t.tagName === 'INPUT' || t.isContentEditable);
}

window.addEventListener('keydown', e => {
  if (isTyping(e)) return;
  Sound.init();
  if (e.ctrlKey && e.code === 'KeyA') { e.preventDefault(); abortLife(); return; }
  if (e.ctrlKey || e.metaKey || e.altKey) return;

  if (G.mode === 'title') {
    if (e.code === 'Enter' || e.code === 'Space') { e.preventDefault(); command('start'); }
    else if (e.code === 'ArrowLeft' || e.code === 'KeyJ') { e.preventDefault(); command('prevLevel'); }
    else if (e.code === 'ArrowRight' || e.code === 'KeyL') { e.preventDefault(); command('nextLevel'); }
    else if (e.code === 'KeyE') command('editor');
    else if (e.code === 'KeyC') command('custom');
    else if (e.code === 'KeyM') command('sound');
    else if (e.key === '+' || e.key === '=') command('faster');
    else if (e.key === '-') command('slower');
    return;
  }
  if (G.mode === 'editor') { editorKey(e); return; }

  const k = KEYMAP[e.code];
  if (k) {
    e.preventDefault();
    if (!G.paused) Input.press(k);
    return;
  }
  switch (e.code) {
    case 'Escape': case 'KeyP': e.preventDefault(); command('pause'); break;
    case 'KeyQ': command('quit'); break;
    case 'Enter': case 'Space': e.preventDefault(); command('start'); break;
    case 'KeyM': command('sound'); break;
    default:
      if (e.key === '+' || e.key === '=') command('faster');
      else if (e.key === '-') command('slower');
  }
});
window.addEventListener('keyup', e => {
  const k = KEYMAP[e.code];
  if (k) Input.release(k);
});
window.addEventListener('blur', () => Input.clear());

// 터치 버튼
document.querySelectorAll('[data-key]').forEach(btn => {
  const k = btn.dataset.key;
  const down = e => { e.preventDefault(); Sound.init(); btn.classList.add('on'); if (!G.paused) Input.press(k); if (G.mode === 'title') command('start'); };
  const up = e => { e.preventDefault(); btn.classList.remove('on'); Input.release(k); };
  btn.addEventListener('pointerdown', down);
  btn.addEventListener('pointerup', up);
  btn.addEventListener('pointercancel', up);
  btn.addEventListener('pointerleave', up);
  btn.addEventListener('contextmenu', e => e.preventDefault());
});
document.querySelectorAll('[data-cmd]').forEach(btn => {
  btn.addEventListener('click', e => { e.preventDefault(); command(btn.dataset.cmd); btn.blur(); });
});

// ---------------------------------------------------------------------------
// 레벨 에디터
// ---------------------------------------------------------------------------
const TOOLS = [
  { ch: ' ', en: 'ERASE', ko: '지우기' },
  { ch: '#', en: 'BRICK', ko: '벽돌' },
  { ch: '@', en: 'SOLID', ko: '콘크리트' },
  { ch: 'H', en: 'LADDER', ko: '사다리' },
  { ch: '-', en: 'ROPE', ko: '밧줄' },
  { ch: 'X', en: 'TRAP', ko: '함정 벽돌' },
  { ch: 'S', en: 'HIDDEN LADDER', ko: '숨은 사다리' },
  { ch: '$', en: 'GOLD', ko: '금' },
  { ch: '0', en: 'GUARD', ko: '경비병' },
  { ch: '&', en: 'PLAYER', ko: '플레이어' }
];

function blankMap() {
  const m = [];
  for (let y = 0; y < ROWS; y++) m.push(' '.repeat(COLS));
  m[ROWS - 1] = '#'.repeat(COLS);
  m[ROWS - 2] = ' &' + ' '.repeat(COLS - 2);
  return m;
}
function normalizeMap(lines) {
  const m = [];
  for (let y = 0; y < ROWS; y++) {
    let l = (lines[y] || '').replace(/\r/g, '');
    l = l.split('').map(c => ('#@H-XS$0& '.includes(c) ? c : ' ')).join('');
    m.push(l.padEnd(COLS, ' ').slice(0, COLS));
  }
  return m;
}

const ED = { levels: [], idx: 0, tool: '#', painting: false, erasing: false, hover: null };
try {
  const a = JSON.parse(storeGet(CUSTOM_KEY) || '[]');
  if (Array.isArray(a)) ED.levels = a.filter(l => l && Array.isArray(l.map)).map(l => ({ name: String(l.name || ''), map: normalizeMap(l.map) }));
} catch (e) { ED.levels = []; }

function saveCustom() { storeSet(CUSTOM_KEY, JSON.stringify(ED.levels)); }

const edPanel = document.getElementById('editor-panel');
const edMsg = document.getElementById('ed-msg');
const edText = document.getElementById('ed-text');
const edTools = document.getElementById('ed-tools');
const edName = document.getElementById('ed-name');
const playHelp = document.getElementById('play-help');

TOOLS.forEach((t, i) => {
  const b = document.createElement('button');
  b.type = 'button';
  b.dataset.tool = t.ch;
  b.textContent = `${(i + 1) % 10} ${t.ko}`;
  b.addEventListener('click', () => { ED.tool = t.ch; refreshEditorUi(); });
  edTools.appendChild(b);
});

function edSay(s) { edMsg.textContent = s; }

function refreshEditorUi() {
  edTools.querySelectorAll('button').forEach(b => b.classList.toggle('sel', b.dataset.tool === ED.tool));
  const lv = ED.levels[ED.idx];
  edName.value = lv ? lv.name : '';
  document.getElementById('ed-count').textContent = `${ED.idx + 1} / ${ED.levels.length}`;
}

function enterEditor() {
  Sound.fallStop();
  if (!ED.levels.length) { ED.levels.push({ name: 'My Level 1', map: blankMap() }); saveCustom(); }
  ED.idx = Math.min(ED.idx, ED.levels.length - 1);
  G.mode = 'editor';
  G.paused = false;
  edPanel.hidden = false;
  playHelp.hidden = true;
  edText.hidden = true;
  edSay('왼쪽 클릭/드래그: 배치 · 오른쪽 클릭: 지우기 · 숫자키 1~0: 도구 선택');
  refreshEditorUi();
}
function hideEditorPanel() { edPanel.hidden = true; playHelp.hidden = false; }

function setCell(x, y, ch) {
  const lv = ED.levels[ED.idx];
  const m = lv.map;
  if (m[y][x] === ch) return;
  if (ch === '&') {
    for (let yy = 0; yy < ROWS; yy++) m[yy] = m[yy].replace(/&/g, ' ');
  }
  if (ch === '0') {
    const n = m.join('').split('0').length - 1;
    if (n >= MAX_GUARDS && m[y][x] !== '0') { edSay(`경비병은 최대 ${MAX_GUARDS}명까지 배치할 수 있습니다.`); return; }
  }
  m[y] = m[y].slice(0, x) + ch + m[y].slice(x + 1);
  saveCustom();
}

function validateLevel(m) {
  const all = m.join('');
  if (!all.includes('&')) return '플레이어(&)를 배치해 주세요.';
  return '';
}

function canvasCell(e) {
  const rect = canvas.getBoundingClientRect();
  const cx = (e.clientX - rect.left) / rect.width * SW;
  const cy = (e.clientY - rect.top) / rect.height * SH;
  const x = Math.floor(cx / TW), y = Math.floor(cy / TH);
  if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return null;
  return { x, y };
}

canvas.addEventListener('pointerdown', e => {
  Sound.init();
  if (G.mode === 'title') { command('start'); return; }
  if (G.mode !== 'editor') return;
  e.preventDefault();
  const c = canvasCell(e);
  if (!c) return;
  ED.painting = true;
  ED.erasing = e.button === 2;
  canvas.setPointerCapture(e.pointerId);
  setCell(c.x, c.y, ED.erasing ? ' ' : ED.tool);
});
canvas.addEventListener('pointermove', e => {
  if (G.mode !== 'editor') return;
  const c = canvasCell(e);
  ED.hover = c;
  if (ED.painting && c) {
    const ch = ED.erasing ? ' ' : ED.tool;
    if (ch !== '&') setCell(c.x, c.y, ch);
  }
});
const stopPaint = () => { ED.painting = false; };
canvas.addEventListener('pointerup', stopPaint);
canvas.addEventListener('pointercancel', stopPaint);
canvas.addEventListener('pointerleave', () => { ED.hover = null; });
canvas.addEventListener('contextmenu', e => e.preventDefault());

function editorKey(e) {
  if (e.key >= '0' && e.key <= '9') {
    const i = (Number(e.key) + 9) % 10;
    if (TOOLS[i]) { ED.tool = TOOLS[i].ch; refreshEditorUi(); }
  } else if (e.code === 'Escape') {
    goTitle();
  } else if (e.code === 'KeyT') {
    edButton('test');
  }
}

function edButton(id) {
  const lv = ED.levels[ED.idx];
  switch (id) {
    case 'prev': ED.idx = (ED.idx + ED.levels.length - 1) % ED.levels.length; edText.hidden = true; break;
    case 'next': ED.idx = (ED.idx + 1) % ED.levels.length; edText.hidden = true; break;
    case 'new':
      ED.levels.push({ name: `My Level ${ED.levels.length + 1}`, map: blankMap() });
      ED.idx = ED.levels.length - 1; saveCustom(); edSay('새 레벨을 만들었습니다.');
      break;
    case 'dup':
      ED.levels.splice(ED.idx + 1, 0, { name: lv.name + ' copy', map: lv.map.slice() });
      ED.idx++; saveCustom(); edSay('레벨을 복제했습니다.');
      break;
    case 'del':
      if (!confirm(`'${lv.name}' 레벨을 삭제할까요?`)) return;
      ED.levels.splice(ED.idx, 1);
      if (!ED.levels.length) ED.levels.push({ name: 'My Level 1', map: blankMap() });
      ED.idx = Math.min(ED.idx, ED.levels.length - 1); saveCustom(); edSay('삭제했습니다.');
      break;
    case 'clear':
      if (!confirm('이 레벨을 모두 지울까요?')) return;
      lv.map = blankMap(); saveCustom();
      break;
    case 'copyMain': {
      const ans = prompt(`가져올 기본 레벨 번호 (1~${MAIN_LEVELS.length})`, '1');
      const n = Math.floor(Number(ans));
      if (ans === null || !(n >= 1 && n <= MAIN_LEVELS.length)) return;
      const src = MAIN_LEVELS[n - 1];
      ED.levels.push({ name: `${src.name} (edit)`, map: normalizeMap(src.map) });
      ED.idx = ED.levels.length - 1; saveCustom(); edSay(`기본 레벨 ${n}을 복사했습니다.`);
      break;
    }
    case 'test': {
      const err = validateLevel(lv.map);
      if (err) { edSay(err); return; }
      newGame('test', 0);
      return;
    }
    case 'export':
      edText.hidden = false;
      edText.value = lv.map.map(l => l.replace(/\s+$/, '')).join('\n');
      edText.select();
      edSay('아래 텍스트를 복사해 두면 나중에 "가져오기"로 불러올 수 있습니다.');
      break;
    case 'import':
      if (edText.hidden) { edText.hidden = false; edText.value = ''; edText.focus(); edSay('텍스트 맵(16줄 x 28칸)을 붙여넣고 "가져오기"를 한 번 더 누르세요.'); return; }
      lv.map = normalizeMap(edText.value.split('\n'));
      saveCustom(); edText.hidden = true; edSay('가져왔습니다.');
      break;
    case 'exit': goTitle(); return;
  }
  refreshEditorUi();
}
document.querySelectorAll('[data-ed]').forEach(b => b.addEventListener('click', () => { edButton(b.dataset.ed); b.blur(); }));
edName.addEventListener('input', () => { ED.levels[ED.idx].name = edName.value.slice(0, 30); saveCustom(); });

// ---------------------------------------------------------------------------
// HUD / 크기 조절 / 메인 루프
// ---------------------------------------------------------------------------
function updateHud() {
  const s = document.getElementById('hud-speed');
  if (s) s.textContent = `${G.speed + 1}/${SPEEDS.length}`;
  const m = document.getElementById('hud-sound');
  if (m) m.textContent = Sound.muted ? '끔' : '켬';
}

function fit() {
  const wrap = document.getElementById('screen-wrap');
  const availW = wrap.clientWidth;
  const availH = Math.max(200, window.innerHeight - (document.body.classList.contains('touch') ? 230 : 120));
  let scale = Math.min(availW / SW, availH / SH);
  if (scale >= 2) scale = Math.floor(scale);
  canvas.style.width = Math.floor(SW * scale) + 'px';
  canvas.style.height = Math.floor(SH * scale) + 'px';
}
if (window.matchMedia && window.matchMedia('(pointer: coarse)').matches) document.body.classList.add('touch');
window.addEventListener('resize', fit);

buildSprites();
updateHud();
fit();

let last = performance.now(), acc = 0;
function loop(now) {
  const dt = Math.min(250, now - last);
  last = now;
  acc += dt;
  const step = SPEEDS[G.speed];
  while (acc >= step) { acc -= step; tick(); }
  render();
  requestAnimationFrame(loop);
}
requestAnimationFrame(loop);

// 테스트/디버깅용 훅
window.LodeRunner = { G, ED, Input, tick, loadLevel, act, command, constants: { COLS, ROWS } };
})();
