#!/usr/bin/env python3
"""
Video Stream Viewer

Connects to the SenseCAP Watcher video stream and displays it using OpenCV.

Usage:
    python view_stream.py [IP_ADDRESS]

If no IP address is provided, it defaults to 192.168.0.100
You can also just open http://<device-ip>/ in your browser.

Requirements:
    pip install opencv-python requests numpy
"""

import sys
import cv2
import numpy as np
import requests
from urllib.parse import urljoin

def view_stream(base_url):
    """View MJPEG stream from the device"""
    stream_url = urljoin(base_url, "/stream")
    print(f"Connecting to stream: {stream_url}")

    try:
        response = requests.get(stream_url, stream=True, timeout=10)
        response.raise_for_status()
    except requests.exceptions.RequestException as e:
        print(f"Failed to connect: {e}")
        return

    print("Connected! Press 'q' to quit, 's' to save a snapshot.")

    # Buffer to accumulate JPEG data
    buffer = bytes()

    cv2.namedWindow("Video Stream", cv2.WINDOW_NORMAL)
    frame_count = 0

    for chunk in response.iter_content(chunk_size=1024):
        buffer += chunk

        # Look for JPEG markers
        start = buffer.find(b'\xff\xd8')  # JPEG start
        end = buffer.find(b'\xff\xd9')    # JPEG end

        if start != -1 and end != -1 and end > start:
            # Extract JPEG data
            jpg_data = buffer[start:end+2]
            buffer = buffer[end+2:]

            # Decode JPEG
            np_arr = np.frombuffer(jpg_data, dtype=np.uint8)
            frame = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)

            if frame is not None:
                frame_count += 1

                # Add info overlay
                cv2.putText(frame, f"Frame: {frame_count}", (10, 30),
                           cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)

                cv2.imshow("Video Stream", frame)

                key = cv2.waitKey(1) & 0xFF
                if key == ord('q'):
                    break
                elif key == ord('s'):
                    filename = f"snapshot_{frame_count}.jpg"
                    cv2.imwrite(filename, frame)
                    print(f"Saved: {filename}")

    cv2.destroyAllWindows()
    print(f"Stream ended. Total frames: {frame_count}")


def get_snapshot(base_url):
    """Get a single snapshot from the device"""
    snapshot_url = urljoin(base_url, "/snapshot")
    print(f"Getting snapshot from: {snapshot_url}")

    try:
        response = requests.get(snapshot_url, timeout=10)
        response.raise_for_status()
    except requests.exceptions.RequestException as e:
        print(f"Failed to get snapshot: {e}")
        return None

    # Decode JPEG
    np_arr = np.frombuffer(response.content, dtype=np.uint8)
    frame = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)

    return frame


def main():
    # Get IP address from command line or use default
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    else:
        ip = "192.168.0.100"

    base_url = f"http://{ip}/"

    print("=" * 50)
    print("SenseCAP Watcher Video Stream Viewer")
    print("=" * 50)
    print(f"Device URL: {base_url}")
    print()
    print("Options:")
    print("  1. View live stream")
    print("  2. Get single snapshot")
    print("  3. Open in browser (just prints URL)")
    print()

    choice = input("Enter choice (1/2/3) or press Enter for stream: ").strip()

    if choice == "2":
        frame = get_snapshot(base_url)
        if frame is not None:
            filename = "snapshot.jpg"
            cv2.imwrite(filename, frame)
            print(f"Saved snapshot to: {filename}")

            cv2.imshow("Snapshot", frame)
            print("Press any key to close...")
            cv2.waitKey(0)
            cv2.destroyAllWindows()
    elif choice == "3":
        print(f"\nOpen this URL in your browser:")
        print(f"  {base_url}")
        print(f"\nOr directly view the stream:")
        print(f"  {base_url}stream")
    else:
        view_stream(base_url)


if __name__ == "__main__":
    main()
