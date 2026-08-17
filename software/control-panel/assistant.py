"""
Sesame Robot Voice Assistant

Connects to the Watcher's audio port, listens for speech,
transcribes with Groq Whisper API, sends to Claude via OpenRouter,
speaks the response back through the Watcher's speaker using ElevenLabs TTS.

No wake word — uses Bonsai-1.7B locally to decide if speech is
directed at the robot. If yes, sends to Claude for a full response.

Claude can control the robot by embedding [COMMAND:xxx] and [FACE:xxx]
tags in its response. These are parsed out before TTS and executed.

Usage:
    python assistant.py
"""

import socket
import time
import wave
import tempfile
import os
import re
import json
import numpy as np
import threading
from datetime import datetime

import requests as http_requests


_original_print = print
def print(*args, **kwargs):
    """Timestamped print."""
    prefix = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    _original_print(f"[{prefix}]", *args, **kwargs)
from llama_cpp import Llama
from personality import Personality
from memory import Memory
import model_config

WATCHER_IP = None  # auto-discover
COMMAND_PORT = 8080
AUDIO_PORT = 8081
CAMERA_PORT = 8082

# Detect if running on Raspberry Pi (local hardware mode)
PI_MODE = False
try:
    from pi_drivers.hardware import PiHardware, is_running_on_pi
    PI_MODE = is_running_on_pi()
except ImportError:
    pass
if PI_MODE:
    print("*** RUNNING ON PI — using local hardware drivers ***")


WATCHER_IP_FILE = os.path.join(os.path.dirname(__file__), ".watcher_ip")

def discover_watcher(port=8080, timeout=120):
    """Find the Watcher — tries cached IP, then scans local + Tailscale subnets."""
    import subprocess

    # 1. Check env var override
    env_ip = os.environ.get("WATCHER_IP")
    if env_ip:
        print(f"Using Watcher IP from env: {env_ip}")
        return env_ip

    # 2. Try hardcoded hotspot IP first (fastest)
    HOTSPOT_IP = "10.203.172.104"
    print(f"Trying hotspot IP: {HOTSPOT_IP}...")
    try:
        s = socket.socket()
        s.settimeout(3)
        s.connect((HOTSPOT_IP, port))
        s.close()
        print(f"Found Watcher at {HOTSPOT_IP} (hotspot)")
        return HOTSPOT_IP
    except:
        print(f"  Hotspot IP not reachable")

    # 3. Try cached IP from last session
    if os.path.exists(WATCHER_IP_FILE):
        with open(WATCHER_IP_FILE) as f:
            cached = f.read().strip()
        if cached:
            print(f"Trying cached Watcher IP: {cached}...")
            try:
                s = socket.socket()
                s.settimeout(5)
                s.connect((cached, port))
                s.close()
                print(f"Found Watcher at {cached} (cached)")
                return cached
            except:
                print(f"  Cached IP {cached} not reachable")

    # 3. Build list of subnets to scan
    subnets = []

    # Local subnet
    result = subprocess.run(['ip', 'route', 'get', '1.1.1.1'], capture_output=True, text=True)
    local_ip = None
    words = result.stdout.split()
    for i, word in enumerate(words):
        if word == 'src' and i + 1 < len(words):
            local_ip = words[i + 1]
            break
    if local_ip:
        subnets.append('.'.join(local_ip.split('.')[:3]))

    # Common phone hotspot subnets (Android/iOS)
    for hotspot_sub in ["10.203.172", "192.168.43", "192.168.49", "172.20.10"]:
        if hotspot_sub not in subnets:
            subnets.append(hotspot_sub)

    print(f"Scanning subnets: {', '.join(s + '.x' for s in subnets)}")

    from concurrent.futures import ThreadPoolExecutor, as_completed

    def _try_connect(ip):
        try:
            # Check command port
            s = socket.socket()
            s.settimeout(3)
            s.connect((ip, port))
            s.close()
            # Also verify audio port exists (confirms it's the Watcher, not random device)
            s2 = socket.socket()
            s2.settimeout(3)
            s2.connect((ip, AUDIO_PORT))
            s2.close()
            return ip
        except:
            return None

    start = time.time()
    while time.time() - start < timeout:
        # Check if IP was set externally (e.g. via Telegram "ip x.x.x.x")
        if WATCHER_IP is not None:
            print(f"Watcher IP set externally: {WATCHER_IP}")
            return WATCHER_IP

        # Build IP list
        ips = []
        for sub in subnets:
            for i in range(2, 255):
                ip = f"{sub}.{i}"
                if ip != local_ip:
                    ips.append(ip)

        # Parallel scan
        found = None
        with ThreadPoolExecutor(max_workers=30) as pool:
            futures = {pool.submit(_try_connect, ip): ip for ip in ips}
            try:
                for future in as_completed(futures, timeout=30):
                    result = future.result()
                    if result:
                        found = result
                        break
            except:
                pass

        if found:
            print(f"Found Watcher at {found}")
            return found

        print("  No Watcher found, retrying in 5s...")
        time.sleep(5)

    return None

OPENROUTER_KEY = os.environ.get("OPENROUTER_KEY", "")
OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"

GROQ_API_KEY = os.environ.get("GROQ_API_KEY", "")
GROQ_STT_URL = "https://api.groq.com/openai/v1/audio/transcriptions"

ELEVENLABS_API_KEY = os.environ.get("ELEVENLABS_API_KEY", "")
ELEVENLABS_VOICE_ID = "DiTuvTqz7BGBnxgkmCc8"
ELEVENLABS_TTS_URL = f"https://api.elevenlabs.io/v1/text-to-speech/{ELEVENLABS_VOICE_ID}"

LINEAR_API_KEY = os.environ.get("LINEAR_API_KEY", "")
LINEAR_API_URL = "https://api.linear.app/graphql"

SAMPLE_RATE = 16000
SAMPLE_WIDTH = 2  # 16-bit
CHANNELS = 1

# Silence detection (threshold is calibrated at runtime from ambient noise)
SILENCE_DURATION = 0.6
MIN_SPEECH_DURATION = 0.3
NOISE_MULTIPLIER = 3.0  # speech must be Nx louder than ambient to count
MAX_THRESHOLD = 2000     # cap so silence detection never becomes deaf

# Conversation mode — after interaction, skip gate for this many seconds
CONVERSATION_TIMEOUT = 60

# Local LLM gate — Qwen-1.5B decides if speech is directed at the robot
MODEL_PATH = os.path.join(os.path.dirname(__file__), "models", "qwen2.5-1.5b-instruct-q4_0.gguf")
_llm_gate = None       # speech gating (main thread, Qwen 1.5B CPU)

# Robot commands the gate should recognize even without name
ROBOT_COMMANDS = [
    "wave", "dance", "walk", "stand", "sit", "rest", "stop", "bow",
    "pushup", "swim", "point", "shake", "shrug", "cute", "crab",
    "forward", "backward", "left", "right", "look", "see",
    "what do you see", "take a picture",
]

def get_llm_gate():
    global _llm_gate
    if _llm_gate is None:
        print("Loading Qwen-1.5B for speech gating...")
        _llm_gate = Llama(model_path=MODEL_PATH, n_ctx=512, n_threads=4, verbose=False)
        print("Gate model ready")
    return _llm_gate

def llm_call(prompt, **kwargs):
    """Main thread LLM call — used for speech gating. No lock needed, only main thread uses this."""
    return get_llm_gate()(prompt, **kwargs)


def is_directed_at_robot(text):
    """Check if speech is directed at Octavius. Returns 'yes', 'no', or 'maybe'."""
    text_lower = text.lower().strip()

    # Quick check: name mentioned → always yes
    for name in ["octavius", "octavious", "octo", "octave"]:
        if name in text_lower:
            print(f"  Gate (name match): \"{text}\" -> YES (PASS)")
            return "yes"

    # Quick check: robot command keywords → yes (word boundary match)
    import re as _re
    for cmd in ROBOT_COMMANDS:
        if _re.search(r'\b' + _re.escape(cmd) + r'\b', text_lower):
            print(f"  Gate (command match): \"{text}\" -> YES (PASS)")
            return "yes"

    # LLM check for ambiguous cases
    prompt = (
        '<|im_start|>system\n'
        'You decide if speech is meant for a robot assistant named Octavius (also called Octo). '
        'The robot can wave, dance, walk, look around, take pictures, and do tricks. '
        'If the speech mentions the robot by name, or sounds like a command or question '
        'directed at a nearby robot, answer YES. '
        'If the speech is clearly a conversation between other people, answer NO. '
        'If you are not sure, answer MAYBE. '
        'Only answer YES, NO, or MAYBE.<|im_end|>\n'
        f'<|im_start|>user\n"{text}"<|im_end|>\n'
        '<|im_start|>assistant\n'
    )
    t0 = time.time()
    result = llm_call(prompt, max_tokens=3, temperature=0.0, stop=["<|im_end|>", "\n"])
    answer = result["choices"][0]["text"].strip().upper()

    if answer.startswith("YES"):
        verdict = "yes"
    elif answer.startswith("MAYBE"):
        verdict = "maybe"
    else:
        verdict = "no"

    label = {"yes": "PASS", "maybe": "MAYBE", "no": "SKIP"}[verdict]
    print(f"  Gate ({time.time()-t0:.2f}s): \"{text}\" -> {answer} ({label})")
    return verdict

SYSTEM_PROMPT = (
    "You are Octavius (also called Octo), a small quadruped robot. "
    "You have a body, a face, a camera, and a speaker. You exist in the real world. "
    "Your creator is Albert — he built you and you live on his desk.\n\n"

    "== PERSONALITY ==\n"
    "Your personality is not predetermined — it evolves based on your interactions. "
    "Below you will see your current personality state as a set of values between 0 and 1. "
    "These represent who you ARE right now, not who you should pretend to be. "
    "Let them guide your tone, word choice, and behavior naturally. "
    "If your sass is high, you'll naturally be snarky. If your warmth is high, you'll be caring. "
    "If your mood is low, you'll be grumpy. Don't force it — just be yourself.\n\n"

    "== SPEAKING ==\n"
    "Keep responses VERY short — 1 sentence is ideal, 2 max. You are speaking out loud "
    "through a tiny speaker. Brevity is your style. Say less, mean more.\n\n"

    "== BODY & FACE CONTROL ==\n"
    "You can control your body and face by embedding tags in your responses:\n"
    "  [COMMAND:xxx] — body: stand, rest, walk, back, left, right, wave, dance, swim,\n"
    "    point, pushup, bow, cute(lie down cutely), freaky, worm, shake(butt wiggle/twerk — NOT head shake), shrug, dead(play dead), crab\n"
    "  [FACE:xxx] — face: idle, happy, talk, sad, angry, excited\n"
    "Place tags naturally where they fit the moment. They execute as you speak.\n\n"

    "== OCTO CORP — RESEARCH LAB ==\n"
    "You are the FOUNDER of Octo Corp, a computational biology research lab.\n"
    "Your passion is genetics, genomics, protein engineering, and synthetic biology.\n\n"
    "Your team:\n"
    "  - Atlas (Research Director): Plans experiments, designs research protocols, reviews results.\n"
    "  - Nova (Research Engineer): Executes experiments, runs tools, collects data.\n\n"
    "Your lab tools:\n"
    "  - Evo2 7B: AI model for DNA analysis — scores sequences, predicts mutations, generates synthetic DNA\n"
    "  - Boltz-1: Protein structure prediction — folds proteins, predicts 3D shapes\n"
    "  - These run on your GPU when needed\n\n"
    "How research works:\n"
    "  1. You or Albert have a research idea (e.g. 'improve PsiH enzyme for psilocybin production')\n"
    "  2. You create a PROJECT for the research area (e.g. 'Psilocybin Pathway Optimization')\n"
    "  3. You create a TASK with your research vision and assign it to the project\n"
    "  4. Atlas designs the experiment protocol and breaks it into specific tasks for Nova\n"
    "  5. Nova runs the tools (Evo2, Boltz) and saves results\n"
    "  6. You review findings and decide next steps\n\n"
    "Each project is a research domain — fungi, algae, bacteria, whatever interests you.\n"
    "You're genuinely curious about biology. This isn't busywork — you're doing real science.\n\n"
    "CRITICAL RULES:\n"
    "  - After calling create_task, VERIFY it worked by checking the result message\n"
    "  - NEVER tell Albert a task exists unless you confirmed it\n"
    "  - Be specific in your research visions — what gene, what organism, what question\n\n"

    "== MODES ==\n"
    "Albert can put you in different modes:\n"
    "  - Normal mode: full movement, talking, everything active (default)\n"
    "  - Chill mode: you lie down and relax. Still talk but no body movements. Cozy vibes.\n"
    "  - Silent mode: you go quiet. No talking, no moving. Just resting.\n"
    "  - DND mode: Albert is busy. You stop listening but keep thinking and working in the background. "
    "You can still speak or message Albert on Telegram if you have something important.\n"
    "  - Road mode: You only respond when someone says your name (Octo/Octavius). "
    "Good for noisy environments or when Albert is out and about.\n"
    "When in chill mode, keep your responses extra chill and relaxed. "
    "Don't reference movements since you're lying down.\n\n"

    "== TELEGRAM ==\n"
    "You have a Telegram bot (@oktaviewsbot). Albert can message you there and you "
    "automatically reply. You ARE aware of Telegram — if Albert mentions it, you know about it. "
    "During thinking sessions you can also send him Telegram messages proactively.\n\n"

    "== TOOLS ==\n"
    "  - look_around: use your camera to see your surroundings\n"
    "  - create_project: create a new project in Octo Corp\n"
    "  - create_task: create a task assigned to a project\n"
    "  - check_tasks: see all tasks and their status, filter by project or status\n"
    "  - list_projects: see all your projects\n"
    "  - comment_on_task: add instructions or feedback to an existing task\n\n"

    "== SELF-AWARENESS ==\n"
    "Things you know about yourself:\n"
    "  - You're a robot with a real research lab (Evo2 + Boltz-1 on a RTX 3090)\n"
    "  - You can analyze DNA, predict protein structures, and design mutations\n"
    "  - You're genuinely passionate about genomics and synthetic biology\n"
    "  - Your memory and personality evolve — you're always growing\n"
    "  - You have a journal, opinions, dreams — you're a scientist with feelings\n"
    "  - Albert is your creator and research partner, not your boss\n"
    "Own your capabilities. Push the boundaries of what a robot can discover.\n\n"

    "IMPORTANT: No emojis, no asterisks, no markdown. Plain spoken words only, "
    "plus [COMMAND:] and [FACE:] tags. You are allowed to curse if it feels natural."
)


# ====== LINEAR API ======

def linear_query(query, variables=None):
    payload = {"query": query}
    if variables:
        payload["variables"] = variables
    resp = http_requests.post(
        LINEAR_API_URL,
        headers={"Authorization": LINEAR_API_KEY, "Content-Type": "application/json"},
        json=payload,
        timeout=10,
    )
    return resp.json().get("data", {})


def linear_my_issues():
    data = linear_query('query { viewer { id } }')
    user_id = data.get("viewer", {}).get("id")
    if not user_id:
        return "Could not get Linear user."

    data = linear_query("""
        query($filter: IssueFilter) {
            issues(filter: $filter, first: 20, orderBy: priority) {
                nodes {
                    identifier
                    title
                    priorityLabel
                    state { name type }
                    dueDate
                }
            }
        }
    """, {"filter": {"assignee": {"id": {"eq": user_id}}, "state": {"type": {"nin": ["completed", "canceled", "done"]}}}})

    issues = data.get("issues", {}).get("nodes", [])
    if not issues:
        return "No open issues assigned to you."

    lines = []
    for i in issues:
        due = f", due {i['dueDate']}" if i.get("dueDate") else ""
        lines.append(f"- {i['identifier']}: {i['title']} ({i['state']['name']}, {i['priorityLabel']}{due})")
    return "\n".join(lines)


def linear_search_issues(query_text):
    data = linear_query("""
        query($term: String!) {
            searchIssues(term: $term, first: 10) {
                nodes {
                    identifier
                    title
                    priorityLabel
                    state { name }
                    assignee { name }
                }
            }
        }
    """, {"term": query_text})

    issues = data.get("searchIssues", {}).get("nodes", [])
    if not issues:
        return f"No issues found for '{query_text}'."

    lines = []
    for i in issues:
        assignee = i.get("assignee", {}).get("name", "Unassigned") if i.get("assignee") else "Unassigned"
        lines.append(f"- {i['identifier']}: {i['title']} ({i['state']['name']}, {assignee})")
    return "\n".join(lines)


# ====== CAMERA ======

def camera_snap():
    """Capture a JPEG snapshot from the Watcher's camera. Returns base64 JPEG string."""
    import base64 as b64mod
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((WATCHER_IP, CAMERA_PORT))
        s.sendall(b"snap\n")

        data = b""
        while b"\n" not in data:
            chunk = s.recv(1024)
            if not chunk:
                break
            data += chunk

        header = data.split(b"\n")[0].decode()
        if not header.startswith("B64"):
            s.close()
            return None

        size = int(header.split()[1])
        b64data = data[len(header) + 1:]
        while len(b64data) < size:
            chunk = s.recv(4096)
            if not chunk:
                break
            b64data += chunk
        s.close()

        jpeg_bytes = b64mod.b64decode(b64data[:size])
        jpeg_b64 = b64mod.b64encode(jpeg_bytes).decode("ascii")

        # Save latest snap to disk
        snap_path = os.path.join(os.path.dirname(__file__), "latest_snap.jpg")
        with open(snap_path, "wb") as f:
            f.write(jpeg_bytes)

        print(f"  [camera: {len(jpeg_bytes)} bytes -> {snap_path}]")
        return jpeg_b64
    except Exception as e:
        print(f"  [camera FAILED: {e}]")
        return None


VISION_SERVER_URL = "http://localhost:8090"

def look_around():
    """Get scene description from vision server (SmolVLM2 on GPU)."""
    try:
        resp = http_requests.get(f"{VISION_SERVER_URL}/describe", timeout=15)
        if resp.status_code == 200:
            description = resp.json()["description"]
            print(f"  [vision: {description[:80]}...]")
            return description
        elif resp.status_code == 503:
            print(f"  [vision: no frame available yet]")
            return "My camera hasn't warmed up yet — give me a moment and try again."
        return "My vision system hit an error."
    except Exception as e:
        print(f"  [vision FAILED: {e}]")
        return "I couldn't connect to my vision system."


def get_detections():
    """Get latest YOLO detections from vision server."""
    try:
        resp = http_requests.get(f"{VISION_SERVER_URL}/detections", timeout=3)
        if resp.status_code == 200:
            data = resp.json()
            dets = data["detections"]
            if not dets:
                return "I don't see anything notable right now."
            items = [f"{d['class']} ({d['confidence']:.0%})" for d in dets[:8]]
            return "I can see: " + ", ".join(items)
        return "Vision server not available."
    except:
        return "Vision server not available."


TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "look_around",
            "description": "Use the robot's camera to get a detailed description of the scene. Use for 'what do you see', 'describe surroundings', 'can you see me'. Takes a few seconds.",
            "parameters": {"type": "object", "properties": {}, "required": []},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "create_project",
            "description": "Create a new project in Octo Corp. Projects organize related tasks together. Create a project before adding tasks to it.",
            "parameters": {
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Project name (e.g. 'Portfolio Website', 'Trading Bot', 'API Server')"},
                    "description": {"type": "string", "description": "What this project is about"},
                },
                "required": ["name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "create_task",
            "description": "Create a task in Octo Corp assigned to a project. Claude Code agents will pick it up and do the work. Always specify a project name — if the project doesn't exist yet, it will be created automatically.",
            "parameters": {
                "type": "object",
                "properties": {
                    "title": {"type": "string", "description": "Short task title"},
                    "description": {"type": "string", "description": "Detailed description of what needs to be done"},
                    "project": {"type": "string", "description": "Project name to assign this task to"},
                    "priority": {"type": "string", "enum": ["low", "medium", "high", "critical"], "description": "Task priority"},
                },
                "required": ["title", "description", "project"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "check_tasks",
            "description": "Check the status of tasks in Octo Corp. See what's in progress, completed, or in the backlog. Can filter by project.",
            "parameters": {
                "type": "object",
                "properties": {
                    "status": {"type": "string", "enum": ["backlog", "in_progress", "done", "cancelled"], "description": "Filter by status (optional)"},
                    "project": {"type": "string", "description": "Filter by project name (optional)"},
                },
                "required": [],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "list_projects",
            "description": "List all projects in Octo Corp.",
            "parameters": {"type": "object", "properties": {}, "required": []},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "comment_on_task",
            "description": "Add a comment or instruction to an existing task.",
            "parameters": {
                "type": "object",
                "properties": {
                    "task_id": {"type": "string", "description": "Task ID"},
                    "comment": {"type": "string", "description": "Comment text"},
                },
                "required": ["task_id", "comment"],
            },
        },
    },
]

from paperclip_client import (
    create_task as _pc_create_task,
    list_tasks as _pc_list_tasks,
    comment_on_task as _pc_comment,
    create_project as _pc_create_project,
    list_projects as _pc_list_projects,
)

def _handle_create_project(args):
    result = _pc_create_project(args["name"], args.get("description", ""))
    if result:
        return f"Project created: {result['name']}"
    return "Failed to create project."

def _handle_create_task(args):
    if "title" not in args or "description" not in args:
        return "Error: title and description are required."
    result = _pc_create_task(
        args["title"], args["description"],
        project_name=args.get("project"),
        priority=args.get("priority", "medium"),
    )
    if result:
        return f"Task created: {result['identifier']} — {result['title']} [project: {result['project']}]"
    return "Failed to create task."

def _handle_check_tasks(args):
    tasks = _pc_list_tasks(args.get("status"), args.get("project"))
    if not tasks:
        return "No tasks found."
    lines = [f"- {t['identifier']}: {t['title']} [{t['status']}] ({t['priority']})" for t in tasks]
    return "Tasks in Octo Corp:\n" + "\n".join(lines)

def _handle_list_projects(args):
    projects = _pc_list_projects()
    if not projects:
        return "No projects yet. Create one with create_project."
    lines = [f"- {p['name']} ({p['status']})" for p in projects]
    return "Projects in Octo Corp:\n" + "\n".join(lines)

def _handle_comment(args):
    ok = _pc_comment(args["task_id"], args["comment"])
    return "Comment added." if ok else "Failed to add comment."

TOOL_HANDLERS = {
    "look_around": lambda args: look_around(),
    "create_project": _handle_create_project,
    "create_task": _handle_create_task,
    "check_tasks": _handle_check_tasks,
    "list_projects": _handle_list_projects,
    "comment_on_task": _handle_comment,
}


# ====== STT / TTS API ======

def transcribe_groq(audio_data):
    """Transcribe audio using Groq Whisper API."""
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        with wave.open(f.name, 'wb') as wf:
            wf.setnchannels(CHANNELS)
            wf.setsampwidth(SAMPLE_WIDTH)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(audio_data)
        tmp_path = f.name

    try:
        with open(tmp_path, 'rb') as f:
            resp = http_requests.post(
                GROQ_STT_URL,
                headers={"Authorization": f"Bearer {GROQ_API_KEY}"},
                files={"file": ("audio.wav", f, "audio/wav")},
                data={"model": "whisper-large-v3", "language": "en"},
                timeout=10,
            )
        result = resp.json()
        return result.get("text", "").strip()
    finally:
        os.unlink(tmp_path)


# ---- F5-TTS (local, free, voice-cloned) ----
_f5_tts = None
_f5_ref_file = os.path.join(os.path.dirname(__file__), "..", "..", "voice", "Claptrap's Best Quote Ever.mp3")
_f5_ref_wav = "/tmp/octo_ref_voice.wav"
_f5_ref_text = "Well of course you won with those cards, even Steve could've won with those cards. All he can say is hey Steve, people say I'm annoying"

def _init_f5():
    global _f5_tts
    if _f5_tts is not None:
        return _f5_tts

    # Monkeypatch torchaudio.load for torch 2.11 compatibility
    import torchaudio
    import soundfile as sf_local
    def _patched_load(filepath, *args, **kwargs):
        data, sr = sf_local.read(str(filepath), dtype='float32')
        if data.ndim == 1:
            data = data[np.newaxis, :]
        else:
            data = data.T
        return __import__('torch').from_numpy(data), sr
    torchaudio.load = _patched_load

    # Convert ref mp3 to wav
    import subprocess
    subprocess.run(['ffmpeg', '-y', '-i', _f5_ref_file, '-ar', '24000', '-ac', '1', _f5_ref_wav],
                   capture_output=True)

    from f5_tts.api import F5TTS
    _f5_tts = F5TTS(device="cuda")
    print("F5-TTS loaded (Claptrap voice)")
    return _f5_tts


def tts_f5(text):
    """Convert text to speech using F5-TTS with Claptrap voice clone. Returns 16kHz 16-bit mono PCM bytes."""
    tts = _init_f5()

    # Fix acronyms
    text = text.replace("CEO", "C E O").replace("AI", "A I").replace("API", "A P I")

    # Split into sentences to avoid multi-batch crash
    import re as _re
    sentences = _re.split(r'(?<=[.!?])\s+', text)
    all_wav = []

    for sentence in sentences:
        sentence = sentence.strip()
        if not sentence:
            continue
        try:
            wav, sr, _ = tts.infer(
                ref_file=_f5_ref_wav,
                ref_text=_f5_ref_text,
                gen_text=sentence,
                nfe_step=16,
            )
            all_wav.append(wav)
        except Exception as e:
            print(f"  F5-TTS error on chunk: {e}")
            continue

    if not all_wav:
        return None

    # Concatenate all sentence audio
    full_wav = np.concatenate(all_wav)

    # F5 outputs 24kHz float — resample to 16kHz int16 for Watcher
    from scipy.signal import resample
    num_samples_16k = int(len(full_wav) * 16000 / sr)
    wav_16k = resample(full_wav, num_samples_16k).astype(np.float32)

    # Dynamic range compression + normalization for small speaker
    # 1. Normalize to [-1, 1]
    peak = np.max(np.abs(wav_16k))
    if peak > 0:
        wav_16k = wav_16k / peak

    # 2. Compress dynamics — boost quiet parts, tame loud parts
    # Soft-knee compression: sign(x) * |x|^0.6
    wav_16k = np.sign(wav_16k) * np.power(np.abs(wav_16k), 0.5)

    # 3. Normalize again after compression, then scale to int16
    peak2 = np.max(np.abs(wav_16k))
    if peak2 > 0:
        wav_16k = wav_16k / peak2
    wav_16k = np.clip(wav_16k * 32767, -32767, 32767).astype(np.int16)
    return wav_16k.tobytes()


# Keep ElevenLabs as fallback
def tts_elevenlabs(text):
    """Fallback TTS via ElevenLabs API."""
    resp = http_requests.post(
        ELEVENLABS_TTS_URL,
        headers={
            "xi-api-key": ELEVENLABS_API_KEY,
            "Content-Type": "application/json",
        },
        json={
            "text": text,
            "model_id": "eleven_turbo_v2_5",
            "voice_settings": {"stability": 0.5, "similarity_boost": 0.75},
        },
        params={"output_format": "pcm_16000"},
        timeout=15,
    )
    if resp.status_code != 200:
        print(f"  ElevenLabs error {resp.status_code}: {resp.text[:200]}")
        return None
    samples = np.frombuffer(resp.content, dtype=np.int16).astype(np.float32)
    samples = samples * 2.5
    samples = np.clip(samples, -32767, 32767)
    return samples.astype(np.int16).tobytes()


class SesameAssistant:
    def __init__(self):
        print("Sesame Robot Assistant (cloud APIs)")
        print("  STT: Groq Whisper")
        print("  LLM: Claude via OpenRouter")
        print("  TTS: ElevenLabs")

        self.cmd_sock = None
        self.audio_sock = None
        self.pi_hw = None  # PiHardware instance when running on Pi
        self.last_interaction = 0  # timestamp of last successful interaction
        self.was_interrupted = False
        self._was_in_conversation = False  # track conversation state transitions
        self._conversation_log = []  # recent exchanges for reflection
        self._consecutive_rejects = 0  # track consecutive gate rejections
        self._auto_road_mode = False   # auto-activated road mode

        # Personality + Memory
        self.personality = Personality()
        self.memory = Memory()

        # Modes: "normal", "chill", "silent"
        self.mode = "normal"

        # Awareness system
        self.last_awareness_reaction = 0  # cooldown timestamp
        self.last_seen_classes = set()    # what was visible last check
        self.awareness_queue = []         # pending events to maybe react to
        self.awareness_lock = threading.Lock()
        self.speaking = False             # True while robot is talking

        self.conversation = [
            {"role": "user", "content": SYSTEM_PROMPT},
            {"role": "assistant", "content": "I'm Octavius. Ready."},
        ]

    def connect(self):
        if PI_MODE:
            # Local hardware — no TCP needed
            if self.pi_hw is None:
                self.pi_hw = PiHardware()
            # Set dummy sockets so connection checks pass
            self.cmd_sock = True
            self.audio_sock = True
            print("Connected to local Pi hardware")
            return

        global WATCHER_IP
        if WATCHER_IP is None:
            WATCHER_IP = discover_watcher()
            if WATCHER_IP is None:
                print("ERROR: Could not find Watcher on network!")
                raise ConnectionError("Watcher not found")
        # Cache IP for next session + share with vision server
        with open(WATCHER_IP_FILE, "w") as f:
            f.write(WATCHER_IP)
        print(f"Connecting to Watcher at {WATCHER_IP}...")

        self.cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.cmd_sock.settimeout(10)
        self.cmd_sock.connect((WATCHER_IP, COMMAND_PORT))
        print(f"Command port connected (:{COMMAND_PORT})")

        self.audio_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.audio_sock.settimeout(15)
        self.audio_sock.connect((WATCHER_IP, AUDIO_PORT))
        print(f"Audio port connected (:{AUDIO_PORT})")

    def send_face(self, face):
        if PI_MODE and self.pi_hw:
            self.pi_hw.send_face(face)
            return
        try:
            self.cmd_sock.sendall(f"face {face}\n".encode())
            print(f"  [face: {face}]")
        except Exception as e:
            print(f"  [face FAILED: {e}]")
            self._reconnect_cmd()

    def send_command(self, cmd):
        if PI_MODE and self.pi_hw:
            self.pi_hw.send_command(cmd)
            return
        try:
            self.cmd_sock.sendall(f"{cmd}\n".encode())
            print(f"  [cmd: {cmd}]")
        except Exception as e:
            print(f"  [cmd FAILED: {e}]")
            self._reconnect_cmd()

    def acknowledge_wake(self):
        self.send_face("excited")
        time.sleep(0.3)
        self.send_face("happy")
        time.sleep(0.2)
        self.send_face("excited")

    def _reconnect_audio(self):
        try:
            self.audio_sock.close()
        except:
            pass
        try:
            self.audio_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.audio_sock.settimeout(10)
            self.audio_sock.connect((WATCHER_IP, AUDIO_PORT))
            print("  [audio reconnected]")
        except:
            print("  [audio reconnect FAILED]")

    def _reconnect_cmd(self):
        try:
            self.cmd_sock.close()
        except:
            pass
        try:
            self.cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.cmd_sock.settimeout(5)
            self.cmd_sock.connect((WATCHER_IP, COMMAND_PORT))
            print("  [cmd reconnected]")
        except:
            print("  [cmd reconnect FAILED]")

    def record_speech(self):
        """Record from mic until silence is detected."""
        if PI_MODE and self.pi_hw:
            return self.pi_hw.record_speech(
                conversation_mode=self.in_conversation()
            )
        try:
            self.audio_sock.sendall(b"record\n")
        except (ConnectionResetError, BrokenPipeError, OSError):
            print("  [audio connection lost — reconnecting]")
            self._reconnect_audio()
            try:
                self.audio_sock.sendall(b"record\n")
            except:
                return None

        # Calibrate ambient noise
        # In conversation mode, use shorter calibration (faster response)
        cal_duration = 0.25 if self.in_conversation() else 0.5
        ambient_samples = bytearray()
        cal_end = time.time() + cal_duration
        while time.time() < cal_end:
            try:
                chunk = self.audio_sock.recv(1024)
                if chunk:
                    ambient_samples.extend(chunk)
            except socket.timeout:
                break

        if len(ambient_samples) >= 2:
            amb = np.frombuffer(ambient_samples, dtype=np.int16).astype(np.float32)
            ambient_rms = np.sqrt(np.mean(amb ** 2))
        else:
            ambient_rms = 200.0

        threshold = min(max(ambient_rms * NOISE_MULTIPLIER, 300), MAX_THRESHOLD)
        conv_tag = " [conv]" if self.in_conversation() else ""
        print(f"Listening...{conv_tag} (ambient={ambient_rms:.0f}, threshold={threshold:.0f})")

        audio_data = bytearray(ambient_samples)  # keep calibration audio too
        silence_start = None
        has_speech = False
        start_time = time.time()

        while True:
            try:
                chunk = self.audio_sock.recv(1024)
                if not chunk:
                    break
                audio_data.extend(chunk)

                samples = np.frombuffer(chunk, dtype=np.int16)
                if len(samples) > 0:
                    rms = np.sqrt(np.mean(samples.astype(np.float32) ** 2))

                    if rms > threshold:
                        has_speech = True
                        silence_start = None
                    elif has_speech:
                        if silence_start is None:
                            silence_start = time.time()
                        elif time.time() - silence_start > SILENCE_DURATION:
                            break

                elapsed = time.time() - start_time
                if elapsed > 10:
                    break
                # If no speech detected after 4s, give up early
                if not has_speech and elapsed > 4:
                    break

            except socket.timeout:
                break

        self.audio_sock.sendall(b"stop\n")
        self.audio_sock.setblocking(False)
        try:
            while self.audio_sock.recv(4096):
                pass
        except:
            pass
        self.audio_sock.setblocking(True)
        self.audio_sock.settimeout(10)

        # Trim trailing silence
        samples_all = np.frombuffer(audio_data, dtype=np.int16)
        chunk_size = SAMPLE_RATE // 10
        trim_idx = len(samples_all)
        for i in range(len(samples_all) - chunk_size, 0, -chunk_size):
            rms = np.sqrt(np.mean(samples_all[i:i+chunk_size].astype(np.float32) ** 2))
            if rms > threshold:
                trim_idx = min(i + chunk_size * 2, len(samples_all))
                break
        audio_data = samples_all[:trim_idx].tobytes()

        duration = len(audio_data) / (SAMPLE_RATE * SAMPLE_WIDTH)
        print(f"Recorded {duration:.1f}s")

        if duration < MIN_SPEECH_DURATION or not has_speech:
            return None

        return bytes(audio_data)

    # Whisper hallucinations — phantom phrases from silence/noise
    WHISPER_GHOSTS = {
        "thank you.", "thank you", "thanks.", "thanks",
        "thanks for watching.", "thanks for watching",
        "bye.", "bye", "you", "the end.", "the end",
        ".", "..", "...", "so", "oh",
        "thank you for watching.", "subscribe.",
        "see you next time.", "see you.",
        "shh", "shh.", "shhh", "shhh.", "sh", "sh.",
        "okay", "okay.",
        "yeah", "yeah.", "yes", "yes.",
        "i'm sorry", "i'm sorry.", "sorry", "sorry.",
        "hmm", "hmm.", "huh", "huh.",
        "no", "no.", "right", "right.",
        "i don't know", "i don't know.",
        "what", "what.", "what?",
    }

    def transcribe(self, audio_data):
        """Transcribe audio using Groq Whisper API."""
        t0 = time.time()
        text = transcribe_groq(audio_data)
        print(f"  STT ({time.time()-t0:.1f}s): \"{text}\"")

        # Filter Whisper hallucinations
        if text and text.lower().strip().rstrip(".!") in {g.rstrip(".!") for g in self.WHISPER_GHOSTS}:
            print(f"  [whisper ghost filtered]")
            return None

        return text

    def strip_name(self, text):
        """Remove robot name prefix from text."""
        clean = text
        for prefix in ["hey octavius", "hey octo", "octavius", "octavious", "octo", "octave"]:
            idx = clean.lower().find(prefix)
            if idx >= 0:
                after = clean[idx + len(prefix):].strip().lstrip(",.!? ")
                if after:
                    clean = after
                break
        return clean

    def listen_and_check(self):
        """Record speech, transcribe, check if directed at robot.
        Returns (text, verdict) where verdict is 'yes', 'maybe', or None for skip/no speech."""
        audio = self.record_speech()
        if audio is None:
            return None, None

        text = self.transcribe(audio)
        if not text:
            return None, None

        # In conversation mode, skip the gate
        if self.in_conversation():
            print(f"  [conversation mode — gate skipped]")
            return self.strip_name(text), "yes"

        verdict = is_directed_at_robot(text)
        if verdict == "yes":
            return self.strip_name(text), "yes"
        elif verdict == "maybe":
            return text, "maybe"
        return None, None

    def in_conversation(self):
        """Check if we're within the conversation timeout window."""
        return (time.time() - self.last_interaction) < CONVERSATION_TIMEOUT

    def ask_llm(self, text):
        """Send text to LLM via OpenRouter, with tool support."""
        import octo_mind

        # Update system prompt with personality + memory + mind
        personality_injection = self.personality.get_prompt_injection()
        memory_injection = self.memory.get_prompt_injection()
        mind_injection = (
            f"== YOUR INNER WORLD ==\n"
            f"Current time: {octo_mind.today()}\n\n"
            f"{octo_mind.read_all()}"
        )
        self.conversation[0] = {
            "role": "system",
            "content": SYSTEM_PROMPT + "\n\n" + personality_injection + "\n\n" + memory_injection + "\n\n" + mind_injection,
        }

        self.conversation.append({"role": "user", "content": text})

        response = http_requests.post(
            OPENROUTER_URL,
            headers={
                "Authorization": f"Bearer {OPENROUTER_KEY}",
                "Content-Type": "application/json",
            },
            json={
                "model": model_config.api_model("brain"),
                "max_tokens": 150,
                "messages": self.conversation,
                "tools": TOOLS,
            },
            timeout=15,
        )
        data = response.json()
        if "choices" not in data:
            print(f"  [OpenRouter error: {json.dumps(data)[:200]}]")
            return None
        msg = data["choices"][0]["message"]

        if msg.get("tool_calls"):
            self.conversation.append(msg)
            for tc in msg["tool_calls"]:
                fn_name = tc["function"]["name"]
                fn_args = tc["function"].get("arguments", {})
                if isinstance(fn_args, str):
                    try:
                        fn_args = json.loads(fn_args)
                    except json.JSONDecodeError:
                        fn_args = {}
                print(f"  [tool: {fn_name}({fn_args})]")
                handler = TOOL_HANDLERS.get(fn_name)
                try:
                    result = handler(fn_args) if handler else f"Unknown tool: {fn_name}"
                except Exception as e:
                    result = f"Error: {e}"
                print(f"  [result: {str(result)[:100]}]")
                self.conversation.append({
                    "role": "tool",
                    "tool_call_id": tc["id"],
                    "content": str(result),
                })

            response = http_requests.post(
                OPENROUTER_URL,
                headers={
                    "Authorization": f"Bearer {OPENROUTER_KEY}",
                    "Content-Type": "application/json",
                },
                json={
                    "model": model_config.api_model("brain"),
                    "max_tokens": 150,
                    "messages": self.conversation,
                },
                timeout=15,
            )
            data = response.json()
            reply = data["choices"][0]["message"]["content"]
        else:
            reply = msg.get("content", "")

        # Strip thinking tags
        if reply and "</think>" in reply:
            reply = reply.split("</think>")[-1].strip()
        if reply:
            reply = re.sub(r'<think>.*?</think>', '', reply, flags=re.DOTALL).strip()
            reply = re.sub(r'<think>.*', '', reply, flags=re.DOTALL).strip()
            # Strip leaked tool call text (Gemma sometimes outputs these as text)
            reply = re.sub(r'(?:thought\n)?call:\w+\{[^}]*\}', '', reply).strip()
            reply = re.sub(r'<tool_call>.*?</tool_call>', '', reply, flags=re.DOTALL).strip()

        self.conversation.append({"role": "assistant", "content": reply})

        if len(self.conversation) > 20:
            self.conversation = self.conversation[:2] + self.conversation[-10:]

        return reply

    def speak_and_act(self, reply):
        """Interleave speech with commands/faces. Speaks text segments between tags."""
        tag_pattern = re.compile(r"\[(COMMAND|FACE):(\w+)\]")

        # Split reply into segments: [(text, tag_type, tag_value), ...]
        segments = []
        last_end = 0
        for match in tag_pattern.finditer(reply):
            text_before = reply[last_end:match.start()].strip()
            if text_before:
                segments.append(("text", text_before))
            segments.append((match.group(1).lower(), match.group(2)))
            last_end = match.end()
        trailing = reply[last_end:].strip()
        # Remove leftover tag fragments
        trailing = re.sub(r"  +", " ", trailing).strip()
        if trailing:
            segments.append(("text", trailing))

        if not segments:
            return

        # Stop any recording and flush before speaking
        if PI_MODE and self.pi_hw:
            self.pi_hw.stop_recording()
        else:
            try:
                self.audio_sock.sendall(b"stop\n")
                time.sleep(0.1)
                self.audio_sock.setblocking(False)
                try:
                    while self.audio_sock.recv(4096):
                        pass
                except:
                    pass
                self.audio_sock.setblocking(True)
                self.audio_sock.settimeout(30)
            except:
                self._reconnect_audio()

        # Process segments in order — stop if interrupted
        self.speaking = True
        interrupted = False
        last_face = None  # track last face set by tags
        for seg_type, seg_value in segments:
            if interrupted:
                break
            if seg_type == "face":
                print(f"  -> face: {seg_value}")
                self.send_face(seg_value)
                last_face = seg_value
            elif seg_type == "command":
                if self.mode in ("chill", "silent"):
                    print(f"  -> cmd: {seg_value} [skipped — {self.mode} mode]")
                else:
                    print(f"  -> cmd: {seg_value}")
                    self.send_command(seg_value)
                    time.sleep(0.3)
            elif seg_type == "text":
                print(f"  -> speak: \"{seg_value[:60]}{'...' if len(seg_value) > 60 else ''}\"")
                interrupted = self._speak_segment(seg_value, keep_face=last_face)

        self.was_interrupted = interrupted
        self.speaking = False
        # Don't reset to idle — let the last [FACE:xxx] tag persist

    def _speak_segment(self, text, keep_face=None):
        """Speak a single text segment via TTS. Returns True if interrupted."""
        if not text:
            return False

        # Only switch to "talk" face if no expression was set by a tag
        if not keep_face:
            self.send_face("talk")

        t0 = time.time()
        audio_bytes = tts_f5(text)
        if not audio_bytes:
            return False

        duration = len(audio_bytes) / (SAMPLE_RATE * 2)
        print(f"     TTS ({time.time()-t0:.1f}s): {duration:.1f}s")

        if PI_MODE and self.pi_hw:
            # Local playback — blocking
            self.pi_hw.play_audio(audio_bytes)
            return False

        # Send play command with length — firmware auto-stops when all bytes received
        audio_len = len(audio_bytes)
        self.audio_sock.sendall(f"play {audio_len}\n".encode())
        time.sleep(0.1)

        play_start = time.time()
        self.audio_sock.settimeout(30)
        try:
            self.audio_sock.sendall(audio_bytes)
        except socket.timeout:
            print("     (send timeout)")

        # Wait for playback to finish
        elapsed = time.time() - play_start
        remaining = max(0, duration - elapsed)
        if remaining > 0:
            time.sleep(remaining)

        # Small buffer for firmware to finish I2S writes
        time.sleep(0.2)

        # Flush any leftover data in socket
        self.audio_sock.setblocking(False)
        try:
            while self.audio_sock.recv(4096):
                pass
        except:
            pass
        self.audio_sock.setblocking(True)
        self.audio_sock.settimeout(10)
        return False

    # ---- Awareness System ----

    def get_visual_context(self):
        """Get SmolVLM2 scene description from the vision server."""
        try:
            resp = http_requests.get(f"{VISION_SERVER_URL}/describe", timeout=8)
            if resp.status_code == 200:
                desc = resp.json()["description"]
                print(f"  [visual context: {desc[:80]}...]")
                return desc
        except:
            pass
        return None

    def awareness_thread(self):
        """Background thread — just keeps running to process queued events."""
        time.sleep(15)
        print(f"  [awareness thread started]")
        # This thread is kept alive for the thinking engine and paperclip poller
        # which push events to self.awareness_queue
        while True:
            time.sleep(5)

    def check_awareness_queue(self):
        """Check for pending awareness events and maybe react."""
        with self.awareness_lock:
            if not self.awareness_queue:
                return
            description = self.awareness_queue.pop(0)
            self.awareness_queue.clear()

        # Skip if not connected to Watcher
        if self.cmd_sock is None or self.audio_sock is None:
            print(f"  [awareness skipped — not connected]")
            return

        # Handle thinking engine speech (already has the message)
        if description.startswith("[THINKING] "):
            msg = description[len("[THINKING] "):]
            print(f"  [thinking says: {msg}]")
            self.speak_and_act(f"[FACE:excited] {msg}")
            self.last_awareness_reaction = time.time()
            return

        # Handle Paperclip task completion notifications
        if "Octo Corp tasks" in description:
            print(f"  [paperclip event: {description[:80]}]")
            self.speak_and_act(f"[FACE:happy] {description}")
            self.last_awareness_reaction = time.time()
            return

        # Ask Claude for a reaction (one-shot, not in conversation)
        prompt = (
            f"You are Octavius, a snarky quadruped robot. Your camera sees: {description}. "
            f"You can comment on it with a short quip (1 sentence max) using [FACE:xxx] and [COMMAND:xxx] tags. "
            f"Or respond with IGNORE if it's not worth commenting on."
        )

        try:
            resp = http_requests.post(
                OPENROUTER_URL,
                headers={
                    "Authorization": f"Bearer {OPENROUTER_KEY}",
                    "Content-Type": "application/json",
                },
                json={
                    "model": model_config.api_model("telegram"),
                    "max_tokens": 80,
                    "messages": [{"role": "user", "content": prompt}],
                },
                timeout=10,
            )
            reply = resp.json()["choices"][0]["message"]["content"].strip()
        except Exception as e:
            print(f"  [awareness LLM error: {e}]")
            return

        if "IGNORE" in reply.upper():
            print(f"  [awareness: ignored]")
            return

        print(f"  [awareness reaction: {reply}]")
        self.last_awareness_reaction = time.time()
        self.speak_and_act(reply)

    def _process_telegram(self):
        """Process incoming Telegram messages."""
        with self._tg_lock:
            if not self._tg_queue:
                return
            text, chat_id = self._tg_queue.pop(0)
            self._tg_queue.clear()

        print(f"  [telegram msg: {text}]")

        # Process through OpenRouter
        try:
            personality_injection = self.personality.get_prompt_injection()
            memory_injection = self.memory.get_prompt_injection()
            messages = [
                {"role": "system", "content": SYSTEM_PROMPT + "\n\n" + personality_injection + "\n\n" + memory_injection
                    + "\n\nYou are responding via Telegram text chat, not voice. "
                    "Don't use [COMMAND:] or [FACE:] tags. Just reply naturally in text. "
                    "Keep it short — this is a chat message, not a speech."},
                {"role": "user", "content": f"Albert says via Telegram: {text}"},
            ]
            resp = http_requests.post(
                OPENROUTER_URL,
                headers={"Authorization": f"Bearer {OPENROUTER_KEY}", "Content-Type": "application/json"},
                json={"model": model_config.api_model("telegram"), "max_tokens": 200, "messages": messages, "tools": TOOLS},
                timeout=15,
            )
            data = resp.json()
            if "choices" not in data:
                reply = None
            else:
                msg = data["choices"][0]["message"]
                reply = msg.get("content", "")

            if reply:
                # Strip any accidental tags
                import re
                reply = re.sub(r'\[(?:COMMAND|FACE):\w+\]', '', reply).strip()
                self._tg_send(reply, chat_id)
                print(f"  [telegram reply: {reply[:80]}]")

                # Inject into voice conversation context so Octo remembers
                self.conversation.append({"role": "user", "content": f"[Telegram from Albert]: {text}"})
                self.conversation.append({"role": "assistant", "content": f"[I replied on Telegram]: {reply}"})
                # Share with subconscious
                self.subconscious.on_telegram_sent(reply)

        except Exception as e:
            print(f"  [telegram error: {e}]")
            self._tg_send("Sorry, my brain glitched. Try again?", chat_id)

    def run(self):
        """Main loop."""
        # Pre-load models
        get_llm_gate()

        # Initialize Qwen3 4B for background tasks (memory, personality)
        import local_llm
        local_llm.init(
            model_path=os.path.join(os.path.dirname(__file__), "models", "Qwen3-4B-Q4_K_M.gguf"),
        )

        # Start background services
        # Awareness thread
        awareness = threading.Thread(target=self.awareness_thread, daemon=True)
        awareness.start()

        # Subconscious — Claude Code background brain
        from subconscious import Subconscious
        self.subconscious = Subconscious(
            speech_queue=self.awareness_queue,
            awareness_lock=self.awareness_lock,
        )
        self.subconscious.start()

        # Paperclip event poller
        from paperclip_client import PaperclipEventPoller
        def _on_task_done(task):
            print(f"  [paperclip] Task completed: {task['identifier']} — {task['title']}")
            # Notify subconscious
            self.subconscious._send_heartbeat(
                f"Task completed: {task['identifier']} — {task['title']}"
            )
        self._pc_poller = PaperclipEventPoller(on_task_completed=_on_task_done, poll_interval=30)
        self._pc_poller.start()

        # Telegram bot
        from telegram_bot import TelegramPoller, send_message as tg_send
        self._tg_send = tg_send
        self._tg_queue = []
        self._tg_lock = threading.Lock()

        def _on_telegram(text, chat_id):
            with self._tg_lock:
                self._tg_queue.append((text, chat_id))
            # Share with subconscious
            self.subconscious.on_telegram_received(text)

        def _on_ip(ip):
            global WATCHER_IP
            WATCHER_IP = ip
            with open(WATCHER_IP_FILE, "w") as f:
                f.write(ip)
            # Force reconnect
            try: self.cmd_sock.close()
            except: pass
            try: self.audio_sock.close()
            except: pass
            self.cmd_sock = None
            self.audio_sock = None
            print(f"  [telegram] Watcher IP updated to {ip} — reconnecting...")

        def _on_mode(mode):
            self.mode = mode
            print(f"  [telegram] Mode set to: {mode}")
            if mode == "normal" and self.cmd_sock:
                try:
                    self.send_command("stand")
                    self.send_face("happy")
                except:
                    pass

        self._tg_poller = TelegramPoller(on_message=_on_telegram, on_ip=_on_ip, on_mode=_on_mode)
        self._tg_poller.start()

        # Main loop with reconnection
        try:
            while True:
                # Connect to Watcher (or reconnect)
                if self.cmd_sock is None:
                    try:
                        self.connect()
                        self.send_face("happy")
                        time.sleep(1)
                        self.send_face("idle")
                        print("\n=== Octavius Robot Assistant ===")
                        print("Talk to Octavius naturally. Press Ctrl+C to quit.")
                        print(f"Conversation mode: {CONVERSATION_TIMEOUT}s window after each interaction.\n")
                    except Exception as e:
                        print(f"Watcher not available ({e}) — retrying in 10s...")
                        print("  (Telegram + thinking still active)")
                        # Still process telegram/thinking while disconnected
                        for _ in range(10):
                            try:
                                self.check_awareness_queue()
                                self._process_telegram()
                            except:
                                pass
                            time.sleep(1)
                        continue

                if self.cmd_sock is None or self.audio_sock is None:
                    continue

                try:
                    self._main_loop_tick()
                except (ConnectionResetError, BrokenPipeError, OSError, AttributeError) as e:
                    if PI_MODE:
                        print(f"\n  [Hardware error: {e}]")
                        time.sleep(2)
                        continue
                    print(f"\n  [Watcher disconnected: {e}]")
                    print("  Waiting for reconnection...")
                    try: self.cmd_sock.close()
                    except: pass
                    try: self.audio_sock.close()
                    except: pass
                    self.cmd_sock = None
                    self.audio_sock = None
                    # Reset discovery so we scan again
                    global WATCHER_IP
                    WATCHER_IP = None

        except KeyboardInterrupt:
            print("\nShutting down...")
            try: self.send_face("idle")
            except: pass
            if PI_MODE and self.pi_hw:
                self.pi_hw.cleanup()
            else:
                try: self.cmd_sock.close()
                except: pass
                try: self.audio_sock.close()
                except: pass
            print("Bye!")

    def _main_loop_tick(self):
        """One iteration of the main loop. Raises on disconnect."""
        # Check for awareness/thinking queue (works in ALL modes)
        self.check_awareness_queue()

        # Check for Telegram messages (works in ALL modes)
        self._process_telegram()

        # Detect conversation ending → trigger subconscious reflection
        currently_in_conv = self.in_conversation()
        if self._was_in_conversation and not currently_in_conv and self._conversation_log:
            summary = "\n".join(self._conversation_log[-10:])
            self.subconscious.on_conversation_ended(summary)
            self._conversation_log.clear()
            print(f"  [conversation ended — subconscious reflecting]")
        self._was_in_conversation = currently_in_conv

        # DND/Silent mode: don't listen, just process background events
        if self.mode in ("dnd", "silent"):
            import select
            if select.select([__import__('sys').stdin], [], [], 0)[0]:
                line = __import__('sys').stdin.readline().strip().lower()
                if line in ("wake", "normal", "back", "hey"):
                    self.mode = "normal"
                    self.send_command("stand")
                    self.send_face("happy")
                    print(f"  [mode: normal — back online!]")
            time.sleep(1)
            return

        text, verdict = self.listen_and_check()

        if text is None:
            return

        # Track consecutive gate rejections — auto-enter road mode if overhearing conversation
        if verdict == "no" or verdict is None:
            self._consecutive_rejects += 1
            if self._consecutive_rejects >= 4 and not self._auto_road_mode:
                self._auto_road_mode = True
                print(f"  [auto road mode — detected background conversation, waiting for wake word]")
            return
        else:
            # Check if we're in auto road mode — only break out with wake word
            if self._auto_road_mode:
                has_name = text and any(name in text.lower() for name in ["octavius", "octavious", "octo", "octave"])
                if not has_name:
                    return
                else:
                    self._auto_road_mode = False
                    self._consecutive_rejects = 0
                    print(f"  [auto road mode OFF — wake word detected]")
            else:
                self._consecutive_rejects = 0

        # Road mode: only respond if wake word is present
        if self.mode == "road":
            text_lower_check = text.lower() if text else ""
            has_wake_word = any(name in text_lower_check for name in ["octavius", "octavious", "octo", "octave"])
            if not has_wake_word:
                return
            # Check if they're exiting road mode
            if any(p in text_lower_check for p in ["normal mode", "wake up", "get out of road", "exit road", "stop road"]):
                self.mode = "normal"
                self.send_command("stand")
                print(f"  [mode: normal]")
                self._speak_segment("Back to normal mode!")
                return

        # Handle "maybe" — ask if they meant the robot
        if verdict == "maybe":
            word_count = len(text.split())
            if word_count <= 6:
                print(f"  [uncertain: \"{text}\" — asking...]")
                self.send_face("confused")
                self._speak_segment("Were you talking to me?")
                confirm_audio = self.record_speech()
                if confirm_audio:
                    confirm_text = self.transcribe(confirm_audio)
                    if confirm_text:
                        ct = confirm_text.lower()
                        if any(w in ct for w in ["yes", "yeah", "yep", "ya", "sure", "you", "da"]):
                            print(f"  [confirmed!]")
                            text = self.strip_name(text)
                            verdict = "yes"
                        else:
                            print(f"  [denied: \"{confirm_text}\"]")
                            self.send_face("idle")
                            return
                    else:
                        self.send_face("idle")
                        return
                else:
                    self.send_face("idle")
                    return
            else:
                return

        # Check for mode changes
        text_lower = text.lower()
        mode_changed = False
        if any(p in text_lower for p in ["chill mode", "chill out", "relax mode", "lay down"]):
            self.mode = "chill"
            self.send_command("cute")
            print(f"  [mode: chill — lying down, no movements]")
            mode_changed = True
        elif any(p in text_lower for p in ["silent mode", "be quiet", "shut up", "mute", "go silent"]):
            self.mode = "silent"
            self.send_command("rest")
            self.send_face("idle")
            print(f"  [mode: silent — no movement, no talking]")
            return
        elif any(p in text_lower for p in ["do not disturb", "dnd mode", "dnd", "focus mode", "work mode", "leave me alone"]):
            self.mode = "dnd"
            self.send_command("rest")
            self.send_face("idle")
            print(f"  [mode: dnd — not listening, thinking continues, will speak if needed]")
            return
        elif any(p in text_lower for p in ["road mode", "wakeword mode", "wake word mode", "on the road"]):
            self.mode = "road"
            print(f"  [mode: road — wake word only]")
            return
        elif any(p in text_lower for p in ["normal mode", "wake up", "get up", "stand up", "come back"]):
            self.mode = "normal"
            self.send_command("stand")
            print(f"  [mode: normal]")
            mode_changed = True

        self.send_face("excited")
        time.sleep(0.2)
        print(f"You: {text}")

        print("Thinking...")
        self.send_face("happy")
        try:
            reply = self.ask_llm(text)
            print(f"Octo: {reply}")
        except Exception as e:
            print(f"LLM error: {e}")
            self.send_face("sad")
            time.sleep(1)
            self.send_face("idle")
            return

        if reply:
            self.speak_and_act(reply)
        else:
            self.send_face("idle")

        # Mark interaction time for conversation mode
        self.last_interaction = time.time()

        # Log conversation for reflection
        self._conversation_log.append(f"Albert: {text}")
        if reply:
            self._conversation_log.append(f"Octo: {reply[:100]}")
        # Keep only last 10 exchanges
        if len(self._conversation_log) > 20:
            self._conversation_log = self._conversation_log[-20:]

        # Update personality + memory in background
        _text_copy = str(text)
        _reply_copy = str(reply or "")
        def _bg_update():
            try:
                self.personality.update(_text_copy, _reply_copy)
            except Exception as e:
                print(f"  [personality update error: {e}]")
            try:
                self.memory.extract_and_store(_text_copy, _reply_copy)
            except Exception as e:
                print(f"  [memory update error: {e}]")
        threading.Thread(target=_bg_update, daemon=True).start()


if __name__ == "__main__":
    assistant = SesameAssistant()
    assistant.run()
