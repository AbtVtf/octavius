"""
Sim-to-real servo calibration for Sesame robot.

Goes joint by joint:
1. Sets all servos to stand pose
2. Moves one joint by +30 sim-degrees
3. You compare with the sim and say if the direction matches

Usage:
    python calibrate.py
    python calibrate.py --ip 192.168.0.2
"""

import socket
import time
import argparse
import numpy as np

# Sim actuator order from MuJoCo XML:
# [0]=R1(fr_hip), [1]=R3(fr_ankle), [2]=L1(fl_hip), [3]=L3(fl_ankle),
# [4]=R2(rr_hip), [5]=R4(rr_ankle), [6]=L2(rl_hip), [7]=L4(rl_ankle)
SIM_JOINT_NAMES = ["R1", "R3", "L1", "L3", "R2", "R4", "L2", "L4"]
SIM_JOINT_DESCRIPTIONS = [
    "R1 - front-right hip (swings leg fwd/back)",
    "R3 - front-right knee (lifts/lowers foot)",
    "L1 - front-left hip (swings leg fwd/back)",
    "L3 - front-left knee (lifts/lowers foot)",
    "R2 - rear-right hip (swings leg fwd/back)",
    "R4 - rear-right knee (lifts/lowers foot)",
    "L2 - rear-left hip (swings leg fwd/back)",
    "L4 - rear-left knee (lifts/lowers foot)",
]

# Known working stand pose servo angles (from tested firmware)
# These are the servo angles that make the robot stand
STAND_SERVO = {
    "R1": 135, "R2": 45, "L1": 45, "L2": 135,
    "R3": 180, "R4": 0,  "L3": 0,  "L4": 180,
}

# Initial guess for direction: +1 means sim+ = servo+, -1 means sim+ = servo-
# We'll verify these during calibration
SERVO_DIRECTION = {
    "R1": +1, "R2": -1, "L1": -1, "L2": +1,
    "R3": +1, "R4": -1, "L3": -1, "L4": +1,
}


def send_cmd(sock, cmd):
    sock.sendall((cmd + "\n").encode())


def set_stand(sock):
    parts = [f"{name}:{angle}" for name, angle in STAND_SERVO.items()]
    send_cmd(sock, ",".join(parts))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="192.168.0.2")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    sock = socket.socket()
    sock.settimeout(5)
    sock.connect((args.ip, args.port))
    print(f"Connected to {args.ip}:{args.port}")

    print("\n=== STEP 1: Verify stand pose ===")
    print("Setting all servos to stand angles...")
    set_stand(sock)
    time.sleep(2)

    input("\nDoes the robot look like it's standing? (press Enter to continue, Ctrl+C to abort) ")

    print("\n=== STEP 2: Test each joint direction ===")
    print("For each joint, I'll move it +30 degrees (sim convention).")
    print("Compare with the sim viewer and tell me if the direction matches.\n")

    test_delta = 30  # degrees

    for i, name in enumerate(SIM_JOINT_NAMES):
        desc = SIM_JOINT_DESCRIPTIONS[i]
        direction = SERVO_DIRECTION[name]
        stand_angle = STAND_SERVO[name]
        test_angle = stand_angle + (direction * test_delta)
        test_angle = max(0, min(180, test_angle))

        print(f"\n--- Joint {i}: {desc} ---")
        print(f"  Stand angle: {stand_angle}°")
        print(f"  Test angle:  {test_angle}° (direction={'+'if direction>0 else '-'})")

        input(f"  Press Enter to move {name} to {test_angle}°...")
        send_cmd(sock, f"{name}:{test_angle}")
        time.sleep(1)

        resp = input(f"  Does {name} move in the SAME direction as sim? (y/n/skip): ").strip().lower()

        if resp == 'n':
            # Flip direction
            SERVO_DIRECTION[name] = -direction
            new_test = stand_angle + (-direction * test_delta)
            new_test = max(0, min(180, new_test))
            print(f"  Flipped! Testing {name} at {new_test}°...")
            send_cmd(sock, f"{name}:{new_test}")
            time.sleep(1)
            input("  Better? (press Enter to continue)")

        # Return to stand
        send_cmd(sock, f"{name}:{stand_angle}")
        time.sleep(0.5)

    print("\n=== CALIBRATION RESULTS ===")
    print(f"STAND_ANGLES (sim order {SIM_JOINT_NAMES}):")
    stand_arr = [STAND_SERVO[n] for n in SIM_JOINT_NAMES]
    print(f"  {stand_arr}")
    print(f"SERVO_DIRECTION (sim order):")
    dir_arr = [SERVO_DIRECTION[n] for n in SIM_JOINT_NAMES]
    print(f"  {dir_arr}")

    print("\n--- Copy-paste for deploy_tcp.py ---")
    print(f'SIM_JOINT_NAMES = {SIM_JOINT_NAMES}')
    print(f'STAND_ANGLES = np.array({stand_arr}, dtype=np.float64)')
    print(f'SERVO_DIRECTION = np.array({dir_arr}, dtype=np.float64)')

    send_cmd(sock, "DETACH")
    sock.close()
    print("\nDone!")


if __name__ == "__main__":
    main()
