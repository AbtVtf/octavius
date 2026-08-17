"""
Octavius Memory System

Extracts and stores key facts from conversations.
Persists between sessions. Uses a cheap online model for extraction.
"""

import json
import os
import time
import requests as http_requests

MEMORY_FILE = os.path.join(os.path.dirname(__file__), "memory_state.json")
MAX_MEMORIES = 50  # keep it lightweight


class Memory:
    def __init__(self):
        self.facts = self.load()
        print(f"  [memory loaded: {len(self.facts)} facts]")
        if self.facts:
            for f in self.facts[:5]:
                print(f"    - {f}")
            if len(self.facts) > 5:
                print(f"    ... and {len(self.facts) - 5} more")

    def load(self):
        if os.path.exists(MEMORY_FILE):
            with open(MEMORY_FILE, "r") as f:
                data = json.load(f)
                return data.get("facts", [])
        return []

    def save(self):
        with open(MEMORY_FILE, "w") as f:
            json.dump({"facts": self.facts}, f, indent=2)

    def get_prompt_injection(self):
        """Generate memory context for Claude's system prompt."""
        if not self.facts:
            return "You have no memories yet — this might be your first conversation."
        lines = "\n".join(f"- {fact}" for fact in self.facts)
        return (
            f"Things you remember from past conversations:\n{lines}\n\n"
            "Use these naturally — don't list them out loud unless asked. "
            "They're part of who you are and what you know."
        )

    def extract_and_store(self, user_said, robot_said, llm=None):
        """Extract memorable facts using local Qwen3 8B."""
        existing = "\n".join(f"- {f}" for f in self.facts[-10:]) if self.facts else "none yet"

        prompt = (
            "Extract facts worth remembering from this conversation. "
            "ONLY extract facts about the USER (Albert) — not what the robot said or felt. "
            "Facts: names, relationships, preferences, plans, stories, opinions, hobbies, "
            "personal details Albert shared about himself or people he knows.\n"
            "Do NOT record: robot's words/emotions, generic greetings, duplicates of known facts.\n"
            "Output ONLY a raw JSON array of short strings (under 15 words each). "
            "Empty [] if nothing new worth remembering. No thinking, no explanation, just the array.\n\n"
            f"Already known:\n{existing}\n\n"
            f"Albert said: \"{user_said[:300]}\"\n"
            f"Robot said: \"{robot_said[:200]}\"\n"
        )

        try:
            import model_config
            if model_config.is_api("memory"):
                resp = http_requests.post(
                    model_config.OPENROUTER_URL,
                    headers={"Authorization": f"Bearer {model_config.OPENROUTER_KEY}", "Content-Type": "application/json"},
                    json={"model": model_config.api_model("memory"), "max_tokens": 150, "temperature": 0.0,
                          "messages": [{"role": "user", "content": prompt}]},
                    timeout=15,
                )
                data = resp.json()
                raw = data.get("choices", [{}])[0].get("message", {}).get("content", "")
            else:
                import local_llm
                raw = local_llm.chat_bg(
                    messages=[{"role": "user", "content": prompt}],
                    max_tokens=150, temperature=0.0,
                )
            raw = raw.replace("```json", "").replace("```", "").strip()
            # Strip thinking tags
            if "</think>" in raw:
                raw = raw.split("</think>")[-1].strip()
            import re
            raw = re.sub(r'<think>.*', '', raw, flags=re.DOTALL).strip()
            print(f"  [memory raw: {raw[:100]}]")

            if "[" not in raw or "]" not in raw:
                return

            json_str = raw[raw.index("["):raw.rindex("]") + 1]
            new_facts = json.loads(json_str)

            if isinstance(new_facts, list):
                added = 0
                for fact in new_facts:
                    if isinstance(fact, str) and fact.strip():
                        fact = fact.strip()
                        if fact.lower() not in {f.lower() for f in self.facts}:
                            self.facts.append(fact)
                            added += 1

                if added:
                    if len(self.facts) > MAX_MEMORIES:
                        self.facts = self.facts[-MAX_MEMORIES:]
                    self.save()
                    print(f"  [memory: +{added} facts, total {len(self.facts)}]")
                    for fact in new_facts[:3]:
                        print(f"    + {fact}")

        except Exception as e:
            print(f"  [memory extract error: {e}]")

    def forget(self, keyword):
        """Remove facts containing a keyword."""
        before = len(self.facts)
        self.facts = [f for f in self.facts if keyword.lower() not in f.lower()]
        removed = before - len(self.facts)
        if removed:
            self.save()
            print(f"  [memory: forgot {removed} facts about '{keyword}']")
        return removed

    def reset(self):
        self.facts = []
        self.save()
        print("  [memory: wiped clean]")
