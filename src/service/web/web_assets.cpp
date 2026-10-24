#include "web_assets.h"

namespace prismix::web {

const std::string& get_web_ui_html() {
    static const std::string s_html = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>PRISMIX // MODEP-Inspired Pedalboard &amp; Live Mixer</title>
  <style>
    :root {
      --bg-dark: #121312;
      --chassis: #1e1f1d;
      --panel: #262724;
      --recessed: #0e0f0d;
      --border: #3c3d38;
      --border-highlight: #bfc2ba;
      --text-main: #d6d6d0;
      --text-muted: #8e8f89;
      --cyan: #38bdf8;
      --green: #22c55e;
      --red: #ef4444;
      --yellow: #eab308;
      --knob-bg: #2a2b27;
      --knob-ring: #555750;
      --knob-pointer: #d6d6d0;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace; }
    body { background: var(--bg-dark); color: var(--text-main); min-height: 100vh; display: flex; flex-direction: column; overflow-x: hidden; }
    
    /* Top Bar */
    header { background: var(--chassis); border-bottom: 2px solid var(--border); padding: 10px 20px; display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 10px; }
    .logo-area { display: flex; align-items: center; gap: 12px; }
    .logo-text { font-size: 16px; font-weight: 700; letter-spacing: 1.5px; color: var(--cyan); }
    .logo-sub { font-size: 11px; color: var(--text-muted); letter-spacing: 0.8px; }
    .status-badge { font-size: 11px; padding: 4px 8px; border-radius: 4px; background: #132b1d; color: var(--green); border: 1px solid #1f5e36; display: flex; align-items: center; gap: 6px; }
    .status-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--green); box-shadow: 0 0 6px var(--green); }
    .status-badge.offline { background: #331414; color: var(--red); border-color: #632222; }
    .status-badge.offline .status-dot { background: var(--red); box-shadow: 0 0 6px var(--red); }
    
    /* Channel Selector Bar */
    .channel-bar { background: var(--panel); border-bottom: 1px solid var(--border); padding: 8px 16px; display: flex; align-items: center; gap: 8px; overflow-x: auto; }
    .ch-btn { background: var(--chassis); border: 1px solid var(--border); color: var(--text-muted); padding: 6px 14px; border-radius: 4px; font-size: 12px; font-weight: 600; cursor: pointer; transition: all 0.15s ease; white-space: nowrap; }
    .ch-btn:hover { background: #32332e; color: var(--text-main); }
    .ch-btn.active { background: #183848; border-color: var(--cyan); color: #fff; }
    
    /* Channel Strip Quick Controls */
    .quick-strip { background: var(--chassis); border-bottom: 1px solid var(--border); padding: 12px 20px; display: flex; align-items: center; gap: 20px; flex-wrap: wrap; }
    .qs-item { display: flex; flex-direction: column; gap: 4px; font-size: 11px; color: var(--text-muted); font-weight: 600; }
    .qs-val { color: var(--text-main); font-size: 12px; }
    .fader-slider { -webkit-appearance: none; appearance: none; width: 140px; height: 6px; border-radius: 3px; background: var(--recessed); outline: none; border: 1px solid var(--border); }
    .fader-slider::-webkit-slider-thumb { -webkit-appearance: none; width: 16px; height: 22px; border-radius: 2px; background: #6b6d64; border: 1px solid #999b90; cursor: pointer; }
    .btn-toggle { background: var(--panel); border: 1px solid var(--border); color: var(--text-muted); padding: 6px 12px; border-radius: 3px; font-size: 11px; font-weight: 700; cursor: pointer; }
    .btn-toggle.mute-active { background: #7f1d1d; border-color: var(--red); color: #fee2e2; }
    .btn-toggle.solo-active { background: #713f12; border-color: var(--yellow); color: #fef08a; }
    .meter-wrap { display: flex; align-items: center; gap: 4px; height: 16px; background: var(--recessed); border: 1px solid var(--border); padding: 2px 4px; border-radius: 3px; width: 100px; }
    .meter-bar { height: 100%; background: linear-gradient(to right, #22c55e 60%, #eab308 85%, #ef4444 100%); width: 0%; border-radius: 1px; transition: width 0.08s ease; }
    
    /* Action Bar */
    .action-bar { padding: 12px 20px; display: flex; justify-content: space-between; align-items: center; gap: 10px; flex-wrap: wrap; }
    .btn-action { background: var(--panel); border: 1px solid var(--border); color: var(--text-main); padding: 8px 16px; border-radius: 4px; font-size: 12px; font-weight: 600; cursor: pointer; display: flex; align-items: center; gap: 6px; }
    .btn-action:hover { background: #353630; border-color: var(--border-highlight); }
    .btn-primary { background: #164e63; border-color: var(--cyan); color: #e0f2fe; }
    .btn-primary:hover { background: #0e7490; }
    
    /* Pedalboard Deck */
    main { flex: 1; padding: 20px; overflow-x: auto; background: radial-gradient(circle at center, #1a1b18 0%, #0d0e0c 100%); }
    .pedalboard { display: flex; gap: 24px; min-height: 480px; align-items: stretch; padding: 10px; border-radius: 8px; border: 2px solid #2f302a; background: #181916; box-shadow: inset 0 0 20px rgba(0,0,0,0.8); }
    
    /* Pedal Unit (Stompbox) */
    .pedal { width: 230px; flex-shrink: 0; background: #262723; border: 2px solid #4a4b44; border-radius: 12px; display: flex; flex-direction: column; padding: 16px; box-shadow: 0 10px 25px rgba(0,0,0,0.6), inset 0 1px 0 rgba(255,255,255,0.1); position: relative; }
    .pedal.cat-dynamics { border-color: #854d0e; }
    .pedal.cat-reverb, .pedal.cat-delay { border-color: #1e3a8a; }
    .pedal.cat-nam { border-color: #0e7490; }
    .pedal.cat-distortion { border-color: #b45309; }
    .pedal.bypassed { opacity: 0.75; filter: grayscale(40%); }
    
    .pedal-header { display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 12px; border-bottom: 1px solid #3d3e38; padding-bottom: 8px; }
    .pedal-title { font-size: 13px; font-weight: 700; letter-spacing: 0.5px; color: var(--text-main); }
    .pedal-cat { font-size: 9px; padding: 2px 6px; border-radius: 3px; background: #1b1c18; color: var(--text-muted); border: 1px solid #33342e; text-transform: uppercase; }
    .pedal-controls { display: flex; gap: 4px; }
    .pedal-ctrl-btn { background: none; border: 1px solid #44453e; color: var(--text-muted); width: 22px; height: 22px; border-radius: 3px; font-size: 10px; cursor: pointer; display: flex; align-items: center; justify-content: center; }
    .pedal-ctrl-btn:hover { background: #3d3e37; color: #fff; }
    
    /* Knobs Area */
    .knobs-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 14px 10px; margin: 12px 0 20px; flex: 1; }
    .knob-box { display: flex; flex-direction: column; align-items: center; gap: 6px; }
    .knob { width: 50px; height: 50px; border-radius: 50%; background: radial-gradient(circle at 35% 35%, #3a3b34, #1a1b18); border: 2px solid var(--knob-ring); position: relative; cursor: ns-resize; box-shadow: 0 4px 8px rgba(0,0,0,0.5); user-select: none; }
    .knob-indicator { position: absolute; width: 3px; height: 16px; background: var(--knob-pointer); top: 3px; left: 50%; transform: translateX(-50%); border-radius: 2px; }
    .knob-lbl { font-size: 10px; color: var(--text-muted); font-weight: 600; text-transform: uppercase; text-align: center; }
    .knob-val { font-size: 10px; color: var(--cyan); font-weight: 700; }
    
    /* Stomp Foot Switch & LED */
    .stomp-area { display: flex; flex-direction: column; align-items: center; gap: 10px; border-top: 1px solid #3a3b34; padding-top: 14px; margin-top: auto; }
    .jewel-led { width: 14px; height: 14px; border-radius: 50%; background: #3b1414; border: 2px solid #5a2020; box-shadow: inset 0 0 4px rgba(0,0,0,0.8); transition: all 0.15s ease; }
    .jewel-led.active { background: #22c55e; border-color: #86efac; box-shadow: 0 0 10px #22c55e, inset 0 0 2px #fff; }
    .stomp-switch { width: 56px; height: 56px; border-radius: 50%; background: radial-gradient(circle at 40% 40%, #c4c5be, #63655c); border: 4px solid #353630; box-shadow: 0 6px 12px rgba(0,0,0,0.6); cursor: pointer; transition: transform 0.08s ease; outline: none; }
    .stomp-switch:active { transform: scale(0.92); box-shadow: 0 2px 5px rgba(0,0,0,0.8); }
    
    /* Empty Dropzone */
    .empty-slot { width: 230px; border: 2px dashed #3a3b34; border-radius: 12px; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 12px; color: var(--text-muted); cursor: pointer; transition: all 0.2s ease; padding: 20px; }
    .empty-slot:hover { border-color: var(--cyan); color: var(--cyan); background: rgba(56, 189, 248, 0.04); }
    .plus-icon { font-size: 32px; font-weight: 300; }
    
    /* Modal / Drawer */
    .modal-overlay { position: fixed; inset: 0; background: rgba(0,0,0,0.75); display: none; align-items: center; justify-content: center; z-index: 100; backdrop-filter: blur(2px); }
    .modal-overlay.open { display: flex; }
    .modal-dialog { width: 90%; max-width: 760px; max-height: 80vh; background: var(--chassis); border: 2px solid var(--cyan); border-radius: 8px; display: flex; flex-direction: column; overflow: hidden; box-shadow: 0 20px 40px rgba(0,0,0,0.9); }
    .modal-hdr { padding: 14px 20px; background: var(--panel); border-bottom: 1px solid var(--border); display: flex; justify-content: space-between; align-items: center; }
    .modal-title { font-size: 14px; font-weight: 700; color: var(--cyan); letter-spacing: 0.5px; }
    .modal-close { background: none; border: none; color: var(--text-muted); font-size: 18px; cursor: pointer; }
    .modal-tabs { display: flex; background: #161714; border-bottom: 1px solid var(--border); }
    .modal-tab-btn { flex: 1; padding: 10px; background: none; border: none; border-bottom: 2px solid transparent; color: var(--text-muted); font-size: 12px; font-weight: 600; cursor: pointer; }
    .modal-tab-btn.active { color: var(--cyan); border-bottom-color: var(--cyan); background: rgba(56, 189, 248, 0.06); }
    .modal-body { padding: 16px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 8px; }
    .catalog-item { background: var(--panel); border: 1px solid var(--border); padding: 12px 16px; border-radius: 4px; display: flex; justify-content: space-between; align-items: center; gap: 10px; cursor: pointer; }
    .catalog-item:hover { border-color: var(--cyan); background: #2f302b; }
    .ci-meta { display: flex; flex-direction: column; gap: 4px; }
    .ci-name { font-size: 13px; font-weight: 700; color: var(--text-main); }
    .ci-desc { font-size: 11px; color: var(--text-muted); }
    
    /* Preset Modal */
    .input-field { background: var(--recessed); border: 1px solid var(--border); color: var(--text-main); padding: 8px 12px; border-radius: 4px; font-size: 13px; width: 100%; margin-top: 6px; }
    .input-field:focus { outline: none; border-color: var(--cyan); }
  </style>
</head>
<body>

  <header>
    <div class="logo-area">
      <div>
        <div class="logo-text">PRISMIX // PEDALBOARD STUDIO</div>
        <div class="logo-sub">MODEP-INSPIRED LIVE LV2 &amp; NAM DSP ENVIRONMENT</div>
      </div>
    </div>
    <div style="display:flex; align-items:center; gap:16px;">
      <div id="engine-status" style="font-size:11px; color:var(--text-muted);">ENGINE: 48 kHz / 128 frames</div>
      <div id="conn-badge" class="status-badge">
        <div class="status-dot"></div>
        <span id="conn-text">LIVE SYNCED</span>
      </div>
    </div>
  </header>

  <div class="channel-bar" id="channel-tabs">
    <!-- Populated dynamically: CH 01, CH 02, AUX 1..4, Master -->
  </div>

  <div class="quick-strip" id="quick-strip">
    <div class="qs-item">
      <span>FADER</span>
      <div style="display:flex; align-items:center; gap:8px;">
        <input type="range" min="0" max="1" step="0.01" class="fader-slider" id="ch-fader">
        <span class="qs-val" id="ch-fader-val">0.0 dB</span>
      </div>
    </div>
    <div class="qs-item">
      <span>PAN</span>
      <div style="display:flex; align-items:center; gap:8px;">
        <input type="range" min="0" max="1" step="0.02" class="fader-slider" style="width:80px;" id="ch-pan">
        <span class="qs-val" id="ch-pan-val">C</span>
      </div>
    </div>
    <div class="qs-item" style="flex-direction:row; gap:8px; align-items:center;">
      <button class="btn-toggle" id="ch-mute">MUTE</button>
      <button class="btn-toggle" id="ch-solo">SOLO</button>
    </div>
    <div class="qs-item">
      <span>POST-FADER PEAK</span>
      <div class="meter-wrap">
        <div class="meter-bar" id="ch-meter"></div>
      </div>
    </div>
  </div>

  <div class="action-bar">
    <div style="display:flex; gap:10px;">
      <button class="btn-action btn-primary" id="open-add-plugin">+ ADD PLUGIN</button>
      <button class="btn-action" id="open-presets">PRESETS</button>
      <button class="btn-action" id="save-preset-btn">CAPTURE PRESET</button>
    </div>
    <div style="font-size:12px; color:var(--text-muted);" id="rack-summary">0 / 8 SLOTS ASSIGNED</div>
  </div>

  <main>
    <div class="pedalboard" id="pedalboard-container">
      <!-- Pedals rendered dynamically -->
    </div>
  </main>

  <!-- Add Plugin Modal -->
  <div class="modal-overlay" id="add-modal">
    <div class="modal-dialog">
      <div class="modal-hdr">
        <div class="modal-title">ADD PROCESSOR TO PEDALBOARD</div>
        <button class="modal-close" onclick="closeModal('add-modal')">&times;</button>
      </div>
      <div class="modal-tabs">
        <button class="modal-tab-btn active" onclick="switchCatalogTab('all')">ALL</button>
        <button class="modal-tab-btn" onclick="switchCatalogTab('builtin')">BUILT-IN DSP</button>
        <button class="modal-tab-btn" onclick="switchCatalogTab('lv2')">LV2 PLUGINS</button>
        <button class="modal-tab-btn" onclick="switchCatalogTab('nam')">NAM MODELS</button>
      </div>
      <div class="modal-body" id="catalog-list"></div>
    </div>
  </div>

  <!-- Presets Modal -->
  <div class="modal-overlay" id="presets-modal">
    <div class="modal-dialog">
      <div class="modal-hdr">
        <div class="modal-title">PEDALBOARD CHAIN PRESETS</div>
        <button class="modal-close" onclick="closeModal('presets-modal')">&times;</button>
      </div>
      <div class="modal-body" id="presets-list"></div>
    </div>
  </div>

  <!-- Save Preset Modal -->
  <div class="modal-overlay" id="save-modal">
    <div class="modal-dialog" style="max-width:440px;">
      <div class="modal-hdr">
        <div class="modal-title">SAVE PEDALBOARD PRESET</div>
        <button class="modal-close" onclick="closeModal('save-modal')">&times;</button>
      </div>
      <div class="modal-body" style="gap:14px;">
        <div>
          <label style="font-size:11px; font-weight:700; color:var(--text-muted);">PRESET NAME</label>
          <input type="text" class="input-field" id="preset-save-name" placeholder="e.g. Lead Solo Rig">
        </div>
        <div>
          <label style="font-size:11px; font-weight:700; color:var(--text-muted);">AUTHOR</label>
          <input type="text" class="input-field" id="preset-save-author" placeholder="PRISMIX Artist">
        </div>
        <div>
          <label style="font-size:11px; font-weight:700; color:var(--text-muted);">DESCRIPTION</label>
          <input type="text" class="input-field" id="preset-save-desc" placeholder="Overdrive into British Lead and Ambient Reverb">
        </div>
        <div style="display:flex; justify-content:flex-end; gap:8px; margin-top:8px;">
          <button class="btn-action" onclick="closeModal('save-modal')">CANCEL</button>
          <button class="btn-action btn-primary" id="confirm-save-preset">SAVE PRESET</button>
        </div>
      </div>
    </div>
  </div>

  <script>
    let gState = null;
    let gCatalog = null;
    let gSelectedChannelId = 1;
    let gCatalogFilter = 'all';

    function faderDb(val) {
      if (val <= 0.86) return (-60.0 + val * (60.0 / 0.86)).toFixed(1);
      return ((val - 0.86) * (10.0 / 0.14)).toFixed(1);
    }

    async function sendCommand(cmd) {
      try {
        const res = await fetch('/api/command', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(cmd)
        });
        const data = await res.json();
        if (data && data.success) {
          fetchState();
        } else if (data && data.error) {
          console.warn('Command error:', data.error);
        }
      } catch (err) {
        console.error('Network command failed:', err);
      }
    }

    async function fetchState() {
      try {
        const res = await fetch('/api/state');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        gState = await res.json();
        renderUI();
        setConnected(true);
      } catch (err) {
        setConnected(false);
      }
    }

    async function fetchCatalog() {
      try {
        const res = await fetch('/api/catalog');
        if (res.ok) gCatalog = await res.json();
      } catch (err) {
        console.warn('Failed to load catalog:', err);
      }
    }

    function setConnected(online) {
      const b = document.getElementById('conn-badge');
      const t = document.getElementById('conn-text');
      if (online) {
        b.className = 'status-badge';
        t.innerText = 'LIVE SYNCED';
      } else {
        b.className = 'status-badge offline';
        t.innerText = 'DISCONNECTED';
      }
    }

    function initSSE() {
      const es = new EventSource('/api/events');
      es.onmessage = (e) => {
        try {
          const d = JSON.parse(e.data);
          if (d && d.version && (!gState || d.version !== gState.version)) {
            fetchState();
          }
        } catch (err) {}
      };
      es.onerror = () => {
        setConnected(false);
        setTimeout(fetchState, 3000);
      };
      es.onopen = () => setConnected(true);
    }

    function renderUI() {
      if (!gState) return;
      renderChannelTabs();
      renderQuickStrip();
      renderPedalboard();
    }

    function renderChannelTabs() {
      const bar = document.getElementById('channel-tabs');
      bar.innerHTML = '';
      if (!gState.channels) return;

      gState.channels.forEach(ch => {
        const b = document.createElement('button');
        b.className = 'ch-btn' + (ch.id === gSelectedChannelId ? ' active' : '');
        b.innerText = `CH ${String(ch.id).padStart(2,'0')} : ${ch.name}`;
        b.onclick = () => { gSelectedChannelId = ch.id; renderUI(); };
        bar.appendChild(b);
      });

      if (gState.aux_buses) {
        gState.aux_buses.forEach(aux => {
          const b = document.createElement('button');
          b.className = 'ch-btn' + (aux.id === gSelectedChannelId ? ' active' : '');
          b.innerText = `${aux.name}`;
          b.onclick = () => { gSelectedChannelId = aux.id; renderUI(); };
          bar.appendChild(b);
        });
      }
    }

    function getSelectedChannel() {
      if (!gState) return null;
      if (gSelectedChannelId >= 101 && gSelectedChannelId <= 104) {
        return gState.aux_buses ? gState.aux_buses.find(a => a.id === gSelectedChannelId) : null;
      }
      return gState.channels ? gState.channels.find(c => c.id === gSelectedChannelId) : null;
    }

    function renderQuickStrip() {
      const ch = getSelectedChannel();
      if (!ch) return;

      const fader = document.getElementById('ch-fader');
      const faderVal = document.getElementById('ch-fader-val');
      fader.value = ch.fader;
      faderVal.innerText = faderDb(ch.fader) + ' dB';
      fader.oninput = (e) => {
        faderVal.innerText = faderDb(parseFloat(e.target.value)) + ' dB';
        sendCommand({ action: 'set_fader', channel: ch.id, value: parseFloat(e.target.value) });
      };

      const pan = document.getElementById('ch-pan');
      const panVal = document.getElementById('ch-pan-val');
      pan.value = ch.pan;
      const p = (ch.pan - 0.5) * 2;
      panVal.innerText = Math.abs(p) < 0.05 ? 'C' : (p < 0 ? `L ${Math.round(Math.abs(p)*100)}` : `R ${Math.round(p*100)}`);
      pan.oninput = (e) => {
        sendCommand({ action: 'set_pan', channel: ch.id, value: parseFloat(e.target.value) });
      };

      const mute = document.getElementById('ch-mute');
      mute.className = 'btn-toggle' + (ch.mute ? ' mute-active' : '');
      mute.onclick = () => {
        sendCommand({ action: 'set_mute', channel: ch.id, value: !ch.mute });
      };

      const solo = document.getElementById('ch-solo');
      solo.className = 'btn-toggle' + (ch.solo ? ' solo-active' : '');
      solo.onclick = () => {
        sendCommand({ action: 'set_solo', channel: ch.id, value: !ch.solo });
      };

      const meter = document.getElementById('ch-meter');
      if (meter && ch.peak_l !== undefined) {
        const peak = Math.max(ch.peak_l, ch.peak_r || ch.peak_l);
        const norm = Math.max(0, Math.min(100, (peak + 60) * (100 / 60)));
        meter.style.width = norm + '%';
      }
    }

    function renderPedalboard() {
      const container = document.getElementById('pedalboard-container');
      container.innerHTML = '';
      const ch = getSelectedChannel();
      if (!ch) return;

      const rack = ch.fx_rack || [];
      document.getElementById('rack-summary').innerText = `${rack.length} / 8 SLOTS ASSIGNED`;

      rack.forEach((slot, idx) => {
        const p = document.createElement('div');
        let catClass = 'cat-dynamics';
        const catLower = (slot.category || '').toLowerCase();
        if (catLower.includes('reverb')) catClass = 'cat-reverb';
        else if (catLower.includes('delay')) catClass = 'cat-delay';
        else if (catLower.includes('nam') || slot.source_type === 2) catClass = 'cat-nam';
        else if (catLower.includes('distortion') || catLower.includes('overdrive')) catClass = 'cat-distortion';

        p.className = `pedal ${catClass}` + (slot.bypassed ? ' bypassed' : '');
        
        // Header
        const hdr = document.createElement('div');
        hdr.className = 'pedal-header';
        hdr.innerHTML = `
          <div>
            <div class="pedal-title">${escapeHtml(slot.name)}</div>
            <span class="pedal-cat">${escapeHtml(slot.category || 'DSP')}</span>
          </div>
          <div class="pedal-controls">
            ${idx > 0 ? `<button class="pedal-ctrl-btn" onclick="moveSlot(${ch.id}, ${idx}, ${idx - 1})">&lt;</button>` : ''}
            ${idx < rack.length - 1 ? `<button class="pedal-ctrl-btn" onclick="moveSlot(${ch.id}, ${idx}, ${idx + 1})">&gt;</button>` : ''}
            <button class="pedal-ctrl-btn" onclick="removeSlot(${ch.id}, ${idx})" style="color:var(--red);">&times;</button>
          </div>
        `;
        p.appendChild(hdr);

        // Knobs
        const knobs = document.createElement('div');
        knobs.className = 'knobs-grid';
        (slot.parameters || []).slice(0, 6).forEach(param => {
          const kb = document.createElement('div');
          kb.className = 'knob-box';
          
          const norm = (param.max > param.min) ? (param.value - param.min) / (param.max - param.min) : 0.5;
          const deg = -135 + norm * 270;

          kb.innerHTML = `
            <div class="knob" id="knob-${slot.slot}-${param.id}" data-min="${param.min}" data-max="${param.max}" data-val="${param.value}">
              <div class="knob-indicator" style="transform: translateX(-50%) rotate(${deg}deg); transform-origin: center 22px;"></div>
            </div>
            <div class="knob-lbl">${escapeHtml(param.name)}</div>
            <div class="knob-val">${param.value.toFixed(1)} ${escapeHtml(param.unit || '')}</div>
          `;

          // Interactive knob drag
          const knobEl = kb.querySelector('.knob');
          setupKnobInteraction(knobEl, ch.id, slot.slot, param);
          knobs.appendChild(kb);
        });
        p.appendChild(knobs);

        // Stomp Area
        const stomp = document.createElement('div');
        stomp.className = 'stomp-area';
        stomp.innerHTML = `
          <div class="jewel-led ${slot.bypassed ? '' : 'active'}"></div>
          <button class="stomp-switch" onclick="toggleBypass(${ch.id}, ${slot.slot}, ${!slot.bypassed})"></button>
          <span style="font-size:10px; font-weight:700; color:var(--text-muted);">${slot.bypassed ? 'BYPASSED' : 'ACTIVE'}</span>
        `;
        p.appendChild(stomp);

        container.appendChild(p);
      });

      // Add Slot Button if < 8
      if (rack.length < 8) {
        const addBtn = document.createElement('div');
        addBtn.className = 'empty-slot';
        addBtn.innerHTML = `
          <div class="plus-icon">+</div>
          <div style="font-size:12px; font-weight:600;">ADD EFFECT PEDAL</div>
        `;
        addBtn.onclick = () => openAddPluginModal();
        container.appendChild(addBtn);
      }
    }

    function setupKnobInteraction(el, chId, slotIdx, param) {
      let startY = 0;
      let startVal = param.value;

      const onMouseMove = (e) => {
        const delta = startY - e.clientY;
        const range = param.max - param.min;
        const step = (range / 150) * delta;
        let newVal = Math.max(param.min, Math.min(param.max, startVal + step));
        if (param.step && param.step > 0) {
          newVal = Math.round(newVal / param.step) * param.step;
        }

        const norm = (newVal - param.min) / (param.max - param.min);
        const deg = -135 + norm * 270;
        const ind = el.querySelector('.knob-indicator');
        if (ind) ind.style.transform = `translateX(-50%) rotate(${deg}deg)`;
        const valEl = el.parentElement.querySelector('.knob-val');
        if (valEl) valEl.innerText = newVal.toFixed(1) + ' ' + (param.unit || '');

        sendCommand({
          action: 'set_plugin_param',
          channel: chId,
          slot: slotIdx,
          param_id: param.id,
          value: newVal
        });
      };

      const onMouseUp = () => {
        window.removeEventListener('mousemove', onMouseMove);
        window.removeEventListener('mouseup', onMouseUp);
      };

      el.addEventListener('mousedown', (e) => {
        startY = e.clientY;
        startVal = param.value;
        window.addEventListener('mousemove', onMouseMove);
        window.addEventListener('mouseup', onMouseUp);
      });
    }

    function toggleBypass(chId, slotIdx, bypassVal) {
      sendCommand({ action: 'set_slot_bypass', channel: chId, slot: slotIdx, value: bypassVal });
    }

    function moveSlot(chId, fromIdx, toIdx) {
      sendCommand({ action: 'move_plugin', channel: chId, slot: fromIdx, to_slot: toIdx });
    }

    function removeSlot(chId, slotIdx) {
      sendCommand({ action: 'remove_plugin', channel: chId, slot: slotIdx });
    }

    function openAddPluginModal() {
      renderCatalogList();
      document.getElementById('add-modal').classList.add('open');
    }

    function switchCatalogTab(tab) {
      gCatalogFilter = tab;
      document.querySelectorAll('.modal-tabs .modal-tab-btn').forEach(btn => btn.classList.remove('active'));
      event.target.classList.add('active');
      renderCatalogList();
    }

    function renderCatalogList() {
      const list = document.getElementById('catalog-list');
      list.innerHTML = '';
      if (!gCatalog || !gCatalog.plugins) return;

      const filtered = gCatalog.plugins.filter(p => {
        if (gCatalogFilter === 'builtin') return p.source_type === 0;
        if (gCatalogFilter === 'lv2') return p.source_type === 1;
        if (gCatalogFilter === 'nam') return p.source_type === 2;
        return true;
      });

      filtered.forEach(p => {
        const item = document.createElement('div');
        item.className = 'catalog-item';
        item.innerHTML = `
          <div class="ci-meta">
            <div class="ci-name">${escapeHtml(p.name)}</div>
            <div class="ci-desc">${escapeHtml(p.category)} &bull; ${escapeHtml(p.author || 'AudioCore')}</div>
          </div>
          <button class="btn-action btn-primary" style="padding:4px 10px; font-size:11px;">+ ADD</button>
        `;
        item.onclick = () => {
          sendCommand({ action: 'add_plugin', channel: gSelectedChannelId, uri: p.uri });
          closeModal('add-modal');
        };
        list.appendChild(item);
      });
    }

    function openPresetsModal() {
      const list = document.getElementById('presets-list');
      list.innerHTML = '';
      if (!gCatalog || !gCatalog.presets) return;

      gCatalog.presets.forEach(pr => {
        const item = document.createElement('div');
        item.className = 'catalog-item';
        item.innerHTML = `
          <div class="ci-meta">
            <div class="ci-name">${escapeHtml(pr.name)}</div>
            <div class="ci-desc">${escapeHtml(pr.description || '')} (${pr.slots_count} units)</div>
          </div>
          <button class="btn-action btn-primary" style="padding:4px 10px; font-size:11px;">LOAD</button>
        `;
        item.onclick = () => {
          sendCommand({ action: 'apply_preset', channel: gSelectedChannelId, preset: pr.name });
          closeModal('presets-modal');
        };
        list.appendChild(item);
      });

      document.getElementById('presets-modal').classList.add('open');
    }

    function closeModal(id) {
      document.getElementById(id).classList.remove('open');
    }

    function escapeHtml(str) {
      return (str || '').replace(/[&<>'"]/g, tag => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;'
      }[tag] || tag));
    }

    // Modal Triggers
    document.getElementById('open-add-plugin').onclick = openAddPluginModal;
    document.getElementById('open-presets').onclick = openPresetsModal;
    document.getElementById('save-preset-btn').onclick = () => {
      document.getElementById('save-modal').classList.add('open');
    };
    document.getElementById('confirm-save-preset').onclick = () => {
      const name = document.getElementById('preset-save-name').value.trim();
      const author = document.getElementById('preset-save-author').value.trim();
      const desc = document.getElementById('preset-save-desc').value.trim();
      if (!name) { alert('Please enter a preset name'); return; }
      sendCommand({
        action: 'save_preset',
        channel: gSelectedChannelId,
        preset: name,
        author: author,
        description: desc
      });
      closeModal('save-modal');
    };

    // Initial Load
    fetchState();
    fetchCatalog();
    initSSE();
    setInterval(fetchState, 1000); // Heartbeat sync
  </script>
</body>
</html>
)rawhtml";
    return s_html;
}

} // namespace prismix::web

