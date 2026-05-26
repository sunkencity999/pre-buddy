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
    { scenario_index: 1, source_event: 'pre.system.wake_word',     led: 'yellow', has_motion: true,  head_x_deg: -35, head_y_deg: 45, duration_ms: 350, note: 'turn_toward_mic',        expression: 'surprised', character: 'sprout', severity: 'normal' },
    { scenario_index: 2, source_event: 'pre.bg_agents.change',     led: 'green',  has_motion: false, head_x_deg: 0,   head_y_deg: 45, duration_ms: 350, note: 'agent_tier_pulse',        expression: 'thinking',  character: 'sprout', severity: 'normal' },
    { scenario_index: 3, source_event: 'pre.router.decision',      led: 'purple', has_motion: true,  head_x_deg: 0,   head_y_deg: 38, duration_ms: 550, note: 'router_escalation_nod',   expression: 'curious',   character: 'sprout', severity: 'normal' },
    { scenario_index: 4, source_event: 'pre.kg.delta',             led: 'cyan',   has_motion: false, head_x_deg: 0,   head_y_deg: 45, duration_ms: 350, note: 'kg_update',               expression: 'thinking',  character: 'sprout', severity: 'normal' },
    { scenario_index: 5, source_event: 'pre.tools.rollup',         led: 'white',  has_motion: false, head_x_deg: 0,   head_y_deg: 45, duration_ms: 350, note: 'tools_rollup',            expression: 'neutral',   character: 'sprout', severity: 'normal' },
    { scenario_index: 6, source_event: 'pre.system.proximity',     led: 'green',  has_motion: true,  head_x_deg: 0,   head_y_deg: 60, duration_ms: 700, note: 'look_up',                 expression: 'curious',   character: 'sprout', severity: 'normal' },
  ];

  // ── Face geometry ──────────────────────────────────────────────────────
  // Per-character identity (eye shape + size) and per-expression overlay
  // (mouth path, brow lift, eye squint). The viewBox is 100×80 so coords
  // here are in that space.

  // Eye geometry: {cx, cy, rx, ry} centers + radii for left/right eye.
  // Sage = round + steady. Sprout = larger + more spaced. Sentinel = narrow vertical slits.
  const CHARACTER_GEOM = {
    sage:     { eye: { rx: 5,  ry: 5,  cy: 36, dx: 18 }, browY: 22 },
    sprout:   { eye: { rx: 7,  ry: 6,  cy: 34, dx: 20 }, browY: 20 },
    sentinel: { eye: { rx: 2,  ry: 8,  cy: 36, dx: 18 }, browY: 22 },
  };

  // Per-expression overlay deltas applied on top of the character base.
  //   eyeDy:   shift eyes vertically (positive = down, for sleepy/concerned)
  //   eyeScale: multiplier on rx/ry; <1 squints, >1 widens
  //   browDy:  shift brows vertically (positive = lower / angrier)
  //   browAngle: degrees, positive = outer end up (worried), negative = down (angry)
  //   mouth:   svg path 'd' attribute
  const EXPRESSIONS = {
    //  Neutral / idle.
    neutral:   { eyeDy: 0,  eyeScale: 1.0, browDy: 0,  browAngle: 0,   mouth: 'M 38 60 Q 50 60 62 60' },
    //  Big eyes, raised brows, small "o" mouth.
    surprised: { eyeDy: -1, eyeScale: 1.2, browDy: -4, browAngle: 0,   mouth: 'M 46 60 Q 50 66 54 60 Q 50 54 46 60 Z' },
    //  Brows tilted up at the inner side (or just slightly higher), eyes
    //  glance up-left, mouth a soft flat line.
    thinking:  { eyeDy: -2, eyeScale: 0.9, browDy: -2, browAngle: 18,  mouth: 'M 40 60 L 60 60' },
    //  Brows drawn down + together, mouth a frown.
    concerned: { eyeDy: 1,  eyeScale: 0.85, browDy: 4, browAngle: -22, mouth: 'M 38 64 Q 50 56 62 64' },
    //  Squinted-happy eyes, big smile.
    happy:     { eyeDy: 0,  eyeScale: 0.6, browDy: -2, browAngle: 6,   mouth: 'M 38 58 Q 50 70 62 58' },
    //  Half-closed eyes (very thin ovals), neutral brows, faint smile.
    sleepy:    { eyeDy: 1,  eyeScale: 0.35, browDy: 2, browAngle: 0,   mouth: 'M 42 62 Q 50 64 58 62' },
    //  Wide-ish eyes, one brow raised (we'll just lift the brows
    //  slightly), small "hmm" mouth.
    curious:   { eyeDy: -1, eyeScale: 1.1, browDy: -3, browAngle: 10,  mouth: 'M 42 60 Q 50 62 58 60' },
    //  X eyes via crossed line groups (drawn separately), straight mouth.
    error:     { eyeDy: 0,  eyeScale: 1.0, browDy: -6, browAngle: -30, mouth: 'M 40 62 L 60 62', xEyes: true },
  };

  const SVG_NS = 'http://www.w3.org/2000/svg';

  function svg(name, attrs) {
    const el = document.createElementNS(SVG_NS, name);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v !== null && v !== undefined) el.setAttribute(k, String(v));
    }
    return el;
  }

  function renderFace(character, expression) {
    const root = document.getElementById('robot-face');
    if (!root) return;
    const c = (character || 'sage').toLowerCase();
    const e = (expression || 'neutral').toLowerCase();
    const geom = CHARACTER_GEOM[c] || CHARACTER_GEOM.sage;
    const overlay = EXPRESSIONS[e] || EXPRESSIONS.neutral;

    // Tag the root with the active character so CSS rules can branch.
    root.setAttribute('data-character', c);
    root.setAttribute('data-expression', e);

    // Wipe previous content and rebuild — simple and bug-resistant given
    // we only repaint when the active row changes (max ~once per second
    // on auto-play).
    while (root.firstChild) root.removeChild(root.firstChild);

    const eyeCy = geom.eye.cy + overlay.eyeDy;
    const eyeRx = geom.eye.rx * overlay.eyeScale;
    const eyeRy = geom.eye.ry * overlay.eyeScale;
    const cxL = 50 - geom.eye.dx;
    const cxR = 50 + geom.eye.dx;

    if (overlay.xEyes) {
      // Error state: draw X marks instead of eyes.
      for (const cx of [cxL, cxR]) {
        const s = 5;
        root.appendChild(svg('line', {
          x1: cx - s, y1: eyeCy - s, x2: cx + s, y2: eyeCy + s,
          stroke: '#0b0f17', 'stroke-width': 2.2, 'stroke-linecap': 'round',
        }));
        root.appendChild(svg('line', {
          x1: cx + s, y1: eyeCy - s, x2: cx - s, y2: eyeCy + s,
          stroke: '#0b0f17', 'stroke-width': 2.2, 'stroke-linecap': 'round',
        }));
      }
    } else {
      root.appendChild(svg('ellipse', {
        class: 'face-eye', cx: cxL, cy: eyeCy, rx: eyeRx, ry: eyeRy,
      }));
      root.appendChild(svg('ellipse', {
        class: 'face-eye', cx: cxR, cy: eyeCy, rx: eyeRx, ry: eyeRy,
      }));
    }

    // Brows — short lines above the eyes, optionally tilted.
    const browY = geom.browY + overlay.browDy;
    for (const [cx, sign] of [[cxL, +1], [cxR, -1]]) {
      const len = 9;
      const angle = overlay.browAngle * sign;
      const rad = (angle * Math.PI) / 180;
      const dx = Math.cos(rad) * (len / 2);
      const dy = Math.sin(rad) * (len / 2);
      root.appendChild(svg('line', {
        class: 'face-brow',
        x1: cx - dx, y1: browY - dy, x2: cx + dx, y2: browY + dy,
      }));
    }

    // Mouth.
    root.appendChild(svg('path', { class: 'face-mouth', d: overlay.mouth }));
  }

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
    rdFace:    $('rd-face'),
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
      renderFace('sage', 'neutral');
      return;
    }
    renderFace(row.character || 'sage', row.expression || 'neutral');

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
    if (dom.rdFace) dom.rdFace.textContent = `${row.character || 'sage'} · ${row.expression || 'neutral'}`;
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
