"""
IO-nity Station Pico - Bridge Server
====================================
HTTPS web server that proxies messages to the Pico display.
Generates a self-signed certificate so all devices on WiFi
can securely send messages to the Pico.

Usage:
    python server.py [--pico-ip <ip>] [--port <port>] [--https-port <port>]
"""

import argparse
import json
import os
import ssl
import sys
import time
import urllib.request
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path

APP_DIR = Path(__file__).parent
CERT_FILE = APP_DIR / "cert.pem"
KEY_FILE = APP_DIR / "key.pem"
STATIC_DIR = APP_DIR / "static"

# ── HTML page ──────────────────────────────────────────────────────
HTML_PAGE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>IO-nity Station Pico</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:linear-gradient(135deg,#0a0a0f 0%,#1a0a0a 50%,#0a0a1a 100%);
color:#e0e0e0;font-family:system-ui,sans-serif;min-height:100vh;
display:flex;flex-direction:column;align-items:center;padding:20px}
.header{text-align:center;margin:30px 0}
.header h1{color:#ff4444;font-size:3em;letter-spacing:6px;text-transform:uppercase;
text-shadow:0 0 40px rgba(255,68,68,0.3)}
.header .sub{color:#44ff44;font-size:1em;letter-spacing:2px;margin-top:8px}
.header .ip{color:#888;font-size:0.8em;margin-top:5px}
.card{background:rgba(26,26,46,0.9);border:1px solid #333;border-radius:16px;
padding:30px;width:100%;max-width:520px;margin-bottom:20px;
backdrop-filter:blur(10px)}
.card h2{color:#ff4444;margin-bottom:20px;font-size:1.3em}
input,textarea{width:100%;padding:14px;background:#0d0d1a;
border:1px solid #444;border-radius:10px;color:#fff;font-size:1em;
margin-bottom:12px;transition:border-color 0.2s}
input:focus,textarea:focus{outline:none;border-color:#ff4444}
textarea{height:90px;resize:vertical}
button{background:linear-gradient(135deg,#ff4444,#cc0000);color:#fff;
border:none;padding:14px 28px;border-radius:10px;font-size:1.1em;
cursor:pointer;width:100%;font-weight:bold;transition:transform 0.1s,box-shadow 0.2s}
button:hover{transform:scale(1.02);box-shadow:0 0 30px rgba(255,68,68,0.4)}
button:active{transform:scale(0.98)}
.msg{background:#0d0d1a;border-left:3px solid #ff4444;padding:12px 18px;
margin-bottom:10px;border-radius:0 10px 10px 0;animation:slideIn 0.3s ease}
@keyframes slideIn{from{opacity:0;transform:translateX(-20px)}to{opacity:1;transform:translateX(0)}}
.msg .author{color:#ff4444;font-size:0.8em;font-weight:bold}
.msg .time{color:#555;font-size:0.7em;float:right}
.msg .text{color:#ccc;margin-top:6px;word-break:break-word;font-size:0.95em}
.status{display:flex;gap:12px;margin-top:15px;font-size:0.8em;color:#888}
.status .dot{color:#44ff44}.status .dot.off{color:#ff4444}
.badge{background:#1a1a2e;padding:6px 14px;border-radius:20px}
</style>
</head>
<body>
<div class="header">
<h1>IO-NITY</h1>
<div class="sub">Station Pico &bull; SDK-Ionity</div>
<div class="ip" id="deviceIp">Pico: connecting...</div>
</div>
<div class="card">
<h2>&#9993; Send Message to Display</h2>
<form id="msgForm">
<input type="text" id="author" placeholder="Your name" maxlength="30" required>
<textarea id="text" placeholder="Type your message... it will scroll across the display!" maxlength="250" required></textarea>
<button type="submit">&#9654; Send to Pico</button>
</form>
<div class="status">
<span>Status: <span class="dot" id="statusDot">&#9679;</span> <span id="statusText">Connected</span></span>
<span class="badge" id="msgCount">0 messages</span>
</div>
</div>
<div class="card">
<h2>&#128172; Recent Messages</h2>
<div id="messages"><p style="color:#555">No messages yet. Be the first!</p></div>
</div>
<script>
const PICO_URL = '/api/proxy';
const form=document.getElementById('msgForm');
const msgs=document.getElementById('messages');
const count=document.getElementById('msgCount');
const statusDot=document.getElementById('statusDot');
const statusText=document.getElementById('statusText');
const deviceIp=document.getElementById('deviceIp');
let msgList=[];

async function sendMsg(e){
e.preventDefault();
const author=document.getElementById('author').value.trim();
const text=document.getElementById('text').value.trim();
if(!author||!text)return;
try{
const r=await fetch(PICO_URL,{
method:'POST',
headers:{'Content-Type':'application/json'},
body:JSON.stringify({author,text})
});
const data=await r.json();
if(data.status==='ok'){
document.getElementById('text').value='';
addMsg({author,text,time:Math.floor(Date.now()/1000)});
statusDot.className='dot';
statusText.textContent='Connected';
}else{
statusDot.className='dot off';
statusText.textContent='Pico offline';
}
}catch(err){
statusDot.className='dot off';
statusText.textContent='Pico offline';
}
}

function addMsg(m){
msgList.push(m);
if(msgList.length>50)msgList.shift();
renderMsgs();
}

function renderMsgs(){
count.textContent=msgList.length+' messages';
if(!msgList.length){msgs.innerHTML='<p style="color:#555">No messages yet. Be the first!</p>';return}
msgs.innerHTML=msgList.slice().reverse().map(m=>{
const d=new Date(m.time*1000);
const t=d.toLocaleTimeString();
return `<div class="msg">
<span class="author">${esc(m.author)}</span>
<span class="time">${t}</span>
<div class="text">${esc(m.text)}</div></div>`;
}).join('');
}

function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;')}

async function checkStatus(){
try{
const r=await fetch('/api/status');
const d=await r.json();
deviceIp.textContent='Pico: '+d.pico_ip+(d.pico_online?' (online)':' (offline)');
statusDot.className=d.pico_online?'dot':'dot off';
statusText.textContent=d.pico_online?'Connected':'Pico offline';
}catch(e){}
}

form.addEventListener('submit',sendMsg);
checkStatus();
setInterval(checkStatus,10000);
</script>
</body>
</html>"""


def generate_self_signed_cert():
    """Generate a self-signed certificate if not present."""
    if CERT_FILE.exists() and KEY_FILE.exists():
        return
    print("[*] Generating self-signed certificate...")
    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import rsa
        from cryptography.hazmat.backends import default_backend
        import datetime

        key = rsa.generate_private_key(
            public_exponent=65537, key_size=2048, backend=default_backend()
        )
        subject = issuer = x509.Name([
            x509.NameAttribute(NameOID.COUNTRY_NAME, "ZA"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "IO-nity Global Pty Ltd"),
            x509.NameAttribute(NameOID.COMMON_NAME, "Station Pico"),
        ])
        cert = (
            x509.CertificateBuilder()
            .subject_name(subject)
            .issuer_name(issuer)
            .public_key(key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime.utcnow())
            .not_valid_after(datetime.datetime.utcnow() + datetime.timedelta(days=3650))
            .add_extension(
                x509.SubjectAlternativeName([x509.DNSName("station-pico.local")]),
                critical=False,
            )
            .sign(key, hashes.SHA256(), default_backend())
        )
        with open(KEY_FILE, "wb") as f:
            f.write(key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.TraditionalOpenSSL,
                encryption_algorithm=serialization.NoEncryption(),
            ))
        with open(CERT_FILE, "wb") as f:
            f.write(cert.public_bytes(serialization.Encoding.PEM))
        print("[+] Certificate generated: cert.pem, key.pem")
    except ImportError:
        print("[!] cryptography not installed. Using openssl fallback...")
        os.system(
            f'openssl req -x509 -newkey rsa:2048 -keyout "{KEY_FILE}" '
            f'-out "{CERT_FILE}" -days 3650 -nodes '
            f'-subj "/C=ZA/O=IO-nity Global Pty Ltd/CN=Station Pico"'
        )


class PicoProxy:
    """Proxy to the Pico's HTTP API."""

    def __init__(self, pico_ip: str):
        self.pico_ip = pico_ip
        self.online = False

    def send_message(self, author: str, text: str) -> dict:
        """Send a message to the Pico display."""
        url = f"http://{self.pico_ip}/message"
        data = json.dumps({"author": author, "text": text}).encode()
        try:
            req = urllib.request.Request(
                url, data=data,
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with urllib.request.urlopen(req, timeout=5) as resp:
                self.online = True
                return {"status": "ok"}
        except Exception as e:
            self.online = False
            return {"status": "error", "error": str(e)}

    def get_messages(self) -> list:
        """Get messages from the Pico."""
        try:
            with urllib.request.urlopen(
                f"http://{self.pico_ip}/messages", timeout=5
            ) as resp:
                self.online = True
                return json.loads(resp.read())
        except Exception:
            self.online = False
            return {"messages": []}


class RequestHandler(BaseHTTPRequestHandler):
    pico_proxy: PicoProxy = None

    def log_message(self, format, *args):
        print(f"[{time.strftime('%H:%M:%S')}] {args[0]}")

    def _send_json(self, data, status=200):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_html(self, html, status=200):
        body = html.encode()
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            self._send_html(HTML_PAGE)
        elif self.path == "/api/status":
            self._send_json({
                "pico_ip": self.pico_proxy.pico_ip,
                "pico_online": self.pico_proxy.online,
            })
        elif self.path == "/api/messages":
            data = self.pico_proxy.get_messages()
            self._send_json(data)
        else:
            self._send_json({"error": "not found"}, 404)

    def do_POST(self):
        if self.path == "/api/proxy":
            content_len = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(content_len)
            try:
                data = json.loads(body)
                result = self.pico_proxy.send_message(
                    data.get("author", "Anonymous"),
                    data.get("text", ""),
                )
                self._send_json(result)
            except json.JSONDecodeError:
                self._send_json({"status": "error", "error": "invalid json"}, 400)
        else:
            self._send_json({"error": "not found"}, 404)


def main():
    parser = argparse.ArgumentParser(description="IO-nity Station Pico Bridge Server")
    parser.add_argument("--pico-ip", default="192.168.1.100",
                        help="IP address of the Pico (default: 192.168.1.100)")
    parser.add_argument("--port", type=int, default=8443,
                        help="HTTPS port (default: 8443)")
    parser.add_argument("--no-ssl", action="store_true",
                        help="Disable HTTPS (use plain HTTP)")
    args = parser.parse_args()

    os.chdir(APP_DIR)

    # Generate certificate
    if not args.no_ssl:
        generate_self_signed_cert()

    # Setup proxy
    proxy = PicoProxy(args.pico_ip)
    RequestHandler.pico_proxy = proxy

    server = HTTPServer(("0.0.0.0", args.port), RequestHandler)

    if not args.no_ssl and CERT_FILE.exists():
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(CERT_FILE, KEY_FILE)
        server.socket = ctx.wrap_socket(server.socket, server_side=True)
        proto = "https"
    else:
        proto = "http"

    print(f"""
╔══════════════════════════════════════════════╗
║         IO-NITY Station Pico Bridge          ║
╠══════════════════════════════════════════════╣
║  Pico IP:   {args.pico_ip:<33} ║
║  Server:    {proto}://0.0.0.0:{args.port:<28} ║
║  Cert:      {CERT_FILE if CERT_FILE.exists() else 'none':<33} ║
╚══════════════════════════════════════════════╝

Open this on any device on your WiFi:
  {proto}://<this-pc-ip>:{args.port}

Press Ctrl+C to stop.
""")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[!] Shutting down...")
        server.shutdown()


if __name__ == "__main__":
    main()