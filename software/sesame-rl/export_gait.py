"""
Export a trained RL walking policy as C++ keyframes for the Sesame firmware.

This extracts the periodic gait cycle from the trained policy and generates
setServoAngle() calls compatible with movement-sequences.h.

Usage:
    python export_gait.py                                       # Auto-detect best model
    python export_gait.py --model checkpoints/best/best_model.zip
    python export_gait.py --output learned_walk.h               # Custom output file
    python export_gait.py --max-frames 12                       # Limit keyframes per cycle
"""

import argparse
import os

import numpy as np
from scipy.signal import find_peaks, correlate

from stable_baselines3 import PPO
from stable_baselines3.common.vec_env import DummyVecEnv, VecNormalize

from sesame_env import SesameQuadrupedEnv, MAX_ANGLE_RAD

# ---- Servo mapping ----
# Sim joint order → firmware servo name
SIM_TO_SERVO = ["R1", "R2", "L1", "L2", "R3", "R4", "L3", "L4"]

# Firmware stand pose (servo degrees) — this is the "zero" reference for export
STAND_ANGLES = np.array([135, 45, 45, 135, 180, 0, 0, 180], dtype=np.float64)

# Direction: how sim joint angle (positive = forward) maps to servo angle change
# Right side: +sim → +servo (servo increases = forward)
# Left side:  +sim → -servo (servo decreases = forward, mirrored mounting)
SERVO_DIRECTION = np.array([+1, -1, -1, +1, +1, -1, -1, +1], dtype=np.float64)


def sim_to_servo_angles(sim_angles_rad):
    """Convert simulation joint angles (radians) to firmware servo angles (degrees 0-180)."""
    delta_deg = np.rad2deg(sim_angles_rad) * SERVO_DIRECTION
    servo_angles = STAND_ANGLES + delta_deg
    return np.clip(np.round(servo_angles), 0, 180).astype(int)


def find_gait_period(joint_trajectory, min_period=5, max_period=100):
    """Detect the gait period using autocorrelation of hip joint."""
    # Use front-right hip (index 0) for period detection
    signal = joint_trajectory[:, 0]
    signal = signal - signal.mean()

    # Autocorrelation
    auto = correlate(signal, signal, mode="full")
    auto = auto[len(auto) // 2:]  # positive lags only
    if auto[0] > 0:
        auto = auto / auto[0]  # normalize

    # Find peaks in autocorrelation
    peaks, properties = find_peaks(auto, height=0.3, distance=min_period)

    if len(peaks) > 0:
        period = peaks[0]
        confidence = properties["peak_heights"][0]
        return period, confidence
    else:
        return 20, 0.0  # default fallback


def extract_gait(model, env, warmup_steps=500, record_steps=500):
    """Run the policy and record joint angle trajectory."""
    obs = env.reset()
    all_actions = []

    # Warmup: let the gait stabilize
    for _ in range(warmup_steps):
        action, _ = model.predict(obs, deterministic=True)
        obs, _, done, _ = env.step(action)
        if done[0]:
            obs = env.reset()

    # Record stable gait
    for _ in range(record_steps):
        action, _ = model.predict(obs, deterministic=True)
        obs, _, done, _ = env.step(action)
        all_actions.append(action[0].copy())
        if done[0]:
            obs = env.reset()

    actions = np.array(all_actions)  # shape: (record_steps, 8), values in [-1, 1]
    joint_angles_rad = actions * MAX_ANGLE_RAD
    return joint_angles_rad


def subsample_cycle(cycle, max_frames):
    """Reduce the number of keyframes by subsampling."""
    if len(cycle) <= max_frames:
        return cycle
    indices = np.linspace(0, len(cycle) - 1, max_frames, dtype=int)
    return cycle[indices]


def generate_cpp(servo_frames, func_name="runLearnedWalkPose"):
    """Generate C++ function with setServoAngle calls."""
    lines = []
    lines.append(f"inline void {func_name}() {{")
    lines.append(f'  Serial.println(F("LEARNED WALK"));')
    lines.append(f'  setFaceWithMode("walk", FACE_ANIM_ONCE);')
    lines.append(f"")
    lines.append(f"  for (int i = 0; i < walkCycles; i++) {{")

    for frame_idx, frame in enumerate(servo_frames):
        # Write all 8 servo angles for this frame
        calls = []
        for j, name in enumerate(SIM_TO_SERVO):
            calls.append(f"setServoAngle({name}, {frame[j]})")
        # Split into two lines of 4 for readability
        lines.append(f"    // Frame {frame_idx + 1}/{len(servo_frames)}")
        lines.append(f"    {'; '.join(calls[:4])};")
        lines.append(f"    {'; '.join(calls[4:])};")
        lines.append(f'    if (!pressingCheck("forward", frameDelay)) return;')

    lines.append(f"  }}")
    lines.append(f"  runStandPose(1);")
    lines.append(f"}}")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Export learned gait to C++ keyframes")
    parser.add_argument("--model", type=str, default=None,
                        help="Path to trained model .zip")
    parser.add_argument("--output", type=str, default="learned_walk.h",
                        help="Output C++ file")
    parser.add_argument("--max-frames", type=int, default=16,
                        help="Maximum keyframes per gait cycle")
    parser.add_argument("--warmup", type=int, default=500,
                        help="Warmup steps before recording")
    parser.add_argument("--record", type=int, default=500,
                        help="Steps to record for gait extraction")
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

    # Create env with normalization
    env = DummyVecEnv([lambda: SesameQuadrupedEnv()])
    norm_path = args.model.replace(".zip", "_vecnormalize.pkl")
    if not os.path.exists(norm_path):
        norm_path = os.path.join(os.path.dirname(args.model), "..", "final_model_vecnormalize.pkl")
    if os.path.exists(norm_path):
        env = VecNormalize.load(norm_path, env)
        env.training = False
        env.norm_reward = False
    else:
        env = VecNormalize(env, norm_obs=True, norm_reward=False, training=False)

    model = PPO.load(args.model, env=env)

    # Extract gait
    print(f"Running policy for {args.warmup} warmup + {args.record} recording steps...")
    joint_trajectory = extract_gait(model, env, args.warmup, args.record)

    # Detect gait period
    period, confidence = find_gait_period(joint_trajectory)
    print(f"Detected gait period: {period} steps (confidence: {confidence:.2f})")
    print(f"  = {period * 0.05:.2f}s per cycle at 20Hz control rate")

    if confidence < 0.2:
        print("Warning: Low periodicity confidence. The gait may not be cyclic.")
        print("  This could mean the policy hasn't converged to a stable walk.")
        print("  Try training for more steps, or adjust reward weights.")

    # Extract one cycle from the middle of the recording
    mid = len(joint_trajectory) // 2
    start = mid - period // 2
    start = max(0, start)
    cycle = joint_trajectory[start : start + period]

    # Subsample if needed
    cycle = subsample_cycle(cycle, args.max_frames)
    print(f"Extracted {len(cycle)} keyframes per gait cycle")

    # Convert to servo angles
    servo_frames = np.array([sim_to_servo_angles(frame) for frame in cycle])

    # Print the servo angle table
    print(f"\nServo angles per frame:")
    header = "  Frame  " + "  ".join(f"{s:>4}" for s in SIM_TO_SERVO)
    print(header)
    print("  " + "-" * (len(header) - 2))
    for i, frame in enumerate(servo_frames):
        vals = "  ".join(f"{v:>4}" for v in frame)
        print(f"  {i+1:>5}  {vals}")

    # Generate C++ code
    cpp_code = generate_cpp(servo_frames)

    # Write output
    output_path = args.output
    with open(output_path, "w") as f:
        f.write(f"// Auto-generated by export_gait.py from RL training\n")
        f.write(f"// Model: {args.model}\n")
        f.write(f"// Gait period: {period} steps ({period * 0.05:.2f}s)\n")
        f.write(f"// Keyframes: {len(servo_frames)}\n")
        f.write(f"//\n")
        f.write(f"// IMPORTANT: The servo direction mapping (SERVO_DIRECTION) may need\n")
        f.write(f"// calibration for your specific robot build. If the robot walks\n")
        f.write(f"// incorrectly, try flipping the sign for individual joints in\n")
        f.write(f"// export_gait.py's SERVO_DIRECTION array.\n")
        f.write(f"\n")
        f.write(cpp_code)
        f.write(f"\n")

    print(f"\nC++ code written to: {output_path}")
    print(f"Copy the function into firmware/movement-sequences.h to use it.")

    env.close()


if __name__ == "__main__":
    main()
