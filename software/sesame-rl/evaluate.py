"""
Evaluate and visualize a trained Sesame walking policy.

Usage:
    python evaluate.py                                          # Best model, interactive viewer
    python evaluate.py --model checkpoints/final_model.zip      # Specific model
    python evaluate.py --record video.mp4 --episodes 3          # Record video
    python evaluate.py --no-render                               # Headless stats only
"""

import argparse
import os
import time

import numpy as np
import mujoco

from stable_baselines3 import PPO
from stable_baselines3.common.vec_env import DummyVecEnv, VecNormalize

from sesame_env import SesameQuadrupedEnv


def find_best_model(checkpoint_dir="checkpoints"):
    """Find the best saved model."""
    best_path = os.path.join(checkpoint_dir, "best", "best_model.zip")
    if os.path.exists(best_path):
        return best_path
    final_path = os.path.join(checkpoint_dir, "final_model.zip")
    if os.path.exists(final_path):
        return final_path
    raise FileNotFoundError(f"No model found in {checkpoint_dir}/")


def evaluate_headless(model, env, n_episodes=20):
    """Run episodes and print stats without rendering."""
    episode_rewards = []
    episode_lengths = []
    episode_distances = []

    for ep in range(n_episodes):
        obs = env.reset()
        total_reward = 0
        steps = 0
        done = False

        while not done:
            action, _ = model.predict(obs, deterministic=True)
            obs, reward, done, info = env.step(action)
            total_reward += reward[0]
            steps += 1

            if done[0]:
                x_dist = info[0].get("x_position", 0)
                episode_rewards.append(total_reward)
                episode_lengths.append(steps)
                episode_distances.append(x_dist)

    print(f"\n--- Evaluation over {n_episodes} episodes ---")
    print(f"  Reward:   {np.mean(episode_rewards):.1f} +/- {np.std(episode_rewards):.1f}")
    print(f"  Length:   {np.mean(episode_lengths):.0f} +/- {np.std(episode_lengths):.0f}")
    print(f"  Distance: {np.mean(episode_distances):.3f}m +/- {np.std(episode_distances):.3f}m")
    print(f"  Avg speed: {np.mean(np.array(episode_distances) / (np.array(episode_lengths) * 0.05)):.3f} m/s")


def evaluate_visual(model, env, n_episodes=5):
    """Run episodes with MuJoCo viewer."""
    raw_env = env.envs[0] if hasattr(env, "envs") else env

    # Get the underlying SesameQuadrupedEnv
    unwrapped = raw_env
    while hasattr(unwrapped, "env"):
        unwrapped = unwrapped.env

    viewer = mujoco.viewer.launch_passive(unwrapped.model, unwrapped.data)

    for ep in range(n_episodes):
        obs = env.reset()
        total_reward = 0
        steps = 0
        done = False

        print(f"\nEpisode {ep + 1}/{n_episodes}")

        while not done and viewer.is_running():
            action, _ = model.predict(obs, deterministic=True)
            obs, reward, done, info = env.step(action)
            total_reward += reward[0]
            steps += 1

            viewer.sync()
            time.sleep(0.05)  # Real-time playback at 20 Hz

        x_dist = info[0].get("x_position", 0)
        print(f"  Reward: {total_reward:.1f} | Steps: {steps} | Distance: {x_dist:.3f}m")

        if not viewer.is_running():
            break

    viewer.close()


def evaluate_record(model, env, output_path, n_episodes=1):
    """Record episodes to video."""
    try:
        import cv2
    except ImportError:
        print("opencv-python required for recording: pip install opencv-python")
        return

    raw_env = env.envs[0] if hasattr(env, "envs") else env
    unwrapped = raw_env
    while hasattr(unwrapped, "env"):
        unwrapped = unwrapped.env

    renderer = mujoco.Renderer(unwrapped.model, height=720, width=1280)
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(output_path, fourcc, 20.0, (1280, 720))

    for ep in range(n_episodes):
        obs = env.reset()
        done = False
        steps = 0

        print(f"Recording episode {ep + 1}/{n_episodes}...")

        while not done:
            action, _ = model.predict(obs, deterministic=True)
            obs, reward, done, info = env.step(action)
            steps += 1

            renderer.update_scene(unwrapped.data, camera="tracking")
            frame = renderer.render()
            writer.write(cv2.cvtColor(frame, cv2.COLOR_RGB2BGR))

    writer.release()
    renderer.close()
    print(f"Saved video to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Evaluate trained Sesame policy")
    parser.add_argument("--model", type=str, default=None,
                        help="Path to model .zip file")
    parser.add_argument("--episodes", type=int, default=5,
                        help="Number of evaluation episodes")
    parser.add_argument("--no-render", action="store_true",
                        help="Headless evaluation (stats only)")
    parser.add_argument("--record", type=str, default=None,
                        help="Record to video file (e.g. video.mp4)")
    args = parser.parse_args()

    # Find model
    model_path = args.model or find_best_model()
    print(f"Loading model: {model_path}")

    # Create environment
    env = DummyVecEnv([lambda: SesameQuadrupedEnv()])

    # Load normalization stats if available
    norm_path = model_path.replace(".zip", "_vecnormalize.pkl")
    if not os.path.exists(norm_path):
        # Also check best model location
        norm_path = os.path.join(os.path.dirname(model_path), "..", "final_model_vecnormalize.pkl")

    if os.path.exists(norm_path):
        print(f"Loading normalization stats: {norm_path}")
        env = VecNormalize.load(norm_path, env)
        env.training = False
        env.norm_reward = False
    else:
        print("Warning: No normalization stats found. Results may differ from training.")
        env = VecNormalize(env, norm_obs=True, norm_reward=False, training=False)

    model = PPO.load(model_path, env=env)

    if args.record:
        evaluate_record(model, env, args.record, args.episodes)
    elif args.no_render:
        evaluate_headless(model, env, args.episodes)
    else:
        evaluate_visual(model, env, args.episodes)

    env.close()


if __name__ == "__main__":
    main()
