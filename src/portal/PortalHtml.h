#pragma once

namespace smartrc {

// Single-page portal: control buttons + collapsible config form.
// Pure HTML/JS, no external assets — talks to /api/* on the same origin.
inline const char PORTAL_HTML[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart RC</title>
<style>
  body { font-family: system-ui, sans-serif; max-width: 520px; margin: 1em auto;
         padding: 0 1em; color: #222; }
  h1 { font-size: 1.4em; }
  fieldset { margin: 1em 0; padding: 0.6em 1em; border-radius: 8px;
             border: 1px solid #ccc; }
  label { display: block; margin: 0.4em 0 0.1em; font-size: 0.9em; }
  input, select { width: 100%; padding: 0.4em; box-sizing: border-box; }
  .pad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px;
         margin-bottom: 1em; }
  .pad button { padding: 1em; font-size: 1em; }
  .pad .estop { background: #c33; color: white; border: none; }
  .pad .blank { visibility: hidden; }
  .row { display: flex; gap: 6px; }
  .row > * { flex: 1; }
  pre { background: #f4f4f4; padding: 0.6em; border-radius: 6px;
        font-size: 0.8em; overflow-x: auto; }
  #wifiList { font-family: monospace; }
  #wifiStatus { font-size: 0.9em; padding: 0.3em 0; min-height: 1.2em; }
  #wifiStatus.ok  { color: #1a7f37; }
  #wifiStatus.err { color: #c33; }
  .pad button.held { background: #2d72d9; color: white; transform: scale(0.97); }
  .pad button { touch-action: manipulation; user-select: none; -webkit-user-select: none; }
  .invert-row { display: flex; gap: 1em; margin-top: 0.4em; font-size: 0.9em; }
  .invert-row label { display: inline-flex; align-items: center; gap: 0.3em; margin: 0; }
  .invert-row input { width: auto; }
  #wsBadge { display:inline-block; padding:2px 6px; border-radius:4px;
             font-size:0.8em; margin-left:0.5em; }
  #wsBadge.on  { background:#d8f5e0; color:#176a2e; }
  #wsBadge.off { background:#f9d6d6; color:#a11212; }
</style>
</head>
<body>
<h1>Smart RC <span id="wsBadge" class="off">WS off</span> <span id="txRate" style="font-size:0.6em;color:#666"></span></h1>
<div id="status"><pre>loading...</pre></div>

<fieldset>
  <legend>Control <small style="font-weight:normal;color:#666">— press &amp; hold</small></legend>
  <div class="pad">
    <span class="blank"></span>
    <button id="btnFwd">▲ Fwd</button>
    <span class="blank"></span>
    <button id="btnLeft">◀ Left</button>
    <button id="btnStop" onclick="cmd('stop')">■ Stop</button>
    <button id="btnRight">▶ Right</button>
    <span class="blank"></span>
    <button id="btnRev">▼ Rev</button>
    <span class="blank"></span>
  </div>
  <button class="estop" style="width:100%;padding:1em" onclick="cmd('estop')">EMERGENCY STOP</button>
  <div class="row" style="margin-top:6px">
    <button onclick="cmd('clear_estop')">Clear E-Stop</button>
    <button onclick="cmd('brake')">Brake</button>
  </div>
</fieldset>

<fieldset>
  <legend>Wi-Fi setup</legend>
  <div class="row">
    <button type="button" onclick="scanWifi()">🔍 Scan networks</button>
  </div>
  <select id="wifiList" size="6" style="margin-top:0.4em" onchange="pickAp()">
    <option disabled>— click Scan to list nearby networks —</option>
  </select>
  <label>SSID</label><input id="provSsid" autocomplete="off">
  <label>Password</label><input id="provPass" type="password" autocomplete="off">
  <button type="button" onclick="provision()"
          style="width:100%;margin-top:0.4em;padding:0.7em;font-weight:600">
    Connect &amp; save
  </button>
  <div id="wifiStatus"></div>
</fieldset>

<fieldset>
  <legend>Configuration</legend>
  <form id="cfg">
    <label>Wi-Fi SSID</label><input name="wifiSsid">
    <label>Wi-Fi password</label><input name="wifiPassword" type="password">
    <label>AP SSID</label><input name="apSsid">
    <label>AP password</label><input name="apPassword" type="password">
    <label>Hostname</label><input name="hostname">
    <label>IP mode</label>
    <select name="useDhcp">
      <option value="true">DHCP</option>
      <option value="false">Static</option>
    </select>
    <label>Static IP</label><input name="staticIp">
    <label>Gateway</label><input name="staticGateway">
    <label>Subnet</label><input name="staticSubnet">
    <label>DNS</label><input name="staticDns">
    <label>Steering pulse (ms)</label><input name="steeringPulseMs" type="number" min="20" max="2000">
    <label>Steering cooldown (ms)</label><input name="steeringCooldownMs" type="number" min="0" max="2000">
    <label>Default drive PWM (0-255)</label><input name="defaultDrivePwm" type="number" min="0" max="255">
    <label>Default steering PWM (0-255)</label><input name="defaultSteerPwm" type="number" min="0" max="255">
    <label>Active brake PWM (0-255)</label><input name="activeBrakePwm" type="number" min="0" max="255">
    <label>Active brake max duration (ms)</label><input name="activeBrakeMaxMs" type="number" min="100" max="5000">
    <label>Heartbeat timeout (ms)</label><input name="heartbeatTimeoutMs" type="number" min="50" max="60000">
    <div class="invert-row">
      <label><input type="checkbox" name="driveInverted"> Invert drive direction</label>
      <label><input type="checkbox" name="steerInverted"> Invert steering direction</label>
    </div>
    <div class="invert-row">
      <label><input type="checkbox" name="imuInvertX"> Invert IMU X (fwd/back)</label>
      <label><input type="checkbox" name="imuInvertY"> Invert IMU Y (left/right)</label>
      <label><input type="checkbox" name="imuInvertZ"> Invert IMU Z (up/down)</label>
    </div>
    <div class="invert-row">
      <label><input type="checkbox" name="apShutdownAfterProvision">
        Close AP hotspot after successful Wi-Fi provision
      </label>
    </div>
    <div class="row" style="margin-top:0.6em">
      <button type="button" onclick="saveCfg()">Save</button>
      <button type="button" onclick="reboot()">Reboot</button>
    </div>
  </form>
  <div class="row" style="margin-top:0.6em">
    <button onclick="resetNet()">Network Reset</button>
    <button onclick="factoryReset()" style="background:#c33;color:#fff">Factory Reset</button>
  </div>
</fieldset>

<script>
// ---------------------------------------------------------------------------
// WebSocket transport (Phase 2+). Opens on load, resubscribes telemetry on
// reconnect, auto-falls-back to HTTP if it can't connect. Every helper is
// designed so the UI works identically whether WS is up or down.
// ---------------------------------------------------------------------------
let ws = null;
let wsReady = false;
let wsRetryMs = 500;         // exponential backoff on reconnect
let cmdId = 0;

function setWsBadge(on, extra) {
  const el = document.getElementById('wsBadge');
  el.className = on ? 'on' : 'off';
  el.textContent = on ? ('WS ' + (extra||'on')) : 'WS off';
}

function openWs() {
  try {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${proto}//${location.host}/ws`);
  } catch (e) { setWsBadge(false); scheduleReconnect(); return; }

  ws.addEventListener('open', () => {
    wsReady = true; wsRetryMs = 500;
    setWsBadge(true, '…');
    // Handshake (no token for default config). Server will respond with
    // its own hello frame including proto/features.
    ws.send(JSON.stringify({t:'hello'}));
    // Subscribe to telemetry stream.
    ws.send(JSON.stringify({t:'sub', streams:['telemetry'], hz:10}));
  });

  ws.addEventListener('message', e => {
    let msg; try { msg = JSON.parse(e.data); } catch { return; }
    onWsMessage(msg);
  });

  ws.addEventListener('close', () => {
    wsReady = false;
    setWsBadge(false);
    scheduleReconnect();
  });

  ws.addEventListener('error', () => {/* close will follow */});
}

function scheduleReconnect() {
  setTimeout(openWs, wsRetryMs);
  wsRetryMs = Math.min(wsRetryMs * 2, 8000);
}

function onWsMessage(msg) {
  if (!msg || !msg.t) return;
  switch (msg.t) {
    case 'hello':
      setWsBadge(true, `proto ${msg.proto}`);
      break;
    case 'telemetry':
      renderStatusFromWs(msg);
      break;
    case 'event':
      // Flash the status pane briefly on edge events.
      console.log('[event]', msg.kind, msg);
      break;
    case 'ack':
      // fire-and-forget, ignore
      break;
    case 'err':
      console.warn('[ws err]', msg);
      break;
  }
}

function renderStatusFromWs(msg) {
  // Decorate a friendly view — mirrors what /api/status returns for the
  // shared fields, plus the WS timestamp for sanity-checking freshness.
  const view = {
    uptime_ms: msg.ts,
    motors:  msg.drive && msg.steer ? {
        drive_moving:   msg.drive.moving,
        steer_state:    msg.steer.state,
        steer_last_dir: msg.steer.lastDir,
      } : undefined,
    safety:  msg.safety,
    net:     msg.net,
    _via: 'ws',
  };
  document.querySelector('#status pre').textContent = JSON.stringify(view, null, 2);
}

// Rolling TX counter — lets us see at a glance whether commands are
// actually leaving the browser. If motors drop mid-hold but this counter
// stays solid, the issue is firmware/Wi-Fi side; if it drops, it's
// something killing the JS timer (wake lock / tab throttling / event bug).
let txCount = 0;
setInterval(() => {
  const el = document.getElementById('txRate');
  if (el) el.textContent = txCount ? (txCount + '/s tx') : '';
  txCount = 0;
}, 1000);

// Fire-and-forget command. Prefers WS (single-digit-ms latency); falls
// back to HTTP POST when the socket is down. Both paths land in the same
// CommandHandler::execute() chokepoint on the device.
function cmd(action, speed) {
  txCount++;
  if (wsReady && ws && ws.readyState === WebSocket.OPEN) {
    const payload = {t:'cmd', action, id: ++cmdId};
    if (speed) payload.speed = speed;
    ws.send(JSON.stringify(payload));
    return Promise.resolve({ok:true, via:'ws'});
  }
  const body = JSON.stringify(speed ? {action, speed} : {action});
  return fetch('/api/control', {method:'POST',
    headers:{'Content-Type':'application/json'}, body}).catch(() => {});
}

// ---------------------------------------------------------------------------
// Wake lock — keep the screen on while ANY control button is held. Without
// this, mobile browsers throttle setInterval when the display dims and
// commands stop being sent → motor drops out until display wakes.
// ---------------------------------------------------------------------------
let wakeLock = null;
let activeHolds = 0;
async function acquireWakeLock() {
  activeHolds++;
  if (wakeLock || !('wakeLock' in navigator)) return;
  try { wakeLock = await navigator.wakeLock.request('screen'); }
  catch (e) { /* no-op — permission denied or unsupported */ }
}
function maybeReleaseWakeLock() {
  activeHolds = Math.max(0, activeHolds - 1);
  if (activeHolds === 0 && wakeLock) {
    wakeLock.release().catch(() => {});
    wakeLock = null;
  }
}

// ---------------------------------------------------------------------------
// Press-and-hold: resend `action` at `repeatMs` while the button is held.
// Uses Pointer Events + setPointerCapture so a drifting cursor / wobbling
// finger CANNOT unbind the hold — the button "owns" the pointer until
// pointerup/pointercancel. This fixes the "motor drops for a split second
// even though I'm still holding" class of bug.
// ---------------------------------------------------------------------------
function bindHold(btn, action, opts) {
  opts = opts || {};
  const repeatMs      = opts.repeatMs      || 200;
  const releaseAction = opts.releaseAction || null;
  let timer = null;
  let capturedPointerId = null;

  const start = e => {
    if (e && e.preventDefault) e.preventDefault();
    if (timer) return;
    if (e && typeof e.pointerId === 'number' && btn.setPointerCapture) {
      try { btn.setPointerCapture(e.pointerId); capturedPointerId = e.pointerId; }
      catch (_) { /* ignore — some browsers refuse capture on primary click */ }
    }
    cmd(action);
    timer = setInterval(() => cmd(action), repeatMs);
    btn.classList.add('held');
    acquireWakeLock();
  };
  const end = () => {
    if (!timer) return;
    if (capturedPointerId !== null && btn.releasePointerCapture) {
      try { btn.releasePointerCapture(capturedPointerId); } catch (_) {}
      capturedPointerId = null;
    }
    clearInterval(timer); timer = null;
    btn.classList.remove('held');
    if (releaseAction) cmd(releaseAction);
    maybeReleaseWakeLock();
  };

  if ('PointerEvent' in window) {
    btn.addEventListener('pointerdown',        start);
    btn.addEventListener('pointerup',          end);
    btn.addEventListener('pointercancel',      end);
    btn.addEventListener('lostpointercapture', end);
    // Note: we intentionally do NOT listen for pointerleave — setPointerCapture
    // keeps events attached to btn even when the pointer moves away.
  } else {
    // Legacy fallback (very old browsers). No `mouseleave` here either —
    // trade a stuck-throttle edge case for no drift-kill in the common case.
    btn.addEventListener('mousedown',   start);
    btn.addEventListener('touchstart',  start, {passive:false});
    btn.addEventListener('mouseup',     end);
    btn.addEventListener('touchend',    end);
    btn.addEventListener('touchcancel', end);
  }

  // Belt-and-braces: release on tab hide / window blur so alt-tab doesn't
  // leave the motor running at full throttle.
  window.addEventListener('blur',     end);
  document.addEventListener('visibilitychange',
    () => { if (document.hidden) end(); });
}
async function refresh() {
  const s = await (await fetch('/api/status')).json();
  document.querySelector('#status pre').textContent = JSON.stringify(s, null, 2);
}
async function loadCfg() {
  const c = await (await fetch('/api/config')).json();
  for (const k in c) {
    const el = document.querySelector(`[name="${k}"]`);
    if (!el) continue;
    if (el.type === 'checkbox') el.checked = !!c[k];
    else el.value = (typeof c[k] === 'boolean') ? String(c[k]) : c[k];
  }
}
async function saveCfg() {
  const form = document.getElementById('cfg');
  const obj = {};
  // Gather every named control, including unchecked checkboxes (which
  // FormData silently skips). Without this, unticking "invert" never
  // reaches the server.
  form.querySelectorAll('[name]').forEach(el => {
    const k = el.name;
    if (el.type === 'checkbox') { obj[k] = el.checked; return; }
    const v = el.value;
    if (v === 'true')       obj[k] = true;
    else if (v === 'false') obj[k] = false;
    else if (v !== '' && !isNaN(v)) obj[k] = Number.isInteger(+v) ? +v : +v;
    else obj[k] = v;
  });
  // Keep IPs and SSIDs as strings.
  ['wifiSsid','wifiPassword','apSsid','apPassword','hostname',
   'staticIp','staticGateway','staticSubnet','staticDns'].forEach(k => {
    if (obj[k] !== undefined) obj[k] = String(obj[k]);
  });
  const r = await fetch('/api/config', {method:'POST',
    headers:{'Content-Type':'application/json'}, body: JSON.stringify(obj)});
  alert((await r.json()).message || 'saved');
}
function setWifiStatus(msg, cls) {
  const el = document.getElementById('wifiStatus');
  el.className = cls || '';
  el.textContent = msg || '';
}
async function scanWifi() {
  setWifiStatus('Scanning… (a few seconds)');
  try {
    const j = await (await fetch('/api/wifi/scan')).json();
    const sel = document.getElementById('wifiList');
    sel.innerHTML = '';
    const nets = (j.networks || []).slice().sort((a,b) => b.rssi - a.rssi);
    if (!nets.length) {
      const opt = document.createElement('option');
      opt.disabled = true; opt.textContent = '(no networks found)';
      sel.appendChild(opt);
      setWifiStatus('No networks found', 'err');
      return;
    }
    // Deduplicate by SSID (keep strongest).
    const seen = new Set();
    for (const n of nets) {
      if (!n.ssid || seen.has(n.ssid)) continue;
      seen.add(n.ssid);
      const opt = document.createElement('option');
      opt.value = n.ssid;
      opt.dataset.secure = n.secure ? '1' : '0';
      const lock = n.secure ? '🔒' : '  ';
      const bars = n.rssi >= -55 ? '▂▄▆█'
                : n.rssi >= -65 ? '▂▄▆ '
                : n.rssi >= -75 ? '▂▄   '
                :                 '▂    ';
      opt.textContent = `${lock} ${bars}  ${n.ssid}  (${n.rssi} dBm)`;
      sel.appendChild(opt);
    }
    setWifiStatus(`${seen.size} network${seen.size === 1 ? '' : 's'} found`, 'ok');
  } catch (e) {
    setWifiStatus('Scan failed', 'err');
  }
}
function pickAp() {
  const sel = document.getElementById('wifiList');
  const opt = sel.options[sel.selectedIndex];
  if (!opt || opt.disabled) return;
  document.getElementById('provSsid').value = opt.value;
  const pw = document.getElementById('provPass');
  if (opt.dataset.secure === '0') { pw.value = ''; pw.placeholder = '(open network)'; pw.disabled = true; }
  else { pw.disabled = false; pw.placeholder = ''; pw.focus(); }
}
async function provision() {
  const ssid = document.getElementById('provSsid').value.trim();
  const pass = document.getElementById('provPass').value;
  if (!ssid) { setWifiStatus('Pick a network or type an SSID first', 'err'); return; }
  setWifiStatus(`Connecting to "${ssid}"… (up to 15 s)`);
  try {
    const r = await fetch('/api/wifi/provision', {method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({ssid, password: pass})});
    const j = await r.json();
    if (j.ok) {
      const ip = j.ip;
      if (j.apWillShutDown) {
        setWifiStatus(
          `✅ Connected to "${ssid}" at ${ip} — saved. Hotspot closing now; ` +
          `reconnect your device to your Wi-Fi and open http://${ip}/`, 'ok');
      } else {
        setWifiStatus(`✅ Connected to "${ssid}" at ${ip} — saved.`, 'ok');
      }
      // Mirror into the main config form so the user can see it's stored.
      const f = document.getElementById('cfg');
      if (f && f.wifiSsid)     f.wifiSsid.value     = ssid;
      if (f && f.wifiPassword) f.wifiPassword.value = pass;
      refresh();
    } else {
      setWifiStatus(`❌ ${j.reason || 'connection failed'} — not saved`, 'err');
    }
  } catch (e) {
    setWifiStatus('❌ Request failed', 'err');
  }
}
async function reboot()       { await fetch('/api/reboot',         {method:'POST'}); }
async function resetNet()     { if (confirm('Reset Wi-Fi config?'))
  await fetch('/api/reset/network', {method:'POST'}); }
async function factoryReset() { if (confirm('FULL factory reset?'))
  await fetch('/api/reset/factory', {method:'POST'}); }

// Press-and-hold bindings.
//   Drive:    repeat every 200 ms, release -> 'stop' (instant brake-off).
//   Steering: repeat every 150 ms (< pulseMs=400) so each ping lands well
//             before the previous would expire — motor stays energised
//             continuously without any pulse gaps. On release send
//             'steer_stop' so the spring can snap the wheels back to
//             center without the motor re-energising against it.
bindHold(document.getElementById('btnFwd'),   'forward', {repeatMs: 200, releaseAction: 'stop'});
bindHold(document.getElementById('btnRev'),   'reverse', {repeatMs: 200, releaseAction: 'stop'});
bindHold(document.getElementById('btnLeft'),  'left',    {repeatMs: 100, releaseAction: 'steer_stop'});
bindHold(document.getElementById('btnRight'), 'right',   {repeatMs: 100, releaseAction: 'steer_stop'});

loadCfg();
// HTTP status polling as a fallback. Skipped once WS telemetry is live —
// renderStatusFromWs() handles updates, and it's >10× more frequent.
refresh();
setInterval(() => { if (!wsReady) refresh(); }, 2000);

openWs();
</script>
</body></html>
)HTML";

}  // namespace smartrc
