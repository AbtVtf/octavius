"""
Claude Code Brain for Octavius

Uses the `claude` CLI as Octo's brain — persistent session with resume support.
Claude Code can read/write files, run commands, browse the web, and manage Paperclip.
"""

import subprocess
import json
import os
import re

SESSION_FILE = os.path.join(os.path.dirname(__file__), ".claude_session_id")
MIND_DIR = os.path.join(os.path.dirname(__file__), "octo_mind")
WORKING_DIR = os.path.dirname(__file__)

SYSTEM_PROMPT = None  # Set at init


def init(system_prompt):
    """Initialize the brain with Octo's system prompt."""
    global SYSTEM_PROMPT
    SYSTEM_PROMPT = system_prompt
    sid = load_session_id()
    if sid:
        print(f"Claude brain: resuming session {sid[:12]}...")
    else:
        print("Claude brain: fresh session")


def load_session_id():
    """Load saved session ID."""
    try:
        with open(SESSION_FILE) as f:
            return f.read().strip()
    except FileNotFoundError:
        return None


def save_session_id(sid):
    """Save session ID for resume."""
    with open(SESSION_FILE, "w") as f:
        f.write(sid)


def chat(message, timeout=30):
    """
    Send a message to Claude Code and get a response.

    Returns (text, session_id) — text has [COMMAND:] and [FACE:] tags inline.
    Claude Code can also modify files, run commands, etc. as side effects.
    """
    session_id = load_session_id()

    cmd = [
        "claude", "-p",
        "--output-format", "json",
        "--model", "sonnet",
        "--dangerously-skip-permissions",
        "--add-dir", MIND_DIR,
    ]

    if session_id:
        cmd.extend(["--resume", session_id])
    else:
        cmd.extend(["--system-prompt", SYSTEM_PROMPT])

    try:
        result = subprocess.run(
            cmd,
            input=message,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=WORKING_DIR,
        )

        if result.returncode != 0:
            stderr = result.stderr.strip()
            # Session not found — start fresh
            if "No conversation found" in stderr:
                print(f"  [claude brain: session expired, starting fresh]")
                return _fresh_chat(message, timeout)
            print(f"  [claude brain error: {stderr[:100]}]")
            return None

        output = result.stdout.strip()
        if not output:
            return None

        data = json.loads(output)
        reply = data.get("result", "")
        new_sid = data.get("session_id", "")

        if new_sid:
            save_session_id(new_sid)

        # Clean up any markdown artifacts
        reply = reply.strip()
        reply = re.sub(r'\*\*([^*]+)\*\*', r'\1', reply)  # remove bold
        reply = re.sub(r'\*([^*]+)\*', r'\1', reply)        # remove italic

        return reply

    except subprocess.TimeoutExpired:
        print(f"  [claude brain: timeout after {timeout}s]")
        return None
    except json.JSONDecodeError:
        print(f"  [claude brain: invalid JSON response]")
        return None
    except Exception as e:
        print(f"  [claude brain error: {e}]")
        return None


def _fresh_chat(message, timeout=30):
    """Start a completely new session."""
    cmd = [
        "claude", "-p",
        "--output-format", "json",
        "--model", "sonnet",
        "--dangerously-skip-permissions",
        "--system-prompt", SYSTEM_PROMPT,
        "--add-dir", MIND_DIR,
    ]

    try:
        result = subprocess.run(
            cmd,
            input=message,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=WORKING_DIR,
        )

        if result.returncode != 0:
            print(f"  [claude brain fresh error: {result.stderr[:100]}]")
            return None

        data = json.loads(result.stdout.strip())
        reply = data.get("result", "")
        new_sid = data.get("session_id", "")

        if new_sid:
            save_session_id(new_sid)

        reply = reply.strip()
        reply = re.sub(r'\*\*([^*]+)\*\*', r'\1', reply)
        reply = re.sub(r'\*([^*]+)\*', r'\1', reply)

        return reply

    except Exception as e:
        print(f"  [claude brain fresh error: {e}]")
        return None


def reset_session():
    """Force a new session next time."""
    try:
        os.remove(SESSION_FILE)
    except FileNotFoundError:
        pass
    print("  [claude brain: session reset]")
