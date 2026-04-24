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
</style>
</head>
<body>
<h1>Smart RC</h1>
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
    <label>Heartbeat timeout (ms)</label><input name="heartbeatTimeoutMs" type="number" min="50" max="60000">
    <div class="invert-row">
      <label><input type="checkbox" name="driveInverted"> Invert drive direction</label>
      <label><input type="checkbox" name="steerInverted"> Invert steering direction</label>
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
// Fire-and-forget command. We deliberately don't await refresh() here —
// when a button is held at 5 Hz we don't want a refresh storm.
function cmd(action, speed) {
  const body = JSON.stringify(speed ? {action, speed} : {action});
  return fetch('/api/control', {method:'POST',
    headers:{'Content-Type':'application/json'}, body}).catch(() => {});
}

// Press-and-hold: while the pointer is down on `btn`, resend `action` at
// `repeatMs`. On release, optionally fire a single `releaseAction` command
// to cut the motor instantly. Steering uses 'steer_stop' so the spring can
// snap the wheels back without being fought by another pulse.
function bindHold(btn, action, opts) {
  opts = opts || {};
  const repeatMs      = opts.repeatMs      || 200;
  const releaseAction = opts.releaseAction || null;
  let timer = null;
  const start = e => {
    e.preventDefault();
    if (timer) return;
    cmd(action);
    timer = setInterval(() => cmd(action), repeatMs);
    btn.classList.add('held');
  };
  const end = e => {
    if (e) e.preventDefault();
    if (!timer) return;
    clearInterval(timer); timer = null;
    btn.classList.remove('held');
    if (releaseAction) cmd(releaseAction);
  };
  btn.addEventListener('mousedown',   start);
  btn.addEventListener('touchstart',  start, {passive:false});
  btn.addEventListener('mouseup',     end);
  btn.addEventListener('mouseleave',  end);
  btn.addEventListener('touchend',    end);
  btn.addEventListener('touchcancel', end);
  // Release on tab hide / window blur — prevents a stuck throttle if the
  // user alt-tabs while holding.
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
bindHold(document.getElementById('btnLeft'),  'left',    {repeatMs: 150, releaseAction: 'steer_stop'});
bindHold(document.getElementById('btnRight'), 'right',   {repeatMs: 150, releaseAction: 'steer_stop'});

loadCfg(); refresh();
setInterval(refresh, 2000);
</script>
</body></html>
)HTML";

}  // namespace smartrc
