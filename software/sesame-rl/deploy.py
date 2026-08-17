"""
Phase 2: Live policy inference on Raspberry Pi.

Runs the trained neural net and sends servo commands to the ESP32
over WiFi using the existing HTTP API.

Requirements on the RPi:
    pip install numpy stable-baselines3 requests

Usage:
    python deploy.py --robot-ip 192.168.4.1                    # Default AP mode
    python deploy.py --robot-ip sesame-robot.local              # mDNS
    python deploy.py --model checkpoints/best/best_model.zip    # Specific model
    python deploy.py --hz 10                                    # Slower control rate

NOTE: This runs the policy open-loop (no IMU feedback). The observation
uses commanded joint angles as "measured" positions. For closed-loop
control, add an IMU and feed real orientation/velocity data.
"""

import argparse
import os
import time
import signal
import sys

import numpy as np
import requests

from stable_baselines3 import PPO

# ---- Servo mapping (same as export_gait.py) ----
SIM_TO_SERVO = ["R1", "R2", "L1", "L2", "R3", "R4", "L3", "L4"]
STAND_ANGLES = np.array([135, 45, 45, 135, 180, 0, 0, 180], dtype=np.float64)
SERVO_DIRECTION = np.array([+1, -1, -1, +1, +1, -1, -1, +1], dtype=np.float64)
MAX_ANGLE_RAD = np.deg2rad(60)


def sim_to_servo_angles(sim_angles_rad):
    delta_deg = np.rad2deg(sim_angles_rad) * SERVO_DIRECTION
    servo_angles = STAND_ANGLES + delta_deg
    return np.clip(np.round(servo_angles), 0, 180).astype(int)


def send_servo_command(robot_url, servo_angles):
    """Send all 8 servo angles to the robot via HTTP."""
    # Use the legacy motor endpoint for individual servo control
    # TODO: Add bulk /api/servos endpoint to firmware for lower latency
    for i, angle in enumerate(servo_angles):
        try:
            requests.get(
                f"{robot_url}/cmd",
                params={"motor": str(i), "value": str(angle)},
                timeout=0.05,
            )
        except requests.exceptions.RequestException:
            pass  # Don't block on network errors


def send_stand(robot_url):
    """Send stand pose."""
    try:
        requests.post(
            f"{robot_url}/api/command",
            json={"command": "stand"},
            timeout=1.0,
        )
    except requests.exceptions.RequestException:
        pass


class OpenLoopState:
    """Fake observation for open-loop inference (no sensors)."""

    def __init__(self, obs_dim=35):
        self.obs = np.zeros(obs_dim, dtype=np.float32)
        self.prev_action = np.zeros(8, dtype=np.float32)
        self.obs_dim = obs_dim

    def update(self, action):
        """Update internal state with the action we just sent."""
        joint_pos = action * MAX_ANGLE_RAD

        # Build observation:
        # [0]    z height (assume standing)
        # [1:5]  quaternion (assume upright: w=1, x=y=z=0)
        # [5:8]  linear velocity (assume moving forward slowly)
        # [8:11] angular velocity (zero)
        # [11:19] joint positions (from our commands)
        # [19:27] joint velocities (estimated from action change)
        # [27:35] previous action
        self.obs[0] = 0.20  # approximate standing height
        self.obs[1] = 1.0   # quat w
        self.obs[2:5] = 0   # quat x,y,z
        self.obs[5] = 0.05  # assume small forward velocity
        self.obs[6:8] = 0
        self.obs[8:11] = 0
        self.obs[11:19] = joint_pos
        self.obs[19:27] = (action - self.prev_action) * MAX_ANGLE_RAD * 20  # rough vel estimate
        self.obs[27:35] = self.prev_action

        self.prev_action = action.copy()
        return self.obs.copy()


def main():
    parser = argparse.ArgumentParser(description="Deploy Sesame RL policy on RPi")
    parser.add_argument("--robot-ip", type=str, default="192.168.4.1",
                        help="Robot IP or hostname")
    parser.add_argument("--model", type=str, default=None,
                        help="Path to trained model .zip")
    parser.add_argument("--hz", type=int, default=10,
                        help="Control frequency in Hz")
    parser.add_argument("--duration", type=float, default=30.0,
                        help="Run duration in seconds (0 = forever)")
    args = parser.parse_args()

    robot_url = f"http://{args.robot_ip}"

    # Find model
    if args.model is None:
        for path in [
            "checkpoints/best/best_model.zip",
            "checkpoints/final_model.zip",
        ]:
            if os.path.exists(path):
                args.model = path
                break
    if args.model is None:
        print("Error: No model found. Specify --model path")
        return

    print(f"Loading model: {args.model}")
    model = PPO.load(args.model)

    # Note: normalization stats need to be applied manually for open-loop
    # since we're not using VecNormalize. For proper deployment, you'd
    # load the stats and normalize observations manually.

    print(f"Robot URL: {robot_url}")
    print(f"Control rate: {args.hz} Hz")
    print(f"Duration: {'forever' if args.duration == 0 else f'{args.duration}s'}")
    print(f"Press Ctrl+C to stop and return to stand pose.")

    # Graceful shutdown
    running = True

    def signal_handler(sig, frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, signal_handler)

    # Test connection
    try:
        r = requests.get(f"{robot_url}/api/status", timeout=2.0)
        print(f"Connected to robot: {r.status_code}")
    except requests.exceptions.RequestException:
        print(f"Warning: Cannot reach robot at {robot_url}. Continuing anyway...")

    # Initialize state
    state = OpenLoopState()
    obs = state.obs.copy()
    period = 1.0 / args.hz
    start_time = time.time()
    step = 0

    print("\nStarting walk...")

    while running:
        t_start = time.time()

        # Check duration
        if args.duration > 0 and (time.time() - start_time) > args.duration:
            break

        # Get action from policy
        # Reshape for SB3 (expects batch dimension)
        action, _ = model.predict(obs.reshape(1, -1), deterministic=True)
        action = action[0]

        # Convert to servo angles
        joint_rad = action * MAX_ANGLE_RAD
        servo_angles = sim_to_servo_angles(joint_rad)

        # Send to robot
        send_servo_command(robot_url, servo_angles)

        # Update internal state
        obs = state.update(action)

        step += 1
        if step % (args.hz * 5) == 0:
            elapsed = time.time() - start_time
            print(f"  Step {step} ({elapsed:.1f}s) | Servos: {servo_angles}")

        # Sleep for remainder of control period
        elapsed = time.time() - t_start
        sleep_time = period - elapsed
        if sleep_time > 0:
            time.sleep(sleep_time)

    # Return to stand
    print("\nReturning to stand pose...")
    send_stand(robot_url)
    print("Done.")


if __name__ == "__main__":
    main()
