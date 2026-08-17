import os
"""
Model Configuration for Octavius

Switch between model providers easily.
Edit ACTIVE_PROFILE to change all models at once.
"""

OPENROUTER_KEY = os.environ.get("OPENROUTER_KEY", "")
OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"

# ---- Profiles ----

PROFILES = {
    "gemma4": {
        "brain": "google/gemma-4-26b-a4b-it",
        "thinking": "google/gemma-4-26b-a4b-it",
        "telegram": "google/gemma-4-26b-a4b-it",
        "memory": "google/gemma-4-26b-a4b-it",
        "personality": "google/gemma-4-26b-a4b-it",
        "gate": "local:qwen1.5b",       # always local
        "vision": "local:smolvlm2",      # always local
        "tts": "local:f5tts",            # always local
        "stt": "groq:whisper",           # always groq
    },
    "qwen3.5": {
        "brain": "qwen/qwen3.5-9b",
        "thinking": "qwen/qwen3.5-9b",
        "telegram": "qwen/qwen3.5-9b",
        "memory": "local:qwen3-4b",
        "personality": "local:qwen3-4b",
        "gate": "local:qwen1.5b",
        "vision": "local:smolvlm2",
        "tts": "local:f5tts",
        "stt": "groq:whisper",
    },
    "claude": {
        "brain": "anthropic/claude-sonnet-4",
        "thinking": "anthropic/claude-sonnet-4",
        "telegram": "anthropic/claude-sonnet-4",
        "memory": "local:qwen3-4b",
        "personality": "local:qwen3-4b",
        "gate": "local:qwen1.5b",
        "vision": "local:smolvlm2",
        "tts": "local:f5tts",
        "stt": "groq:whisper",
    },
    "local": {
        "brain": "local:qwen3-8b",
        "thinking": "local:qwen3-8b",
        "telegram": "local:qwen3-8b",
        "memory": "local:qwen3-4b",
        "personality": "local:qwen3-4b",
        "gate": "local:qwen1.5b",
        "vision": "local:smolvlm2",
        "tts": "local:f5tts",
        "stt": "groq:whisper",
    },
}

# ---- Active Profile ----
ACTIVE_PROFILE = "gemma4"


def get(role):
    """Get the model ID for a role."""
    return PROFILES[ACTIVE_PROFILE][role]


def is_local(role):
    """Check if a role uses a local model."""
    return get(role).startswith("local:")


def is_api(role):
    """Check if a role uses an API model (OpenRouter)."""
    return not is_local(role)


def api_model(role):
    """Get the OpenRouter model ID for a role."""
    return get(role)
