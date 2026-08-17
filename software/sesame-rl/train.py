"""
Train a PPO policy to walk the Sesame quadruped in MuJoCo simulation.

Usage:
    python train.py                         # Default: 5M steps, 16 envs
    python train.py --timesteps 10000000    # More training
    python train.py --n-envs 8              # Fewer parallel envs
    python train.py --resume checkpoints/best_model.zip  # Resume training
"""

import argparse
import os
import sys

import numpy as np
from stable_baselines3 import PPO
from stable_baselines3.common.vec_env import SubprocVecEnv, VecNormalize, VecMonitor
from stable_baselines3.common.callbacks import (
    EvalCallback,
    CheckpointCallback,
    BaseCallback,
)

from sesame_env import SesameQuadrupedEnv


class ForwardDistanceCallback(BaseCallback):
    """Log forward distance at episode end."""

    def __init__(self, verbose=0):
        super().__init__(verbose)

    def _on_step(self):
        infos = self.locals.get("infos", [])
        for info in infos:
            if "x_position" in info:
                self.logger.record("rollout/x_position", info["x_position"])
                self.logger.record("rollout/forward_vel", info["forward_vel"])
        return True


def make_env(rank, seed=0):
    def _init():
        env = SesameQuadrupedEnv()
        env.reset(seed=seed + rank)
        return env

    return _init


def main():
    parser = argparse.ArgumentParser(description="Train Sesame walking policy")
    parser.add_argument("--timesteps", type=int, default=5_000_000,
                        help="Total training timesteps")
    parser.add_argument("--n-envs", type=int, default=16,
                        help="Number of parallel environments")
    parser.add_argument("--resume", type=str, default=None,
                        help="Path to model checkpoint to resume from")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed")
    parser.add_argument("--eval-freq", type=int, default=50_000,
                        help="Evaluate every N timesteps")
    parser.add_argument("--log-dir", type=str, default="logs",
                        help="TensorBoard log directory")
    parser.add_argument("--checkpoint-dir", type=str, default="checkpoints",
                        help="Model checkpoint directory")
    args = parser.parse_args()

    os.makedirs(args.log_dir, exist_ok=True)
    os.makedirs(args.checkpoint_dir, exist_ok=True)

    print(f"Creating {args.n_envs} parallel environments...")

    # Training environments
    train_env = SubprocVecEnv(
        [make_env(i, seed=args.seed) for i in range(args.n_envs)]
    )
    train_env = VecMonitor(train_env)
    train_env = VecNormalize(
        train_env,
        norm_obs=True,
        norm_reward=True,
        clip_obs=10.0,
        clip_reward=10.0,
    )

    # Evaluation environment (single, not normalized during eval)
    eval_env = SubprocVecEnv([make_env(100, seed=args.seed + 100)])
    eval_env = VecMonitor(eval_env)
    eval_env = VecNormalize(
        eval_env,
        norm_obs=True,
        norm_reward=False,
        clip_obs=10.0,
    )

    # PPO hyperparameters tuned for quadruped locomotion
    policy_kwargs = dict(
        net_arch=dict(pi=[256, 256], vf=[256, 256]),
    )

    if args.resume:
        print(f"Resuming from {args.resume}")
        model = PPO.load(
            args.resume,
            env=train_env,
            tensorboard_log=args.log_dir,
        )
        # Load normalization stats if available
        norm_path = args.resume.replace(".zip", "_vecnormalize.pkl")
        if os.path.exists(norm_path):
            train_env = VecNormalize.load(norm_path, train_env)
            print(f"Loaded normalization stats from {norm_path}")
    else:
        model = PPO(
            "MlpPolicy",
            train_env,
            learning_rate=3e-4,
            n_steps=2048,
            batch_size=64,
            n_epochs=10,
            gamma=0.99,
            gae_lambda=0.95,
            clip_range=0.2,
            ent_coef=0.01,
            vf_coef=0.5,
            max_grad_norm=0.5,
            policy_kwargs=policy_kwargs,
            verbose=1,
            tensorboard_log=args.log_dir,
            seed=args.seed,
            device="auto",  # will use CUDA if available
        )

    # Callbacks
    eval_callback = EvalCallback(
        eval_env,
        best_model_save_path=os.path.join(args.checkpoint_dir, "best"),
        log_path=os.path.join(args.log_dir, "eval"),
        eval_freq=max(args.eval_freq // args.n_envs, 1),
        n_eval_episodes=10,
        deterministic=True,
    )

    checkpoint_callback = CheckpointCallback(
        save_freq=max(100_000 // args.n_envs, 1),
        save_path=args.checkpoint_dir,
        name_prefix="sesame_ppo",
    )

    distance_callback = ForwardDistanceCallback()

    print(f"Training for {args.timesteps:,} timesteps...")
    print(f"Logging to: {args.log_dir}")
    print(f"Checkpoints: {args.checkpoint_dir}")
    print(f"Monitor with: tensorboard --logdir {args.log_dir}")

    try:
        model.learn(
            total_timesteps=args.timesteps,
            callback=[eval_callback, checkpoint_callback, distance_callback],
            progress_bar=True,
        )
    except KeyboardInterrupt:
        print("\nTraining interrupted. Saving...")

    # Save final model + normalization stats
    final_path = os.path.join(args.checkpoint_dir, "final_model")
    model.save(final_path)
    train_env.save(final_path + "_vecnormalize.pkl")
    print(f"Saved final model to {final_path}.zip")
    print(f"Saved normalization stats to {final_path}_vecnormalize.pkl")

    train_env.close()
    eval_env.close()


if __name__ == "__main__":
    main()
