// CanSat dashboard: talks to the CanSat board over USB (Web Serial, Chrome or Edge).

const HEADER = 'millis,packet,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_C,pressure_Pa,alt_m,gps_fix,gps_lat,gps_lon,gps_alt_m,gps_sats,gps_time'.split(',');
const TEXT   = ['gps_time'];              // fields that are not plotted
const ALT    = HEADER.indexOf('alt_m');
const POINTS = 600;                       // points kept per graph

let writer     = null;                    // serial output
let hist       = HEADER.map(() => []);    // history of each field for the graphs
let lastPacket = 0;                       // time the last data line arrived
let files      = null;                    // file list being received, null when not listing
let dump       = null;                    // { name, lines } while a file is being received

const $ = id => document.getElementById(id);

async function connect() {      // pick the COM port, open it, ask for status and files, start reading
  const port = await navigator.serial.requestPort();
  await port.open({ baudRate: 115200 });
  writer = port.writable.getWriter();
  $('status').textContent = 'COM port: connected';
  read(port);
  write('STATUS');
  write('LIST');
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
      while ((i = buf.indexOf('\n')) >= 0) { receive(buf.slice(0, i).replace(/\r$/, '')); buf = buf.slice(i + 1); }
    }
  } catch (e) {}
  writer = null;
  $('status').textContent = 'COM port: disconnected';
}

function write(cmd) {           // one command line out of the COM port
  if (writer) writer.write(new TextEncoder().encode(cmd + '\n'));
}

function link() {               // CanSat link: receiving, or seconds since the last data line
  const s = Math.round((Date.now() - lastPacket) / 1000);
  $('link').textContent = !lastPacket ? 'CanSat: no data' : s < 2 ? 'CanSat: receiving' : 'CanSat: no data for ' + s + ' s';
}
setInterval(link, 1000);

function receive(line) {        // route one line: file being downloaded, file list, message, or data
  if (dump) { line === 'END,' + dump.name ? saveDump() : dump.lines.push(line); return; }
  if (line.startsWith('BEGIN,'))  { dump = { name: line.slice(6), lines: [] }; return; }
  if (line === 'FILES')           { files = []; return; }
  if (line.startsWith('FILE,') && files) { files.push(line.split(',').slice(1)); return; }
  if (line === 'END_FILES')       { showFiles(files); files = null; return; }
  if (line.startsWith('MSG,'))    { $('msg').textContent = line.slice(4); return; }
  const v = line.split(',');
  if (v.length !== HEADER.length) return;
  lastPacket = Date.now();
  v.forEach((x, i) => { hist[i].push(parseFloat(x)); if (hist[i].length > POINTS) hist[i].shift(); });
  show(v);
  plot($('chart'), hist[ALT], true);
}

function showFiles(list) {      // SD card files with size and a download button
  $('files').innerHTML = list.length
    ? list.map(([n, s]) => `<tr><td>${n}</td><td>${s} B</td><td><button onclick="write('DUMP ${n}')">Download</button></td></tr>`).join('')
    : '<tr><td>No files</td></tr>';
}

function saveDump() {           // a file finished arriving: save it to the computer
  while (dump.lines.length && dump.lines[dump.lines.length - 1] === '') dump.lines.pop();
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([dump.lines.join('\n') + '\n'], { type: 'text/csv' }));
  a.download = dump.name.replace(/^\//, '');
  a.click();
  $('msg').textContent = 'Saved ' + a.download;
  dump = null;
}

function show(v) {              // every field as a number with a small graph
  if (!$('data').children.length)
    $('data').innerHTML = HEADER.map(h => `<div><p><span>${h}</span><b></b></p>${TEXT.includes(h) ? '' : '<canvas></canvas>'}</div>`).join('');
  [...$('data').children].forEach((d, i) => {
    d.querySelector('b').textContent = v[i];
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
