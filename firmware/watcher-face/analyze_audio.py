import wave
import struct
import sys

def analyze_wav(filename):
    try:
        wf = wave.open(filename, 'rb')
    except FileNotFoundError:
        print(f"File {filename} not found.")
        return

    print(f"Analyzing {filename}...")
    print(f"Channels: {wf.getnchannels()}")
    print(f"Sample Width: {wf.getsampwidth()} bytes")
    print(f"Frame Rate: {wf.getframerate()}")
    print(f"Total Frames: {wf.getnframes()}")

    raw_data = wf.readframes(100) # Read first 100 frames (200 bytes)
    wf.close()

    print("\nFirst 50 Samples (as 32-bit signed integers):")
    # Unpack as Little Endian 32-bit
    num_samples = len(raw_data) // 4
    samples = struct.unpack('<' + 'i' * num_samples, raw_data)
    
    for i in range(min(50, len(samples))):
        print(f"{samples[i]} ", end='')
    print("\n")
    
    print("First 40 Bytes (Hex):")
    for i in range(min(40, len(raw_data))):
        print(f"{raw_data[i]:02X} ", end='')
    print("\n")

if __name__ == "__main__":
    analyze_wav("output.wav")

