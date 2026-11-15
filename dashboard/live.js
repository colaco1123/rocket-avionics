// Live dashboard: app.js plus the online feed, showing COM data first and online data when COM goes quiet.

const TOPIC    = 'rocket/telemetry';  // the web ground station publishes each line here
const PORT     = 8084;                    // EMQX Serverless secure WebSocket port
const STALE_MS = 3000;                    // a source with no packet for this long counts as gone
const PING_MS  = 10000;                   // the pink box stays lit this long, then the time stays on grey
const LOGIN    = window.ONLINE_LOGIN || { host: '', username: 'viewer', password: '' };   // from secrets.js, used when this browser has no saved login
const SOURCES  = {
  com:    ['#d9730d', 'COM port data'],
  online: ['#1a7f37', 'Online data'],
  none:   ['#111', 'No data'],
};

let seen   = { com: 0, online: 0 };       // time of the last packet from each source
let client = null;                        // online connection
let pingTimer = null;                     // turns the pink box off

const showLine = window.receive;          // app.js shows, graphs and logs a line

function isPacket(line) {       // true for a full telemetry line, not a radio status message
  return line.split(',').length === HEADER.length;
}

function source() {             // COM is direct and can send commands, so it wins while it has data
  const now = Date.now();
  if (now - seen.com < STALE_MS) return 'com';
  return now - seen.online < STALE_MS ? 'online' : 'none';
}

function ping(time) {           // Online test from the web ground: light the pink box with the click time
  clearTimeout(pingTimer);
  $('ping').textContent = 'Web ground Online test clicked at ' + time;
  $('ping').hidden = false;
  $('ping').classList.add('lit');
  pingTimer = setTimeout(() => $('ping').classList.remove('lit'), PING_MS);
}

function arrive(from, line) {   // keep a packet only if it came from the source in use
  if (from === 'online' && line.startsWith('PING,')) { ping(line.slice(5)); return; }
  if (!isPacket(line)) return;
  seen[from] = Date.now();
  if (source() === from) showLine(line);
}

window.receive = line => arrive('com', line);

function indicator() {          // one light: orange COM, green online, black no data; commands need the COM port
  const [color, label] = SOURCES[source()];
  $('dot').style.background = color;
  $('dot').setAttribute('aria-label', label);
  $('source').textContent = label;
  $('controls').disabled = !writer;
}
setInterval(indicator, 500);

function brokerUrl(host) {      // a full address is used as it is, a host name gets the secure port
  return host.includes('://') ? host : `wss://${host}:${PORT}/mqtt`;
}

function connectOnline() {      // connect to the online service; it reconnects by itself
  const login = { host: $('host').value.trim(), username: $('user').value, password: $('pass').value };
  if (!login.host) { $('online').textContent = 'Online: enter the broker address'; return; }
  try { localStorage.setItem('online', JSON.stringify(login)); } catch (e) {}
  if (client) client.end(true);
  client = mqtt.connect(brokerUrl(login.host), { username: login.username, password: login.password, reconnectPeriod: 3000 });
  client.on('connect', () => { client.subscribe(TOPIC); $('online').textContent = 'Online: connected'; });
  client.on('offline', () => { $('online').textContent = 'Online: reconnecting'; });
  client.on('error', e => { $('online').textContent = 'Online: ' + e.message; });
  client.on('message', (t, m) => arrive('online', m.toString().trim()));
}

async function autoCom() {      // reopen a COM port this browser was already allowed to use
  if (writer || !navigator.serial) return;
  const [port] = await navigator.serial.getPorts();
  if (!port) return;
  try {
    await port.open({ baudRate: 115200 });
    writer = port.writable.getWriter();
    $('status').textContent = 'COM port: connected';
    read(port);
  } catch (e) {}
}

function restore() {            // fill in the login and connect both sources
  let login = LOGIN;
  try { login = JSON.parse(localStorage.getItem('online')) || LOGIN; } catch (e) {}
  $('host').value = login.host; $('user').value = login.username; $('pass').value = login.password;
  connectOnline();
  autoCom();
  if (navigator.serial) navigator.serial.addEventListener('connect', autoCom);
}

indicator();
restore();
