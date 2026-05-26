// PRE Buddy scenario viewer.
//
// Loads a timeline JSON (the `--format json` output of `pre-buddy simulate`)
// and animates the robot through each event. Pure vanilla JS, no deps, no
// build step. Open this file directly via file:// or serve it with
// `pre-buddy viewer`.

(function () {
  'use strict';

  // ── Colour palette ─────────────────────────────────────────────────────
  // Mirrors mock_robot.py + DESIGN.md guidance. Keep in sync with embodiment.

  const LED_COLORS = {
    green:  '#10b981',
    blue:   '#3b82f6',
    purple: '#8b5cf6',
    yellow: '#eab308',
    amber:  '#f59e0b',
    white:  '#f8fafc',
    red:    '#ef4444',
    cyan:   '#06b6d4',
  };

  function ledHex(name) {
    return LED_COLORS[name] || LED_COLORS.blue;
  }

  // ── Built-in demo timeline ─────────────────────────────────────────────
  // Pre-baked so the viewer is useful on first open (no file load needed).
  // Mirrors mock_robot.simulate_event() output for the daily_flow scenario.

  const DEMO_TIMELINE = [
    { scenario_index: 1, source_event: 'pre.system.wake_word',     led: 'yellow', has_motion: true,  head_x_deg: -35, head_y_deg: 45, duration_ms: 350, note: 'turn_toward_mic',        character: 'sprout', severity: 'normal' },
    { scenario_index: 2, source_event: 'pre.bg_agents.change',     led: 'green',  has_motion: false, head_x_deg: 0,   head_y_deg: 45, duration_ms: 350, note: 'agent_tier_pulse',        character: 'sprout', severity: 'normal' },
    { scenario_index: 3, source_event: 'pre.router.decision',      led: 'purple', has_motion: true,  head_x_deg: 0,   head_y_deg: 38, duration_ms: 550, note: 'router_escalation_nod',   character: 'sprout', severity: 'normal' },
    { scenario_index: 4, source_event: 'pre.kg.delta',             led: 'cyan',   has_motion: false, head_x_deg: 0,   head_y_deg: 45, duration_ms: 350, note: 'kg_update',               character: 'sprout', severity: 'normal' },
    { scenario_index: 5, source_event: 'pre.tools.rollup',         led: 'white',  has_motion: false, head_x_deg: 0,   head_y_deg: 45, duration_ms: 350, note: 'tools_rollup',            character: 'sprout', severity: 'normal' },
    { scenario_index: 6, source_event: 'pre.system.proximity',     led: 'green',  has_motion: true,  head_x_deg: 0,   head_y_deg: 60, duration_ms: 700, note: 'look_up',                 character: 'sprout', severity: 'normal' },
  ];

  // ── State ──────────────────────────────────────────────────────────────

  const state = {
    timeline: DEMO_TIMELINE,
    index: -1,
    playing: false,
    speed: 1.0,
    playTimer: null,
  };

  // ── DOM refs ──────────────────────────────────────────────────────────

  const $ = (id) => document.getElementById(id);
  const dom = {
    head:      $('robot-head'),
    led:       $('robot-led'),
    rdEvent:   $('rd-event'),
    rdNote:    $('rd-note'),
    rdLed:     $('rd-led'),
    rdHead:    $('rd-head'),
    rdDuration: $('rd-duration'),
    metaChar:  $('meta-character'),
    metaSev:   $('meta-severity'),
    metaStep:  $('meta-step'),
    btnPrev:   $('btn-prev'),
    btnPlay:   $('btn-playpause'),
    btnNext:   $('btn-next'),
    btnReset:  $('btn-reset'),
    speedSel:  $('speed-select'),
    scrubber:  $('scrubber'),
    fileInput: $('file-input'),
    pasteToggle: $('btn-paste-toggle'),
    pasteArea: $('paste-area'),
    loadDemo:  $('btn-load-demo'),
    log:       $('event-log'),
  };

  // ── Rendering ─────────────────────────────────────────────────────────

  function renderRow(row) {
    if (!row) {
      dom.rdEvent.textContent = '—';
      dom.rdNote.textContent = '—';
      dom.rdHead.textContent = 'x=0° y=45°';
      dom.rdDuration.textContent = '—';
      dom.led.style.background = LED_COLORS.blue;
      dom.led.style.boxShadow = `0 0 12px ${LED_COLORS.blue}, 0 0 28px ${LED_COLORS.blue}`;
      dom.rdLed.style.background = LED_COLORS.blue;
      dom.rdLed.textContent = '—';
      dom.head.style.transform = 'rotateZ(0deg) rotateX(0deg)';
      dom.head.classList.remove('motion');
      return;
    }

    const color = ledHex(row.led);
    dom.led.style.background = color;
    dom.led.style.boxShadow = `0 0 12px ${color}, 0 0 28px ${color}`;
    dom.head.style.setProperty('--led-default', color);
    dom.rdLed.style.background = color;
    dom.rdLed.style.color = color === LED_COLORS.white ? '#0b0f17' : '#0b0f17';
    dom.rdLed.textContent = row.led;

    // Head pose: Z-rotate for X (turn left/right), X-rotate inverted from
    // y_deg (90° = level, lower = nodding down, higher = looking up).
    const turnZ = Number(row.head_x_deg) || 0;
    const tiltX = 45 - (Number(row.head_y_deg) || 45);  // positive = nod up
    if (row.has_motion) {
      dom.head.classList.add('motion');
      dom.head.style.transform = `rotateZ(${turnZ * 0.6}deg) rotateX(${tiltX}deg)`;
    } else {
      dom.head.classList.remove('motion');
      dom.head.style.transform = 'rotateZ(0deg) rotateX(0deg)';
    }

    dom.rdEvent.textContent = row.source_event;
    dom.rdNote.textContent = row.note || '—';
    dom.rdHead.textContent = `x=${turnZ.toFixed(0)}° y=${(Number(row.head_y_deg) || 45).toFixed(0)}°`;
    dom.rdDuration.textContent = `${row.duration_ms || 0}ms` + (row.has_motion ? '' : ' · still');
  }

  function renderMeta() {
    const first = state.timeline[0];
    dom.metaChar.textContent = `character: ${first ? first.character : '—'}`;
    dom.metaSev.textContent  = `severity: ${first ? first.severity : '—'}`;
    dom.metaStep.textContent = `step: ${state.index >= 0 ? state.index + 1 : 0} / ${state.timeline.length}`;
  }

  function renderLog() {
    dom.log.innerHTML = '';
    state.timeline.forEach((row, idx) => {
      const li = document.createElement('li');
      if (idx === state.index) li.classList.add('active');
      li.dataset.index = String(idx);

      const color = ledHex(row.led);
      const motion = row.has_motion
        ? `x=${row.head_x_deg.toFixed(0)} y=${row.head_y_deg.toFixed(0)} ${row.duration_ms}ms`
        : `still ${row.duration_ms}ms`;

      li.innerHTML = `
        <span class="idx">${String(idx + 1).padStart(2, '0')}</span>
        <span class="ev">${row.source_event}</span>
        <span class="led" style="background:${color}">${row.led}</span>
        <span class="motion">${row.has_motion ? 'motion' : 'still'}</span>
        <span class="motion">${motion}</span>
        <span class="note">${row.note || ''}</span>
      `;
      li.addEventListener('click', () => jumpTo(idx));
      dom.log.appendChild(li);
    });
  }

  function refresh() {
    renderRow(state.timeline[state.index] || null);
    renderMeta();
    // Update active log row.
    [...dom.log.children].forEach((li, i) => {
      li.classList.toggle('active', i === state.index);
    });
    // Scrubber.
    dom.scrubber.max = String(Math.max(0, state.timeline.length - 1));
    dom.scrubber.value = String(Math.max(0, state.index));
  }

  // ── Playback ──────────────────────────────────────────────────────────

  function jumpTo(idx) {
    state.index = Math.max(0, Math.min(idx, state.timeline.length - 1));
    refresh();
  }

  function step(delta) {
    const next = state.index + delta;
    if (next < 0) {
      state.index = 0;
    } else if (next >= state.timeline.length) {
      state.index = state.timeline.length - 1;
      pause();
    } else {
      state.index = next;
    }
    refresh();
  }

  function play() {
    if (state.timeline.length === 0) return;
    if (state.index >= state.timeline.length - 1) state.index = -1;
    state.playing = true;
    dom.btnPlay.textContent = '⏸';
    scheduleNext();
  }

  function pause() {
    state.playing = false;
    dom.btnPlay.textContent = '▶';
    if (state.playTimer) {
      clearTimeout(state.playTimer);
      state.playTimer = null;
    }
  }

  function scheduleNext() {
    if (!state.playing) return;
    step(1);
    if (!state.playing) return;
    const row = state.timeline[state.index];
    const dur = row ? Math.max(row.duration_ms || 600, 350) : 600;
    const wait = state.speed === 0 ? 50 : Math.max(80, dur / state.speed);
    state.playTimer = setTimeout(scheduleNext, wait);
  }

  // ── Loaders ───────────────────────────────────────────────────────────

  function setTimeline(rows) {
    if (!Array.isArray(rows) || rows.length === 0) {
      alert('Timeline must be a non-empty JSON array of simulator rows.');
      return false;
    }
    // Sanity-check the first row's shape.
    const required = ['source_event', 'led', 'has_motion', 'head_x_deg', 'head_y_deg', 'duration_ms'];
    for (const key of required) {
      if (!(key in rows[0])) {
        alert(`Row 1 missing required field "${key}". Did you pass the output of \`pre-buddy simulate --format json\`?`);
        return false;
      }
    }
    pause();
    state.timeline = rows;
    state.index = 0;
    renderLog();
    refresh();
    return true;
  }

  async function loadFile(file) {
    try {
      const text = await file.text();
      const parsed = JSON.parse(text);
      setTimeline(parsed);
    } catch (err) {
      alert(`Failed to parse JSON: ${err.message}`);
    }
  }

  // ── Wire up ───────────────────────────────────────────────────────────

  dom.btnPrev.addEventListener('click', () => { pause(); step(-1); });
  dom.btnNext.addEventListener('click', () => { pause(); step(1); });
  dom.btnReset.addEventListener('click', () => { pause(); jumpTo(0); });
  dom.btnPlay.addEventListener('click', () => { state.playing ? pause() : play(); });

  dom.speedSel.addEventListener('change', () => {
    state.speed = parseFloat(dom.speedSel.value);
  });

  dom.scrubber.addEventListener('input', () => {
    pause();
    jumpTo(parseInt(dom.scrubber.value, 10));
  });

  dom.fileInput.addEventListener('change', () => {
    const f = dom.fileInput.files && dom.fileInput.files[0];
    if (f) loadFile(f);
  });

  dom.pasteToggle.addEventListener('click', () => {
    dom.pasteArea.hidden = !dom.pasteArea.hidden;
    if (!dom.pasteArea.hidden) dom.pasteArea.focus();
  });

  dom.pasteArea.addEventListener('keydown', (ev) => {
    if ((ev.metaKey || ev.ctrlKey) && ev.key === 'Enter') {
      try {
        const rows = JSON.parse(dom.pasteArea.value);
        if (setTimeline(rows)) dom.pasteArea.hidden = true;
      } catch (err) {
        alert(`Failed to parse JSON: ${err.message}`);
      }
    }
  });

  dom.loadDemo.addEventListener('click', () => setTimeline(DEMO_TIMELINE));

  // Keyboard: arrows for prev/next, space for play/pause.
  document.addEventListener('keydown', (ev) => {
    if (ev.target && (ev.target.tagName === 'TEXTAREA' || ev.target.tagName === 'INPUT')) return;
    if (ev.key === 'ArrowLeft')  { pause(); step(-1); }
    if (ev.key === 'ArrowRight') { pause(); step(1); }
    if (ev.key === ' ')          { ev.preventDefault(); state.playing ? pause() : play(); }
  });

  // Boot.
  state.index = 0;
  renderLog();
  refresh();
})();
