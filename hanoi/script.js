(() => {
  const MOVE_INTERVAL = 1000; // 원반 하나를 옮기는 주기 (ms)
  const ANIM_DURATION = 750;  // 실제 애니메이션 시간 (주기보다 짧게)
  const MIN_DISKS = 1;
  const MAX_DISKS = 10;
  const BASE_TOP_OFFSET = 24; // 보드 아래쪽에서 받침대 윗면까지 거리(px)

  const board = document.getElementById('board');
  const diskInput = document.getElementById('diskCount');
  const startBtn = document.getElementById('startBtn');
  const pauseBtn = document.getElementById('pauseBtn');
  const resetBtn = document.getElementById('resetBtn');
  const moveCountEl = document.getElementById('moveCount');
  const totalMovesEl = document.getElementById('totalMoves');
  const moveInfoEl = document.getElementById('moveInfo');

  const PEG_NAMES = ['A', 'B', 'C'];

  let n = 0;
  let pegs = [[], [], []];   // 각 기둥에 쌓인 원반 번호 (아래 → 위)
  let disks = [];            // 원반 번호 → DOM 요소 (1이 가장 작음)
  let moves = [];
  let step = 0;
  let timer = null;
  let running = false;
  let paused = false;
  let animating = false;
  let geom = null;

  function clampCount(value) {
    const v = parseInt(value, 10);
    if (Number.isNaN(v)) return 4;
    return Math.min(MAX_DISKS, Math.max(MIN_DISKS, v));
  }

  // 재귀로 이동 순서를 미리 계산
  function solve(count, from, to, via, out) {
    if (count === 0) return;
    solve(count - 1, from, via, to, out);
    out.push([from, to]);
    solve(count - 1, via, to, from, out);
  }

  // 보드 크기와 원반 개수에 따라 원반 너비/두께를 계산
  function computeGeometry() {
    const w = board.clientWidth;
    const h = board.clientHeight;
    const pegSpace = w / 3;
    const maxDiskW = pegSpace * 0.9;
    const minDiskW = Math.max(pegSpace * 0.22, 28);

    // 들어올릴 공간을 남기고 사용 가능한 높이에 맞춰 두께 결정
    const usableH = h - BASE_TOP_OFFSET - 70;
    const diskH = Math.max(10, Math.min(36, usableH / n));
    const rodH = diskH * n + 30;

    const widths = [];
    for (let i = 1; i <= n; i++) {
      const t = n === 1 ? 1 : (i - 1) / (n - 1);
      widths[i] = minDiskW + (maxDiskW - minDiskW) * t;
    }

    return {
      centers: [pegSpace * 0.5, pegSpace * 1.5, pegSpace * 2.5],
      diskH,
      rodH,
      widths,
      liftY: BASE_TOP_OFFSET + rodH + 16, // 들어올릴 때의 bottom 값
      fontSize: Math.max(9, Math.min(16, diskH * 0.55)),
    };
  }

  function diskColor(i) {
    const hue = Math.round(((i - 1) / Math.max(1, n)) * 300);
    return `hsl(${hue}, 70%, 52%)`;
  }

  function diskPos(diskNo, peg, level) {
    const width = geom.widths[diskNo];
    return {
      left: geom.centers[peg] - width / 2,
      bottom: BASE_TOP_OFFSET + level * geom.diskH,
    };
  }

  function placeDisk(diskNo, peg, level) {
    const el = disks[diskNo];
    const p = diskPos(diskNo, peg, level);
    el.style.left = p.left + 'px';
    el.style.bottom = p.bottom + 'px';
  }

  function layout() {
    geom = computeGeometry();
    board.querySelectorAll('.rod').forEach((rod, idx) => {
      rod.style.left = geom.centers[idx] + 'px';
      rod.style.bottom = BASE_TOP_OFFSET + 'px';
      rod.style.height = geom.rodH + 'px';
    });
    for (let i = 1; i <= n; i++) {
      const el = disks[i];
      el.style.width = geom.widths[i] + 'px';
      el.style.height = geom.diskH + 'px';
      el.style.fontSize = geom.fontSize + 'px';
    }
    pegs.forEach((stack, peg) => stack.forEach((d, level) => placeDisk(d, peg, level)));
  }

  function build() {
    stop();
    n = clampCount(diskInput.value);
    diskInput.value = n;

    board.querySelectorAll('.rod, .disk').forEach(el => el.remove());
    for (let i = 0; i < 3; i++) {
      const rod = document.createElement('div');
      rod.className = 'rod';
      board.appendChild(rod);
    }

    disks = [];
    pegs = [[], [], []];
    for (let i = n; i >= 1; i--) {
      const el = document.createElement('div');
      el.className = 'disk';
      el.textContent = i;
      el.style.background = diskColor(i);
      board.appendChild(el);
      disks[i] = el;
      pegs[0].push(i);
    }

    moves = [];
    solve(n, 0, 2, 1, moves);
    step = 0;
    moveCountEl.textContent = '0';
    totalMovesEl.textContent = moves.length;
    moveInfoEl.textContent = '원반 개수를 정하고 시작 버튼을 누르세요.';
    layout();
    updateButtons();
  }

  function animateMove(from, to, done) {
    const diskNo = pegs[from].pop();
    const el = disks[diskNo];
    const start = diskPos(diskNo, from, pegs[from].length);
    const end = diskPos(diskNo, to, pegs[to].length);
    pegs[to].push(diskNo);

    // 위로 들기 → 옆으로 이동 → 내려놓기
    const lift = geom.liftY;
    const frames = [
      { left: start.left + 'px', bottom: start.bottom + 'px' },
      { left: start.left + 'px', bottom: lift + 'px', offset: 0.33 },
      { left: end.left + 'px', bottom: lift + 'px', offset: 0.67 },
      { left: end.left + 'px', bottom: end.bottom + 'px' },
    ];

    animating = true;
    el.classList.add('moving');
    const anim = el.animate(frames, { duration: ANIM_DURATION, easing: 'ease-in-out' });
    anim.onfinish = () => {
      el.style.left = end.left + 'px';
      el.style.bottom = end.bottom + 'px';
      el.classList.remove('moving');
      animating = false;
      done(diskNo);
    };
  }

  function nextMove() {
    timer = null;
    if (!running || paused) return;
    if (step >= moves.length) {
      finish();
      return;
    }
    const [from, to] = moves[step];
    animateMove(from, to, (diskNo) => {
      step++;
      moveCountEl.textContent = step;
      moveInfoEl.textContent = `원반 ${diskNo}: ${PEG_NAMES[from]} → ${PEG_NAMES[to]}`;
      if (step >= moves.length) {
        finish();
      }
    });
    // 애니메이션과 별개로 1초마다 다음 이동 예약
    if (step + 1 < moves.length) {
      timer = setTimeout(nextMove, MOVE_INTERVAL);
    }
  }

  function finish() {
    running = false;
    paused = false;
    moveInfoEl.textContent = `완료! 총 ${moves.length}번 이동했습니다.`;
    updateButtons();
  }

  function stop() {
    if (timer) clearTimeout(timer);
    timer = null;
    running = false;
    paused = false;
    board.querySelectorAll('.disk').forEach(el => el.getAnimations().forEach(a => a.cancel()));
    animating = false;
  }

  function updateButtons() {
    startBtn.disabled = running;
    diskInput.disabled = running;
    pauseBtn.disabled = !running;
    pauseBtn.textContent = paused ? '재개' : '일시정지';
  }

  startBtn.addEventListener('click', () => {
    // 끝난 상태이거나 개수를 바꿨으면 새로 구성
    if (step > 0 || clampCount(diskInput.value) !== n) build();
    running = true;
    paused = false;
    updateButtons();
    moveInfoEl.textContent = '진행 중...';
    nextMove();
  });

  pauseBtn.addEventListener('click', () => {
    if (!running) return;
    paused = !paused;
    if (paused) {
      if (timer) clearTimeout(timer);
      timer = null;
    } else {
      // 진행 중인 애니메이션이 끝날 시간을 기다린 뒤 재개
      timer = setTimeout(nextMove, animating ? ANIM_DURATION : 0);
    }
    updateButtons();
  });

  resetBtn.addEventListener('click', build);

  diskInput.addEventListener('change', () => {
    if (!running) build();
  });

  window.addEventListener('resize', () => {
    if (!animating) layout();
  });

  build();
})();
