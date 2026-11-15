// Ground station dashboard: talks to the ground T-Beam over USB (Web Serial, Chrome or Edge).

const HEADER   = 'ID,TIME_S,PACKET,ALT_M,PRESSURE_PA,TEMP_C,VOLTAGE_V,GNSS_TIME_S,LAT,LON,GNSS_ALT_M,SATS,AX,AY,AZ,ROLL,PITCH,SPIN_DPS,STATE,VSPEED_MS,MAG_OK,ACK,RSSI_DBM,SNR_DB'.split(',');   // last two added by the ground station
const TEXT     = ['ID', 'STATE', 'ACK'];   // fields that are not plotted
const STATES   = ['BENCH', 'TEST', 'ASCEND', 'LAUNCH', 'APOGEE', 'DESCEND', 'IMPACT'];   // STATE arrives as 0-6
const EVENTS   = [['Armed', 2], ['Launch', 3], ['Apogee', 4], ['Descent', 5], ['Impact', 6]];   // event, first state
const ALT      = HEADER.indexOf('ALT_M');
const ACK      = HEADER.indexOf('ACK');
const LOG_MS   = 10 * 60 * 1000;          // stop the Arm log 10 min after Arm
const ARM_FILE = 'Flight_arm.csv';
const ALL_FILE = 'Flight.csv';
const POINTS   = 600;                     // points kept per graph
const RETRY_MS = 2500;                    // resend until ACK; longer than one send + ACK round trip
const RETRIES  = 5;
const ONCE     = ['OPENNOSE'];            // never repeated: each one fires the solenoid

let writer     = null;                    // serial output
let all        = [];                      // every line since connecting
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

async function connect() {      // pick the COM port, open it and start reading
  const port = await navigator.serial.requestPort();
  await port.open({ baudRate: 115200 });
  writer = port.writable.getWriter();
  $('status').textContent = 'COM port: connected';
  read(port);
}

async function read(port) {     // split the serial stream into lines until the port closes
  const reader = port.readable.pipeThrough(new TextDecoderStream()).getReader();
  let buf = '';
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      buf += value;
      let i;
      while ((i = buf.indexOf('\n')) >= 0) { receive(buf.slice(0, i).trim()); buf = buf.slice(i + 1); }
    }
  } catch (e) {}
  writer = null;
  $('status').textContent = 'COM port: disconnected';
}

function link() {               // rocket link: receiving, or seconds since the last packet
  const s = Math.round((Date.now() - lastPacket) / 1000);
  const rssi = hist[HEADER.indexOf('RSSI_DBM')].slice(-1)[0];
  $('link').textContent = !lastPacket ? 'Rocket: no data' : s < 3 ? 'Rocket: receiving, ' + rssi + ' dBm' : 'Rocket: no data for ' + s + ' s';
}
setInterval(link, 1000);

function write(cmd) {           // one command line out of the COM port
  if (writer) writer.write(new TextEncoder().encode(cmd + '\n'));
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
  all.push(line);
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

function saveAll() {            // every line received since connecting
  download(all, ALL_FILE);
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
