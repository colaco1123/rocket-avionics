// Web page served by web.cpp.
#pragma once

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Flight computer</title>
<style>
body{font:15px/1.4 system-ui,sans-serif;margin:16px;max-width:640px;color:#222;background:#fff}
h1{font-size:18px;margin:0 0 8px}
h2{font-size:15px;margin:24px 0 8px}
table{border-collapse:collapse;width:100%}
td{border-bottom:1px solid #ddd;padding:4px 6px}
td+td{text-align:right}
button{font:inherit;padding:3px 10px;border:1px solid #999;background:#f4f4f4;border-radius:3px;cursor:pointer}
a{color:#222}
</style></head><body>
<h1>Flight computer</h1>
<div id="c" style="color:#666">Connecting...</div>
<h2>Sensors</h2><table id="d"></table>
<h2>Solenoid</h2><button onclick="fire()">Test solenoid</button> <span id="m"></span>
<h2>SD card</h2><table id="f"></table>
<script>
async function data(){
  try{
    const t=(await (await fetch('/data')).text()).trim().split('\n');
    const h=t[0].split(','),v=(t[1]||'').split(',');
    d.innerHTML=h.map((k,i)=>`<tr><td>${k}</td><td>${v[i]??''}</td></tr>`).join('');
    c.textContent='Connected, updated '+new Date().toLocaleTimeString();
  }catch(e){c.textContent='Not connected to the flight computer';}
  setTimeout(data,1000);
}
async function files(){
  const t=(await (await fetch('/files')).text()).trim();
  f.innerHTML=t?t.split('\n').map(l=>{const[n,s]=l.split(',');
    return `<tr><td>${n}</td><td>${s} B</td><td><a href="/get?name=${encodeURIComponent(n)}">Save</a> <button onclick="del('${n}')">Delete</button></td></tr>`}).join('')
    :'<tr><td>No files</td></tr>';
}
async function del(n){
  if(!confirm('Delete '+n+'?'))return;
  await fetch('/delete?name='+encodeURIComponent(n),{method:'POST'});
  files();
}
async function fire(){
  if(!confirm('Fire the solenoid?'))return;
  await fetch('/fire',{method:'POST'});
  m.textContent='Fired at '+new Date().toLocaleTimeString();
}
data();files();
</script></body></html>)HTML";
