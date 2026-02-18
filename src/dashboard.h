#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <pgmspace.h>

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>brakeMachine</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0d1117;--sf:#161b22;--bd:#30363d;--tx:#c9d1d9;--tx2:#8b949e;
--ac:#58a6ff;--ok:#3fb950;--wn:#d29922;--er:#f85149}
body{background:var(--bg);color:var(--tx);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;min-height:100vh}
.hdr{padding:14px 20px;border-bottom:1px solid var(--bd);display:flex;justify-content:space-between;align-items:center}
.hdr h1{font-size:1.15em;color:var(--ac);letter-spacing:-.5px}
.hdr .up{font-size:.8em;color:var(--tx2)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;padding:16px 20px}
.card{background:var(--sf);border:1px solid var(--bd);border-radius:8px;padding:12px 14px}
.card .lb{font-size:.7em;color:var(--tx2);text-transform:uppercase;letter-spacing:.5px;margin-bottom:4px}
.card .vl{font-size:1.5em;font-weight:600}
.ok{color:var(--ok)}.wn{color:var(--wn)}.er{color:var(--er)}.ac{color:var(--ac)}
section{padding:0 20px 14px}
section h3{font-size:.85em;color:var(--tx2);text-transform:uppercase;letter-spacing:.5px;margin-bottom:8px}
.panel{background:var(--sf);border:1px solid var(--bd);border-radius:8px;padding:14px}
.panel .meta{font-size:.85em;margin-bottom:6px}
.panel pre{font-size:.75em;color:var(--tx2);overflow-x:auto;white-space:pre-wrap;word-break:break-all;max-height:90px;margin-top:8px;padding:8px;background:var(--bg);border-radius:4px}
.badge{display:inline-block;padding:2px 8px;border-radius:4px;font-size:.75em;font-weight:600}
.badge.ok{background:#3fb95022;color:var(--ok)}.badge.wn{background:#d2992222;color:var(--wn)}.badge.er{background:#f8514922;color:var(--er)}
.no-url{background:#d2992218;border:1px solid var(--wn);border-radius:8px;padding:12px 14px;margin-bottom:14px;font-size:.85em;color:var(--wn)}
input[type=text],input[type=url],input[type=password]{width:100%;padding:9px 10px;border:1px solid var(--bd);border-radius:6px;background:var(--bg);color:var(--tx);font-size:.9em;margin:4px 0 10px}
button{padding:10px 16px;border:none;border-radius:6px;font-size:.85em;cursor:pointer;font-weight:500}
.btn-p{background:var(--ac);color:#fff;width:100%}.btn-p:hover{opacity:.85}
.btn-d{background:var(--er);color:#fff;width:100%;margin-top:8px}.btn-d:hover{opacity:.85}
.saved{color:var(--ok);font-size:.8em;margin-top:4px}
details{padding:0 20px 20px}
details summary{cursor:pointer;color:var(--ac);font-size:.85em;padding:6px 0}
details .panel{margin-top:8px}
label{font-size:.8em;color:var(--tx2)}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
.dot.ok{background:var(--ok)}.dot.er{background:var(--er)}.dot.wn{background:var(--wn)}
</style>
</head><body>

<div class="hdr">
<h1>brakeMachine</h1>
<span class="up" id="uptime">--</span>
</div>

<div class="grid">
<div class="card"><div class="lb">WiFi</div><div class="vl" id="wifi">--</div></div>
<div class="card"><div class="lb">Total Dumps</div><div class="vl" id="dumps">--</div></div>
<div class="card"><div class="lb">Uploaded</div><div class="vl ok" id="uploaded">--</div></div>
<div class="card"><div class="lb">Failed</div><div class="vl" id="failed">--</div></div>
<div class="card"><div class="lb">Queued</div><div class="vl" id="queued">--</div></div>
</div>

<div id="url-warning" class="no-url" style="margin:0 20px 14px;display:none">
No receiver URL configured &mdash; dumps will be queued locally until a URL is set below.
</div>

<section>
<h3>Last Dump</h3>
<div class="panel">
<div class="meta" id="dump-info">No dumps received yet</div>
<div id="dump-badge"></div>
<pre id="dump-preview" style="display:none"></pre>
</div>
</section>

<section>
<h3>Receiver URL</h3>
<div class="panel">
<form id="url-form">
<input type="url" id="recv-url" placeholder="http://192.168.1.100:5000/upload">
<button type="submit" class="btn-p">Save</button>
</form>
<div id="url-status"></div>
</div>
</section>

<details>
<summary>WiFi Configuration</summary>
<div class="panel">
<form action="/save" method="POST">
<label>SSID</label>
<input type="text" name="ssid" id="ssid-in">
<label>Password</label>
<input type="password" name="pass">
<button type="submit" class="btn-p">Connect</button>
</form>
<form action="/api/clear-wifi" method="POST">
<button type="submit" class="btn-d">Forget Network</button>
</form>
</div>
</details>

<script>
const $=id=>document.getElementById(id);
let urlEdited=false;

function fmt(s){
  const h=Math.floor(s/3600),m=Math.floor((s%3600)/60);
  return h>0?h+'h '+m+'m':m+'m '+s%60+'s';
}

async function refresh(){
  try{
    const d=await fetch('/api/status').then(r=>r.json());

    $('wifi').innerHTML=(d.wifi_mode==='connected'
      ?'<span class="dot ok"></span>Connected'
      :'<span class="dot er"></span>'+d.wifi_mode);

    $('uptime').textContent=fmt(d.uptime);
    $('dumps').textContent=d.dump_count;
    $('uploaded').textContent=d.upload_success;

    const f=d.upload_failed;
    $('failed').textContent=f;
    $('failed').className='vl'+(f>0?' er':'');

    const q=d.queue_depth;
    $('queued').textContent=q;
    $('queued').className='vl'+(q>0?' wn':' ok');

    const noUrl=!d.receiver_url||d.receiver_url.length===0;
    $('url-warning').style.display=noUrl?'block':'none';

    if(d.last_dump&&d.last_dump.id>0){
      $('dump-info').textContent=d.last_dump.timestamp+' \u2014 '+(d.last_dump.size/1024).toFixed(1)+' KB';
      if(d.last_dump.uploaded){
        $('dump-badge').innerHTML='<span class="badge ok">Uploaded</span>';
      }else if(q>0){
        $('dump-badge').innerHTML='<span class="badge wn">Queued</span>';
      }else{
        $('dump-badge').innerHTML='<span class="badge er">Failed</span>';
      }
      if(d.last_dump.preview){
        $('dump-preview').textContent=d.last_dump.preview;
        $('dump-preview').style.display='block';
      }
    }

    if(d.receiver_url&&!$('recv-url').value&&!urlEdited){
      $('recv-url').value=d.receiver_url;
    }
  }catch(e){}
}

$('url-form').addEventListener('submit',async e=>{
  e.preventDefault();
  const url=$('recv-url').value;
  const res=await fetch('/api/set-receiver',{
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'url='+encodeURIComponent(url)
  });
  $('url-status').innerHTML=res.ok?'<span class="saved">Saved!</span>':'<span class="er">Error</span>';
  setTimeout(()=>$('url-status').innerHTML='',3000);
  refresh();
});

$('recv-url').addEventListener('input',()=>urlEdited=true);

refresh();
setInterval(refresh,5000);
</script>
</body></html>
)rawliteral";

#endif // DASHBOARD_H
