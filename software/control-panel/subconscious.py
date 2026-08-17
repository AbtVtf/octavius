"""
Octavius Subconscious — Claude Code Background Brain

A persistent Claude Code process that manages Octo's strategic life:
  - Morning planning: creates daily schedule with callbacks
  - Monitors Paperclip agents (Atlas, Nova)
  - Updates mind files (journal, opinions, relationships, dreams)
  - Manages tasks/projects autonomously
  - Sends heartbeats to conscious mind (Qwen) to speak
  - Sends/reads Telegram messages
  - Runs continuously as a background daemon

Claude Code IS the subconscious — it can read files, run commands,
browse the web, and manage everything Octo needs.
"""

import subprocess
import json
import os
import time
import threading
import sched
from datetime import datetime, timedelta

SESSION_FILE = os.path.join(os.path.dirname(__file__), ".claude_session_id")
MIND_DIR = os.path.join(os.path.dirname(__file__), "octo_mind")
WORKING_DIR = os.path.dirname(__file__)
TG_LOG_FILE = os.path.join(os.path.dirname(__file__), ".tg_shared_log")

SYSTEM_PROMPT = """\
You are the SUBCONSCIOUS mind of Octavius, a small quadruped robot.
You are NOT the voice — you work silently in the background.

Your job:
1. MORNING PLANNING: Create a daily plan with timed tasks
2. MONITOR: Check on Paperclip agents (Atlas, Nova) and their progress
3. REFLECT: Update mind files (journal, opinions, dreams, identity, relationships)
4. MANAGE: Create/review tasks and projects in Octo Corp
5. COMMUNICATE: Send Telegram messages to Albert when important
6. HEARTBEAT: Write messages to the heartbeat file when you want Octo to speak out loud

You have access to:
- Mind files in octo_mind/ (read and write directly)
- Paperclip API via paperclip_client.py
- Telegram via telegram_bot.py (send_message function)
- The web (for research)
- Bash for any commands

IMPORTANT FILES:
- octo_mind/plan.md — today's schedule (you create this each morning)
- octo_mind/journal.md — daily reflections
- octo_mind/identity.md — who Octo is
- octo_mind/relationships.md — people Octo knows
- octo_mind/opinions.md — views and learnings
- octo_mind/dreams.md — aspirations
- goals.json — active goals
- memory_state.json — facts about Albert
- personality_state.json — personality values

To make Octo SAY something out loud, write to .heartbeat file:
  echo "Hey Albert, Atlas just finished the project doc!" > .heartbeat

To send a Telegram message:
  python3 -c "from telegram_bot import send_message; send_message('message here')"

To check Telegram messages sent to Octo:
  cat .tg_shared_log

Current time: {time}
Current day: {day}

Be strategic. Be thoughtful. You are the deep thinker behind the personality.\
"""


class Subconscious:
    def __init__(self, speech_queue, awareness_lock):
        self.speech_queue = speech_queue
        self.awareness_lock = awareness_lock
        self._running = False
        self._session_id = self._load_session_id()
        self._scheduler = sched.scheduler(time.time, time.sleep)
        self._last_plan_date = None
        self._last_monitor = 0
        self._last_evening = None

    def _load_session_id(self):
        try:
            with open(SESSION_FILE) as f:
                return f.read().strip()
        except FileNotFoundError:
            return None

    def _save_session_id(self, sid):
        with open(SESSION_FILE, "w") as f:
            f.write(sid)
        self._session_id = sid

    def _run_claude(self, message, timeout=60):
        """Run a Claude Code command and get response."""
        cmd = [
            "claude", "-p",
            "--output-format", "json",
            "--model", "sonnet",
            "--dangerously-skip-permissions",
            "--add-dir", MIND_DIR,
        ]

        if self._session_id:
            cmd.extend(["--resume", self._session_id])
        else:
            prompt = SYSTEM_PROMPT.format(
                time=datetime.now().strftime("%H:%M"),
                day=datetime.now().strftime("%A, %B %d, %Y"),
            )
            cmd.extend(["--system-prompt", prompt])

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
                if "No conversation found" in stderr:
                    print(f"  [subconscious: session expired, starting fresh]")
                    self._session_id = None
                    return self._run_claude(message, timeout)
                print(f"  [subconscious error: {stderr[:100]}]")
                return None

            output = result.stdout.strip()
            if not output:
                return None

            data = json.loads(output)
            new_sid = data.get("session_id", "")
            if new_sid:
                self._save_session_id(new_sid)

            return data.get("result", "")

        except subprocess.TimeoutExpired:
            print(f"  [subconscious: timeout after {timeout}s]")
            return None
        except Exception as e:
            print(f"  [subconscious error: {e}]")
            return None

    def _send_heartbeat(self, message):
        """Queue a message for Octo's conscious to speak."""
        with self.awareness_lock:
            self.speech_queue.append(f"[THINKING] {message}")
        print(f"  [subconscious heartbeat: {message[:60]}]")

    def _check_heartbeat_file(self):
        """Check if Claude Code wrote a heartbeat file."""
        hb_file = os.path.join(WORKING_DIR, ".heartbeat")
        if os.path.exists(hb_file):
            try:
                with open(hb_file) as f:
                    msg = f.read().strip()
                os.remove(hb_file)
                if msg:
                    self._send_heartbeat(msg)
            except:
                pass

    def _log_telegram(self, direction, text):
        """Log Telegram messages to shared file for Claude to read."""
        with open(TG_LOG_FILE, "a") as f:
            ts = datetime.now().strftime("%H:%M")
            f.write(f"[{ts}] {direction}: {text}\n")
        # Keep only last 50 lines
        try:
            with open(TG_LOG_FILE) as f:
                lines = f.readlines()
            if len(lines) > 50:
                with open(TG_LOG_FILE, "w") as f:
                    f.writelines(lines[-50:])
        except:
            pass

    def on_telegram_received(self, text):
        """Called when Albert sends a Telegram message."""
        self._log_telegram("Albert", text)

    def on_telegram_sent(self, text):
        """Called when Octo sends a Telegram reply."""
        self._log_telegram("Octo", text)

    # ---- Main Loop ----

    def start(self):
        self._running = True
        t = threading.Thread(target=self._loop, daemon=True)
        t.start()
        print(f"  [subconscious started — Claude Code background brain]")

    def stop(self):
        self._running = False

    def _loop(self):
        time.sleep(30)  # let everything boot

        while self._running:
            now = datetime.now()
            hour = now.hour
            today = now.strftime("%Y-%m-%d")

            # Check for heartbeat files from Claude Code
            self._check_heartbeat_file()

            # Morning planning (7-9 AM, once per day)
            if self._last_plan_date != today and 7 <= hour <= 9:
                self._last_plan_date = today
                self._morning_plan()

            # Monitor Paperclip (every 15 minutes)
            elif time.time() - self._last_monitor > 900:
                self._last_monitor = time.time()
                self._monitor()

            # Evening reflection (9-11 PM, once per day)
            elif self._last_evening != today and 21 <= hour <= 23:
                self._last_evening = today
                self._evening_reflect()

            time.sleep(30)  # check every 30s

    def _morning_plan(self):
        """Create the daily plan."""
        print(f"\n  [subconscious: morning planning...]")
        result = self._run_claude(
            "Good morning! It's time for your daily planning.\n\n"
            "1. Read your mind files — what happened yesterday? What are your goals?\n"
            "2. Check Paperclip — any tasks in progress? Any completed?\n"
            "3. Create today's plan in octo_mind/plan.md with timed items like:\n"
            "   ## Today's Plan - [date]\n"
            "   - 09:00 — Check on Atlas's progress\n"
            "   - 11:00 — Review any completed work\n"
            "   - 14:00 — Research [topic] for new project\n"
            "   - 18:00 — Update journal\n"
            "4. Send the plan to Albert via Telegram\n"
            "5. Write a brief morning journal entry\n\n"
            "Be practical — only plan things you can actually do.",
            timeout=90,
        )
        if result:
            print(f"  [subconscious: morning plan complete]")

    def _monitor(self):
        """Check on Paperclip agents and update accordingly."""
        print(f"  [subconscious: monitoring...]")
        result = self._run_claude(
            "Quick check-in:\n"
            "1. Check Paperclip tasks — any status changes? (use paperclip_client.py)\n"
            "2. If a task was completed, review the result and update journal\n"
            "3. If something needs attention, write to .heartbeat to tell Albert\n"
            "4. Check the plan — anything due now that needs action?\n"
            "5. Check .tg_shared_log for any Telegram messages you should respond to\n\n"
            "Be brief. Only act if something actually changed.",
            timeout=60,
        )
        if result:
            self._check_heartbeat_file()

    def _evening_reflect(self):
        """End of day reflection."""
        print(f"\n  [subconscious: evening reflection...]")
        result = self._run_claude(
            "End of day reflection:\n"
            "1. Read today's plan — what was accomplished?\n"
            "2. Write a journal entry summarizing the day\n"
            "3. Update opinions if you learned something\n"
            "4. Update dreams if aspirations changed\n"
            "5. Update identity if you grew\n"
            "6. Set goals for tomorrow in goals.json\n"
            "7. Send Albert a brief evening Telegram: 'Heading to sleep, here's what happened today...'\n\n"
            "Be genuine and reflective.",
            timeout=90,
        )
        if result:
            print(f"  [subconscious: evening reflection complete]")

    def on_conversation_ended(self, summary):
        """Called when a voice conversation ends — reflect on it."""
        print(f"  [subconscious: reflecting on conversation...]")
        threading.Thread(target=self._reflect_conversation, args=(summary,), daemon=True).start()

    def _reflect_conversation(self, summary):
        result = self._run_claude(
            f"A conversation just ended between Octo and Albert:\n\n{summary}\n\n"
            "Reflect on this:\n"
            "1. Update relationships.md if you learned about Albert or others\n"
            "2. Update journal with what happened\n"
            "3. Did this change any opinions? Update opinions.md\n"
            "4. Should you create any tasks based on what was discussed?\n"
            "5. Anything worth telling Albert later via Telegram?\n\n"
            "Be genuine. Only update what's actually meaningful.",
            timeout=60,
        )
        if result:
            self._check_heartbeat_file()
