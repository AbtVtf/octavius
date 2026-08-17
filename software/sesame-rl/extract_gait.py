"""
Extract one gait cycle from the sim and save as joint angle sequence.
"""

import os
import numpy as np
from stable_baselines3 import PPO
from stable_baselines3.common.vec_env import DummyVecEnv, VecNormalize
from sesame_env import SesameQuadrupedEnv, MAX_ANGLE_RAD

# Sim joint order: [R1, R3, L1, L3, R2, R4, L2, L4]
SIM_JOINT_NAMES = ["R1", "R3", "L1", "L3", "R2", "R4", "L2", "L4"]
STAND_ANGLES = np.array([135, 180, 45, 0, 45, 0, 135, 180], dtype=np.float64)
SERVO_DIRECTION = np.array([+1, -1, -1, +1, -1, +1, +1, -1], dtype=np.float64)


def sim_to_servo(sim_angles_rad):
    delta_deg = np.rad2deg(sim_angles_rad) * SERVO_DIRECTION
    servo_angles = STAND_ANGLES + delta_deg
    return np.clip(np.round(servo_angles), 0, 180).astype(int)


def main():
    model_path = "checkpoints/final_model.zip"
    norm_path = "checkpoints/final_model_vecnormalize.pkl"

    print(f"Loading {model_path}")
    vec_env = DummyVecEnv([lambda: SesameQuadrupedEnv()])
    if os.path.exists(norm_path):
        vec_env = VecNormalize.load(norm_path, vec_env)
        vec_env.training = False
        vec_env.norm_reward = False
    model = PPO.load(model_path, env=vec_env)

    obs = vec_env.reset()
    frames = []

    # Run for 200 steps to get a few gait cycles
    for i in range(200):
        action, _ = model.predict(obs, deterministic=True)
        obs, _, done, _ = vec_env.step(action)
        if done[0]:
            obs = vec_env.reset()

        # Convert action to servo angles
        joint_rad = action[0] * MAX_ANGLE_RAD
        servo_angles = sim_to_servo(joint_rad)
        frames.append(servo_angles.copy())

    frames = np.array(frames)

    # Find a repeating gait cycle by looking at periodicity
    # Use autocorrelation on one hip joint
    hip_signal = frames[:, 0].astype(float)  # R1
    hip_signal -= hip_signal.mean()
    corr = np.correlate(hip_signal, hip_signal, mode='full')
    corr = corr[len(corr)//2:]  # positive lags only
    corr = corr / corr[0]  # normalize

    # Find first peak after initial decay (that's the period)
    min_period = 5
    for i in range(min_period, len(corr) - 1):
        if corr[i] > corr[i-1] and corr[i] > corr[i+1] and corr[i] > 0.5:
            period = i
            break
    else:
        period = 20  # fallback

    print(f"Detected gait period: {period} steps")

    # Extract one cycle from the middle (after warmup)
    start = 50
    cycle = frames[start:start + period]

    print(f"Gait cycle: {len(cycle)} frames")
    print(f"Joint order: {SIM_JOINT_NAMES}")
    print(f"\nFrame angles:")
    for i, f in enumerate(cycle):
        cmd = ",".join(f"{n}:{a}" for n, a in zip(SIM_JOINT_NAMES, f))
        print(f"  [{i:2d}] {cmd}")

    # Save
    np.save("gait_cycle.npy", cycle)
    print(f"\nSaved to gait_cycle.npy ({cycle.shape})")

    vec_env.close()


if __name__ == "__main__":
    main()
