"""Walk forward, turn left, walk more, stop. Over WiFi TCP."""
import socket, time

# Nano pin order: [D4=L4, D5=L2, D6=L3, D7=L1, D8=R2, D9=R4, D10=R3, D11=R1]
# Index:            0      1      2      3      4      5      6      7

# Map ESP32 servo names to Nano indices
R1, R2, L1, L2, R4, R3, L3, L4 = 7, 4, 3, 1, 5, 6, 2, 0

FRAME_DELAY = 0.1  # 100ms between frames

def send(sock, angles_dict, current):
    """Update current angles with partial dict, send bulk command."""
    current.update(angles_dict)
    cmd = "a " + " ".join(str(current[i]) for i in range(8)) + "\n"
    sock.sendall(cmd.encode())
    time.sleep(FRAME_DELAY)

def stand(sock, current):
    send(sock, {R1: 135, R2: 45, L1: 45, L2: 135, R4: 0, R3: 180, L3: 0, L4: 180}, current)

def walk_forward(sock, current, cycles=3):
    # Initial step
    send(sock, {R3: 135, L3: 45, R2: 100, L1: 25}, current)

    for _ in range(cycles):
        send(sock, {R3: 135, L3: 0}, current)
        send(sock, {L4: 135, L2: 90, R4: 0, R1: 180}, current)
        send(sock, {R2: 45, L1: 90}, current)
        send(sock, {R4: 45, L4: 180}, current)
        send(sock, {R3: 180, L3: 45, R2: 90, L1: 0}, current)
        send(sock, {L2: 135, R1: 90}, current)

def turn_left(sock, current, cycles=3):
    for _ in range(cycles):
        send(sock, {R3: 135, L4: 135}, current)
        send(sock, {R1: 180, L2: 180}, current)
        send(sock, {R3: 180, L4: 180}, current)
        send(sock, {R1: 135, L2: 135}, current)
        send(sock, {R4: 45, L3: 45}, current)
        send(sock, {R2: 90, L1: 90}, current)
        send(sock, {R4: 0, L3: 0}, current)
        send(sock, {R2: 45, L1: 45}, current)

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect(('192.168.0.3', 8080))
    time.sleep(0.5)

    s.sendall(b'rl\n')
    time.sleep(0.5)

    # Track current angles
    current = {i: 90 for i in range(8)}

    print("Standing up...")
    stand(s, current)
    time.sleep(1)

    print("Walking forward...")
    walk_forward(s, current, cycles=4)

    print("Turning left...")
    turn_left(s, current, cycles=3)

    print("Walking forward again...")
    walk_forward(s, current, cycles=4)

    print("Standing...")
    stand(s, current)
    time.sleep(1)

    print("Stopping...")
    s.sendall(b'stop\n')
    time.sleep(0.5)
    s.close()
    print("Done!")

if __name__ == "__main__":
    main()
