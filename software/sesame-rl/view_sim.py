"""
Launch the MuJoCo 3D viewer to inspect the Sesame robot.

Controls:
    Left click + drag:   Rotate camera
    Right click + drag:  Pan camera
    Scroll:              Zoom
    Ctrl + right-click:  Apply force to body
    Space:               Pause/resume
    Backspace:           Reset

    W/S:    Walk forward / backward
    A/D:    Turn left / right
    Q:      Stop (stand still)

Usage:
    python view_sim.py                     # Interactive viewer, no policy
    python view_sim.py --walk              # Run trained policy (if available)
    python view_sim.py --random            # Random actions
    python view_sim.py --model PATH        # Specific model checkpoint
"""

import argparse
import os
import time
import sys
import numpy as np
import mujoco
import mujoco.viewer

from sesame_env import SesameQuadrupedEnv, MAX_ANGLE_RAD


# Keyboard state for command control
_key_state = {"w": False, "s": False, "a": False, "d": False}


def key_callback(keycode):
    """Track WASD key presses for velocity commands."""
    key = chr(keycode) if 32 <= keycode < 127 else ""
    key = key.lower()
    if key in _key_state:
        _key_state[key] = True


def get_command_from_keys():
    """Convert held keys to velocity commands."""
    fwd = 0.0
    turn = 0.0
    if _key_state.get("w"):
        fwd += 0.25
    if _key_state.get("s"):
        fwd -= 0.25
    if _key_state.get("a"):
        turn += 1.0
    if _key_state.get("d"):
        turn -= 1.0
    # Reset key state (simple toggle, not held-key detection)
    return fwd, turn


def main():
    parser = argparse.ArgumentParser(description="View Sesame robot in MuJoCo 3D")
    parser.add_argument("--walk", action="store_true",
                        help="Run trained walking policy")
    parser.add_argument("--random", action="store_true",
                        help="Apply random actions")
    parser.add_argument("--model", type=str, default=None,
                        help="Path to trained model .zip (with --walk)")
    parser.add_argument("--cmd-fwd", type=float, default=0.2,
                        help="Forward velocity command (m/s)")
    parser.add_argument("--cmd-turn", type=float, default=0.0,
                        help="Turn rate command (rad/s)")
    args = parser.parse_args()

    env = SesameQuadrupedEnv()
    obs, _ = env.reset()

    policy = None
    vec_env = None
    if args.walk:
        from stable_baselines3 import PPO
        from stable_baselines3.common.vec_env import DummyVecEnv, VecNormalize

        model_path = args.model
        if model_path is None:
            for p in ["checkpoints/best/best_model.zip", "checkpoints/final_model.zip"]:
                if os.path.exists(p):
                    model_path = p
                    break
        if model_path is None:
            print("No trained model found. Run train.py first.")
            return

        print(f"Loading policy: {model_path}")
        vec_env = DummyVecEnv([lambda: SesameQuadrupedEnv()])
        norm_path = model_path.replace(".zip", "_vecnormalize.pkl")
        if os.path.exists(norm_path):
            vec_env = VecNormalize.load(norm_path, vec_env)
            vec_env.training = False
            vec_env.norm_reward = False
        policy = PPO.load(model_path, env=vec_env)

        # Set initial command
        inner_env = vec_env.envs[0]
        inner_env._cmd_fwd_vel = args.cmd_fwd
        inner_env._cmd_turn_rate = args.cmd_turn
        obs = vec_env.reset()

    print("\nLaunching MuJoCo viewer...")
    print("  Rotate: left-click drag | Pan: right-click drag | Zoom: scroll")
    print("  Ctrl+right-click: push the robot | Space: pause | Backspace: reset")
    if policy:
        print(f"\n  W/S: forward/backward | A/D: turn left/right | Q: stop")
        print(f"  Current command: fwd={args.cmd_fwd:.2f} m/s, turn={args.cmd_turn:.2f} rad/s")
    print("  Esc: quit\n")

    raw = vec_env.envs[0] if vec_env else env
    while hasattr(raw, "env"):
        raw = raw.env
    mj_model = raw.model
    mj_data = raw.data

    step_count = 0
    cmd_fwd = args.cmd_fwd
    cmd_turn = args.cmd_turn

    with mujoco.viewer.launch_passive(mj_model, mj_data) as viewer:
        while viewer.is_running():
            t_start = time.time()

            if policy is not None:
                action, _ = policy.predict(obs, deterministic=True)
                obs, reward, done, info = vec_env.step(action)
                if hasattr(done, '__len__'):
                    done = done[0]
                if done:
                    # Keep the command across resets
                    inner = vec_env.envs[0]
                    inner._cmd_fwd_vel = cmd_fwd
                    inner._cmd_turn_rate = cmd_turn
                    obs = vec_env.reset()
            elif args.random:
                action = np.random.uniform(-0.3, 0.3, size=8)
                ctrl = action * MAX_ANGLE_RAD
                mj_data.ctrl[:] = ctrl
                for _ in range(25):
                    mujoco.mj_step(mj_model, mj_data)
            else:
                for _ in range(25):
                    mujoco.mj_step(mj_model, mj_data)

            viewer.sync()
            step_count += 1

            if step_count % 100 == 0:
                z = mj_data.qpos[2]
                x = mj_data.qpos[0]
                vx = mj_data.qvel[0]
                yaw = mj_data.qvel[5]
                cmd_str = f"cmd=[{cmd_fwd:+.2f}, {cmd_turn:+.2f}]" if policy else ""
                print(f"  Step {step_count} | x={x:.3f}m z={z:.3f}m vx={vx:.3f}m/s yaw={yaw:.2f}r/s {cmd_str}")

            elapsed = time.time() - t_start
            sleep_time = 0.05 - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    if vec_env:
        vec_env.close()
    else:
        env.close()
    print("Viewer closed.")


if __name__ == "__main__":
    main()
