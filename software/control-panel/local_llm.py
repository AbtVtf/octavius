"""
Local LLM Brain for Octavius — Qwen3 8B on GPU

Replaces OpenRouter API calls with local inference.
Handles Qwen3's <tool_call> XML format and <think> tags.
"""

import json
import os
import re
import threading
from llama_cpp import Llama

QWEN3_MODEL = None       # 8B — brain (conversation, thinking, telegram)
QWEN3_BG_MODEL = None    # 4B — background (memory, personality)
_gpu_lock = threading.Lock()
_bg_lock = threading.Lock()


def init(model_path, bg_model_path=None):
    """Load Qwen3 models on GPU. Call once at startup."""
    global QWEN3_MODEL, QWEN3_BG_MODEL

    # Main model (used for thinking engine if no bg model)
    print(f"Loading {os.path.basename(model_path)} on GPU...")
    QWEN3_MODEL = Llama(
        model_path=model_path,
        n_ctx=4096,
        n_gpu_layers=-1,
        n_threads=4,
        verbose=False,
    )
    print(f"Model ready: {os.path.basename(model_path)}")

    if bg_model_path and bg_model_path != model_path:
        print(f"Loading {os.path.basename(bg_model_path)} on GPU...")
        QWEN3_BG_MODEL = Llama(
            model_path=bg_model_path,
            n_ctx=4096,
            n_gpu_layers=-1,
            n_threads=2,
            verbose=False,
        )
        print(f"Background model ready: {os.path.basename(bg_model_path)}")

    return QWEN3_MODEL


def get_model():
    return QWEN3_MODEL


def chat(messages, tools=None, max_tokens=500, temperature=0.7):
    """
    Send messages to Qwen3, return (text, tool_calls).

    text: the reply text (with <think> stripped)
    tool_calls: list of {"name": ..., "arguments": {...}} dicts
    """
    if QWEN3_MODEL is None:
        raise RuntimeError("Qwen3 not initialized — call local_llm.init() first")

    with _gpu_lock:
        response = QWEN3_MODEL.create_chat_completion(
            messages=messages,
            tools=tools,
            max_tokens=max_tokens,
            temperature=temperature,
        )

    msg = response["choices"][0]["message"]
    raw = msg.get("content", "") or ""

    # Strip <think>...</think> tags
    if "</think>" in raw:
        raw = raw.split("</think>")[-1].strip()

    # Parse <tool_call> XML tags (Qwen3 format)
    tool_calls = []
    for match in re.finditer(r'<tool_call>\s*(\{.*?\})\s*</tool_call>', raw, re.DOTALL):
        try:
            tc = json.loads(match.group(1))
            tool_calls.append(tc)
        except json.JSONDecodeError:
            pass

    # Also check structured tool_calls (if llama-cpp parsed them)
    if msg.get("tool_calls"):
        for tc in msg["tool_calls"]:
            fn_args = tc["function"].get("arguments", {})
            if isinstance(fn_args, str):
                try:
                    fn_args = json.loads(fn_args)
                except:
                    fn_args = {}
            tool_calls.append({"name": tc["function"]["name"], "arguments": fn_args})

    # Clean text — remove tool_call tags
    clean_text = re.sub(r'<tool_call>.*?</tool_call>', '', raw, flags=re.DOTALL).strip()
    # Remove leftover think tags
    clean_text = re.sub(r'</?think>', '', clean_text).strip()

    return clean_text, tool_calls


def chat_bg(messages, max_tokens=200, temperature=0.0):
    """
    Background chat using Qwen3 4B. For memory extraction, personality updates.
    Returns text only (no tool parsing needed).
    Does NOT block the main brain model.
    """
    model = QWEN3_BG_MODEL or QWEN3_MODEL
    lock = _bg_lock if QWEN3_BG_MODEL else _gpu_lock

    with lock:
        response = model.create_chat_completion(
            messages=messages,
            max_tokens=max_tokens,
            temperature=temperature,
        )

    raw = response["choices"][0]["message"].get("content", "") or ""
    # Strip all thinking content
    if "</think>" in raw:
        raw = raw.split("</think>")[-1].strip()
    raw = re.sub(r'<think>.*?</think>', '', raw, flags=re.DOTALL).strip()
    raw = re.sub(r'<think>.*', '', raw, flags=re.DOTALL).strip()  # unclosed think tag
    raw = re.sub(r'</?think>', '', raw).strip()
    return raw
