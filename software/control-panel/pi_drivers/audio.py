"""
Audio driver for Raspberry Pi with INMP441 I2S microphone and PWM audio output.

Recording:  INMP441 I2S MEMS microphone via ALSA capture device.
Playback:   bcm2835 headphones (PWM on GPIO 12/13 via audremap overlay)
            amplified by SC8002B.

Audio format (matches the rest of the system):
    - 16000 Hz sample rate
    - 16-bit signed PCM (2 bytes per sample)
    - Mono (1 channel)

Requires: pyaudio, numpy
"""

import time
import threading
import numpy as np

try:
    import pyaudio
except ImportError:
    pyaudio = None

# ---------------------------------------------------------------------------
# Audio constants — must match the rest of the Octavius system
# ---------------------------------------------------------------------------
SAMPLE_RATE = 16000
SAMPLE_WIDTH = 2       # bytes (16-bit)
CHANNELS = 1
FORMAT = None          # set at runtime once pyaudio is available (paInt16)
FRAMES_PER_BUFFER = 1024


def _find_device_index(pa: "pyaudio.PyAudio", target_name: str,
                       is_input: bool) -> int | None:
    """Return the ALSA device index whose name contains *target_name*."""
    for i in range(pa.get_device_count()):
        info = pa.get_device_info_by_index(i)
        channels_key = "maxInputChannels" if is_input else "maxOutputChannels"
        if info.get(channels_key, 0) > 0 and target_name.lower() in info.get("name", "").lower():
            return i
    return None


def _find_capture_device(pa: "pyaudio.PyAudio") -> int | None:
    """Locate the INMP441 I2S capture device.

    The device may appear under various names depending on the overlay
    configuration ("snd_rpi_simple_card", "i2s", "inmp441", "googlevoicehat",
    etc.).  We try several known names, then fall back to the ALSA default
    capture device.
    """
    candidate_names = [
        "inmp441",
        "i2s",
        "simple-card",
        "snd_rpi_simple",
        "googlevoicehat",
    ]
    for name in candidate_names:
        idx = _find_device_index(pa, name, is_input=True)
        if idx is not None:
            return idx

    # Fall back: first device with input channels
    for i in range(pa.get_device_count()):
        info = pa.get_device_info_by_index(i)
        if info.get("maxInputChannels", 0) > 0:
            return i
    return None


def _find_playback_device(pa: "pyaudio.PyAudio") -> int | None:
    """Locate the bcm2835 PWM audio playback device (card 0).

    Tries "bcm2835" first, then "headphones", then falls back to the first
    output device available.
    """
    candidate_names = [
        "bcm2835",
        "headphones",
    ]
    for name in candidate_names:
        idx = _find_device_index(pa, name, is_input=False)
        if idx is not None:
            return idx

    for i in range(pa.get_device_count()):
        info = pa.get_device_info_by_index(i)
        if info.get("maxOutputChannels", 0) > 0:
            return i
    return None


class AudioDriver:
    """Record from the INMP441 I2S mic and play back through ALSA PWM output."""

    def __init__(self):
        if pyaudio is None:
            raise RuntimeError("pyaudio is not installed — run: pip install pyaudio")

        self._pa = pyaudio.PyAudio()
        self._format = pyaudio.paInt16

        self._capture_idx = _find_capture_device(self._pa)
        self._playback_idx = _find_playback_device(self._pa)

        if self._capture_idx is not None:
            name = self._pa.get_device_info_by_index(self._capture_idx).get("name", "?")
            print(f"[audio] capture device #{self._capture_idx}: {name}")
        else:
            print("[audio] WARNING: no capture device found")

        if self._playback_idx is not None:
            name = self._pa.get_device_info_by_index(self._playback_idx).get("name", "?")
            print(f"[audio] playback device #{self._playback_idx}: {name}")
        else:
            print("[audio] WARNING: no playback device found")

        # Threading controls
        self._stop_event = threading.Event()
        self._lock = threading.Lock()

    # ------------------------------------------------------------------
    # Recording
    # ------------------------------------------------------------------

    def record_speech(
        self,
        silence_duration: float = 0.6,
        min_speech_duration: float = 0.3,
        noise_multiplier: float = 3.0,
        max_threshold: float = 2000.0,
        max_duration: float = 10.0,
        no_speech_timeout: float = 4.0,
        conversation_mode: bool = False,
    ) -> bytes | None:
        """Record speech from the I2S microphone until silence is detected.

        Returns raw 16-bit PCM bytes, or ``None`` if the microphone is
        unavailable or no speech was detected within *no_speech_timeout*
        seconds.
        """
        if self._capture_idx is None:
            print("[audio] no capture device — skipping recording")
            return None

        self._stop_event.clear()

        try:
            stream = self._pa.open(
                format=self._format,
                channels=CHANNELS,
                rate=SAMPLE_RATE,
                input=True,
                input_device_index=self._capture_idx,
                frames_per_buffer=FRAMES_PER_BUFFER,
            )
        except Exception as exc:
            print(f"[audio] failed to open capture stream: {exc}")
            return None

        try:
            return self._record_loop(
                stream,
                silence_duration=silence_duration,
                min_speech_duration=min_speech_duration,
                noise_multiplier=noise_multiplier,
                max_threshold=max_threshold,
                max_duration=max_duration,
                no_speech_timeout=no_speech_timeout,
                conversation_mode=conversation_mode,
            )
        finally:
            try:
                stream.stop_stream()
                stream.close()
            except Exception:
                pass

    def _record_loop(
        self,
        stream,
        *,
        silence_duration: float,
        min_speech_duration: float,
        noise_multiplier: float,
        max_threshold: float,
        max_duration: float,
        no_speech_timeout: float,
        conversation_mode: bool,
    ) -> bytes | None:
        # --- 1. Calibrate ambient noise level ---
        cal_duration = 0.25 if conversation_mode else 0.5
        cal_chunks: list[bytes] = []
        cal_end = time.time() + cal_duration

        while time.time() < cal_end and not self._stop_event.is_set():
            try:
                chunk = stream.read(FRAMES_PER_BUFFER, exception_on_overflow=False)
                cal_chunks.append(chunk)
            except Exception:
                break

        if cal_chunks:
            cal_samples = np.frombuffer(b"".join(cal_chunks), dtype=np.int16).astype(np.float32)
            ambient_rms = float(np.sqrt(np.mean(cal_samples ** 2)))
        else:
            ambient_rms = 200.0

        threshold = min(max(ambient_rms * noise_multiplier, 300.0), max_threshold)
        mode_tag = " [conv]" if conversation_mode else ""
        print(f"[audio] listening...{mode_tag} (ambient={ambient_rms:.0f}, threshold={threshold:.0f})")

        # --- 2. Record until silence after speech ---
        audio_data = bytearray(b"".join(cal_chunks))
        silence_start: float | None = None
        has_speech = False
        start_time = time.time()

        while not self._stop_event.is_set():
            try:
                chunk = stream.read(FRAMES_PER_BUFFER, exception_on_overflow=False)
            except Exception:
                break

            audio_data.extend(chunk)

            samples = np.frombuffer(chunk, dtype=np.int16)
            if len(samples) > 0:
                rms = float(np.sqrt(np.mean(samples.astype(np.float32) ** 2)))

                if rms > threshold:
                    has_speech = True
                    silence_start = None
                elif has_speech:
                    if silence_start is None:
                        silence_start = time.time()
                    elif time.time() - silence_start > silence_duration:
                        break

            elapsed = time.time() - start_time
            if elapsed > max_duration:
                break
            if not has_speech and elapsed > no_speech_timeout:
                break

        if self._stop_event.is_set():
            print("[audio] recording interrupted")
            return None

        # --- 3. Trim trailing silence ---
        samples_all = np.frombuffer(audio_data, dtype=np.int16)
        chunk_size = SAMPLE_RATE // 10  # 100 ms chunks
        trim_idx = len(samples_all)
        for i in range(len(samples_all) - chunk_size, 0, -chunk_size):
            rms = float(np.sqrt(np.mean(samples_all[i:i + chunk_size].astype(np.float32) ** 2)))
            if rms > threshold:
                trim_idx = min(i + chunk_size * 2, len(samples_all))
                break
        audio_data = samples_all[:trim_idx].tobytes()

        duration = len(audio_data) / (SAMPLE_RATE * SAMPLE_WIDTH)
        print(f"[audio] recorded {duration:.1f}s")

        if duration < min_speech_duration or not has_speech:
            return None

        return bytes(audio_data)

    # ------------------------------------------------------------------
    # Playback
    # ------------------------------------------------------------------

    def play_audio(self, audio_bytes: bytes) -> None:
        """Play raw 16-bit PCM mono audio through the ALSA playback device.

        Blocks until playback is complete.
        """
        if not audio_bytes:
            return

        if self._playback_idx is None:
            print("[audio] no playback device — skipping playback")
            return

        try:
            stream = self._pa.open(
                format=self._format,
                channels=CHANNELS,
                rate=SAMPLE_RATE,
                output=True,
                output_device_index=self._playback_idx,
                frames_per_buffer=FRAMES_PER_BUFFER,
            )
        except Exception as exc:
            print(f"[audio] failed to open playback stream: {exc}")
            return

        try:
            # Write audio in chunks
            offset = 0
            chunk_bytes = FRAMES_PER_BUFFER * SAMPLE_WIDTH * CHANNELS
            while offset < len(audio_bytes):
                end = min(offset + chunk_bytes, len(audio_bytes))
                chunk = audio_bytes[offset:end]
                # Pad final chunk to a full frame if needed
                if len(chunk) < chunk_bytes:
                    chunk = chunk + b"\x00" * (chunk_bytes - len(chunk))
                stream.write(chunk)
                offset = end
        except Exception as exc:
            print(f"[audio] playback error: {exc}")
        finally:
            try:
                stream.stop_stream()
                stream.close()
            except Exception:
                pass

    # ------------------------------------------------------------------
    # Control
    # ------------------------------------------------------------------

    def stop_recording(self) -> None:
        """Signal any in-progress recording to stop immediately."""
        self._stop_event.set()

    def cleanup(self) -> None:
        """Release all PyAudio resources."""
        self._stop_event.set()
        try:
            self._pa.terminate()
        except Exception:
            pass
