# TTS Models for Robot Integration: Kokoro vs Orpheus

## Executive Summary

Two strong open-source TTS candidates exist for a robot project requiring low-latency, expressive, and potentially custom-voice speech output. **Kokoro-82M** is an extremely fast, lightweight (82M parameter) StyleTTS2-derived model that fits entirely in ~300 MB of GPU memory and achieves RTF as low as 0.03 on GPU — meaning it generates 10 seconds of audio in ~0.3 seconds. It does not natively clone voices but a third-party tool (KokoClone) adds zero-shot voice conversion from 3–10 s of reference audio.

**Orpheus-3B** is a 3-billion-parameter LLM-based model (Llama-3B backbone) that produces remarkably human-sounding, emotionally expressive speech with built-in tags for laugh, sigh, cough, gasp, and more. It is significantly heavier: FP16 inference requires approximately 14–16 GB VRAM just for the LLM component, making it a tight fit on a 24 GB card when sharing with a vision model. It supports zero-shot voice cloning through the pretrained checkpoint via audio-conditioned prompting, and fine-tuned voice cloning is also straightforward.

For a 24 GB RTX 3090 that must also run a 2.2B vision model, **Kokoro is the practical winner** for co-resident deployment. Orpheus can run on the same card but requires FP8 quantization and careful VRAM budgeting; coexistence with a concurrent vision model is risky without CPU offloading tricks.

---

## Table of Contents

1. [Kokoro TTS (hexgrad/kokoro)](#1-kokoro-tts)
   - 1.1 Architecture & Overview
   - 1.2 Inference Speed (RTF)
   - 1.3 VRAM Requirements
   - 1.4 Audio Output Format
   - 1.5 Installation
   - 1.6 Python API
   - 1.7 Voice Cloning

2. [Orpheus TTS (canopyai/Orpheus-TTS)](#2-orpheus-tts)
   - 2.1 Architecture & Overview
   - 2.2 Inference Speed (RTF / Latency)
   - 2.3 VRAM Requirements
   - 2.4 Audio Output Format
   - 2.5 Installation
   - 2.6 Python API
   - 2.7 Emotional Speech Tags
   - 2.8 Voice Cloning

3. [Head-to-Head Comparison](#3-head-to-head-comparison)

4. [VRAM Budget: RTX 3090 + 2.2B Vision Model](#4-vram-budget-rtx-3090--22b-vision-model)

5. [Voice Cloning Deep Dive](#5-voice-cloning-deep-dive)

6. [Deployment Recommendations](#6-deployment-recommendations)

7. [Resources](#7-resources)

---

## 1. Kokoro TTS

**Repository**: https://github.com/hexgrad/kokoro  
**Model weights**: https://huggingface.co/hexgrad/Kokoro-82M  
**PyPI**: `pip install kokoro`

### 1.1 Architecture and Overview

Kokoro-82M is a hybrid model that combines StyleTTS 2 and iSTFTNet. It removes the computationally expensive diffusion-based style modeling from StyleTTS 2 and uses iSTFTNet for direct waveform synthesis instead of a separate vocoder stage. The result is a single-stage, transformer-based decoder that takes phonemes + a voice embedding tensor and outputs audio directly.

- **Parameter count**: 82 million
- **Model file size**: ~350 MB (PyTorch checkpoint)
- **Architecture**: StyleTTS2 hybrid + iSTFTNet vocoder
- **Voice representation**: Each voice is a dense embedding tensor of shape `511 x 1 x 256`. Swapping the tensor changes the speaker identity without reloading the model.
- **MOS score**: 4.2–4.5 (highest among open-source TTS models as of early 2025)
- **Languages**: American English (`a`), British English (`b`), Japanese (`j`), Mandarin (`z`), Spanish (`e`), French (`f`), Hindi (`h`), Italian (`i`), Brazilian Portuguese (`p`)

### 1.2 Inference Speed (RTF)

RTF (Real-Time Factor) = inference time / audio duration. Lower is faster. RTF < 1.0 means faster than real-time.

| Backend | Hardware | RTF |
|---|---|---|
| PyTorch (GPU) | NVIDIA A10G | ~0.03 |
| PyTorch (GPU) | NVIDIA L4 | ~0.05 |
| PyTorch (GPU) | NVIDIA T4 | ~0.08 |
| PyTorch (GPU) | RTX 3090 (estimated) | ~0.03–0.04 |
| ONNX (GPU) | A10G | ~0.04 |
| ONNX (CPU, optimized) | Modern x86 | ~0.4 (2.4x real-time) |
| PyTorch (CPU) | Modern x86 | ~0.8–1.2 |

At RTF 0.03 on GPU: a 10-second audio clip is generated in ~0.3 seconds. A 1-second utterance takes roughly 30 ms of compute. Kokoro-FastAPI reports ~35x real-time speed on CUDA, and can synthesize a 100-word paragraph in under 3 seconds.

The Rust port (Kokoros, `lucasjinreal/Kokoros`) reports even lower latency and is described as "insanely fast" for real-time robotics scenarios.

**Benchmarks source**: https://gist.github.com/efemaer/23d9a3b949b751dde315192b4dcf0653

### 1.3 VRAM Requirements

| Precision | VRAM Used |
|---|---|
| FP32 (PyTorch default) | ~1.5–2 GB |
| FP16 | ~0.8–1 GB |
| ONNX (GPU) | ~0.7–1 GB |

The entire model fits in under 2 GB of VRAM. Multiple community deployments confirm it runs without issue alongside other GPU-resident services. On a 24 GB RTX 3090 it is effectively "free" in terms of VRAM budget — it occupies roughly 8% of available memory at FP32.

### 1.4 Audio Output Format

| Property | Value |
|---|---|
| Sample rate | 24,000 Hz (24 kHz) |
| Bit depth | 16-bit PCM (int16) when saved via soundfile |
| Channels | Mono |
| Supported output containers | WAV, FLAC, MP3, Opus, PCM (raw) |

The pipeline returns a NumPy float32 array internally. The caller decides how to encode it. The canonical example writes 24 kHz WAV with `soundfile`.

### 1.5 Installation

**System dependency** (required for phonemization of unknown words):

```bash
# Debian/Ubuntu
sudo apt-get install espeak-ng

# macOS
brew install espeak-ng
```

**Python package (PyTorch backend, GPU)**:

```bash
pip install kokoro>=0.9.4 soundfile misaki[en]

# If you need CUDA support and don't have it yet:
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu121
```

**ONNX backend (lighter, no PyTorch)**:

```bash
pip install kokoro-onnx
# GPU ONNX:
pip install onnxruntime-gpu
```

Python version requirement: `>=3.10, <3.13`.

### 1.6 Python API

**Basic synthesis (PyTorch backend)**:

```python
from kokoro import KPipeline
import soundfile as sf
import numpy as np

# lang_code: 'a'=American English, 'b'=British English, etc.
pipeline = KPipeline(lang_code='a')

text = "Hello, I am Claptrap, your Interplanetary Ninja Assassin Robot!"

# voice is a string name; built-in voices: af_heart, af_bella, am_adam, etc.
generator = pipeline(text, voice='af_heart', speed=1.0)

audio_chunks = []
for i, (graphemes, phonemes, audio) in enumerate(generator):
    audio_chunks.append(audio)

# Concatenate and save
full_audio = np.concatenate(audio_chunks)
sf.write('output.wav', full_audio, 24000)
```

**GPU device selection**:

```python
import torch
pipeline = KPipeline(lang_code='a', device=torch.device('cuda:0'))
```

**Voice blending** (mix two voices):

```python
import torch
from kokoro import KPipeline

pipeline = KPipeline(lang_code='a')
# Load two voice embeddings and interpolate
v1 = pipeline.load_voice('af_heart')   # shape: [511, 1, 256]
v2 = pipeline.load_voice('am_adam')
blended = 0.5 * v1 + 0.5 * v2         # simple linear blend
# Pass blended tensor directly
generator = pipeline(text, voice=blended)
```

**Streaming / real-time use**: The `generator` object yields audio chunks sentence-by-sentence, so you can pipe each chunk to an audio output device without waiting for the full text to be processed. This gives latency proportional to the first sentence length.

**ONNX backend** (simpler, no PyTorch):

```python
from kokoro_onnx import Kokoro

kokoro = Kokoro("kokoro-v1.0.onnx", "voices-v1.0.bin")
samples, sample_rate = kokoro.create("Hello, world!", voice="af_heart", speed=1.0)
# samples is float32 numpy array, sample_rate is 24000
```

### 1.7 Voice Cloning

**Native Kokoro**: No zero-shot voice cloning. Voices are pre-trained embedding tensors. You can blend existing voices but cannot extract a new embedding from arbitrary reference audio using the base model.

**KokoClone** (third-party, https://github.com/Ashish-Patnaik/kokoclone):

- Adds zero-shot voice cloning on top of Kokoro-ONNX
- Mechanism: uses the Kanade speech tokenizer to disentangle speaker identity from content in the reference audio, then injects the extracted speaker embedding into Kokoro's voice conditioning pathway
- Reference audio: **3–10 seconds** minimum; 20–30 seconds gives more reliable speaker identity transfer
- Works in two modes:
  1. Text-to-speech in cloned voice (upload reference audio + type text)
  2. Audio-to-audio voice conversion (re-voice any recording without transcription)
- Zero-shot: yes — no fine-tuning needed, runs on CPU or GPU
- Quality caveat: zero-shot cloning captures general tone/timbre but misses fine prosodic quirks and speaking rhythm. For a very distinctive voice like Claptrap, results will approximate the character but won't be exact.

---

## 2. Orpheus TTS

**Repository**: https://github.com/canopyai/Orpheus-TTS  
**Model weights**: https://huggingface.co/canopylabs/orpheus-3b-0.1-ft  
**PyPI**: `pip install orpheus-speech`

### 2.1 Architecture and Overview

Orpheus is fundamentally an LLM-based TTS system. It is built on the **Llama-3B backbone** fine-tuned to output audio tokens instead of text tokens. Audio tokens are decoded by the **SNAC (Multi-Scale Neural Audio Codec)** decoder to produce waveforms. This two-stage (LLM token generation + SNAC decode) pipeline is what gives it human-like prosody and emotional range — the LLM can model long-range prosodic context in ways that classical acoustic models cannot.

- **Parameter count**: 3 billion (LLM) + SNAC decoder (~50M)
- **Model file size**: ~6 GB at FP16, ~3 GB at INT4/Q4 GGUF
- **Architecture**: Llama-3B fine-tuned for audio token prediction + SNAC codec decoder
- **MOS score**: 4.2 (comparable to Kokoro)
- **Languages**: Primarily English (8 preset voices); multilingual fine-tunes exist (German confirmed)
- **Preset voices** (in order of conversational realism): tara, leah, jess, leo, dan, mia, zac, zoe

### 2.2 Inference Speed (RTF / Latency)

Orpheus generates ~83 audio tokens per second of speech (each 7-token frame = 1 audio frame). Real-time threshold is ~83 tokens/sec.

| Hardware | Backend | Performance | Notes |
|---|---|---|---|
| H100 (full) | vLLM FP8 | ~150 ms TTFB, 25+ concurrent streams | Production tier |
| H100 MIG (40 GB) | vLLM FP8 | ~200 ms TTFB | Canopy/Baseten benchmark |
| RTX 3090 (24 GB) | vLLM FP8 | Real-time single stream | Confirmed by bitbasti |
| RTX 3090 (24 GB) | vLLM FP16 | Near real-time single stream | Tighter; may drop frames under load |
| RTX 3090 (24 GB) | llama.cpp GGUF Q4 | Slower than FP8, CPU fallback available | Community reports |

**Key number**: The RTX 3090 with FP8 quantization can produce real-time audio for a single voice stream. Unlike Kokoro's 35x real-time headroom, Orpheus is approximately at 1x–2x real-time on a 3090 — meaning there is little margin for concurrent load.

**Time to first byte** on RTX 3090: estimated 300–500 ms (extrapolated from H100 numbers and community reports). Kokoro's comparable latency is 50–150 ms.

### 2.3 VRAM Requirements

| Component | FP16 | FP8 | INT4 GGUF |
|---|---|---|---|
| Orpheus-3B LLM | ~6–7 GB | ~4–5 GB | ~2.5–3 GB |
| SNAC decoder | ~0.5–1 GB (GPU) | ~0.5–1 GB | ~0.5 GB |
| vLLM KV cache overhead | ~2–4 GB | ~1–2 GB | N/A |
| **Total (GPU)** | **~10–14 GB** | **~6–8 GB** | **~3.5–5 GB** |

Confirmed data points:
- 12 GB VRAM (RTX 3060): fails with standard vLLM FP16; possible with GGUF Q4 + llama.cpp
- 16 GB VRAM: workable with FP8 or INT4
- 24 GB VRAM (RTX 3090): comfortable with FP16; easy with FP8
- SNAC can be offloaded to CPU to save ~0.5–1 GB at the cost of ~5–10 ms decode latency per chunk

**Practical tip**: Set `--gpu-memory-utilization 0.7` in vLLM to leave headroom for SNAC and FastAPI overhead when running on a 3090.

### 2.4 Audio Output Format

| Property | Value |
|---|---|
| Sample rate | 24,000 Hz (24 kHz) |
| Bit depth | 16-bit PCM (int16) |
| Channels | Mono |
| Container | WAV (canonical), PCM chunks for streaming |

Same sample rate as Kokoro. The SNAC decoder outputs raw PCM at 24 kHz.

### 2.5 Installation

```bash
# Install orpheus-speech (pulls in vLLM)
pip install orpheus-speech

# If vLLM 0.7.x has issues (reported March 2025):
pip install vllm==0.7.3

# SNAC decoder (usually pulled in as dependency)
pip install snac
```

**GGUF / llama.cpp path** (lower VRAM, no vLLM):

```bash
# Download GGUF from Hugging Face: Mungert/orpheus-3b-0.1-ft-GGUF
# Run with llama-server:
./llama-server -m orpheus-3b-0.1-ft-Q4_K_M.gguf \
    --ctx-size 8192 \
    --n-predict 8192 \
    --rope-scaling linear

# Then use orpheus-cpp (freddyaboulton/orpheus-cpp) as Python client
pip install orpheus-cpp
```

### 2.6 Python API

**Basic synthesis (orpheus-speech / vLLM backend)**:

```python
from orpheus_tts import OrpheusModel
import wave

model = OrpheusModel(
    model_name="canopylabs/orpheus-tts-0.1-finetune-prod",
    max_model_len=2048
)

prompt = "I missed you <laugh> so much! It's been way too long."

syn_tokens = model.generate_speech(
    prompt=prompt,
    voice="tara",   # tara, leah, jess, leo, dan, mia, zac, zoe
)

with wave.open("output.wav", "wb") as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)        # 16-bit
    wf.setframerate(24000)
    for audio_chunk in syn_tokens:  # generator — streams audio
        wf.writeframes(audio_chunk)
```

**Streaming to sounddevice (robot real-time playback)**:

```python
import sounddevice as sd
import numpy as np

model = OrpheusModel(model_name="canopylabs/orpheus-tts-0.1-finetune-prod")

def speak(text, voice="tara"):
    stream = sd.RawOutputStream(samplerate=24000, channels=1, dtype='int16')
    stream.start()
    for chunk in model.generate_speech(prompt=text, voice=voice):
        stream.write(chunk)
    stream.stop()
    stream.close()

speak("Initiating robot sequence. <sigh> Here we go again.")
```

**FP8 quantization with vLLM** (save ~2 GB VRAM):

```python
from orpheus_tts import OrpheusModel

model = OrpheusModel(
    model_name="canopylabs/orpheus-tts-0.1-finetune-prod",
    quantization="fp8",              # vLLM flag
    gpu_memory_utilization=0.70,     # leave room for SNAC + overhead
)
```

### 2.7 Emotional Speech Tags

Orpheus supports in-text emotion tags that trigger natural-sounding vocalizations. These are treated as special tokens by the underlying LLM.

| Tag | Sound |
|---|---|
| `<laugh>` | Full laugh |
| `<chuckle>` | Short, breathy chuckle |
| `<giggle>` | Lighter giggle |
| `<sigh>` | Audible sigh |
| `<cough>` | Cough |
| `<sniffle>` | Quiet sniffle |
| `<groan>` | Groan |
| `<yawn>` | Yawn |
| `<gasp>` | Sudden gasp |

**Usage example**:

```python
text = "Oh no, not another human. <sigh> I suppose I can help you. <chuckle> Just kidding. <laugh>"
```

Tags are placed inline in the text string. They are not SSML — they are literal special tokens the model was trained to produce. Putting them mid-sentence is supported and they blend naturally with surrounding speech.

**Note**: Emotion tags are only reliable with the fine-tuned model (`orpheus-tts-0.1-finetune-prod`). The pretrained base model supports them less consistently.

### 2.8 Voice Cloning

Orpheus supports two voice cloning approaches:

**A. Zero-shot via the pretrained model (audio-conditioned prompting)**

The pretrained checkpoint (`canopylabs/orpheus-3b-0.1-pretrained`) can condition generation on one or more `(text, audio)` pairs provided as context in the prompt. This works because the pretrained LLM saw diverse speech during training and can continue generating "in the style of" the provided examples.

- Reference audio: a few seconds minimum; more pairs in the prompt = more reliable voice identity
- No fine-tuning required
- Quality: captures general tone and timbre, loses prosodic quirks and rhythm details
- The fine-tuned model (`orpheus-tts-0.1-finetune-prod`) does NOT support this — it was trained on fixed voice IDs. You must use the pretrained base.

**B. Fine-tuning (best quality)**

Canopy Labs provides data processing scripts to fine-tune from the pretrained base on custom speaker audio. The Unsloth framework supports Orpheus-3B fine-tuning directly.

- Data requirement: typically 10–60 minutes of clean reference audio for good results; community fine-tunes report usable quality with as little as 5–10 minutes
- Training time: 1–4 hours on a single A100/H100; longer on consumer hardware
- The resulting fine-tuned model gets a new named voice and is loaded like any other Orpheus voice

---

## 3. Head-to-Head Comparison

| Dimension | Kokoro-82M | Orpheus-3B |
|---|---|---|
| **Parameters** | 82M | 3B |
| **Model weight size** | ~350 MB | ~6 GB FP16 / ~3 GB Q4 |
| **GPU VRAM (inference)** | ~1–2 GB | ~8–14 GB (depends on quant) |
| **RTF on RTX 3090** | ~0.03–0.04 (35x real-time) | ~0.7–1.0 (near real-time) |
| **Latency to first audio** | 50–150 ms | 300–500 ms (estimated) |
| **MOS quality score** | 4.2–4.5 | 4.2 |
| **Emotional expressiveness** | Moderate (prosody from training data) | High (explicit emotion tags) |
| **Emotion tags** | No | Yes (9 tags) |
| **Native voice cloning** | No | Yes (pretrained, zero-shot) |
| **Zero-shot cloning ease** | Via KokoClone (external) | Built into pretrained model |
| **Fine-tune for custom voice** | Possible but undocumented | Well-supported with Unsloth |
| **Languages** | 9 languages | Primarily English |
| **Streaming support** | Yes (generator-based) | Yes (chunk-based generator) |
| **CPU-only usable** | Yes (ONNX, 2.4x real-time) | Slow (not recommended) |
| **Edge/embedded feasible** | Yes | No |
| **Best use case** | Low-latency, lightweight, multilingual | Expressive, emotional, voice-cloned |

---

## 4. VRAM Budget: RTX 3090 + 2.2B Vision Model

Total available: **24 GB VRAM**

### Estimated VRAM consumption

| Component | Estimated VRAM |
|---|---|
| 2.2B vision model (FP16) | ~5–6 GB |
| 2.2B vision model (INT4) | ~2.5–3 GB |
| Kokoro-82M (FP16 PyTorch) | ~0.8–1 GB |
| Orpheus-3B (FP16 + SNAC) | ~10–14 GB |
| Orpheus-3B (FP8 + SNAC on CPU) | ~5–6 GB |
| Orpheus-3B (INT4 GGUF + SNAC on CPU) | ~3–4 GB |

### Scenario A: Kokoro + 2.2B Vision (RECOMMENDED)

```
2.2B vision model (FP16):  ~6 GB
Kokoro-82M (FP16):         ~1 GB
System overhead:           ~1–2 GB
------------------------
Total:                     ~8–9 GB  (well within 24 GB)
Headroom:                  ~15 GB free
```

This is extremely comfortable. Both models can be kept resident simultaneously, and Kokoro's near-zero VRAM footprint means the vision model can use a large batch size or long context without conflict.

### Scenario B: Orpheus (FP16) + 2.2B Vision

```
2.2B vision model (FP16):  ~6 GB
Orpheus-3B LLM (FP16):    ~7 GB
SNAC decoder (GPU):        ~1 GB
vLLM KV cache:             ~3 GB
System overhead:           ~2 GB
------------------------
Total:                     ~19 GB  (tight but fits)
```

Possible, but there is only ~5 GB of headroom. Under load (long KV cache for vision model, long TTS prompt) you may hit OOM. Not recommended for concurrent inference without careful scheduling.

### Scenario C: Orpheus (FP8) + 2.2B Vision (INT4)

```
2.2B vision model (INT4):  ~3 GB
Orpheus-3B LLM (FP8):     ~5 GB
SNAC decoder (CPU offload):~0 GB GPU
vLLM KV cache:             ~2 GB
System overhead:           ~1–2 GB
------------------------
Total:                     ~11 GB  (comfortable)
```

This is the viable path if you want Orpheus on the 3090. Accept a small quality trade-off on both models for comfortable coexistence.

### Critical note on simultaneous inference

Kokoro and the vision model can genuinely run simultaneously (both resident, interleaved CUDA calls). Orpheus with vLLM is more complex — vLLM pre-allocates its KV cache on startup and will not dynamically yield memory to other processes without careful `--gpu-memory-utilization` tuning.

---

## 5. Voice Cloning Deep Dive

### Can either model clone a Claptrap-style voice?

| Question | Kokoro + KokoClone | Orpheus (pretrained) | Orpheus (fine-tuned) |
|---|---|---|---|
| Zero-shot? | Yes | Yes | No (requires training) |
| Reference audio length | 3–10 s minimum | "a few seconds"; more = better | 5–60 min for good quality |
| Captures tone/timbre? | Yes | Yes | Yes (best) |
| Captures prosodic quirks? | Partial | Partial | Yes (if training data has them) |
| Handles robotic/cartoon voice? | Unknown — depends on how well Kanade tokenizes non-typical voice | Better due to LLM context | Best with enough samples |
| Add emotion tags to cloned voice? | No (Kokoro has no emotion tags) | Yes (pretrained only) | Yes |
| Latency impact of cloning? | None (embedding swap, zero cost) | Higher (longer prompt = more tokens) | Same as standard fine-tune |

**Practical recommendation for Claptrap**: Collect 15–30 seconds of Claptrap audio clips from the game (clean speech only, no music/SFX). Try Orpheus pretrained with 3–5 example pairs first. If quality is insufficient, fine-tune with Unsloth using 5+ minutes of clips. Kokoro + KokoClone is worth testing first for its speed advantage.

### How voice style works in each model

**Kokoro**: Voice = a 511x1x256 embedding tensor. The model decodes (phonemes, voice\_embedding) -> audio. To clone, you need to produce a new embedding that represents the target speaker. KokoClone does this by encoding reference audio through Kanade (a disentangled speech tokenizer), extracting speaker features, and mapping them to Kokoro's embedding space.

**Orpheus**: Voice = a named token (`<|tara|>`) in the LLM's vocabulary. The model was trained to associate each token with a speaker style. For zero-shot cloning, the pretrained model uses in-context audio conditioning: you prepend `[(text1, audio_tokens1), (text2, audio_tokens2), ...]` to the generation prompt and the LLM continues in the same voice. This works because audio tokens are part of the LLM's vocabulary, so "reference audio" is just more context.

---

## 6. Deployment Recommendations

### For a robot requiring fast TTS with minimal footprint

**Use Kokoro** as the primary TTS engine:

```python
# Recommended: keep pipeline resident, call per utterance
from kokoro import KPipeline
import sounddevice as sd
import numpy as np

pipeline = KPipeline(lang_code='a', device='cuda:0')

def speak(text: str, voice: str = 'af_heart', speed: float = 1.0):
    chunks = []
    for _, _, audio in pipeline(text, voice=voice, speed=speed):
        chunks.append(audio)
    audio = np.concatenate(chunks)
    sd.play(audio, samplerate=24000, blocking=True)
```

For streaming (lower latency):

```python
def speak_streaming(text: str, voice: str = 'af_heart'):
    stream = sd.OutputStream(samplerate=24000, channels=1, dtype='float32')
    stream.start()
    for _, _, audio in pipeline(text, voice=voice):
        stream.write(audio)
    stream.stop()
```

### For a robot requiring emotional expressiveness

**Use Orpheus** with FP8 quantization and SNAC on CPU:

```python
from orpheus_tts import OrpheusModel

model = OrpheusModel(
    model_name="canopylabs/orpheus-tts-0.1-finetune-prod",
    quantization="fp8",
    gpu_memory_utilization=0.65,  # leave room for vision model
)

def speak(text: str, voice: str = "tara"):
    import sounddevice as sd
    stream = sd.RawOutputStream(samplerate=24000, channels=1, dtype='int16')
    stream.start()
    for chunk in model.generate_speech(prompt=text, voice=voice):
        stream.write(chunk)
    stream.stop()

speak("Let's get this robot party started! <laugh> Oh yeah!")
```

### Recommended architecture for co-resident 3090 deployment

```
GPU (24 GB):
  - Vision model (2.2B, FP16 or INT4): loaded always
  - Kokoro-82M (PyTorch FP16):         loaded always  [if using Kokoro]
  - Orpheus-3B (FP8, vLLM):            loaded always  [if using Orpheus]
  - SNAC decoder:                       CPU (if Orpheus + tight VRAM)

CPU:
  - SNAC decoder (Orpheus path, if offloaded)
  - Text preprocessing / phonemization (Kokoro espeak-ng)
  - Application logic
```

---

## 7. Resources

### Kokoro

- [hexgrad/kokoro — Official Repository](https://github.com/hexgrad/kokoro)
- [hexgrad/Kokoro-82M — Model Weights on Hugging Face](https://huggingface.co/hexgrad/Kokoro-82M)
- [kokoro on PyPI](https://pypi.org/project/kokoro/)
- [Kokoro v1 Benchmark (PyTorch/ONNX, CPU/GPU) — Gist by efemaer](https://gist.github.com/efemaer/23d9a3b949b751dde315192b4dcf0653)
- [remsky/Kokoro-FastAPI — Docker wrapper with benchmarks](https://github.com/remsky/Kokoro-FastAPI)
- [thewh1teagle/kokoro-onnx — Pure ONNX implementation](https://github.com/thewh1teagle/kokoro-onnx)
- [Ashish-Patnaik/kokoclone — Zero-shot voice cloning for Kokoro](https://github.com/Ashish-Patnaik/kokoclone)
- [Show HN: KokoClone — Hacker News discussion](https://news.ycombinator.com/item?id=47252271)
- [Kokoro-82M — UnfoldAI analysis](https://unfoldai.com/kokoro-82m/)
- [lucasjinreal/Kokoros — Rust port for maximum speed](https://github.com/lucasjinreal/Kokoros)

### Orpheus

- [canopyai/Orpheus-TTS — Official Repository](https://github.com/canopyai/Orpheus-TTS)
- [canopylabs/orpheus-3b-0.1-ft — Fine-tuned weights on Hugging Face](https://huggingface.co/canopylabs/orpheus-3b-0.1-ft)
- [canopylabs/orpheus-3b-0.1-pretrained — Pretrained base (for zero-shot cloning)](https://huggingface.co/canopylabs/orpheus-3b-0.1-pretrained)
- [Real-Time TTS Streaming with Orpheus on RTX 3090 — bitbasti](https://bitbasti.com/blog/audio-streaming-with-orpheus)
- [VRAM Requirements Issue #9 — GitHub](https://github.com/canopyai/Orpheus-TTS/issues/9)
- [Zero-Shot Voice Cloning Issue #6 — GitHub](https://github.com/canopyai/Orpheus-TTS/issues/6)
- [Lex-au/Orpheus-FastAPI — High-performance server with emotion tags](https://github.com/Lex-au/Orpheus-FastAPI)
- [Mungert/orpheus-3b-0.1-ft-GGUF — GGUF quantized weights](https://huggingface.co/Mungert/orpheus-3b-0.1-ft-GGUF)
- [Orpheus TTS Fine-tuning with Unsloth](https://docs.unsloth.ai/basics/text-to-speech-tts-fine-tuning)
- [Orpheus-3B HN discussion](https://news.ycombinator.com/item?id=43417894)
- [freddyaboulton/orpheus-cpp — llama.cpp-based streaming TTS](https://github.com/freddyaboulton/orpheus-cpp)
- [Baseten: Canopy Labs inference partnership](https://www.baseten.co/blog/canopy-labs-selects-baseten-as-preferred-inference-provider-for-orpheus-tts-model/)

### Comparisons

- [12 Best Open-Source TTS Models Compared (2025) — Inferless](https://www.inferless.com/learn/comparing-different-text-to-speech---tts--models-part-2)
- [Orpheus 3B vs Kokoro TTS comparison — CoderSera](https://codersera.com/blog/orpheus-3b-vs-kokoro-tts-comparison-of-open-source-ai-voice-synthesis-models)
- [Best Open-Source TTS Models 2026 — CodeSOTA](https://www.codesota.com/guides/tts-models)
- [Choosing TTS Models: F5-TTS, Kokoro, SparkTTS, Sesame CSM — DigitalOcean](https://www.digitalocean.com/community/tutorials/best-text-to-speech-models)
