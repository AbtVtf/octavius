"""
Train PPO with clock signal for periodic gait learning.
Usage: python train_clock.py --timesteps 10000000
"""

import argparse
import os
from stable_baselines3 import PPO
from stable_baselines3.common.vec_env import SubprocVecEnv, VecNormalize, VecMonitor
from stable_baselines3.common.callbacks import EvalCallback, CheckpointCallback

from sesame_env_clock import SesameQuadrupedClockEnv


def make_env(rank, seed=0):
    def _init():
        env = SesameQuadrupedClockEnv()
        env.reset(seed=seed + rank)
        return env
    return _init


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timesteps", type=int, default=10_000_000)
    parser.add_argument("--n-envs", type=int, default=16)
    parser.add_argument("--resume", type=str, default=None)
    args = parser.parse_args()

    os.makedirs("checkpoints_clock", exist_ok=True)
    os.makedirs("checkpoints_clock/best", exist_ok=True)

    env = SubprocVecEnv([make_env(i) for i in range(args.n_envs)])
    env = VecMonitor(env)

    if args.resume:
        env = VecNormalize.load(
            args.resume.replace(".zip", "_vecnormalize.pkl"), env
        )
        env.training = True
        model = PPO.load(args.resume, env=env, device="cpu")
        print(f"Resumed from {args.resume}")
    else:
        env = VecNormalize(env, norm_obs=True, norm_reward=True)
        model = PPO(
            "MlpPolicy",
            env,
            learning_rate=3e-4,
            n_steps=2048,
            batch_size=512,
            n_epochs=10,
            gamma=0.99,
            gae_lambda=0.95,
            clip_range=0.2,
            ent_coef=0.01,
            vf_coef=0.5,
            max_grad_norm=0.5,
            verbose=1,
            tensorboard_log="logs_clock/",
            device="cpu",
        )

    eval_env = SubprocVecEnv([make_env(100)])
    eval_env = VecMonitor(eval_env)
    eval_env = VecNormalize(eval_env, norm_obs=True, norm_reward=False, training=False)

    callbacks = [
        CheckpointCallback(
            save_freq=100_000 // args.n_envs,
            save_path="checkpoints_clock/",
            name_prefix="sesame_clock",
        ),
        EvalCallback(
            eval_env,
            best_model_save_path="checkpoints_clock/best/",
            eval_freq=50_000 // args.n_envs,
            n_eval_episodes=5,
            deterministic=True,
        ),
    ]

    print(f"Training for {args.timesteps} steps with clock signal...")
    model.learn(total_timesteps=args.timesteps, callback=callbacks, progress_bar=True)

    model.save("checkpoints_clock/final_model")
    env.save("checkpoints_clock/final_model_vecnormalize.pkl")
    print("Done!")


if __name__ == "__main__":
    main()
