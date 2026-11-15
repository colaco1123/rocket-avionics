// Dashboard served by stationweb.cpp: dashboard/index.html and app.js, over WiFi instead of the COM port.
#pragma once

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ground station</title>
<style>
body{font:14px/1.4 system-ui,sans-serif;margin:16px;max-width:1100px;color:#222;background:#fff}
h1{font-size:18px;margin:0 0 12px}
section{margin-bottom:16px}
button{font:inherit;padding:4px 10px;margin:0 4px 4px 0;border:1px solid #999;border-radius:3px;background:#f4f4f4;cursor:pointer}
input{font:inherit;width:80px;padding:3px 6px;border:1px solid #999;border-radius:3px}
input[type=file]{width:auto;max-width:100%}
a{color:#222}
#data{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:8px 16px}
#data div{border-bottom:1px solid #ddd;padding:2px 0}
#data p{display:flex;justify-content:space-between;margin:0}
#data span{color:#666}
#data canvas{display:block;width:100%;height:28px}
#chart{display:block;width:100%;height:240px;border:1px solid #ddd}
#events,#files{border-collapse:collapse;max-width:640px;width:100%}
#events td,#files td{border-bottom:1px solid #ddd;padding:3px 8px}
#events tr:first-child td{color:#666}
.muted{color:#666}
</style>
</head>
<body>
<h1>Ground station</h1>

<section>
  <span id="status" class="muted">Station: connecting</span> &middot;
  <span id="link" class="muted">Rocket: no data</span>
</section>

<section>
  <button onclick="onlineTest()">Online test</button>
  <span id="testState" class="muted">Lights the pink box on the online dashboard</span>
</section>

<section>
  <button onclick="arm()">Arm</button>
  <button onclick="disarm()">Disarm</button>
  <button onclick="send('TEST')">Test</button>
  <button onclick="send('OPENNOSE')">Open nose</button>
  <button onclick="send('WIFI_ON')">WiFi on</button>
  <button onclick="send('WIFI_OFF')">WiFi off</button>
  <button onclick="send('CX_ON')">Telemetry on</button>
  <button onclick="send('CX_OFF')">Telemetry off</button>
  <button onclick="send('ZERO')">Zero altitude</button>
  <button onclick="send('ZERO_ACCEL')">Zero accel</button>
  <button onclick="send('MAG_CAL')">Mag cal (turn 20 s)</button>
  Deploy from <input id="apogee" type="number" value="1000" min="1"> m
  <button onclick="send('APOGEE=' + apogee.value)">Set</button>
</section>

<section>
  <button onclick="send('FREQ_MAIN')">Main channel</button>
  <button onclick="send('FREQ_BACKUP')">Backup channel</button>
  <button onclick="send('PWR_HIGH')">Power high</button>
  <button onclick="send('PWR_LOW')">Power low</button>
  <span id="last" class="muted">Last command: none</span>
</section>

<section>
  Simulation (one pressure in Pa per line, 1 per second) <input id="simFile" type="file" accept=".csv,.txt">
  <button onclick="simStart()">Run simulation</button>
  <button onclick="simStop()">Stop simulation</button>
  <span id="simState" class="muted">Simulation off</span>
</section>

<section><table id="events"><tr><td>Flight events appear here once the rocket is armed</td></tr></table></section>
<section><canvas id="chart"></canvas></section>
<section id="data"></section>
<section id="logState" class="muted">Not logging</section>
<section><table id="files"></table></section>

<script>
const HEADER   = 'ID,TIME_S,PACKET,ALT_M,PRESSURE_PA,TEMP_C,VOLTAGE_V,GNSS_TIME_S,LAT,LON,GNSS_ALT_M,SATS,AX,AY,AZ,ROLL,PITCH,SPIN_DPS,STATE,VSPEED_MS,MAG_OK,ACK,RSSI_DBM,SNR_DB'.split(',');   // last two added by the ground station
const TEXT     = ['ID', 'STATE', 'ACK'];   // fields that are not plotted
const STATES   = ['BENCH', 'TEST', 'ASCEND', 'LAUNCH', 'APOGEE', 'DESCEND', 'IMPACT'];   // STATE arrives as 0-6
const EVENTS   = [['Armed', 2], ['Launch', 3], ['Apogee', 4], ['Descent', 5], ['Impact', 6]];   // event, first state
const ALT      = HEADER.indexOf('ALT_M');
const ACK      = HEADER.indexOf('ACK');
const LOG_MS   = 10 * 60 * 1000;          // stop the Arm log 10 min after Arm
const ARM_FILE = 'Flight_arm.csv';
const POINTS   = 600;                     // points kept per graph
const RETRY_MS = 2500;                    // resend until ACK; longer than one send + ACK round trip
const RETRIES  = 5;
const ONCE     = ['OPENNOSE'];            // never repeated: each one fires the solenoid
const POLL_MS  = 500;                     // ask the station for new lines this often
const FILES_MS = 10000;                   // refresh the stored file list this often

let next       = 0;                       // index of the next line to ask the station for
let log        = null;                    // lines since Arm, null when not logging
let logTimer   = null;                    // 10 min stop timer
let hist       = HEADER.map(() => []);    // history of each field for the graphs
let pending    = null;                    // command waiting for its ACK
let retryTimer = null;
let lastPacket = 0;                       // time the last telemetry line arrived
let flight     = {};                      // flight events seen: name -> { clock, t, alt }
let peak       = -Infinity;               // highest altitude since launch
let simData    = [];                      // simulated pressures (Pa) from the chosen file
let simTimer   = null;

const $ = id => document.getElementById(id);

async function poll() {         // fetch new lines from the station, repeat
  try {
    const l = (await (await fetch('/lines?from=' + next)).text()).split('\n');
    next = +l[0];
    l.slice(1).forEach(x => { if (x) receive(x.trim()); });
    $('status').textContent = 'Station: connected';
  } catch (e) { $('status').textContent = 'Station: not connected'; }
  setTimeout(poll, POLL_MS);
}

function link() {               // rocket link: receiving, or seconds since the last packet
  const s = Math.round((Date.now() - lastPacket) / 1000);
  const rssi = hist[HEADER.indexOf('RSSI_DBM')].slice(-1)[0];
  $('link').textContent = !lastPacket ? 'Rocket: no data' : s < 3 ? 'Rocket: receiving, ' + rssi + ' dBm' : 'Rocket: no data for ' + s + ' s';
}
setInterval(link, 1000);

function write(cmd) {           // one command to the station
  fetch('/cmd', { method: 'POST', body: new URLSearchParams({ c: cmd }) }).catch(() => {});
}

function onlineTest() {         // send the click time through the online link to the dashboard
  const t = new Date().toLocaleTimeString();
  fetch('/test', { method: 'POST', body: new URLSearchParams({ t }) })
    .then(() => { $('testState').textContent = 'Sent at ' + t; })
    .catch(() => { $('testState').textContent = 'Not sent, station not connected'; });
}

function send(cmd) {            // send a command and repeat it until the rocket ACKs it
  clearInterval(retryTimer);
  pending = cmd;
  let tries = 0;
  const max = ONCE.includes(cmd) ? 1 : RETRIES;
  const attempt = () => {
    if (tries >= max) { clearInterval(retryTimer); lastCommand(cmd, 'no ACK'); return; }
    write(cmd);
    lastCommand(cmd, 'sent ' + ++tries + 'x');
  };
  attempt();
  retryTimer = setInterval(attempt, RETRY_MS);
}

function lastCommand(cmd, note) {   // show the last command and its status
  $('last').textContent = 'Last command: ' + cmd + ' (' + note + ')';
}

function receive(line) {        // one telemetry line: check ACK, show, graph, log
  const v = line.split(',');
  if (v.length !== HEADER.length) return;
  lastPacket = Date.now();
  if (pending && v[ACK] === pending) { clearInterval(retryTimer); lastCommand(pending, 'ACK'); pending = null; }
  if (log) log.push(line);
  v.forEach((x, i) => { hist[i].push(parseFloat(x)); if (hist[i].length > POINTS) hist[i].shift(); });
  show(v);
  flightEvents(v);
  plot($('chart'), hist[ALT], true);
}

function flightEvents(v) {      // record each flight event once: clock time, mission time, time since launch, altitude
  const st = +v[HEADER.indexOf('STATE')], t = +v[HEADER.indexOf('TIME_S')], alt = +v[ALT];
  if (st < 2) { flight = {}; peak = -Infinity; }               // disarmed: start a new flight
  if (st >= 3) peak = Math.max(peak, alt);
  EVENTS.forEach(([name, s]) => {
    if (st >= s && !flight[name]) flight[name] = { clock: new Date().toLocaleTimeString(), t, alt: name === 'Apogee' ? peak : alt };
  });
  const launch = flight.Launch ? flight.Launch.t : null;
  $('events').innerHTML = '<tr><td>Event</td><td>Clock</td><td>Mission time</td><td>T+ launch</td><td>Altitude</td></tr>' +
    EVENTS.map(([name]) => {
      const e = flight[name];
      return `<tr><td>${name}</td><td>${e ? e.clock : '-'}</td><td>${e ? e.t + ' s' : '-'}</td>` +
             `<td>${e && launch !== null ? (e.t - launch) + ' s' : '-'}</td><td>${e ? e.alt.toFixed(1) + ' m' : '-'}</td></tr>`;
    }).join('');
}

function show(v) {              // every field as a number with a small graph
  if (!$('data').children.length)
    $('data').innerHTML = HEADER.map(h => `<div><p><span>${h}</span><b></b></p>${TEXT.includes(h) ? '' : '<canvas></canvas>'}</div>`).join('');
  [...$('data').children].forEach((d, i) => {
    d.querySelector('b').textContent = HEADER[i] === 'STATE' ? v[i] + ' ' + (STATES[v[i]] || '') : v[i];
    if (!TEXT.includes(HEADER[i])) plot(d.querySelector('canvas'), hist[i], false);
  });
}

function plot(c, data, labels) {    // line graph, auto scaled
  const g = c.getContext('2d');
  const w = c.width = c.clientWidth, h = c.height = c.clientHeight, pad = labels ? 18 : 3;
  const d = data.filter(x => !isNaN(x));
  if (d.length < 2) return;
  let lo = Math.min(...d), hi = Math.max(...d);
  if (hi - lo < 1e-6) { hi += 0.5; lo -= 0.5; }
  g.strokeStyle = '#222';
  g.beginPath();
  d.forEach((y, i) => {
    const px = i / (d.length - 1) * w, py = pad + (hi - y) / (hi - lo) * (h - 2 * pad);
    i ? g.lineTo(px, py) : g.moveTo(px, py);
  });
  g.stroke();
  if (!labels) return;
  g.fillStyle = '#666';
  g.fillText('ALT ' + hi.toFixed(1) + ' m', 4, 12);
  g.fillText(lo.toFixed(1) + ' m', 4, h - 4);
}

function download(lines, name) {    // save lines as a CSV file with a header row
  const blob = new Blob([HEADER.join(',') + '\n' + lines.join('\n') + '\n'], { type: 'text/csv' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
}

async function files() {        // files stored on the station
  try {
    const t = (await (await fetch('/files')).text()).trim();
    $('files').innerHTML = t ? t.split('\n').map(l => {
      const [n, s] = l.split(','), u = encodeURIComponent(n);
      return `<tr><td>${n}</td><td>${(s / 1024).toFixed(1)} kB</td><td><a href="/get?name=${u}">Save</a> <button onclick="del('${u}')">Delete</button></td></tr>`;
    }).join('') : '<tr><td>No stored data yet</td></tr>';
  } catch (e) {}
}
setInterval(files, FILES_MS);

async function del(u) {         // delete a stored file
  if (!confirm('Delete ' + decodeURIComponent(u) + ' from the station?')) return;
  await fetch('/delete?name=' + u, { method: 'POST' });
  files();
}

function arm() {                // arm the rocket and start the Arm log
  send('ARM');
  log = [];
  clearTimeout(logTimer);
  logTimer = setTimeout(stopLog, LOG_MS);
  $('logState').textContent = 'Logging since ' + new Date().toLocaleTimeString();
}

function disarm() {             // disarm the rocket and save the Arm log
  send('DISARM');
  stopLog();
}

async function simStart() {     // SIM_ON, first pressure (= pad), ARM, then one pressure per second
  const f = $('simFile').files[0];
  if (!f) { $('simState').textContent = 'Choose a file first'; return; }
  simData = (await f.text()).split(/\r?\n/).map(parseFloat).filter(x => !isNaN(x));
  let i = 0, armSent = false;
  clearInterval(simTimer);
  send('SIM_ON');
  simTimer = setInterval(() => {
    if (pending) { $('simState').textContent = 'Waiting for ' + pending + ' ACK (telemetry must be on)'; return; }
    if (i === 1 && !armSent) { armSent = true; arm(); return; }
    if (i >= simData.length) { clearInterval(simTimer); $('simState').textContent = 'Simulation finished, press Stop simulation'; return; }
    write('SIMP=' + Math.round(simData[i++]));
    $('simState').textContent = 'Simulation ' + i + ' / ' + simData.length;
  }, 1000);
}

function simStop() {            // SIM_OFF: rocket disarms and returns to its barometer
  clearInterval(simTimer);
  send('SIM_OFF');
  stopLog();
  $('simState').textContent = 'Simulation off';
}

function stopLog() {            // download the Arm log and stop logging
  if (!log) return;
  download(log, ARM_FILE);
  log = null;
  clearTimeout(logTimer);
  $('logState').textContent = 'Not logging (last log saved)';
}

poll();
files();
</script>
</body>
</html>)HTML";
