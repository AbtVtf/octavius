"""
Deploy RL walking policy over WiFi TCP to the Sesame robot.

Sends servo commands through the SenseCAP Watcher TCP bridge
to the Arduino Nano using the named-leg protocol (L1:angle,R2:angle,...).

Usage:
    python deploy_tcp.py                                    # defaults
    python deploy_tcp.py --model checkpoints/best/best_model.zip
    python deploy_tcp.py --hz 10
    python deploy_tcp.py --duration 10
"""

import argparse
import os
import time
import signal
import socket
import sys

import numpy as np
from stable_baselines3 import PPO

from sesame_env import MAX_ANGLE_RAD

# ---- Servo mapping ----
# Sim actuator order (from MuJoCo XML kinematic tree):
# [0]=R1(fr_hip), [1]=R3(fr_ankle), [2]=L1(fl_hip), [3]=L3(fl_ankle),
# [4]=R2(rr_hip), [5]=R4(rr_ankle), [6]=L2(rl_hip), [7]=L4(rl_ankle)
SIM_JOINT_NAMES = ["R1", "R3", "L1", "L3", "R2", "R4", "L2", "L4"]

# Stand angles per joint (sim order), in degrees
STAND_ANGLES = np.array([135, 180, 45, 0, 45, 0, 135, 180], dtype=np.float64)

# Direction multiplier: maps positive sim rotation to servo direction
# Hips: R1+1, L1-1, R2-1, L2+1 (verified by calibration)
# Knees: R3-1, L3+1, R4+1, L4-1 (positive sim = lift foot)
SERVO_DIRECTION = np.array([+1, -1, -1, +1, -1, +1, +1, -1], dtype=np.float64)


def sim_to_servo_angles(sim_angles_rad):
    delta_deg = np.rad2deg(sim_angles_rad) * SERVO_DIRECTION
    servo_angles = STAND_ANGLES + delta_deg
    return np.clip(np.round(servo_angles), 0, 180).astype(int)


def angles_to_cmd(servo_angles):
    """Convert servo angles array (sim order) to named-leg protocol command."""
    parts = []
    for name, angle in zip(SIM_JOINT_NAMES, servo_angles):
        parts.append(f"{name}:{angle}")
    return ",".join(parts)


class OpenLoopState:
    def __init__(self, obs_dim=35):
        self.obs = np.zeros(obs_dim, dtype=np.float32)
        self.prev_action = np.zeros(8, dtype=np.float32)

    def update(self, action):
        joint_pos = action * MAX_ANGLE_RAD
        self.obs[0] = 0.20
        self.obs[1] = 1.0
        self.obs[2:5] = 0
        self.obs[5] = 0.05
        self.obs[6:8] = 0
        self.obs[8:11] = 0
        self.obs[11:19] = joint_pos
        self.obs[19:27] = (action - self.prev_action) * MAX_ANGLE_RAD * 20
        self.obs[27:35] = self.prev_action
        self.prev_action = action.copy()
        return self.obs.copy()


def main():
    parser = argparse.ArgumentParser(description="Deploy Sesame RL policy over TCP")
    parser.add_argument("--watcher-ip", type=str, default="192.168.0.2")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--model", type=str, default=None)
    parser.add_argument("--hz", type=int, default=10)
    parser.add_argument("--duration", type=float, default=30.0)
    args = parser.parse_args()

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

    # Load VecNormalize stats if available
    norm_path = args.model.replace(".zip", "_vecnormalize.pkl")
    obs_rms = None
    if os.path.exists(norm_path):
        import pickle
        with open(norm_path, "rb") as f:
            vec_norm = pickle.load(f)
        obs_rms = vec_norm.obs_rms
        clip_obs = vec_norm.clip_obs
        print(f"Loaded observation normalization from {norm_path}")
    else:
        print("No VecNormalize found, using raw observations")

    print(f"Connecting to Watcher at {args.watcher_ip}:{args.port}")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    sock.connect((args.watcher_ip, args.port))
    print("Connected!")

    # Graceful shutdown
    running = True
    def signal_handler(sig, frame):
        nonlocal running
        running = False
    signal.signal(signal.SIGINT, signal_handler)

    # Stand up first
    print("Standing up...")
    stand_servo = sim_to_servo_angles(np.zeros(8))
    cmd = angles_to_cmd(stand_servo)
    sock.sendall((cmd + "\n").encode())
    time.sleep(2)

    # Initialize state
    state = OpenLoopState()
    obs = state.obs.copy()
    period = 1.0 / args.hz
    start_time = time.time()
    step = 0

    print(f"Walking at {args.hz}Hz for {args.duration}s... Ctrl+C to stop")

    while running:
        t_start = time.time()

        if args.duration > 0 and (time.time() - start_time) > args.duration:
            break

        # Normalize observation if VecNormalize was used during training
        obs_input = obs.copy()
        if obs_rms is not None:
            obs_input = (obs_input - obs_rms.mean) / np.sqrt(obs_rms.var + 1e-8)
            obs_input = np.clip(obs_input, -clip_obs, clip_obs)

        # Get action from policy
        action, _ = model.predict(obs_input.reshape(1, -1).astype(np.float32), deterministic=True)
        action = action[0]

        # Convert to servo angles
        joint_rad = action * MAX_ANGLE_RAD
        servo_angles = sim_to_servo_angles(joint_rad)

        # Send named-leg command
        cmd = angles_to_cmd(servo_angles)
        try:
            sock.sendall((cmd + "\n").encode())
        except socket.error:
            print("Connection lost!")
            break

        # Update state
        obs = state.update(action)

        step += 1
        if step % (args.hz * 5) == 0:
            elapsed = time.time() - start_time
            print(f"  Step {step} ({elapsed:.1f}s) | {cmd}")

        # Sleep for remainder
        elapsed = time.time() - t_start
        sleep_time = period - elapsed
        if sleep_time > 0:
            time.sleep(sleep_time)

    # Stop - return to rest
    print("\nStopping...")
    sock.sendall(b"ALL:90\n")
    time.sleep(1)
    sock.sendall(b"DETACH\n")
    time.sleep(0.5)
    sock.close()
    print("Done.")


if __name__ == "__main__":
    main()
