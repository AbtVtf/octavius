# Lightweight Vision-Language Models for Local RTX 3090 Inference: Comprehensive Research Document

## Executive Summary

As of early 2026, the landscape for small vision-language models (VLMs) running locally on consumer hardware has matured dramatically. A range of models under 4B parameters can now describe images, answer visual questions, detect objects, and perform OCR — all at sub-2-second latency on a 24 GB NVIDIA RTX 3090. The standout recommendations for fast, low-VRAM local inference are **moondream2** (2B, ~0.1s/image on RTX 3090 via Photon), **SmolVLM2-2.2B** (~5.2 GB VRAM), **Qwen2.5-VL-3B-Instruct** (best general capability in the sub-4B tier), and **Florence-2-large** (0.77B, fastest overall, optimized for structured tasks). For use cases requiring reasoning and richer descriptions, **InternVL3-2B** and **Phi-4-multimodal** (5.6B, just above the 4B cutoff) are also strong contenders.

LLaVA-1.5-7B, while widely cited, has been largely superseded for this hardware tier. It uses 14–15 GB VRAM in FP16 and is slower than modern equivalents. Models from 2025 onward achieve better benchmark scores at smaller sizes. For a 24 GB GPU, you have substantial headroom to run multiple smaller models concurrently or to run a capable 7B-class model if you relax the strict sub-4B requirement.

All models listed are loadable via Python through HuggingFace Transformers, GGUF/llama.cpp, ONNX Runtime, or a dedicated pip package. Code examples for every viable option are provided below.

---

## Table of Contents

1. [Background and Context](#1-background-and-context)
2. [Hardware Context: RTX 3090 24 GB](#2-hardware-context-rtx-3090-24-gb)
3. [Model-by-Model Analysis](#3-model-by-model-analysis)
   - 3.1 moondream2 / moondream (2B)
   - 3.2 SmolVLM2 (256M / 500M / 2.2B)
   - 3.3 Florence-2 (0.23B / 0.77B)
   - 3.4 Qwen2.5-VL-3B-Instruct
   - 3.5 Qwen3-VL-2B-Instruct
   - 3.6 InternVL2.5 / InternVL3 (1B / 2B / 4B)
   - 3.7 PaliGemma2-3B
   - 3.8 Phi-3.5-Vision (4.2B)
   - 3.9 Phi-4-multimodal (5.6B)
   - 3.10 LLaVA-1.5 (7B / 13B) — Legacy
   - 3.11 Flash-VL 2B
   - 3.12 DeepSeek-VL 1.3B
   - 3.13 MiniCPM-V 2.6 (8B) — Honorable Mention
4. [Comparison Table](#4-comparison-table)
5. [Loading Methods and Code Examples](#5-loading-methods-and-code-examples)
6. [Best Practices for RTX 3090 Inference](#6-best-practices-for-rtx-3090-inference)
7. [Recommendation Matrix](#7-recommendation-matrix)
8. [Common Pitfalls](#8-common-pitfalls)
9. [Resources and Further Reading](#9-resources-and-further-reading)

---

## 1. Background and Context

Vision-language models (VLMs) combine a vision encoder (typically a variant of CLIP, SigLIP, or InternViT) with a large language model decoder. The vision encoder processes the image into tokens, which are projected into the LLM's embedding space. The LLM then autoregressively generates a text response conditioned on both the image tokens and the text prompt.

The key factors affecting local inference performance are:
- **Parameter count** — directly scales VRAM and compute requirements
- **Image token count** — how many tokens the image is compressed into before entering the LLM. This is often a bigger speed driver than parameter count. SmolVLM encodes a 384x384 patch into 81 tokens; Qwen2-VL can use 16,000 tokens for the same image.
- **Quantization** — INT4 and INT8 quantization dramatically reduce VRAM and increase throughput, often with minimal quality loss for captioning/VQA tasks
- **Attention implementation** — Flash Attention 2 (Ampere and later, i.e., RTX 3090 qualifies) gives significant speedups for long sequences
- **Inference engine** — llama.cpp, vLLM, lmdeploy, ONNX Runtime, and purpose-built engines (Moondream Photon) each offer different tradeoffs

The RTX 3090 belongs to the Ampere generation, supporting Flash Attention 2, bf16, and all modern CUDA features. 24 GB VRAM is generous for this class of models.

---

## 2. Hardware Context: RTX 3090 24 GB

| Spec | Value |
|---|---|
| VRAM | 24 GB GDDR6X |
| Memory bandwidth | 936 GB/s |
| Architecture | Ampere (sm_86) |
| Flash Attention 2 support | Yes |
| BF16 support | Yes |
| INT8 / INT4 support (bitsandbytes/GGUF) | Yes |

With 24 GB of VRAM you can comfortably fit:
- Any model under 4B parameters in FP16 (typically 8–10 GB)
- Any model under 4B in BF16/INT8 with ample room for KV cache
- A 7B model in INT4/GGUF (~4–5 GB) with headroom to spare
- Two 2B models simultaneously in FP16

Token generation speed on a 3090 for a 2B text-only LLM is roughly 100–130 tokens/s in FP16. For VLMs the effective rate depends heavily on image token count. With compilation and quantization, moondream2 reaches 184 tokens/s on a 3090.

---

## 3. Model-by-Model Analysis

### 3.1 moondream2 / moondream (2B)

**Repository:** `vikhyatk/moondream2` (HuggingFace), `moondream/moondream-2b-2025-04-14` (latest checkpoint)  
**Pip package:** `pip install moondream`  
**License:** Apache 2.0

**Overview:**  
Moondream2 is purpose-built for efficiency on consumer hardware. The team maintains frequent checkpoint releases with dated revisions (e.g., `2025-06-21`). Capabilities include image captioning, visual Q&A, object detection with bounding boxes, and pointing (returning (x,y) coordinates for objects). As of 2025, a 0.5B variant exists for extremely constrained hardware, and a Moondream 3 preview (9B total / 2B active MoE) was released offering stronger reasoning at similar inference speeds.

**Size and VRAM:**
| Precision | VRAM usage | Model size on disk |
|---|---|---|
| FP16 | ~4 GB | ~4 GB |
| INT8 | ~2 GB | ~2 GB |
| INT4 (4-bit) | ~2.45 GB | ~1 GB |

**Inference speed (RTX 3090):**
- Standard FP16 with `torch.compile()`: ~123 tokens/s
- 4-bit quantized: ~184 tokens/s
- Via Photon inference engine (pip package): ~2x further speedup, sub-100ms first-token latency

The 4-bit model generates a short caption (20–50 tokens) in approximately 0.1–0.3 seconds on a 3090. This comfortably beats the 2-second target.

**Photon engine note:** The `moondream` pip package wraps Moondream's custom Photon inference engine which uses hand-written CUDA kernels, paged KV caching, and automatic batching. It runs on Ampere through Blackwell GPUs. For production local deployments this is the fastest path. Requires a free API key from moondream.ai (even for local-only use via `local=True`).

**Strengths:** Fastest inference in class, tiny VRAM, excellent Python API, active development  
**Weaknesses:** Less capable on complex reasoning than larger models; Photon requires API key registration

---

### 3.2 SmolVLM2 (256M / 500M / 2.2B)

**Repository:** `HuggingFaceTB/SmolVLM2-2.2B-Instruct`, `HuggingFaceTB/SmolVLM2-500M-Instruct`, `HuggingFaceTB/SmolVLM2-256M-Instruct`  
**License:** Apache 2.0

**Overview:**  
SmolVLM2 is the second-generation model from Hugging Face's SmolLM team, released in early 2025. A major architectural innovation is aggressive image token compression: a 384x384 image patch encodes to only 81 tokens, versus 16,000+ tokens for Qwen2-VL on the same image. This results in 3–4.5x faster prefill and up to 16x faster generation than Qwen2-VL at comparable sizes. SmolVLM2 added video understanding, multi-image support, and improved document understanding over the original SmolVLM.

**Size and VRAM:**
| Model | Parameters | VRAM (BF16) |
|---|---|---|
| SmolVLM2-256M | 256M | < 1 GB |
| SmolVLM2-500M | 500M | ~1.5 GB |
| SmolVLM2-2.2B | 2.2B | ~5.2 GB |

**Inference speed:** On GPU, generation speed is up to 16x faster than Qwen2-VL for equivalent single-image tasks. An RTX 3090 can run the 2.2B at well under 1 second for typical captions (50–100 tokens), and the 500M at well under 0.5 seconds. Community benchmarks show the 2.2B model achieving 1.75 images/second (batch captioning) on an RTX 3090.

**Strengths:** Extremely fast due to token compression, three size tiers, supports video, MIT-compatible license, native Transformers support  
**Weaknesses:** Lower quality than Qwen2.5-VL on complex reasoning; aggressive token compression loses fine detail on small text / dense documents

---

### 3.3 Florence-2 (0.23B base / 0.77B large)

**Repository:** `microsoft/Florence-2-base`, `microsoft/Florence-2-large`, `microsoft/Florence-2-large-ft`  
**License:** MIT

**Overview:**  
Florence-2 is a vision foundation model from Microsoft that uses a unified sequence-to-sequence architecture with task-specific text prompts. Rather than being a conversational VLM, Florence-2 is a structured task model: you pass it a prompt token like `<DETAILED_CAPTION>`, `<OD>` (object detection), `<OCR>`, or `<VQA>` along with the image, and it returns structured output. This makes it extremely fast and deterministic.

**Size and VRAM:**
| Model | Parameters | VRAM (FP16) |
|---|---|---|
| Florence-2-base | 0.23B | < 1 GB |
| Florence-2-large | 0.77B | ~2 GB |

**Inference speed:**  
On an NVIDIA T4 (weaker than RTX 3090), Florence-2 achieves ~1 second per image for captioning. On a 3090 it runs in 0.1–0.3 seconds for most tasks. It is the fastest model in this comparison for structured tasks.

**Supported prompt tokens:**
- `<CAPTION>` / `<DETAILED_CAPTION>` / `<MORE_DETAILED_CAPTION>` — tiered captioning
- `<OD>` — object detection (returns bounding boxes)
- `<DENSE_REGION_CAPTION>` — region-level captions
- `<REFERRING_EXPRESSION_SEGMENTATION>` — segmentation masks
- `<OCR>` / `<OCR_WITH_REGION>` — text extraction
- `<VQA>` — visual question answering (limited, fine-tuned variant recommended)
- `<OPEN_VOCABULARY_DETECTION>` — grounded detection

**Important caveat:** Florence-2's VQA capability requires fine-tuning (`Florence-2-large-ft`) for open-ended questions. For free-form conversational VQA, moondream or Qwen2.5-VL are better choices. Florence-2 excels at captioning and structured vision tasks.

**Strengths:** Fastest, smallest, MIT license, deterministic structured output, OCR, detection  
**Weaknesses:** Not conversational, VQA is limited in base form, less capable for complex scene descriptions

---

### 3.4 Qwen2.5-VL-3B-Instruct

**Repository:** `Qwen/Qwen2.5-VL-3B-Instruct`  
**License:** Apache 2.0  
**Also available as GGUF:** `Mungert/Qwen2.5-VL-3B-Instruct-GGUF`

**Overview:**  
Qwen2.5-VL is Alibaba's third-generation VLM series, released January 2025. The 3B variant provides the best quality-to-size ratio in the sub-4B tier. It supports documents, charts, screenshots, handwriting, and natural scene understanding. Native dynamic resolution means image token count scales with image complexity rather than being fixed.

A key consideration: Qwen2.5-VL uses a dynamic number of image tokens. By default a 1024x1024 image can produce 1000–4000 tokens. This makes it significantly slower than SmolVLM2 on a single-image basis, but the quality is substantially higher. Setting `max_pixels` in the processor limits image tokens and speeds up inference.

**Size and VRAM (FP16):** 3B parameters, ~7–8 GB VRAM  
**Context length:** 32K tokens

**Inference speed:** With default settings, expect 0.5–2.0 seconds per image depending on image resolution and output length on an RTX 3090. With `min_pixels=256*28*28, max_pixels=512*28*28` to cap resolution, expect 0.3–0.8 seconds.

**Strengths:** Best general-purpose capability under 4B, strong on documents and text-in-image, 32K context, GGUF available, Flash Attention 2 support  
**Weaknesses:** Slower than moondream/SmolVLM2 by default due to high token counts; requires `qwen_vl_utils` helper package

---

### 3.5 Qwen3-VL-2B-Instruct

**Repository:** `Qwen/Qwen3-VL-2B-Instruct`  
**Released:** October 2025  
**License:** Apache 2.0

**Overview:**  
The next generation after Qwen2.5-VL, released in October 2025. Comes in 2B, 8B, and 32B sizes. The 2B variant improves over Qwen2.5-VL-3B on reasoning benchmarks while being slightly smaller. Supports vLLM 0.11.0+ for optimized serving. Some community reports note it is slightly slower than Qwen2.5-VL due to architectural changes; the Qwen team recommends using vLLM for production.

**Size and VRAM:** 2B parameters, ~5–6 GB VRAM in BF16  
**Recommended serving:** vLLM 0.11.0+

**Strengths:** Improved reasoning over Qwen2.5-VL-3B, smaller footprint  
**Weaknesses:** Community reports of slightly slower raw inference vs Qwen2.5-VL when not using vLLM; newer so less community documentation

---

### 3.6 InternVL2.5 / InternVL3 (1B / 2B / 4B)

**Repositories:** `OpenGVLab/InternVL2_5-1B`, `OpenGVLab/InternVL2_5-2B`, `OpenGVLab/InternVL3-1B`, `OpenGVLab/InternVL3-2B`, `OpenGVLab/InternVL3-4B`  
**License:** MIT (InternVL2.5), Apache 2.0 (InternVL3)

**Overview:**  
InternVL is a highly-competitive VLM series from OpenGVLab (Shanghai AI Lab). InternVL2.5 was released December 2024; InternVL3 in April 2025. The series consistently tops open-source VLM benchmarks at each size tier. InternVL3 added native transformers integration (`AutoModel` pipeline) and native support in the HuggingFace model hub.

InternVL uses the InternViT-300M or InternViT-6B vision encoder (depending on model size) combined with InternLM or Qwen language models. The small variants (1B, 2B) use InternViT-300M.

**Size and VRAM:**
| Model | Parameters | VRAM (BF16) |
|---|---|---|
| InternVL3-1B | 1B | ~3 GB |
| InternVL3-2B | 2B | ~5 GB |
| InternVL3-4B | 4B | ~9 GB |
| InternVL2.5-2B | 2B | ~5 GB |

**Inference speed:** On RTX 3090 class hardware, InternVL3-2B in BF16 with Flash Attention 2 generates short captions in approximately 0.3–0.8 seconds. The 4B model runs comfortably within 1–1.5 seconds for typical queries.

**Important quantization note:** BNB 4-bit quantization is not recommended for InternVL — it causes significant quality degradation due to the InternViT encoder. Use GPTQ-INT4 or AWQ quantization instead if needed.

**Strengths:** Top benchmarks in class, strong OCR and chart understanding, MIT/Apache license, native Transformers support from InternVL3 onward  
**Weaknesses:** BNB 4-bit not recommended; Flash Attention 2 strongly recommended for speed; trust_remote_code required for InternVL2.5

---

### 3.7 PaliGemma2-3B

**Repository:** `google/paligemma2-3b-mix-224`, `google/paligemma2-3b-pt-224`  
**License:** Gemma license (permissive for research and commercial use with attribution)

**Overview:**  
PaliGemma 2 is Google DeepMind's VLM series based on Gemma 2 language models. The 3B variant combines a SigLIP-So400M vision encoder with a Gemma-2-2B language model. It is available at multiple resolutions (224px, 448px, 896px). PaliGemma was originally designed as a base model for fine-tuning rather than a general-purpose instruction-following assistant.

The `-mix` variants (e.g., `paligemma2-3b-mix-224`) are trained on a mixture of tasks and work reasonably well zero-shot for captioning and VQA. The `-pt` variants are pure pretrained models meant as fine-tuning bases.

**Size and VRAM:** 3B parameters. Requires 4–6 GB VRAM in BF16. The 224px version is fastest.

**Inference speed:** Generally comparable to Qwen2.5-VL-3B for similar tasks. CPU inference is also viable (several seconds), tested in the official paper.

**Strengths:** Clean architecture, strong fine-tuning base, Google-maintained, good benchmark scores  
**Weaknesses:** Less capable zero-shot than Qwen2.5-VL-3B for general VQA; Gemma license requires agreement; designed more for fine-tuning than drop-in use; no GGUF distribution as of early 2026

---

### 3.8 Phi-3.5-Vision (4.2B)

**Repository:** `microsoft/Phi-3.5-vision-instruct`  
**ONNX variant:** `microsoft/Phi-3.5-vision-instruct-onnx`  
**License:** MIT

**Overview:**  
Phi-3.5-Vision is a 4.2B parameter model from Microsoft combining a CLIP-based vision encoder, a connector/projector, and the Phi-3 Mini language model. Released mid-2024, it was one of the first small models to achieve strong performance on document understanding and multi-image reasoning (up to 128K context window).

**Size and VRAM:**
- FP16: ~7.72 GB VRAM (approximately 8–10 GB with KV cache overhead)
- INT4: ~1.93 GB VRAM (very compressed but viable for captioning)

**Inference speed:** On FP16, generates tokens at 60–70 tokens/s on consumer hardware. For a 50-token caption, that is roughly 0.7–1 second. INT4 is faster but with quality tradeoff.

**ONNX Runtime path:** Microsoft officially provides ONNX versions for deployment on CPU, CUDA, and DirectML backends via `microsoft/Phi-3.5-vision-instruct-onnx`. This is the recommended path for cross-platform deployment.

**Strengths:** Long context (128K), multi-image capable, MIT license, ONNX support, good document understanding, strong fine-tuning ecosystem  
**Weaknesses:** Superseded by Phi-4-multimodal in 2025; slightly older architecture; 4.2B pushes right at the 4B cutoff

---

### 3.9 Phi-4-multimodal (5.6B)

**Repository:** `microsoft/Phi-4-multimodal-instruct`  
**License:** MIT

**Overview:**  
Released February 2025, Phi-4-multimodal is Microsoft's successor to Phi-3.5-Vision. It is the first model in this comparison to natively handle speech, vision, and text in a unified architecture. The backbone is Phi-4-Mini-Instruct (a 3.8B language model) plus dedicated vision and speech encoders/adapters.

While slightly above the strict 4B parameter cutoff at 5.6B, it runs comfortably on a 3090 in FP16 using ~8 GB VRAM, leaving ample headroom.

**Size and VRAM:**
| Precision | VRAM |
|---|---|
| FP16 | ~8 GB |
| INT4 (Q4_K_M) | ~2–3 GB |

**Inference speed:** Designed for low-latency on-device execution. On an A100, generates at approximately 70–90 tokens/s for vision tasks. On an RTX 3090, expect similar or marginally lower throughput, putting a 50-token caption at ~0.6–0.8 seconds.

**Strengths:** Audio + vision + text in one model, MIT license, 128K context, strong benchmarks, ONNX available, Phi-4 LLM quality  
**Weaknesses:** Slightly above 4B; flash_attn 2.7.4 required; heavier dependency set; audio encoder overhead even when unused

---

### 3.10 LLaVA-1.5 (7B / 13B) — Legacy

**Repository:** `llava-hf/llava-1.5-7b-hf`, `liuhaotian/llava-v1.5-7b`  
**License:** Apache 2.0 (Vicuna base), LLaMA Community License (Llama base)

**Overview:**  
LLaVA-1.5 (2023) was one of the first widely-adopted open-source VLMs. Based on CLIP + Vicuna/LLaMA, it achieved strong results for its time. LLaVA-1.6 (LLaVA-NeXT) improved upon it in 2024 with higher resolution and better reasoning.

**As of 2025–2026, LLaVA-1.5 is largely superseded.** It remains useful as a baseline and for compatibility with established tooling (Ollama, LM Studio), but newer models achieve better results at smaller sizes.

**Size and VRAM:**
- LLaVA-1.5-7B in FP16: ~14–15 GB VRAM
- LLaVA-1.5-7B with INT4: ~5–6 GB VRAM
- 576 vision tokens per image (fixed)

**Inference speed:** 0.46 seconds per image at ~15 GB VRAM in FP16 (community benchmarks). Approximately 50–70 tokens/s generation speed.

**Why still mentioned:** Very wide ecosystem support (Ollama, LM Studio, llama.cpp GGUF, ComfyUI). If you use Ollama for model management, `ollama run llava` is the easiest single-command deployment. For a 3090, the 7B INT4 GGUF is practical.

**Recommendation:** Use Qwen2.5-VL-3B or InternVL3-2B instead for new projects. Use LLaVA-1.5 only if you need Ollama compatibility or are working with an existing pipeline.

---

### 3.11 Flash-VL 2B

**Paper:** arXiv:2505.09498 (May 2025)  
**License:** Apache 2.0  
**HuggingFace:** `ByteDance-Seed/Flash-VL-2B` (verify current repo name)

**Overview:**  
Flash-VL 2B is a research model from ByteDance explicitly optimized for ultra-low latency and high throughput. It introduces "implicit semantic stitching," a novel image processing technique that balances computational load and quality. Evaluated on 11 standard VLM benchmarks, it achieves state-of-the-art speed/accuracy tradeoffs for the 2B class.

**Size and VRAM:** 2B parameters, similar to moondream (~4–5 GB FP16)

**Inference speed:** Paper claims state-of-the-art results in both speed and accuracy for the class. Designed for resource-constrained real-time deployment.

**Strengths:** Purpose-built for latency, strong benchmarks for size  
**Weaknesses:** Newer, less community documentation than moondream or Qwen; may require checking current HuggingFace availability

---

### 3.12 DeepSeek-VL 1.3B

**Repository:** `deepseek-ai/deepseek-vl-1.3b-chat`  
**License:** DeepSeek License (commercial use permitted with conditions)

**Overview:**  
DeepSeek-VL 1.3B is one of the smallest models with strong scientific and mathematical visual reasoning. Released 2024, it combines a SigLIP encoder with a DeepSeek-LLM 1.3B model. Particularly strong on science, math, and chart understanding for its size.

**Size and VRAM:** 1.3B parameters, ~3 GB VRAM in FP16

**Inference speed:** Very fast given size. Short captions in well under 0.5 seconds on a 3090.

**Strengths:** Tiny, fast, good scientific reasoning, permissive license for a 1.3B model  
**Weaknesses:** Older than 2025 competitors; outperformed by InternVL3-1B and SmolVLM2-500M on general benchmarks; DeepSeek-VL2 (released late 2024) is the updated version

---

### 3.13 MiniCPM-V 2.6 / MiniCPM-o 2.6 (8B) — Honorable Mention

**Repository:** `openbmb/MiniCPM-V-2_6`, `openbmb/MiniCPM-o-2_6`  
**License:** Apache 2.0

**Overview:**  
MiniCPM-V 2.6 is an 8B model (SigLIP-400M + Qwen2-7B) that is frequently cited as "GPT-4V level" for certain benchmarks. MiniCPM-o 2.6 adds real-time audio streaming. Both are above the 4B parameter limit but comfortably fit on a 3090 in INT4 (~5–6 GB) or in FP16 (~16 GB, leaving 8 GB for KV cache).

MiniCPM-V 2.6 produces only 640 tokens for a 1.8-megapixel image (75% fewer than most models), making it faster than its 8B size suggests.

**Strengths:** Excellent quality, very efficient image tokenization, ollama support, GGUF available  
**Weaknesses:** Above 4B parameter target; 8B requires more VRAM in FP16

---

## 4. Comparison Table

| Model | Params | VRAM (FP16) | VRAM (INT4) | RTX 3090 speed (est.) | Captioning | VQA | OCR | License |
|---|---|---|---|---|---|---|---|---|
| moondream2 (4-bit) | 2B | 4 GB | 2.5 GB | ~0.1–0.3s | Excellent | Good | Limited | Apache 2.0 |
| SmolVLM2-2.2B | 2.2B | 5.2 GB | ~2 GB | ~0.2–0.5s | Good | Good | Moderate | Apache 2.0 |
| SmolVLM2-500M | 500M | 1.5 GB | < 1 GB | ~0.1–0.2s | Fair | Fair | Limited | Apache 2.0 |
| Florence-2-large | 0.77B | 2 GB | N/A | ~0.1–0.3s | Excellent | Fair* | Excellent | MIT |
| Florence-2-base | 0.23B | < 1 GB | N/A | < 0.1s | Good | Fair* | Good | MIT |
| Qwen2.5-VL-3B | 3B | 7–8 GB | ~3 GB | ~0.5–1.5s | Excellent | Excellent | Excellent | Apache 2.0 |
| Qwen3-VL-2B | 2B | 5–6 GB | ~2.5 GB | ~0.5–1.5s | Excellent | Excellent | Excellent | Apache 2.0 |
| InternVL3-2B | 2B | 5 GB | Not recommended | ~0.3–0.8s | Excellent | Excellent | Good | Apache 2.0 |
| InternVL3-4B | 4B | 9 GB | ~4 GB (GPTQ) | ~0.5–1.5s | Excellent | Excellent | Excellent | Apache 2.0 |
| PaliGemma2-3B | 3B | 6 GB | N/A | ~0.5–1.5s | Good | Good | Good | Gemma License |
| Phi-3.5-Vision | 4.2B | 8–10 GB | 2 GB | ~0.5–1s | Good | Good | Good | MIT |
| Phi-4-multimodal | 5.6B | 8 GB | 2–3 GB | ~0.5–1s | Excellent | Excellent | Good | MIT |
| LLaVA-1.5-7B | 7B | 14–15 GB | 5–6 GB | ~0.5–1s | Good | Good | Limited | Apache 2.0 |
| DeepSeek-VL 1.3B | 1.3B | 3 GB | N/A | < 0.3s | Fair | Good (sci) | Limited | DeepSeek |
| MiniCPM-V 2.6 | 8B | 16 GB | 5–6 GB | ~0.5–1s | Excellent | Excellent | Excellent | Apache 2.0 |

*Florence-2 VQA requires the `-ft` fine-tuned variant or additional fine-tuning for free-form questions.

---

## 5. Loading Methods and Code Examples

### 5.1 moondream2 — via pip package (fastest path)

```python
pip install moondream pillow
```

```python
import moondream as md
from PIL import Image

# Local GPU inference via Photon engine
# Requires free API key from moondream.ai
model = md.vl(api_key="your_api_key_here", local=True)

image = Image.open("image.jpg")

# Image captioning
caption = model.caption(image)["caption"]
# Short caption (faster)
short_caption = model.caption(image, length="short")["caption"]

# Visual Q&A
answer = model.query(image, "How many people are in the image?")["answer"]

# Object detection
objects = model.detect(image, "person")["objects"]
# Returns list of {"x_min", "y_min", "x_max", "y_max", "label"} dicts

# Point localization
points = model.point(image, "person")["points"]
```

### 5.2 moondream2 — via HuggingFace Transformers (no API key required)

```python
from transformers import AutoModelForCausalLM, AutoTokenizer
from PIL import Image
import torch

model = AutoModelForCausalLM.from_pretrained(
    "vikhyatk/moondream2",
    revision="2025-06-21",      # pin to a specific dated checkpoint
    trust_remote_code=True,
    device_map={"": "cuda"},
    torch_dtype=torch.float16
)
tokenizer = AutoTokenizer.from_pretrained(
    "vikhyatk/moondream2",
    revision="2025-06-21",
    trust_remote_code=True
)

# Optional: compile for ~2x speedup
model = torch.compile(model)

image = Image.open("image.jpg")
enc_image = model.encode_image(image)

# Caption
caption = model.answer_question(enc_image, "Describe this image.", tokenizer)

# VQA
answer = model.answer_question(enc_image, "What color is the car?", tokenizer)
```

Or use the newer API (2025 checkpoints):

```python
caption = model.caption(image, length="normal")["caption"]
answer = model.query(image, "What is in the image?")["answer"]
```

### 5.3 SmolVLM2-2.2B — HuggingFace Transformers

```python
pip install transformers accelerate flash-attn pillow num2words
```

```python
from transformers import AutoProcessor, AutoModelForImageTextToText
from PIL import Image
import torch

model_path = "HuggingFaceTB/SmolVLM2-2.2B-Instruct"
processor = AutoProcessor.from_pretrained(model_path)
model = AutoModelForImageTextToText.from_pretrained(
    model_path,
    torch_dtype=torch.bfloat16,
    _attn_implementation="flash_attention_2"
).to("cuda")

image = Image.open("image.jpg")

messages = [
    {
        "role": "user",
        "content": [
            {"type": "image", "image": image},
            {"type": "text", "text": "Describe this image in detail."}
        ]
    }
]

inputs = processor.apply_chat_template(
    messages,
    add_generation_prompt=True,
    tokenize=True,
    return_tensors="pt"
).to("cuda")

generated_ids = model.generate(**inputs, max_new_tokens=200)
caption = processor.batch_decode(generated_ids, skip_special_tokens=True)[0]
```

### 5.4 Florence-2 — HuggingFace Transformers

```python
from transformers import AutoProcessor, AutoModelForCausalLM
from PIL import Image
import torch

device = "cuda"
dtype = torch.float16

model = AutoModelForCausalLM.from_pretrained(
    "microsoft/Florence-2-large",
    torch_dtype=dtype,
    trust_remote_code=True
).to(device)
processor = AutoProcessor.from_pretrained(
    "microsoft/Florence-2-large",
    trust_remote_code=True
)

def run_florence(task_prompt: str, image: Image.Image, text_input: str = None):
    prompt = task_prompt if text_input is None else task_prompt + text_input
    inputs = processor(text=prompt, images=image, return_tensors="pt").to(device, dtype)
    generated_ids = model.generate(
        input_ids=inputs["input_ids"],
        pixel_values=inputs["pixel_values"],
        max_new_tokens=1024,
        num_beams=3
    )
    generated_text = processor.batch_decode(generated_ids, skip_special_tokens=False)[0]
    return processor.post_process_generation(
        generated_text,
        task=task_prompt,
        image_size=(image.width, image.height)
    )

image = Image.open("image.jpg")

# Detailed caption
result = run_florence("<DETAILED_CAPTION>", image)
# {'<DETAILED_CAPTION>': 'A person standing in front of a mountain...'}

# VQA (requires Florence-2-large-ft for best results)
result = run_florence("<VQA>", image, "What is the person doing?")

# OCR
result = run_florence("<OCR>", image)

# Object detection
result = run_florence("<OD>", image)
```

### 5.5 Qwen2.5-VL-3B-Instruct — HuggingFace Transformers

```python
pip install transformers accelerate qwen-vl-utils pillow
```

```python
from transformers import Qwen2_5_VLForConditionalGeneration, AutoProcessor
from qwen_vl_utils import process_vision_info
from PIL import Image
import torch

model = Qwen2_5_VLForConditionalGeneration.from_pretrained(
    "Qwen/Qwen2.5-VL-3B-Instruct",
    torch_dtype=torch.bfloat16,
    attn_implementation="flash_attention_2",
    device_map="auto"
)
processor = AutoProcessor.from_pretrained(
    "Qwen/Qwen2.5-VL-3B-Instruct",
    # Cap image resolution to limit token count and speed up inference
    min_pixels=256*28*28,
    max_pixels=512*28*28
)

messages = [
    {
        "role": "user",
        "content": [
            {"type": "image", "image": "path/to/image.jpg"},
            {"type": "text", "text": "Describe this image."}
        ]
    }
]

text = processor.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
image_inputs, video_inputs = process_vision_info(messages)
inputs = processor(
    text=[text],
    images=image_inputs,
    videos=video_inputs,
    padding=True,
    return_tensors="pt"
).to("cuda")

generated_ids = model.generate(**inputs, max_new_tokens=256)
generated_ids_trimmed = [
    out_ids[len(in_ids):] for in_ids, out_ids in zip(inputs.input_ids, generated_ids)
]
output = processor.batch_decode(generated_ids_trimmed, skip_special_tokens=True)[0]
```

### 5.6 InternVL3 — HuggingFace Transformers

```python
pip install transformers accelerate flash-attn pillow
```

```python
import torch
from transformers import AutoTokenizer, AutoModel
from PIL import Image
import torchvision.transforms as T
from torchvision.transforms.functional import InterpolationMode

IMAGENET_MEAN = (0.485, 0.456, 0.406)
IMAGENET_STD = (0.229, 0.224, 0.225)

def build_transform(input_size):
    return T.Compose([
        T.Lambda(lambda img: img.convert('RGB') if img.mode != 'RGB' else img),
        T.Resize((input_size, input_size), interpolation=InterpolationMode.BICUBIC),
        T.ToTensor(),
        T.Normalize(mean=IMAGENET_MEAN, std=IMAGENET_STD)
    ])

path = "OpenGVLab/InternVL3-2B"
model = AutoModel.from_pretrained(
    path,
    torch_dtype=torch.bfloat16,
    low_cpu_mem_usage=True,
    use_flash_attn=True,
    trust_remote_code=True
).eval().cuda()
tokenizer = AutoTokenizer.from_pretrained(path, trust_remote_code=True, use_fast=False)

# Load and preprocess image
transform = build_transform(448)
image = Image.open("image.jpg")
pixel_values = transform(image).unsqueeze(0).to(torch.bfloat16).cuda()

generation_config = dict(max_new_tokens=256, do_sample=False)

# Single-turn VQA
response = model.chat(tokenizer, pixel_values, "Describe this image in detail.", generation_config)

# Multi-turn conversation
history = []
response, history = model.chat(
    tokenizer, pixel_values, "What is in the image?", generation_config, history=history, return_history=True
)
response2, history = model.chat(
    tokenizer, pixel_values, "What color is the main object?", generation_config, history=history, return_history=True
)
```

Alternatively, with native HuggingFace pipeline (InternVL3 only):

```python
from transformers import pipeline

pipe = pipeline("image-text-to-text", model="OpenGVLab/InternVL3-1B-hf")
result = pipe(
    {"image": "image.jpg", "text": "Describe this image."},
    max_new_tokens=200
)
```

### 5.7 PaliGemma2-3B — HuggingFace Transformers

```python
from transformers import AutoProcessor, PaliGemmaForConditionalGeneration
from PIL import Image
import torch

model_id = "google/paligemma2-3b-mix-224"
model = PaliGemmaForConditionalGeneration.from_pretrained(
    model_id,
    torch_dtype=torch.bfloat16,
    device_map="auto"
).eval()
processor = AutoProcessor.from_pretrained(model_id)

image = Image.open("image.jpg")
prompt = "Describe this image."  # or a VQA prompt like "What is the person doing?"

inputs = processor(text=prompt, images=image, return_tensors="pt").to("cuda", torch.bfloat16)

with torch.inference_mode():
    generated_ids = model.generate(**inputs, max_new_tokens=200, do_sample=False)

result = processor.decode(generated_ids[0], skip_special_tokens=True)
# Strip the input prompt from the output
result = result[len(prompt):].strip()
```

### 5.8 Phi-3.5-Vision — HuggingFace Transformers

```python
from transformers import AutoModelForCausalLM, AutoProcessor
from PIL import Image
import torch

model_id = "microsoft/Phi-3.5-vision-instruct"
model = AutoModelForCausalLM.from_pretrained(
    model_id,
    device_map="cuda",
    trust_remote_code=True,
    torch_dtype="auto",
    _attn_implementation="flash_attention_2"
)
processor = AutoProcessor.from_pretrained(model_id, trust_remote_code=True, num_crops=4)

image = Image.open("image.jpg")

messages = [
    {"role": "user", "content": "<|image_1|>\nDescribe this image in detail."}
]
prompt = processor.tokenizer.apply_chat_template(
    messages, tokenize=False, add_generation_prompt=True
)
inputs = processor(prompt, [image], return_tensors="pt").to("cuda:0")

generation_args = {
    "max_new_tokens": 500,
    "temperature": 0.0,
    "do_sample": False
}
generate_ids = model.generate(**inputs, eos_token_id=processor.tokenizer.eos_token_id, **generation_args)
generate_ids = generate_ids[:, inputs["input_ids"].shape[1]:]
response = processor.batch_decode(generate_ids, skip_special_tokens=True)[0]
```

### 5.9 Phi-4-multimodal — HuggingFace Transformers

```python
pip install transformers>=4.48.2 accelerate flash-attn==2.7.4.post1 pillow
```

```python
from transformers import AutoModelForCausalLM, AutoProcessor
from PIL import Image
import torch

model_id = "microsoft/Phi-4-multimodal-instruct"
model = AutoModelForCausalLM.from_pretrained(
    model_id,
    device_map="cuda",
    torch_dtype=torch.float16,
    trust_remote_code=True,
    _attn_implementation="flash_attention_2"
)
processor = AutoProcessor.from_pretrained(model_id, trust_remote_code=True)

image = Image.open("image.jpg")

# Phi-4 uses a special image placeholder tag
prompt = "<|image_1|>\nDescribe this image in detail."
messages = [{"role": "user", "content": prompt}]
text = processor.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)

inputs = processor(text=text, images=[image], return_tensors="pt").to("cuda")

with torch.no_grad():
    generated_ids = model.generate(**inputs, max_new_tokens=500, do_sample=False)

generated_text = processor.batch_decode(
    generated_ids[:, inputs["input_ids"].shape[1]:],
    skip_special_tokens=True
)[0]
```

### 5.10 GGUF via llama.cpp (any compatible model)

For maximum CPU-offload flexibility or pure CPU inference, use llama.cpp:

```bash
# Install llama-cpp-python with CUDA support
CMAKE_ARGS="-DGGML_CUDA=on" pip install llama-cpp-python

# Example: moondream via llama.cpp
python -c "
from llama_cpp import Llama
from llama_cpp.llama_chat_format import MoondreamChatHandler

chat_handler = MoondreamChatHandler.from_pretrained(
    repo_id='vikhyatk/moondream2',
    filename='moondream2-text-model-f16.gguf',
)

llm = Llama(
    model_path=None,
    chat_handler=chat_handler,
    n_ctx=2048,
    n_gpu_layers=-1  # offload all layers to GPU
)

result = llm.create_chat_completion(
    messages=[{
        'role': 'user',
        'content': [
            {'type': 'image_url', 'image_url': {'url': 'data:image/jpeg;base64,...'}},
            {'type': 'text', 'text': 'Describe this image.'}
        ]
    }]
)
"
```

For Qwen2.5-VL-3B GGUF (e.g., from `Mungert/Qwen2.5-VL-3B-Instruct-GGUF`), use llama.cpp's `llava_chat_format`.

---

## 6. Best Practices for RTX 3090 Inference

### 6.1 Always use Flash Attention 2

The RTX 3090 (Ampere) fully supports Flash Attention 2. Pass `attn_implementation="flash_attention_2"` or `use_flash_attn=True` when loading models. This reduces both memory and latency, especially for longer sequences.

### 6.2 Use BF16 or FP16 — not FP32

FP32 doubles VRAM usage with no benefit for inference. Use `torch_dtype=torch.bfloat16` (preferred on Ampere) or `torch.float16`.

### 6.3 Encode images once, query many times

If you need to ask multiple questions about the same image, encode it once and reuse the encoded representation:

```python
# moondream2
enc_image = model.encode_image(image)
a1 = model.answer_question(enc_image, "Question 1?", tokenizer)
a2 = model.answer_question(enc_image, "Question 2?", tokenizer)
```

### 6.4 Use torch.compile() for repeated inference

```python
model = torch.compile(model)  # ~2x speedup after warmup
```

Warmup with one dummy call after compilation before timing. PyTorch 2.x compile works best with static shapes.

### 6.5 Cap image resolution / token count

For Qwen2.5-VL, set `max_pixels` in the processor to limit the number of image tokens:

```python
processor = AutoProcessor.from_pretrained(
    "Qwen/Qwen2.5-VL-3B-Instruct",
    max_pixels=512 * 28 * 28  # ~400K pixels max
)
```

### 6.6 Use max_new_tokens appropriately

Set `max_new_tokens` to a realistic value for your use case. Generating 500 tokens when you need 50 wastes time. Short captions need 30–80 tokens; detailed descriptions need 100–300.

### 6.7 Quantization strategy by model

| Model | Recommended quantization |
|---|---|
| moondream2 | 4-bit native via pip / HF repo |
| SmolVLM2 | BF16 (small enough; INT4 optional) |
| Florence-2 | FP16 (already very small) |
| Qwen2.5-VL-3B | BF16 or AWQ INT4 |
| InternVL3 | BF16 or GPTQ INT4 (not BNB INT4) |
| Phi-3.5-Vision | INT4 GGUF for llama.cpp |
| Phi-4-multimodal | FP16 (8 GB easily fits 3090) |

### 6.8 For the production use case in this repo

Given the existing `vision_server.py` in this project, a practical loading pattern is to load the model once at server startup and reuse it per request. The moondream pip package handles this automatically. For transformers-based models:

```python
# At startup
model.eval()
if hasattr(torch, 'compile'):
    model = torch.compile(model, mode="reduce-overhead")

# Per request — avoid .to() calls, image stays on CPU until processor
with torch.no_grad():
    with torch.cuda.amp.autocast(dtype=torch.bfloat16):
        result = model.generate(...)
```

---

## 7. Recommendation Matrix

| Use case | Recommended model | Reason |
|---|---|---|
| Fastest possible captioning, any quality | Florence-2-large (FP16) | Structured task, 0.1–0.3s, MIT |
| Fast captioning with natural language output | moondream2 (4-bit) + Photon | ~0.1–0.3s, excellent API |
| Best quality captioning/VQA under 4B | Qwen2.5-VL-3B or InternVL3-2B | Top benchmarks |
| Lowest VRAM footprint (< 2 GB) | SmolVLM2-500M or Florence-2-base | Both fit in < 1.5 GB |
| Multi-image or video understanding | SmolVLM2-2.2B or Qwen2.5-VL-3B | Both support multiple images |
| OCR / document / text in image | Florence-2-large or Qwen2.5-VL-3B | Best text-in-image handling |
| Drop-in replacement for LLaVA in existing pipeline | InternVL3-2B or Qwen2.5-VL-3B | Same transformer API |
| Audio + vision in one model | Phi-4-multimodal (5.6B) | Only option in this class |
| Maximum quality with 3090 headroom | MiniCPM-V 2.6 INT4 (8B) | ~5.5 GB INT4, GPT-4V class |
| llama.cpp / Ollama / GGUF workflow | LLaVA-1.5-7B or Qwen2.5-VL-3B GGUF | Wide GGUF availability |

**Top 3 picks for this project (RTX 3090, fast VQA + captioning, Python):**

1. **moondream2** — Best for speed-critical pipelines. Use the pip package with Photon for < 0.3s latency. Zero VRAM overhead on a 3090.
2. **Qwen2.5-VL-3B-Instruct** — Best for quality. With Flash Attention 2 and max_pixels cap, fits in 1.5 seconds for most images. Excellent document and fine-detail understanding.
3. **SmolVLM2-2.2B** — Best balance of speed and quality. 5.2 GB VRAM, Flash Attention 2, very fast due to aggressive image token compression.

---

## 8. Common Pitfalls

**1. Not pinning a model revision**  
Moondream releases frequent checkpoint updates. Use `revision="2025-06-21"` or similar to avoid breaking changes in production.

**2. Forgetting trust_remote_code=True**  
InternVL2.5, moondream2 (transformers path), Phi-3.5-Vision all require this flag. HuggingFace may prompt you to confirm.

**3. Using BNB INT4 with InternVL**  
The InternViT encoder in InternVL2.5/3 degrades badly with BitsAndBytes 4-bit quantization. Use GPTQ or AWQ instead, or load in BF16.

**4. High image token count with Qwen2.5-VL**  
Without capping `max_pixels`, a single high-resolution image can generate 4,000+ tokens, causing multi-second latency even on a 3090. Always set `max_pixels` for speed-critical applications.

**5. Forgetting warmup after torch.compile()**  
The first call after `torch.compile()` triggers JIT compilation and will be slow (5–30 seconds). Always run a warmup pass before starting timed inference.

**6. Flash Attention 2 installation**  
`flash-attn` must be compiled against your exact CUDA and PyTorch version. Install with:  
`pip install flash-attn --no-build-isolation`  
Or use pre-built wheels from the flash-attention releases page.

**7. Moondream API key for local use**  
The moondream pip package requires a free API key even when running fully locally (`local=True`). The key is used for authentication against the Photon engine, not for data transmission. Register at moondream.ai.

**8. PaliGemma2 requires license agreement**  
Must accept the Gemma license on HuggingFace before download. Use `huggingface-cli login` and ensure terms are accepted on the model page.

**9. LLaVA-1.5 using wrong HuggingFace ID**  
The HF-native version is `llava-hf/llava-1.5-7b-hf` (uses standard AutoModel). The original version from `liuhaotian/llava-v1.5-7b` requires the LLaVA codebase installed separately.

**10. Phi-4-multimodal requiring exact flash-attn version**  
Phi-4-multimodal requires `flash_attn==2.7.4.post1` specifically. Newer versions may not be compatible without additional testing.

---

## 9. Resources and Further Reading

### Official Model Pages

- [vikhyatk/moondream2 on HuggingFace](https://huggingface.co/vikhyatk/moondream2)
- [Moondream documentation](https://docs.moondream.ai/)
- [Moondream Photon blog post](https://moondream.ai/blog/photon-real-time-vision-ai-is-finally-here)
- [HuggingFaceTB/SmolVLM2-2.2B-Instruct](https://huggingface.co/HuggingFaceTB/SmolVLM2-2.2B-Instruct)
- [SmolVLM HuggingFace blog post](https://huggingface.co/blog/smolvlm)
- [SmolVLM paper (arXiv:2504.05299)](https://arxiv.org/abs/2504.05299)
- [microsoft/Florence-2-large](https://huggingface.co/microsoft/Florence-2-large)
- [Qwen/Qwen2.5-VL-3B-Instruct](https://huggingface.co/Qwen/Qwen2.5-VL-3B-Instruct)
- [Qwen3-VL GitHub](https://github.com/QwenLM/Qwen3-VL)
- [OpenGVLab/InternVL3-2B](https://huggingface.co/OpenGVLab/InternVL3-2B)
- [InternVL3 blog post](https://internvl.github.io/blog/2025-04-11-InternVL-3.0/)
- [google/paligemma2-3b-mix-224](https://huggingface.co/google/paligemma2-3b-mix-224)
- [PaliGemma2 HuggingFace blog post](https://huggingface.co/blog/paligemma2)
- [microsoft/Phi-3.5-vision-instruct](https://huggingface.co/microsoft/Phi-3.5-vision-instruct)
- [microsoft/Phi-4-multimodal-instruct](https://huggingface.co/microsoft/Phi-4-multimodal-instruct)
- [Flash-VL 2B paper (arXiv:2505.09498)](https://arxiv.org/abs/2505.09498)

### Benchmarks and Comparisons

- [HuggingFace VLMs-2025 overview blog post](https://huggingface.co/blog/vlms-2025)
- [Clarifai: Benchmarking Open-Source VLMs — Gemma 3 vs MiniCPM vs Qwen2.5-VL](https://www.clarifai.com/blog/benchmarking-best-open-source-vision-language-models)
- [Roboflow: Best Local Vision-Language Models for Offline AI](https://blog.roboflow.com/local-vision-language-models/)
- [InternVL2.5 benchmarks](https://internvl.github.io/blog/2024-12-05-InternVL-2.5/)
- [Trelis Research: Top Vision Models 2025](https://trelis.substack.com/p/top-vision-models-2025)

### GPU and VRAM References

- [Moondream 2025-03-27 release — RTX 3090 benchmarks](https://moondream.ai/blog/moondream-2025-03-27-release)
- [Hardware Corner: Definitive GPU Ranking for LLMs](https://www.hardware-corner.net/gpu-ranking-local-llm/)

### Loading and Deployment Guides

- [Moondream Transformers docs](https://docs.moondream.ai/advanced/transformers/)
- [Qwen2.5-VL Transformers documentation](https://huggingface.co/docs/transformers/en/model_doc/qwen2_5_vl)
- [InternVL3 Quick Start](https://internvl.readthedocs.io/en/latest/internvl3.0/quick_start.html)
- [SmolVLM2 inference guide — DebuggerCafe](https://debuggercafe.com/getting-started-with-smolvlm2-code-inference/)
- [HuggingFace fine-tuning Florence-2 blog](https://huggingface.co/blog/finetune-florence2)
- [Red Hat: 3.5x faster VLMs with quantization](https://developers.redhat.com/articles/2025/04/01/enable-faster-vision-language-models-quantization)

---

*Research compiled April 2026. Model weights, API availability, and benchmark standings change frequently. Always verify against the current HuggingFace model cards and official documentation before deployment.*
