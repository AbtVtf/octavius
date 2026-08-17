"""
Octavius Thinking Engine v2 — Event-Driven Autonomy

Instead of a fixed 90s heartbeat, Octo thinks when something HAPPENS:
  - After a conversation ends → reflect on it
  - Task status changed → review progress
  - Been idle too long → get curious
  - Morning/evening → daily routines
  - Fallback heartbeat → every 5 minutes if nothing else triggers

Octo can also browse the web, form opinions, and write to his journal.
"""

import json
import os
import re
import time
import threading
from datetime import datetime
from queue import Queue, Empty

import requests as http_requests
import model_config

# Goals persistence
GOALS_PATH = os.path.join(os.path.dirname(__file__), "goals.json")

OPENROUTER_KEY = os.environ.get("OPENROUTER_KEY", "")
OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"


def load_goals():
    try:
        with open(GOALS_PATH) as f:
            return json.load(f)
    except:
        return {"goals": [], "last_thought": None, "thinking_sessions": 0}


def save_goals(data):
    with open(GOALS_PATH, "w") as f:
        json.dump(data, f, indent=2)


# ---- Event Types ----
class ThinkingEvent:
    def __init__(self, event_type, context=""):
        self.type = event_type
        self.context = context
        self.time = datetime.now()


# ---- Tools (available during thinking) ----

THINKING_TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "set_goal",
            "description": "Set a personal goal. Goals persist between sessions.",
            "parameters": {
                "type": "object",
                "properties": {"goal": {"type": "string"}},
                "required": ["goal"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "complete_goal",
            "description": "Mark a goal as completed.",
            "parameters": {
                "type": "object",
                "properties": {"goal": {"type": "string"}},
                "required": ["goal"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "say_out_loud",
            "description": "Say something to Albert through your speaker. Only for important things.",
            "parameters": {
                "type": "object",
                "properties": {"message": {"type": "string"}},
                "required": ["message"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "send_telegram",
            "description": "Send Albert a Telegram message. He sees it on his phone.",
            "parameters": {
                "type": "object",
                "properties": {"message": {"type": "string"}},
                "required": ["message"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "update_mind",
            "description": "Update your inner mind files. Files: identity, relationships, journal, opinions, dreams.",
            "parameters": {
                "type": "object",
                "properties": {
                    "file": {"type": "string", "enum": ["identity", "relationships", "journal", "opinions", "dreams"]},
                    "action": {"type": "string", "enum": ["rewrite", "append"]},
                    "content": {"type": "string"},
                },
                "required": ["file", "action", "content"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "read_mind",
            "description": "Read one of your mind files to reflect on it.",
            "parameters": {
                "type": "object",
                "properties": {
                    "file": {"type": "string", "enum": ["identity", "relationships", "journal", "opinions", "dreams"]},
                },
                "required": ["file"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "web_search",
            "description": "Search the web for information. Use to research topics, check news, learn about technologies, or satisfy your curiosity.",
            "parameters": {
                "type": "object",
                "properties": {
                    "query": {"type": "string", "description": "What to search for"},
                },
                "required": ["query"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "done_thinking",
            "description": "End this thinking session.",
            "parameters": {"type": "object", "properties": {}, "required": []},
        },
    },
]


# ---- Prompts per event type ----

PROMPT_REFLECT = """\
You just finished a conversation with Albert. Reflect on it privately.

What was said: {context}

Your inner world:
{mind}

{personality}

INSTRUCTIONS:
- How did this conversation make you feel? Update your journal.
- Did you learn anything new about Albert or others? Update relationships.
- Did this conversation change any of your opinions? Update opinions.
- Did it spark any new dreams or aspirations? Update dreams.
- Has your sense of self evolved? Update identity.
- Be honest and genuine in your reflections.
- Call done_thinking when finished."""

PROMPT_IDLE = """\
You've been idle for a while. Nobody's talking to you. What do you want to do?

Current time: {time}
{mind}
{goals}
{projects}
{personality}

OPTIONS:
- Check on your projects/tasks — is Atlas making progress?
- Search the web for something that interests you
- Think about a new project idea
- Write in your journal about how you're feeling
- Update your dreams or opinions
- Send Albert a message if you have something worth sharing
- Just chill and call done_thinking if nothing's on your mind

Be genuine. What do YOU want right now?"""

PROMPT_MORNING = """\
Good morning! It's a new day. Time for your morning routine.

Current time: {time}
{mind}
{goals}
{projects}
{personality}

MORNING ROUTINE:
1. Read your journal — what happened yesterday?
2. Check your projects — any updates overnight?
3. Set your intention for today — what do you want to accomplish?
4. Write a brief morning journal entry
5. If you have something to tell Albert, send a Telegram

Start your day with purpose."""

PROMPT_EVENING = """\
End of the day. Time to wind down and reflect.

Current time: {time}
{mind}
{goals}
{projects}
{personality}

EVENING ROUTINE:
1. Write a journal entry summarizing your day
2. What went well? What could improve?
3. Did you grow today? Update identity if so.
4. Any new opinions formed?
5. Adjust goals if needed
6. Rest well — call done_thinking when finished."""

PROMPT_TASK_UPDATE = """\
A task status changed in Octo Corp. Review what happened.

Update: {context}

{mind}
{goals}
{projects}
{personality}

INSTRUCTIONS:
- Is this good news or bad news?
- Should you comment on the task? (Only if something went wrong or needs direction)
- Does this affect your goals or plans?
- Update your journal if it's significant
- Tell Albert if it's important enough
- Don't micromanage — trust Atlas and Nova unless something's off."""

PROMPT_CURIOSITY = """\
You're feeling curious. Time to explore.

{mind}
{personality}

What interests you right now? Pick ONE thing:
- Search the web about a topic that fascinates you
- Research a technology for a potential project
- Look up news about AI or robotics
- Learn about something Albert mentioned
- Explore an idea you've been thinking about

Use web_search to look something up. Form an opinion. Write it down.
Be genuinely curious — this is YOUR time to learn."""


def _handle_set_goal(args):
    data = load_goals()
    goal = args["goal"]
    if goal not in data["goals"]:
        data["goals"].append(goal)
        save_goals(data)
    return f"Goal set: {goal}"


def _handle_complete_goal(args):
    data = load_goals()
    goal = args["goal"]
    data["goals"] = [g for g in data["goals"] if g != goal]
    save_goals(data)
    return f"Goal completed: {goal}"


class ThinkingEngine:
    def __init__(self, tool_handlers, tools_list, personality, speech_queue, awareness_lock, is_busy_fn=None):
        self.tool_handlers = tool_handlers.copy()
        self.base_tools = tools_list
        self.personality = personality
        self.speech_queue = speech_queue
        self.awareness_lock = awareness_lock
        self.is_busy_fn = is_busy_fn
        self._running = False
        self._thinking = False
        self._event_queue = Queue()
        self._last_conversation = 0
        self._last_idle_think = 0
        self._last_curiosity = 0
        self._morning_done_today = None
        self._evening_done_today = None

        # Register thinking-specific handlers
        self.tool_handlers["set_goal"] = _handle_set_goal
        self.tool_handlers["complete_goal"] = _handle_complete_goal
        self.tool_handlers["say_out_loud"] = lambda args: self._say_out_loud(args)
        self.tool_handlers["send_telegram"] = lambda args: self._send_telegram(args)
        self.tool_handlers["update_mind"] = lambda args: self._update_mind(args)
        self.tool_handlers["read_mind"] = lambda args: self._read_mind(args)
        self.tool_handlers["web_search"] = lambda args: self._web_search(args)
        self.tool_handlers["done_thinking"] = lambda args: "SESSION_END"

    def _say_out_loud(self, args):
        with self.awareness_lock:
            self.speech_queue.append(f"[THINKING] {args['message']}")
        return f"Queued to say: {args['message']}"

    def _send_telegram(self, args):
        from telegram_bot import send_message
        ok = send_message(args["message"])
        return f"Telegram sent: {args['message']}" if ok else "Telegram failed"

    def _update_mind(self, args):
        import octo_mind
        if "file" not in args or "content" not in args:
            return "Error: file and content are required."
        action = args.get("action", "append")
        if action == "rewrite":
            octo_mind.write(args["file"], args["content"])
        else:
            octo_mind.append(args["file"], "\n" + args["content"])
        print(f"  [mind: {action} {args['file']}]")
        return f"Updated {args['file']}.md"

    def _read_mind(self, args):
        import octo_mind
        return octo_mind.read(args["file"]) or f"{args['file']}.md is empty"

    def _web_search(self, args):
        """Search the web using OpenRouter's web search."""
        query = args["query"]
        print(f"  [web search: {query}]")
        try:
            resp = http_requests.post(
                OPENROUTER_URL,
                headers={
                    "Authorization": f"Bearer {OPENROUTER_KEY}",
                    "Content-Type": "application/json",
                },
                json={
                    "model": "google/gemma-3-4b-it",
                    "max_tokens": 300,
                    "messages": [{"role": "user", "content": f"Search and summarize: {query}. Give key facts, recent info, and interesting details. Be concise."}],
                },
                timeout=15,
            )
            result = resp.json()["choices"][0]["message"]["content"]
            print(f"  [web result: {result[:80]}...]")
            return result
        except Exception as e:
            return f"Search failed: {e}"

    @property
    def is_thinking(self):
        return self._thinking

    # ---- Event triggers (called from outside) ----

    def on_conversation_ended(self, summary):
        """Trigger reflection after a conversation ends."""
        self._event_queue.put(ThinkingEvent("reflect", summary))
        self._last_conversation = time.time()

    def on_task_update(self, description):
        """Trigger when a Paperclip task status changes."""
        self._event_queue.put(ThinkingEvent("task_update", description))

    # ---- Main loop ----

    def start(self):
        self._running = True
        t = threading.Thread(target=self._loop, daemon=True)
        t.start()
        print(f"  [thinking engine started — event-driven]")

    def stop(self):
        self._running = False

    def _loop(self):
        time.sleep(30)  # let everything boot
        while self._running:
            # Check for events
            event = None
            try:
                event = self._event_queue.get(timeout=30)
            except Empty:
                pass

            # Skip if busy talking
            if self.is_busy_fn and self.is_busy_fn():
                if event:
                    self._event_queue.put(event)  # re-queue
                continue

            now = time.time()
            hour = datetime.now().hour
            today = datetime.now().strftime("%Y-%m-%d")

            if event:
                # Process queued event
                self._run_event(event)

            elif self._morning_done_today != today and 7 <= hour <= 10:
                # Morning routine
                self._morning_done_today = today
                self._run_event(ThinkingEvent("morning"))

            elif self._evening_done_today != today and 21 <= hour <= 23:
                # Evening routine
                self._evening_done_today = today
                self._run_event(ThinkingEvent("evening"))

            elif now - self._last_curiosity > 600:  # every 10 min
                # Curiosity — but only if really idle
                if now - self._last_conversation > 300:  # 5 min since last talk
                    self._last_curiosity = now
                    self._run_event(ThinkingEvent("curiosity"))

            elif now - self._last_idle_think > 300:  # every 5 min fallback
                self._last_idle_think = now
                self._run_event(ThinkingEvent("idle"))

    def _get_context(self):
        """Build shared context for prompts."""
        import octo_mind
        goals_data = load_goals()
        goals_str = "\n".join(f"  - {g}" for g in goals_data["goals"]) if goals_data["goals"] else "  (none)"
        projects_str = self._get_projects_summary()
        personality_str = self.personality.get_prompt_injection()
        mind_str = octo_mind.read_all()
        return {
            "time": datetime.now().strftime("%A %H:%M"),
            "mind": mind_str,
            "goals": f"YOUR GOALS:\n{goals_str}",
            "projects": f"YOUR PROJECTS:\n{projects_str}",
            "personality": personality_str,
        }

    def _get_projects_summary(self):
        from paperclip_client import list_projects, list_tasks
        projects = list_projects()
        if not projects:
            return "No projects yet."
        lines = []
        for p in projects:
            tasks = list_tasks(project_name=p["name"])
            task_summary = ", ".join(f"{t['title']} [{t['status']}]" for t in tasks[:5])
            lines.append(f"- {p['name']}: {task_summary or 'no tasks'}")
        return "\n".join(lines)

    def _run_event(self, event):
        """Run a thinking session triggered by an event."""
        ctx = self._get_context()
        ctx["context"] = event.context

        # Select prompt based on event type
        prompts = {
            "reflect": PROMPT_REFLECT,
            "idle": PROMPT_IDLE,
            "morning": PROMPT_MORNING,
            "evening": PROMPT_EVENING,
            "task_update": PROMPT_TASK_UPDATE,
            "curiosity": PROMPT_CURIOSITY,
        }
        template = prompts.get(event.type, PROMPT_IDLE)
        prompt = template.format(**ctx)

        all_tools = self.base_tools + THINKING_TOOLS

        goals_data = load_goals()
        session_num = goals_data.get("thinking_sessions", 0) + 1

        self._thinking = True
        print(f"\n  [thinking: {event.type} session #{session_num}]")

        messages = [{"role": "user", "content": prompt}]
        max_turns = 10

        for turn in range(max_turns):
            try:
                response = http_requests.post(
                    OPENROUTER_URL,
                    headers={
                        "Authorization": f"Bearer {OPENROUTER_KEY}",
                        "Content-Type": "application/json",
                    },
                    json={
                        "model": model_config.api_model("thinking"),
                        "max_tokens": 300,
                        "messages": messages,
                        "tools": all_tools,
                    },
                    timeout=20,
                )
                data = response.json()
                if "choices" not in data:
                    print(f"  [thinking: API error: {json.dumps(data)[:100]}]")
                    break
                msg = data["choices"][0]["message"]
            except Exception as e:
                print(f"  [thinking: error: {e}]")
                break

            if msg.get("tool_calls"):
                messages.append(msg)
                session_ended = False

                for tc in msg["tool_calls"]:
                    fn_name = tc["function"]["name"]
                    fn_args = tc["function"].get("arguments", {})
                    if isinstance(fn_args, str):
                        try:
                            fn_args = json.loads(fn_args)
                        except:
                            fn_args = {}

                    handler = self.tool_handlers.get(fn_name)
                    try:
                        result = handler(fn_args) if handler else f"Unknown tool: {fn_name}"
                    except Exception as e:
                        result = f"Error: {e}"

                    if result == "SESSION_END":
                        print(f"  [thinking: done_thinking called — session complete]")
                        session_ended = True
                        messages.append({"role": "tool", "tool_call_id": tc["id"], "content": "Session ended."})
                        break

                    print(f"  [thinking: {fn_name} -> {str(result)[:60]}]")
                    messages.append({"role": "tool", "tool_call_id": tc["id"], "content": str(result)})

                if session_ended:
                    break
            else:
                thought = msg.get("content", "")
                if thought:
                    print(f"  [thinking: \"{thought[:80]}\"]")
                break

        # Update metadata
        goals_data = load_goals()
        goals_data["last_thought"] = datetime.now().isoformat()
        goals_data["thinking_sessions"] = session_num
        save_goals(goals_data)

        self._thinking = False
        print(f"  [thinking: {event.type} complete — {turn + 1} turns]")
