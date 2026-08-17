"""
Servo driver for Octavius robot using PCA9685 I2C PWM board on Raspberry Pi.

Controls 8 servos mapped to spider-robot legs. Ports all animations from the
original Arduino Nano code.

Dependencies:
    pip install adafruit-circuitpython-pca9685 adafruit-circuitpython-motor
"""

import time
import threading
import logging
from typing import Optional, Dict, List

try:
    import board
    import busio
    from adafruit_pca9685 import PCA9685
    from adafruit_motor import servo as adafruit_servo

    HW_AVAILABLE = True
except ImportError:
    HW_AVAILABLE = False

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Servo channel mapping (PCA9685 channel -> name)
# ---------------------------------------------------------------------------
CHANNEL_MAP: Dict[str, int] = {
    "L1": 0,   # Left leg 1, front
    "L4": 1,   # Left leg 4, back
    "R2": 2,   # Right leg 2, front
    "R4": 3,   # Right leg 4, back
    "R3": 4,   # Right leg 3, back
    "L3": 5,   # Left leg 3, back
    "R1": 6,   # Right leg 1, front
    "L2": 7,   # Left leg 2, front
}

# Order used for bulk commands — matches Arduino pin order
BULK_ORDER = ["L1", "L4", "R2", "R4", "R3", "L3", "R1", "L2"]

# Servo pulse range (microseconds) for 0-180 degrees
SERVO_MIN_PULSE = 500    # us at 0 degrees
SERVO_MAX_PULSE = 2500   # us at 180 degrees

# PCA9685 runs at 50 Hz for standard servos
PWM_FREQUENCY = 50

# Auto-detach timeout (seconds)
AUTO_DETACH_TIMEOUT = 2.0


class ServoDriver:
    """Driver for 8 servos on a PCA9685 board."""

    def __init__(self, i2c_address: int = 0x40):
        self._address = i2c_address
        self._pca: Optional[PCA9685] = None
        self._servos: Dict[str, adafruit_servo.Servo] = {}  # type: ignore[name-defined]
        self._current_angles: Dict[str, float] = {name: 90.0 for name in CHANNEL_MAP}

        # Animation control
        self._anim_thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        self._lock = threading.Lock()

        # Auto-detach
        self._detach_timer: Optional[threading.Timer] = None
        self._detached = True

        self._init_hardware()

    # ------------------------------------------------------------------
    # Hardware initialisation
    # ------------------------------------------------------------------
    def _init_hardware(self):
        if not HW_AVAILABLE:
            logger.warning("Adafruit libraries not available — running in simulation mode")
            return

        try:
            i2c = busio.I2C(board.SCL, board.SDA)
            self._pca = PCA9685(i2c, address=self._address)
            self._pca.frequency = PWM_FREQUENCY

            for name, channel in CHANNEL_MAP.items():
                s = adafruit_servo.Servo(
                    self._pca.channels[channel],
                    min_pulse=SERVO_MIN_PULSE,
                    max_pulse=SERVO_MAX_PULSE,
                    actuation_range=180,
                )
                self._servos[name] = s

            self._detached = False
            logger.info("PCA9685 initialised at 0x%02X with %d servos", self._address, len(self._servos))
        except Exception:
            logger.exception("Failed to initialise PCA9685 hardware")
            self._pca = None

    # ------------------------------------------------------------------
    # Low-level servo helpers
    # ------------------------------------------------------------------
    def _set_angle(self, name: str, angle: float):
        """Set a single servo to an angle (0-180). NOT thread-safe — caller must hold _lock."""
        angle = max(0.0, min(180.0, angle))
        self._current_angles[name] = angle

        if name in self._servos:
            try:
                self._servos[name].angle = angle
            except Exception:
                logger.exception("Error setting %s to %.1f", name, angle)
        else:
            logger.debug("SIM %s -> %.1f", name, angle)

        self._reset_detach_timer()

    def _set_angles(self, angles: Dict[str, float]):
        """Set multiple servos at once."""
        with self._lock:
            for name, angle in angles.items():
                self._set_angle(name, angle)

    def _set_all(self, angle: float):
        """Set every servo to the same angle."""
        self._set_angles({name: angle for name in CHANNEL_MAP})

    def _delay(self, ms: int):
        """Sleep for *ms* milliseconds, checking the stop event every 10 ms."""
        remaining = ms / 1000.0
        while remaining > 0 and not self._stop_event.is_set():
            chunk = min(remaining, 0.01)
            time.sleep(chunk)
            remaining -= chunk

    def _stopped(self) -> bool:
        return self._stop_event.is_set()

    # ------------------------------------------------------------------
    # Auto-detach (stop PWM signals after inactivity)
    # ------------------------------------------------------------------
    def _reset_detach_timer(self):
        if self._detach_timer is not None:
            self._detach_timer.cancel()
        self._detach_timer = threading.Timer(AUTO_DETACH_TIMEOUT, self._detach_servos)
        self._detach_timer.daemon = True
        self._detach_timer.start()

    def _detach_servos(self):
        """Turn off PWM on all channels (servos go limp)."""
        with self._lock:
            if self._pca is not None:
                for name, channel in CHANNEL_MAP.items():
                    try:
                        self._pca.channels[channel].duty_cycle = 0
                    except Exception:
                        pass
            self._detached = True
            logger.debug("Servos detached (auto-timeout)")

    def _reattach_if_needed(self):
        """Re-send current angles if servos were detached."""
        if self._detached and self._pca is not None:
            self._detached = False
            # Re-apply last known angles
            with self._lock:
                for name in CHANNEL_MAP:
                    self._set_angle(name, self._current_angles[name])

    # ------------------------------------------------------------------
    # Stand pose (base position)
    # ------------------------------------------------------------------
    def _stand_pose(self):
        self._set_angles({
            "L1": 45, "L4": 180, "R2": 45, "R4": 0,
            "R3": 180, "L3": 0, "R1": 135, "L2": 135,
        })

    # ------------------------------------------------------------------
    # Animations
    # ------------------------------------------------------------------
    def _anim_stand(self):
        self._stand_pose()

    def _anim_rest(self):
        """Slowly interpolate all servos to 90 (20 steps, 40 ms each)."""
        start = dict(self._current_angles)
        steps = 20
        for step in range(1, steps + 1):
            if self._stopped():
                return
            frac = step / steps
            angles = {name: start[name] + (90.0 - start[name]) * frac for name in CHANNEL_MAP}
            self._set_angles(angles)
            self._delay(40)

    def _anim_walk_forward(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({"R3": 135, "L3": 45, "R2": 100, "L1": 25})
        self._delay(100)

        for _ in range(10):
            if self._stopped():
                return
            self._set_angles({"R3": 135, "L3": 0})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"L4": 135, "L2": 90, "R4": 0, "R1": 180})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R2": 45, "L1": 90})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R4": 45, "L4": 180})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R3": 180, "L3": 45, "R2": 90, "L1": 0})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"L2": 135, "R1": 90})
            self._delay(100)

        self._stand_pose()

    def _anim_walk_backward(self):
        self._stand_pose()
        self._delay(200)

        for _ in range(10):
            if self._stopped():
                return
            self._set_angles({"R3": 135, "L3": 0})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"L4": 135, "L2": 135, "R4": 0, "R1": 90})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R2": 90, "L1": 0})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R4": 45, "L4": 180})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R3": 180, "L3": 45, "R2": 45, "L1": 90})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"L2": 90, "R1": 180})
            self._delay(100)

        self._stand_pose()

    def _anim_turn_left(self):
        self._stand_pose()
        self._delay(200)

        for _ in range(10):
            if self._stopped():
                return
            self._set_angles({"R3": 135, "L4": 135})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R1": 180, "L2": 180})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R3": 180, "L4": 180})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R1": 135, "L2": 135})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R4": 45, "L3": 45})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R2": 90, "L1": 90})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R4": 0, "L3": 0})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R2": 45, "L1": 45})
            self._delay(100)

        self._stand_pose()

    def _anim_turn_right(self):
        self._stand_pose()
        self._delay(200)

        for _ in range(10):
            if self._stopped():
                return
            self._set_angles({"R4": 45, "L3": 45})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R2": 0, "L1": 0})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R4": 0, "L3": 0})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R2": 45, "L1": 45})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R3": 135, "L4": 135})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R1": 90, "L2": 90})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R3": 180, "L4": 180})
            self._delay(100)
            if self._stopped():
                return
            self._set_angles({"R1": 135, "L2": 135})
            self._delay(100)

        self._stand_pose()

    def _anim_wave(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({"R4": 80, "L2": 60, "R1": 100})
        self._delay(300)
        if self._stopped():
            return

        self._set_angles({"L3": 180})
        self._delay(300)

        for _ in range(4):
            if self._stopped():
                return
            self._set_angles({"L3": 180})
            self._delay(300)
            if self._stopped():
                return
            self._set_angles({"L3": 100})
            self._delay(300)

        self._stand_pose()

    def _anim_dance(self):
        self._set_angles({
            "R1": 90, "R2": 90, "L1": 90, "L2": 90,
            "R4": 160, "R3": 160, "L3": 10, "L4": 10,
        })
        self._delay(300)

        for _ in range(5):
            if self._stopped():
                return
            self._set_angles({"R4": 115, "R3": 115, "L3": 10, "L4": 10})
            self._delay(300)
            if self._stopped():
                return
            self._set_angles({"R4": 160, "R3": 160, "L3": 65, "L4": 65})
            self._delay(300)

        self._stand_pose()

    def _anim_swim(self):
        self._set_all(90)

        for _ in range(4):
            if self._stopped():
                return
            self._set_angles({"R1": 135, "R2": 45, "L1": 45, "L2": 135})
            self._delay(400)
            if self._stopped():
                return
            self._set_angles({"R1": 90, "R2": 90, "L1": 90, "L2": 90})
            self._delay(400)

        self._stand_pose()

    def _anim_point(self):
        self._set_angles({
            "L2": 60, "R1": 135, "R2": 100, "L4": 180,
            "L1": 25, "L3": 145, "R4": 80, "R3": 170,
        })
        self._delay(2000)
        if self._stopped():
            return
        self._stand_pose()

    def _anim_pushup(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({"L1": 0, "R1": 180, "L3": 90, "R3": 90})
        self._delay(500)

        for _ in range(4):
            if self._stopped():
                return
            self._set_angles({"L3": 0, "R3": 180})
            self._delay(600)
            if self._stopped():
                return
            self._set_angles({"L3": 90, "R3": 90})
            self._delay(500)

        self._stand_pose()

    def _anim_bow(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({
            "L1": 0, "R1": 180, "L3": 0, "R3": 180,
            "L2": 180, "R2": 0, "R4": 0, "L4": 180,
        })
        self._delay(600)
        if self._stopped():
            return

        self._set_angles({"L3": 90, "R3": 90})
        self._delay(3000)
        if self._stopped():
            return

        self._stand_pose()

    def _anim_cute(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({
            "L2": 160, "R2": 20, "R4": 180, "L4": 0,
            "L1": 0, "R1": 180, "L3": 180, "R3": 0,
        })
        self._delay(200)

        for _ in range(5):
            if self._stopped():
                return
            self._set_angles({"R4": 180, "L4": 45})
            self._delay(300)
            if self._stopped():
                return
            self._set_angles({"R4": 135, "L4": 0})
            self._delay(300)

        self._stand_pose()

    def _anim_freaky(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({"L1": 0, "R1": 180, "L2": 180, "R2": 0, "R4": 90, "R3": 0})
        self._delay(200)

        for _ in range(3):
            if self._stopped():
                return
            self._set_angles({"R3": 25})
            self._delay(400)
            if self._stopped():
                return
            self._set_angles({"R3": 0})
            self._delay(400)

        self._stand_pose()

    def _anim_worm(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({
            "R1": 180, "R2": 0, "L1": 0, "L2": 180,
            "R4": 90, "R3": 90, "L3": 90, "L4": 90,
        })
        self._delay(200)

        for _ in range(5):
            if self._stopped():
                return
            self._set_angles({"R3": 45, "L3": 135, "R4": 45, "L4": 135})
            self._delay(300)
            if self._stopped():
                return
            self._set_angles({"R3": 135, "L3": 45, "R4": 135, "L4": 45})
            self._delay(300)

        self._stand_pose()

    def _anim_shake(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({"R1": 135, "L1": 45, "L3": 90, "R3": 90, "L2": 90, "R2": 90})
        self._delay(200)

        for _ in range(5):
            if self._stopped():
                return
            self._set_angles({"R4": 45, "L4": 135})
            self._delay(300)
            if self._stopped():
                return
            self._set_angles({"R4": 0, "L4": 180})
            self._delay(300)

        self._stand_pose()

    def _anim_shrug(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({"R3": 90, "R4": 90, "L3": 90, "L4": 90})
        self._delay(1000)
        if self._stopped():
            return

        self._set_angles({"R3": 0, "R4": 180, "L3": 180, "L4": 0})
        self._delay(1500)
        if self._stopped():
            return

        self._stand_pose()

    def _anim_dead(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({"R3": 90, "R4": 90, "L3": 90, "L4": 90})
        # No standPose after — stays dead

    def _anim_crab(self):
        self._stand_pose()
        self._delay(200)
        if self._stopped():
            return

        self._set_angles({
            "R1": 90, "R2": 90, "L1": 90, "L2": 90,
            "R4": 0, "R3": 180, "L3": 45, "L4": 135,
        })

        for _ in range(5):
            if self._stopped():
                return
            self._set_angles({"R4": 45, "R3": 135, "L3": 0, "L4": 180})
            self._delay(300)
            if self._stopped():
                return
            self._set_angles({"R4": 0, "R3": 180, "L3": 45, "L4": 135})
            self._delay(300)

        self._stand_pose()

    # ------------------------------------------------------------------
    # Command-to-animation mapping
    # ------------------------------------------------------------------
    COMMAND_MAP = {
        "stand":   "_anim_stand",
        "rest":    "_anim_rest",
        "walk":    "_anim_walk_forward",
        "back":    "_anim_walk_backward",
        "left":    "_anim_turn_left",
        "right":   "_anim_turn_right",
        "wave":    "_anim_wave",
        "dance":   "_anim_dance",
        "swim":    "_anim_swim",
        "point":   "_anim_point",
        "pushup":  "_anim_pushup",
        "bow":     "_anim_bow",
        "cute":    "_anim_cute",
        "freaky":  "_anim_freaky",
        "worm":    "_anim_worm",
        "shake":   "_anim_shake",
        "shrug":   "_anim_shrug",
        "dead":    "_anim_dead",
        "crab":    "_anim_crab",
    }

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------
    def stop(self):
        """Interrupt any running animation immediately."""
        self._stop_event.set()
        if self._anim_thread is not None and self._anim_thread.is_alive():
            self._anim_thread.join(timeout=2.0)
        self._stop_event.clear()
        logger.debug("Animation stopped")

    def handle_command(self, cmd: str):
        """
        Parse and execute a servo command.

        Accepted formats:
            "stand", "walk", "dance", ...   -- named animations
            "L1:45,R2:90"                   -- individual servo angles
            "B 45 180 45 0 180 0 135 135"   -- bulk set (pin order)
            "ALL:90"                         -- all servos to one angle
        """
        cmd = cmd.strip()
        if not cmd:
            return

        self._reattach_if_needed()

        # ----- ALL:angle -----
        if cmd.upper().startswith("ALL:"):
            try:
                angle = float(cmd.split(":")[1])
            except (IndexError, ValueError):
                logger.warning("Invalid ALL command: %s", cmd)
                return
            self.stop()
            self._set_all(angle)
            return

        # ----- Bulk command: "B a0 a1 a2 a3 a4 a5 a6 a7" -----
        if cmd.upper().startswith("B "):
            parts = cmd.split()
            if len(parts) != 9:  # B + 8 angles
                logger.warning("Bulk command needs 8 angles, got %d: %s", len(parts) - 1, cmd)
                return
            self.stop()
            try:
                angles = {BULK_ORDER[i]: float(parts[i + 1]) for i in range(8)}
            except ValueError:
                logger.warning("Invalid angle values in bulk command: %s", cmd)
                return
            self._set_angles(angles)
            return

        # ----- Individual servo: "L1:45,R2:90" -----
        if ":" in cmd and cmd.split(":")[0].upper() in CHANNEL_MAP:
            self.stop()
            pairs = cmd.split(",")
            angles: Dict[str, float] = {}
            for pair in pairs:
                pair = pair.strip()
                if ":" not in pair:
                    continue
                name, val = pair.split(":", 1)
                name = name.strip().upper()
                if name not in CHANNEL_MAP:
                    logger.warning("Unknown servo name: %s", name)
                    continue
                try:
                    angles[name] = float(val.strip())
                except ValueError:
                    logger.warning("Invalid angle for %s: %s", name, val)
            if angles:
                self._set_angles(angles)
            return

        # ----- Named animation -----
        cmd_lower = cmd.lower()
        if cmd_lower in self.COMMAND_MAP:
            self._run_animation(self.COMMAND_MAP[cmd_lower])
            return

        logger.warning("Unknown command: %s", cmd)

    def _run_animation(self, method_name: str):
        """Run an animation method in a background thread."""
        self.stop()
        method = getattr(self, method_name)

        def _worker():
            try:
                method()
            except Exception:
                logger.exception("Animation %s failed", method_name)

        self._anim_thread = threading.Thread(target=_worker, daemon=True)
        self._anim_thread.start()

    def shutdown(self):
        """Stop everything and deinitialise hardware."""
        self.stop()
        if self._detach_timer is not None:
            self._detach_timer.cancel()
        self._detach_servos()
        if self._pca is not None:
            try:
                self._pca.deinit()
            except Exception:
                pass
            self._pca = None
        logger.info("ServoDriver shut down")

    # ------------------------------------------------------------------
    # Context manager support
    # ------------------------------------------------------------------
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.shutdown()
        return False


# ---------------------------------------------------------------------------
# Standalone test
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    driver = ServoDriver()
    print("Available commands:", ", ".join(sorted(ServoDriver.COMMAND_MAP.keys())))
    try:
        while True:
            cmd = input("servo> ").strip()
            if cmd.lower() in ("quit", "exit", "q"):
                break
            if cmd.lower() == "stop":
                driver.stop()
            else:
                driver.handle_command(cmd)
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        driver.shutdown()
        print("Done.")
