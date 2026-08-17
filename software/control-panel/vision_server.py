"""
Octavius Vision Server

Grabs camera frames from the Watcher, runs SmolVLM2 for scene descriptions.
Serves results via local HTTP API.

Usage:
    python vision_server.py
"""

import socket
import time
import base64
import json
import threading
import os
from io import BytesIO
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler

import torch
from PIL import Image, ImageFilter

# ---- Config ----
WATCHER_IP = None
CAMERA_PORT = 8082
VISION_API_PORT = 8090
FRAME_INTERVAL = 5

# ---- State ----
latest_frame = None
latest_frame_time = 0
frame_lock = threading.Lock()
cam_sock = None  # persistent camera connection

# ---- Models ----
smol_model = None
smol_processor = None


def ts():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


WATCHER_IP_FILE = os.path.join(os.path.dirname(__file__), ".watcher_ip")

def discover_watcher(timeout=60):
    """Find Watcher — via env, shared IP file, phone-home UDP, or subnet scan."""
    import subprocess

    # Check env var
    env_ip = os.environ.get("WATCHER_IP")
    if env_ip:
        print(f"[{ts()}] Using Watcher IP from env: {env_ip}")
        return env_ip

    # Check shared IP file (written by assistant)
    if os.path.exists(WATCHER_IP_FILE):
        with open(WATCHER_IP_FILE) as f:
            ip = f.read().strip()
        if ip:
            try:
                s = socket.socket()
                s.settimeout(3)
                s.connect((ip, CAMERA_PORT))
                s.close()
                print(f"[{ts()}] Using Watcher from cache: {ip}")
                return ip
            except:
                pass

    # Scan local subnet + re-check cached file periodically
    result = subprocess.run(['ip', 'route', 'get', '1.1.1.1'], capture_output=True, text=True)
    local_ip = None
    words = result.stdout.split()
    for i, word in enumerate(words):
        if word == 'src' and i + 1 < len(words):
            local_ip = words[i + 1]
            break
    subnet = '.'.join(local_ip.split('.')[:3]) if local_ip else None
    print(f"[{ts()}] Scanning {subnet}.0/24 for Watcher..." if subnet else "")
    start = time.time()
    while time.time() - start < timeout:
        # Re-check cached file (assistant may have found it via phone-home)
        if os.path.exists(WATCHER_IP_FILE):
            with open(WATCHER_IP_FILE) as f:
                cached = f.read().strip()
            if cached:
                try:
                    s = socket.socket()
                    s.settimeout(3)
                    s.connect((cached, CAMERA_PORT))
                    s.close()
                    print(f"[{ts()}] Using Watcher from cache: {cached}")
                    return cached
                except:
                    pass

        # Scan local subnet
        if subnet:
            for i in range(1, 255):
                ip = f"{subnet}.{i}"
                if ip == local_ip:
                    continue
                try:
                    s = socket.socket()
                    s.settimeout(0.3)
                    s.connect((ip, 8080))
                    s.close()
                    print(f"[{ts()}] Found Watcher at {ip}")
                    return ip
                except:
                    pass
        print(f"[{ts()}]   No Watcher found, retrying...")
        time.sleep(3)
    return None


def grab_frame(watcher_ip):
    """Grab a single frame using persistent connection."""
    global cam_sock
    # Connect if needed
    if cam_sock is None:
        try:
            s = socket.socket()
            s.settimeout(8)
            s.connect((watcher_ip, CAMERA_PORT))
            cam_sock = s
            print(f"[{ts()}] Camera connected to {watcher_ip}:{CAMERA_PORT}")
        except Exception as e:
            print(f"[{ts()}] grab_frame: connect failed: {e}")
            return None
    try:
        cam_sock.sendall(b"snap\n")
        data = b""
        while b"\n" not in data:
            chunk = cam_sock.recv(1024)
            if not chunk:
                raise ConnectionError("connection closed")
            data += chunk
        header = data.split(b"\n")[0].decode()
        if not header.startswith("B64"):
            # Soft error (e.g. "ERROR timeout") — keep connection, just retry next time
            return None
        size = int(header.split()[1])
        b64data = data[len(header) + 1:]
        while len(b64data) < size:
            chunk = cam_sock.recv(4096)
            if not chunk:
                raise ConnectionError("connection closed mid-transfer")
            b64data += chunk
        jpeg_bytes = base64.b64decode(b64data[:size])
        img = Image.open(BytesIO(jpeg_bytes)).convert("RGB")
        # Upscale small images for better VLM performance
        if img.width <= 240:
            img = img.resize((480, 480), Image.LANCZOS)
            img = img.filter(ImageFilter.SHARPEN)
        print(f"[{ts()}] grab_frame: OK {img.size}, {len(jpeg_bytes)} bytes")
        return img
    except Exception as e:
        print(f"[{ts()}] grab_frame error: {e}")
        try: cam_sock.close()
        except: pass
        cam_sock = None
        return None


def load_smolvlm():
    global smol_model, smol_processor
    if smol_model is not None:
        return
    from transformers import AutoModelForImageTextToText, AutoProcessor
    print(f"[{ts()}] Loading SmolVLM2-2.2B on GPU...")
    smol_model = AutoModelForImageTextToText.from_pretrained(
        "HuggingFaceTB/SmolVLM2-2.2B-Instruct",
        dtype=torch.bfloat16,
        device_map="cuda",
    )
    smol_processor = AutoProcessor.from_pretrained("HuggingFaceTB/SmolVLM2-2.2B-Instruct")
    # Warmup
    dummy = Image.new("RGB", (64, 64))
    inputs = smol_processor(text="<|im_start|>user\n<image>Hi<|im_end|>\n<|im_start|>assistant\n", images=[dummy], return_tensors="pt").to("cuda")
    smol_model.generate(**inputs, max_new_tokens=5)
    print(f"[{ts()}] SmolVLM2 ready")


def describe_image(image, prompt="This is a low-resolution 240x240 camera image. Describe ONLY what you can clearly identify — do not guess or invent details you're unsure about. Be brief, 1-2 sentences. Focus on people, their position, and obvious large objects."):
    load_smolvlm()
    full_prompt = f"<|im_start|>user\n<image>{prompt}<|im_end|>\n<|im_start|>assistant\n"
    inputs = smol_processor(text=full_prompt, images=[image], return_tensors="pt").to("cuda")
    t0 = time.time()
    out = smol_model.generate(**inputs, max_new_tokens=150, do_sample=False)
    answer = smol_processor.decode(out[0][inputs["input_ids"].shape[1]:], skip_special_tokens=True)
    print(f"[{ts()}] SmolVLM2 ({time.time()-t0:.1f}s): {answer[:80]}...")
    return answer


# ---- Frame Grabber Thread ----

def grab_fresh_frame():
    """Grab a fresh frame on-demand. Caches for a few seconds to avoid hammering."""
    global latest_frame, latest_frame_time
    # Return cached frame if recent enough (within 3 seconds)
    with frame_lock:
        if latest_frame is not None and time.time() - latest_frame_time < 3:
            return latest_frame

    if not WATCHER_IP:
        return None

    img = grab_frame(WATCHER_IP)
    if img is not None:
        with frame_lock:
            latest_frame = img
            latest_frame_time = time.time()
    return img


# ---- HTTP API ----

class VisionHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def do_GET(self):
        if self.path == "/describe":
            img = grab_fresh_frame()
            if img is None:
                self._json_response({"error": "no frame"}, 503)
                return
            description = describe_image(img)
            with frame_lock:
                age = time.time() - latest_frame_time if latest_frame_time else -1
            self._json_response({"description": description, "frame_age": round(age, 1)})

        elif self.path == "/snap":
            img = grab_fresh_frame()
            if img is None:
                self.send_response(503)
                self.end_headers()
                return
            buf = BytesIO()
            img.save(buf, format="JPEG", quality=85)
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.end_headers()
            self.wfile.write(buf.getvalue())

        elif self.path == "/status":
            self._json_response({"status": "ok", "watcher_ip": WATCHER_IP})

        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == "/describe":
            content_length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(content_length).decode() if content_length else ""
            prompt = json.loads(body).get("prompt", "Describe what you see.") if body else "Describe what you see."
            img = grab_fresh_frame()
            if img is None:
                self._json_response({"error": "no frame"}, 503)
                return
            description = describe_image(img, prompt)
            self._json_response({"description": description})
        else:
            self.send_response(404)
            self.end_headers()

    def _json_response(self, data, status=200):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())


def watcher_discovery():
    """Background: find Watcher IP."""
    global WATCHER_IP
    WATCHER_IP = discover_watcher()
    if not WATCHER_IP:
        print(f"[{ts()}] Could not find Watcher!")
    else:
        print(f"[{ts()}] Watcher ready at {WATCHER_IP} — camera is on-demand")


def main():
    load_smolvlm()

    # Start HTTP API immediately so assistant can connect
    server = HTTPServer(("0.0.0.0", VISION_API_PORT), VisionHandler)
    server.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    print(f"[{ts()}] Vision API on http://localhost:{VISION_API_PORT}")
    print(f"[{ts()}]   GET  /describe — SmolVLM2 scene description")
    print(f"[{ts()}]   GET  /snap     — latest JPEG")
    print(f"[{ts()}]   GET  /status   — health check")

    # Discover Watcher in background (camera grabs are on-demand now)
    threading.Thread(target=watcher_discovery, daemon=True).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print(f"\n[{ts()}] Vision server stopped")


if __name__ == "__main__":
    main()
