# Octavius — an autonomous AI quadruped

A walking robot with a full AI stack: local vision-language perception,
voice-cloned speech, a learned RL gait, an agentic "brain" that drives
Claude Code as a persistent agent — and a computational-biology pipeline,
because a robot should have hobbies.

Octavius is built on the open-source
[Sesame robot](https://github.com/dorianborian/sesame-robot) hardware
platform (ESP32-S3, Apache-2.0 — thank you!), with custom firmware on top, a
self-designed power-distribution PCB, and everything above the servos built
from scratch.

## What it does

- **Perception** — local vision-language model (SmolVLM2-2.2B) for scene
  understanding plus YOLOv8 for object detection, running on an RTX 3090.
- **Voice** — speech-to-text via Whisper (Groq), replies through a
  voice-cloned F5-TTS voice with an ElevenLabs fallback. A small local LLM
  gates whether speech is actually directed at the robot before it responds.
- **Agentic brain** — drives the Claude Code CLI as a persistent,
  memory-backed agent (reads/writes files, runs commands, browses), and can
  orchestrate a multi-agent research crew (planner + engineer) via PaperclipAI.
- **Learned locomotion** — sim-to-real gait trained with PPO
  (Stable-Baselines3) in MuJoCo, exported to the firmware and streamed to the
  robot over TCP. Training code in `software/sesame-rl/`.
- **Computational biology** — the robot runs real protein/genomics work:
  structure prediction with Boltz-1 and mutation-effect scoring with the
  Evo2 7B DNA foundation model. Executed studies include a 2,079-mutation
  scan of PETase (a plastic-degrading enzyme); results live in `research/`.

## Repository layout

```
firmware/   ESP32-S3 firmware (Arduino) — locomotion, watcher, bridge
hardware/   CAD + the custom power-distribution PCB
software/
  control-panel/   the brain: perception, voice, agent orchestration
  sesame-rl/       PPO gait training (MuJoCo → firmware export)
  sesame-studio/   tooling
research/   comp-bio pipeline outputs (folded structures, scores)
docs/       notes and documentation
```

## Running it

Model weights (GGUF/YOLO/Kokoro) are not in the repo — the control panel
expects them under `software/control-panel/models/`. API keys are read from
environment variables (`OPENROUTER_KEY`, `GROQ_API_KEY`, `LINEAR_API_KEY`, `ELEVENLABS_API_KEY`).

## Credits & license

Hardware platform: [Sesame robot](https://github.com/dorianborian/sesame-robot)
by dorianborian, Apache-2.0. Everything else: Apache-2.0.
