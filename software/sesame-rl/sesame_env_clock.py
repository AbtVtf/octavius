"""
Sesame Quadruped - MuJoCo Gymnasium Environment WITH CLOCK SIGNAL

Same as sesame_env.py but adds a phase clock (sin/cos) to the observation.
This helps the policy learn periodic gaits without sensor feedback.

Observation (37 dims):
    [0]       torso height (z)
    [1:5]     torso quaternion (w, x, y, z)
    [5:8]     torso linear velocity
    [8:11]    torso angular velocity
    [11:19]   joint positions (8 joints, radians)
    [19:27]   joint velocities (8 joints)
    [27:35]   previous action
    [35]      clock sin(phase)
    [36]      clock cos(phase)
"""

import os
import numpy as np
import gymnasium as gym
from gymnasium import spaces
import mujoco


SESAME_XML = os.path.join(os.path.dirname(__file__), "model", "sesame.xml")
MAX_ANGLE_RAD = np.deg2rad(60)
SERVO_MAX_SPEED_NORM = 0.25

# Clock frequency: one full gait cycle in ~0.5 seconds (10 steps at 20Hz)
CLOCK_FREQ_HZ = 2.0


class SesameQuadrupedClockEnv(gym.Env):
    metadata = {"render_modes": ["human", "rgb_array"], "render_fps": 20}

    def __init__(self, render_mode=None, frame_skip=25):
        self.model = mujoco.MjModel.from_xml_path(SESAME_XML)
        self.data = mujoco.MjData(self.model)
        self.frame_skip = frame_skip
        self.dt = self.model.opt.timestep * self.frame_skip

        # Observation: z(1) + quat(4) + lin_vel(3) + ang_vel(3) + jpos(8) + jvel(8) + prev_act(8) + clock(2)
        obs_dim = 1 + 4 + 3 + 3 + 8 + 8 + 8 + 2  # 37
        high = np.inf * np.ones(obs_dim, dtype=np.float32)
        self.observation_space = spaces.Box(-high, high, dtype=np.float32)

        self.action_space = spaces.Box(
            low=-1.0, high=1.0, shape=(8,), dtype=np.float32
        )

        self._prev_action = np.zeros(8, dtype=np.float32)
        self._step_count = 0
        self._max_episode_steps = 1000
        self._init_z = 0.095
        self._phase = 0.0

        self.render_mode = render_mode
        self._renderer = None
        self._viewer = None

    @property
    def sim_joint_pos(self):
        return self.data.qpos[7:15]

    @property
    def sim_joint_vel(self):
        return self.data.qvel[6:14]

    def _get_obs(self):
        qpos = self.data.qpos
        qvel = self.data.qvel

        z = np.array([qpos[2]], dtype=np.float32)
        quat = qpos[3:7].astype(np.float32)
        lin_vel = qvel[0:3].astype(np.float32)
        ang_vel = qvel[3:6].astype(np.float32)
        jpos = self.sim_joint_pos.astype(np.float32)
        jvel = self.sim_joint_vel.astype(np.float32)

        clock = np.array([
            np.sin(self._phase),
            np.cos(self._phase),
        ], dtype=np.float32)

        obs = np.concatenate([z, quat, lin_vel, ang_vel, jpos, jvel, self._prev_action, clock])

        if not np.all(np.isfinite(obs)):
            obs = np.nan_to_num(obs, nan=0.0, posinf=1e6, neginf=-1e6)

        return obs

    def _compute_reward(self, action):
        qvel = self.data.qvel

        v_forward = qvel[0]
        v_lateral = abs(qvel[1])
        yaw_rate = abs(qvel[5])
        energy = np.sum(np.square(action))
        action_rate = np.sum(np.square(action - self._prev_action))
        alive = 1.0

        reward = (
            1.5 * v_forward
            + 0.5 * alive
            - 0.1 * energy
            - 0.3 * v_lateral
            - 0.1 * yaw_rate
            - 0.5 * action_rate
        )

        return float(reward)

    def _is_terminated(self):
        z = self.data.qpos[2]
        if z < 0.03:
            return True
        quat = self.data.qpos[3:7]
        up_z = 1.0 - 2.0 * (quat[1] ** 2 + quat[2] ** 2)
        if up_z < 0.3:
            return True
        return False

    def step(self, action):
        action = np.clip(action, -1.0, 1.0).astype(np.float32)

        action_delta = action - self._prev_action
        action_delta = np.clip(action_delta, -SERVO_MAX_SPEED_NORM, SERVO_MAX_SPEED_NORM)
        action = self._prev_action + action_delta

        ctrl = action * MAX_ANGLE_RAD
        self.data.ctrl[:] = ctrl

        for _ in range(self.frame_skip):
            mujoco.mj_step(self.model, self.data)

        # Advance clock phase
        self._phase += 2.0 * np.pi * CLOCK_FREQ_HZ * self.dt
        if self._phase > 2.0 * np.pi:
            self._phase -= 2.0 * np.pi

        obs = self._get_obs()
        reward = self._compute_reward(action)
        terminated = self._is_terminated()

        self._step_count += 1
        truncated = self._step_count >= self._max_episode_steps

        self._prev_action = action.copy()

        info = {
            "x_position": float(self.data.qpos[0]),
            "y_position": float(self.data.qpos[1]),
            "z_height": float(self.data.qpos[2]),
            "forward_vel": float(self.data.qvel[0]),
        }

        if self.render_mode == "human":
            self.render()

        return obs, reward, terminated, truncated, info

    def reset(self, seed=None, options=None):
        super().reset(seed=seed)

        mujoco.mj_resetData(self.model, self.data)

        key_id = mujoco.mj_name2id(self.model, mujoco.mjtObj.mjOBJ_KEY, "home")
        if key_id >= 0:
            self.data.qpos[:] = self.model.key_qpos[key_id]

        noise = self.np_random.uniform(-0.005, 0.005, size=self.data.qpos.shape)
        noise[3:7] = 0
        self.data.qpos[:] += noise
        self.data.qpos[2] = self._init_z
        self.data.qvel[:] = 0

        self._prev_action = np.zeros(8, dtype=np.float32)
        self._step_count = 0
        self._phase = 0.0

        mujoco.mj_forward(self.model, self.data)

        return self._get_obs(), {}

    def render(self):
        if self.render_mode == "rgb_array":
            if self._renderer is None:
                self._renderer = mujoco.Renderer(self.model, height=480, width=640)
            self._renderer.update_scene(self.data, camera="tracking")
            return self._renderer.render()
        elif self.render_mode == "human":
            if self._viewer is None:
                import mujoco.viewer
                self._viewer = mujoco.viewer.launch_passive(self.model, self.data)
            self._viewer.sync()

    def close(self):
        if self._renderer:
            self._renderer.close()
        if self._viewer:
            self._viewer.close()
