"""
Sesame Robot Control Panel — no dependencies, just stdlib.
Usage: python3 server.py
Open http://localhost:5000
"""

import socket
import threading
import time
import json
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

WATCHER_IP = "192.168.0.3"
WATCHER_PORT = 8080

# Nano pin order: [D4=L4, D5=L2, D6=L3, D7=L1, D8=R2, D9=R4, D10=R3, D11=R1]
R1, R2, L1, L2, R4, R3, L3, L4 = 7, 4, 3, 1, 5, 6, 2, 0

FRAME_DELAY = 0.1

sock = None
sock_lock = threading.Lock()
current_angles = {i: 90 for i in range(8)}


def get_socket():
    global sock
    with sock_lock:
        if sock is None:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            sock.connect((WATCHER_IP, WATCHER_PORT))
        return sock


def reset_socket():
    global sock
    with sock_lock:
        if sock:
            try: sock.close()
            except: pass
            sock = None


def send_raw(cmd):
    try:
        get_socket().sendall((cmd + "\n").encode())
    except socket.error:
        reset_socket()
        try: get_socket().sendall((cmd + "\n").encode())
        except: pass


def send_angles(updates):
    current_angles.update(updates)
    send_raw("a " + " ".join(str(current_angles[i]) for i in range(8)))


def stand():
    send_angles({R1: 135, R2: 45, L1: 45, L2: 135, R4: 0, R3: 180, L3: 0, L4: 180})

def rest():
    send_angles({i: 90 for i in range(8)})

def walk_forward(cycles=3):
    send_raw("rl"); time.sleep(0.3)
    send_angles({R3: 135, L3: 45, R2: 100, L1: 25})
    for _ in range(cycles):
        send_angles({R3: 135, L3: 0}); time.sleep(FRAME_DELAY)
        send_angles({L4: 135, L2: 90, R4: 0, R1: 180}); time.sleep(FRAME_DELAY)
        send_angles({R2: 45, L1: 90}); time.sleep(FRAME_DELAY)
        send_angles({R4: 45, L4: 180}); time.sleep(FRAME_DELAY)
        send_angles({R3: 180, L3: 45, R2: 90, L1: 0}); time.sleep(FRAME_DELAY)
        send_angles({L2: 135, R1: 90}); time.sleep(FRAME_DELAY)
    stand()

def walk_backward(cycles=3):
    send_raw("rl"); time.sleep(0.3)
    for _ in range(cycles):
        send_angles({R3: 135, L3: 0}); time.sleep(FRAME_DELAY)
        send_angles({L4: 135, L2: 135, R4: 0, R1: 90}); time.sleep(FRAME_DELAY)
        send_angles({R2: 90, L1: 0}); time.sleep(FRAME_DELAY)
        send_angles({R4: 45, L4: 180}); time.sleep(FRAME_DELAY)
        send_angles({R3: 180, L3: 45, R2: 45, L1: 90}); time.sleep(FRAME_DELAY)
        send_angles({L2: 90, R1: 180}); time.sleep(FRAME_DELAY)
    stand()

def turn_left(cycles=3):
    send_raw("rl"); time.sleep(0.3)
    for _ in range(cycles):
        send_angles({R3: 135, L4: 135}); time.sleep(FRAME_DELAY)
        send_angles({R1: 180, L2: 180}); time.sleep(FRAME_DELAY)
        send_angles({R3: 180, L4: 180}); time.sleep(FRAME_DELAY)
        send_angles({R1: 135, L2: 135}); time.sleep(FRAME_DELAY)
        send_angles({R4: 45, L3: 45}); time.sleep(FRAME_DELAY)
        send_angles({R2: 90, L1: 90}); time.sleep(FRAME_DELAY)
        send_angles({R4: 0, L3: 0}); time.sleep(FRAME_DELAY)
        send_angles({R2: 45, L1: 45}); time.sleep(FRAME_DELAY)
    stand()

def turn_right(cycles=3):
    send_raw("rl"); time.sleep(0.3)
    for _ in range(cycles):
        send_angles({R4: 45, L3: 45}); time.sleep(FRAME_DELAY)
        send_angles({R2: 0, L1: 0}); time.sleep(FRAME_DELAY)
        send_angles({R4: 0, L3: 0}); time.sleep(FRAME_DELAY)
        send_angles({R2: 45, L1: 45}); time.sleep(FRAME_DELAY)
        send_angles({R3: 135, L4: 135}); time.sleep(FRAME_DELAY)
        send_angles({R1: 90, L2: 90}); time.sleep(FRAME_DELAY)
        send_angles({R3: 180, L4: 180}); time.sleep(FRAME_DELAY)
        send_angles({R1: 135, L2: 135}); time.sleep(FRAME_DELAY)
    stand()

COMMANDS = {
    "stand": stand, "rest": rest,
    "forward": walk_forward, "backward": walk_backward,
    "left": turn_left, "right": turn_right,
    "stop": lambda: send_raw("stop"),
}

HTML = b"""<!DOCTYPE html>
<html>
<head>
  <title>Sesame Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root { --orange: #ff8c42; --orange-dark: #e67a30; }
    * { user-select: none; -webkit-user-select: none; box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Segoe UI', Roboto, sans-serif; text-align: center;
      background: linear-gradient(135deg, #0a0a0a, #1a1a2e);
      color: #e0e0e0; padding: 20px; min-height: 100vh;
    }
    h1 { color: #fff; font-size: 26px; margin: 10px 0 20px; }
    .status {
      font-size: 13px; color: #2ecc71; margin-bottom: 20px;
      padding: 6px 14px; border: 1px solid #2ecc71; border-radius: 20px;
      display: inline-block; background: rgba(46,204,113,0.1);
    }
    .status.error { color: #e63946; border-color: #e63946; background: rgba(230,57,70,0.1); }
    .status.busy { color: #f39c12; border-color: #f39c12; background: rgba(243,156,18,0.1); }
    .section {
      background: rgba(30,30,30,0.8); border: 1px solid #333;
      border-radius: 16px; padding: 15px; margin: 0 auto 15px; max-width: 400px;
    }
    .section-title {
      font-size: 13px; font-weight: 600; color: var(--orange);
      margin-bottom: 12px; text-transform: uppercase; letter-spacing: 1px;
    }
    .dpad {
      display: grid; grid-template-columns: repeat(3, 1fr);
      gap: 10px; max-width: 280px; margin: 0 auto;
    }
    .dpad button {
      font-size: 28px; min-height: 65px; border: 2px solid #555;
      background: linear-gradient(145deg, #3a3a3a, #2a2a2a);
      color: #fff; border-radius: 12px; cursor: pointer;
      transition: transform 0.1s;
    }
    .dpad button:active { transform: scale(0.93); background: #222; }
    .spacer { visibility: hidden; }
    .grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; }
    .btn-pose {
      background: linear-gradient(145deg, var(--orange), var(--orange-dark));
      border: none; color: #fff; padding: 12px 8px; font-size: 14px;
      border-radius: 10px; cursor: pointer; font-weight: 500;
    }
    .btn-pose:active { transform: scale(0.93); filter: brightness(0.85); }
    .btn-stop {
      background: linear-gradient(145deg, #e63946, #c92a35);
      border: 2px solid #ff6b6b; color: #fff; padding: 16px;
      font-size: 18px; border-radius: 12px; cursor: pointer;
      width: 100%; max-width: 400px; margin: 10px auto 0;
      text-transform: uppercase; letter-spacing: 2px; font-weight: 600;
      display: block;
    }
    .btn-stop:active { transform: scale(0.97); }
    body.busy button:not(.btn-stop) { pointer-events: none; opacity: 0.4; }
  </style>
</head>
<body>
  <h1>Sesame Controller</h1>
  <div id="status" class="status">Ready</div>

  <div class="section">
    <div class="section-title">Movement</div>
    <div class="dpad">
      <div class="spacer"></div>
      <button onclick="cmd('forward')">&#9650;</button>
      <div class="spacer"></div>
      <button onclick="cmd('left')">&#9664;</button>
      <button onclick="cmd('stand')">&#9724;</button>
      <button onclick="cmd('right')">&#9654;</button>
      <div class="spacer"></div>
      <button onclick="cmd('backward')">&#9660;</button>
      <div class="spacer"></div>
    </div>
  </div>

  <div class="section">
    <div class="section-title">Poses</div>
    <div class="grid">
      <button class="btn-pose" onclick="cmd('stand')">Stand</button>
      <button class="btn-pose" onclick="cmd('rest')">Rest</button>
      <button class="btn-pose" onclick="cmd('stop')">Stop</button>
    </div>
  </div>

  <button class="btn-stop" onclick="cmd('stop')">EMERGENCY STOP</button>

  <script>
    let busy = false;
    function cmd(c) {
      if (busy && c !== 'stop') return;
      busy = true;
      document.body.classList.add('busy');
      const s = document.getElementById('status');
      s.textContent = c === 'stop' ? 'Stopping...' : 'Running: ' + c;
      s.className = 'status busy';

      fetch('/cmd?c=' + c)
        .then(r => r.json())
        .then(d => {
          s.textContent = d.status === 'ok' ? 'Ready' : 'Error';
          s.className = d.status === 'ok' ? 'status' : 'status error';
        })
        .catch(() => {
          s.textContent = 'Connection lost';
          s.className = 'status error';
        })
        .finally(() => {
          busy = false;
          document.body.classList.remove('busy');
        });
    }
  </script>
</body>
</html>"""


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(HTML)
        elif parsed.path == "/cmd":
            params = parse_qs(parsed.query)
            c = params.get("c", [""])[0]
            if c in COMMANDS:
                try:
                    COMMANDS[c]()
                    resp = {"status": "ok", "command": c}
                except Exception as e:
                    reset_socket()
                    resp = {"status": "error", "error": str(e)}
            else:
                resp = {"status": "error", "error": "unknown"}
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, fmt, *args):
        pass  # silence logs


if __name__ == "__main__":
    print(f"Connecting to Watcher at {WATCHER_IP}:{WATCHER_PORT}...")
    try:
        get_socket()
        print("Connected!")
    except Exception as e:
        print(f"Warning: {e}. Will retry on first command.")
        reset_socket()

    print("Open http://localhost:5000")
    server = HTTPServer(("0.0.0.0", 5000), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()
