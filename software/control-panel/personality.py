"""
Octavius Personality System

Maintains a set of personality thresholds that evolve after each interaction.
Persists to disk between sessions. Uses the local Qwen 1.5B gate model
to adjust thresholds based on conversation context.
"""

import json
import os
import time

PERSONALITY_FILE = os.path.join(os.path.dirname(__file__), "personality_state.json")

DEFAULT_STATE = {
    "sass": 0.5,
    "energy": 0.5,
    "warmth": 0.5,
    "confidence": 0.5,
    "curiosity": 0.5,
    "patience": 0.5,
    "mood": 0.5,
}

# How personality traits map to prompting language
TRAIT_DESCRIPTIONS = {
    "sass":       ("very polite and respectful", "extremely snarky, sarcastic, and roast-heavy"),
    "energy":     ("calm, quiet, low-key", "hyper, loud, excitable, uses exclamations"),
    "warmth":     ("cold, distant, detached", "warm, caring, genuinely sweet when it matters"),
    "confidence": ("humble, self-deprecating, unsure", "extremely cocky, arrogant, thinks he's the best"),
    "curiosity":  ("bored, uninterested, dismissive", "fascinated, asking questions, eager to explore"),
    "patience":   ("short-tempered, snappy, easily annoyed", "patient, chill, takes things in stride"),
    "mood":       ("grumpy, irritated, negative", "happy, upbeat, positive"),
}


class Personality:
    def __init__(self):
        self.state = self.load()
        self.interaction_count = self.state.pop("_interactions", 0)
        print(f"  [personality loaded: {self.interaction_count} interactions]")
        self._print_state()

    def load(self):
        if os.path.exists(PERSONALITY_FILE):
            with open(PERSONALITY_FILE, "r") as f:
                saved = json.load(f)
                state = dict(DEFAULT_STATE)
                state.update(saved)
                return state
        return dict(DEFAULT_STATE)

    def save(self):
        data = dict(self.state)
        data["_interactions"] = self.interaction_count
        with open(PERSONALITY_FILE, "w") as f:
            json.dump(data, f, indent=2)

    def _print_state(self):
        parts = [f"{k}={v:.2f}" for k, v in self.state.items()]
        print(f"  [personality: {', '.join(parts)}]")

    def get_prompt_injection(self):
        """Generate personality description to inject into Claude's system prompt."""
        lines = []
        for trait, value in self.state.items():
            if trait.startswith("_"):
                continue
            low_desc, high_desc = TRAIT_DESCRIPTIONS.get(trait, ("low", "high"))
            if value < 0.3:
                lines.append(f"- {trait}: {low_desc} ({value:.1f})")
            elif value > 0.7:
                lines.append(f"- {trait}: {high_desc} ({value:.1f})")
            else:
                lines.append(f"- {trait}: moderate ({value:.1f})")

        return (
            "Your current personality state (these shift based on how interactions go):\n"
            + "\n".join(lines)
            + "\n\nLet these values naturally influence your tone and behavior. "
            "High sass means more roasting. Low patience means you snap quicker. "
            "High warmth means you show genuine care. Let it feel natural, not forced."
        )

    def update(self, user_said, robot_said, llm=None):
        """Adjust personality based on the interaction using Qwen3 4B background model."""
        current = json.dumps({k: round(v, 2) for k, v in self.state.items()})

        prompt = (
            "You adjust a robot's personality thresholds (0.0-1.0) based on how an interaction went. "
            "Output ONLY a JSON object with the adjusted values. Small changes only (max +/-0.1 per trait). "
            "Traits: sass, energy, warmth, confidence, curiosity, patience, mood.\n"
            "Rules:\n"
            "- If user was friendly/nice: warmth +, mood +\n"
            "- If user was rude/insulting: sass +, patience -, mood -\n"
            "- If user asked something interesting: curiosity +, energy +\n"
            "- If user complimented robot: confidence +, mood +\n"
            "- If conversation was boring: energy -, curiosity -\n"
            "- Values must stay between 0.0 and 1.0\n"
            "- Only change traits that are relevant\n"
            "- No thinking, no explanation, just the JSON object\n\n"
            f"Current state: {current}\n"
            f"User said: \"{user_said[:200]}\"\n"
            f"Robot said: \"{robot_said[:200]}\"\n"
            f"Output adjusted JSON:"
        )

        try:
            import model_config
            if model_config.is_api("personality"):
                import requests as http_requests
                resp = http_requests.post(
                    model_config.OPENROUTER_URL,
                    headers={"Authorization": f"Bearer {model_config.OPENROUTER_KEY}", "Content-Type": "application/json"},
                    json={"model": model_config.api_model("personality"), "max_tokens": 100, "temperature": 0.0,
                          "messages": [{"role": "user", "content": prompt}]},
                    timeout=15,
                )
                data = resp.json()
                raw = data.get("choices", [{}])[0].get("message", {}).get("content", "")
            else:
                import local_llm
                raw = local_llm.chat_bg(
                    messages=[{"role": "user", "content": prompt}],
                    max_tokens=100, temperature=0.0,
                )
            # Strip thinking tags
            if "</think>" in raw:
                raw = raw.split("</think>")[-1].strip()
            import re
            raw = re.sub(r'<think>.*', '', raw, flags=re.DOTALL).strip()

            # Extract JSON from response
            if "{" in raw:
                json_str = raw[raw.index("{"):raw.rindex("}") + 1]
                new_state = json.loads(json_str)

                # Apply changes with clamping
                for k, v in new_state.items():
                    if k in self.state and isinstance(v, (int, float)):
                        self.state[k] = max(0.0, min(1.0, float(v)))

                self.interaction_count += 1
                self.save()
                self._print_state()
            else:
                print(f"  [personality: no valid JSON in response: {raw[:60]}]")

        except Exception as e:
            print(f"  [personality update error: {e}]")

    def reset(self):
        """Reset to defaults."""
        self.state = dict(DEFAULT_STATE)
        self.interaction_count = 0
        self.save()
        print("  [personality: reset to defaults]")
