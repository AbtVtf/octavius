"""
Unified hardware interface for Octavius running directly on Raspberry Pi.
Replaces the TCP-based Watcher connection with local GPIO drivers.
"""

import time
import threading


class PiHardware:
    """Drop-in replacement for the Watcher TCP connection.

    Provides the same interface as SesameAssistant's hardware methods:
    - send_face(face)
    - send_command(cmd)
    - record_speech() -> bytes | None
    - play_audio(audio_bytes)
    """

    def __init__(self):
        self._servo = None
        self._display = None
        self._audio = None
        self._init_errors = []

        # Initialize servo driver
        try:
            from pi_drivers.servo import ServoDriver
            self._servo = ServoDriver()
            print("[PiHW] Servo driver ready")
        except Exception as e:
            self._init_errors.append(f"Servo: {e}")
            print(f"[PiHW] Servo init failed: {e}")

        # Initialize display driver
        try:
            from pi_drivers.display import RoundDisplay
            self._display = RoundDisplay()
            self._display.set_face("idle")
            print("[PiHW] Display driver ready")
        except Exception as e:
            self._init_errors.append(f"Display: {e}")
            print(f"[PiHW] Display init failed: {e}")

        # Initialize audio driver
        try:
            from pi_drivers.audio import AudioDriver
            self._audio = AudioDriver()
            print("[PiHW] Audio driver ready")
        except Exception as e:
            self._init_errors.append(f"Audio: {e}")
            print(f"[PiHW] Audio init failed: {e}")

        if self._init_errors:
            print(f"[PiHW] Warnings: {'; '.join(self._init_errors)}")
        else:
            print("[PiHW] All hardware initialized OK")

    def send_face(self, face):
        """Set face expression on the round LCD."""
        if self._display:
            try:
                self._display.set_face(face)
                print(f"  [face: {face}]")
            except Exception as e:
                print(f"  [face FAILED: {e}]")
        else:
            print(f"  [face: {face}] (no display)")

    def send_command(self, cmd):
        """Send movement command to servos via PCA9685."""
        if self._servo:
            try:
                self._servo.handle_command(cmd)
                print(f"  [cmd: {cmd}]")
            except Exception as e:
                print(f"  [cmd FAILED: {e}]")
        else:
            print(f"  [cmd: {cmd}] (no servo driver)")

    def record_speech(self, conversation_mode=False):
        """Record from I2S microphone until silence detected."""
        if self._audio:
            try:
                return self._audio.record_speech(
                    conversation_mode=conversation_mode
                )
            except Exception as e:
                print(f"  [record FAILED: {e}]")
                return None
        else:
            print("  [record: no audio driver]")
            return None

    def play_audio(self, audio_bytes):
        """Play audio through PWM output to SC8002B amp."""
        if self._audio:
            try:
                self._audio.play_audio(audio_bytes)
            except Exception as e:
                print(f"  [play FAILED: {e}]")
        else:
            print("  [play: no audio driver]")

    def stop_recording(self):
        """Interrupt ongoing recording."""
        if self._audio:
            self._audio.stop_recording()

    def stop_movement(self):
        """Stop any running animation."""
        if self._servo:
            self._servo.stop()

    def cleanup(self):
        """Release all hardware resources."""
        if self._servo:
            try:
                self._servo.cleanup()
            except:
                pass
        if self._display:
            try:
                self._display.cleanup()
            except:
                pass
        if self._audio:
            try:
                self._audio.cleanup()
            except:
                pass
        print("[PiHW] Cleanup done")


def is_running_on_pi():
    """Check if we're running on a Raspberry Pi."""
    try:
        with open("/proc/device-tree/model") as f:
            model = f.read()
        return "raspberry pi" in model.lower()
    except:
        return False
