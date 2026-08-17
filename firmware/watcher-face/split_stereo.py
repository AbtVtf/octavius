import wave
import struct
import sys

def split_wav(filename):
    try:
        wf = wave.open(filename, 'rb')
    except FileNotFoundError:
        print("File not found.")
        return

    if wf.getnchannels() != 2:
        print("Input file is not stereo.")
        return

    raw_data = wf.readframes(wf.getnframes())
    wf.close()

    total_samples = len(raw_data) // 2 # 16-bit samples
    samples = struct.unpack('<' + 'h' * total_samples, raw_data)

    left = samples[0::2]
    right = samples[1::2]

    print("Saving output_left.wav...")
    wf_l = wave.open("output_left.wav", 'wb')
    wf_l.setnchannels(1)
    wf_l.setsampwidth(2)
    wf_l.setframerate(16000)
    wf_l.writeframes(struct.pack('<' + 'h' * len(left), *left))
    wf_l.close()

    print("Saving output_right.wav...")
    wf_r = wave.open("output_right.wav", 'wb')
    wf_r.setnchannels(1)
    wf_r.setsampwidth(2)
    wf_r.setframerate(16000)
    wf_r.writeframes(struct.pack('<' + 'h' * len(right), *right))
    wf_r.close()
    print("Done.")

if __name__ == "__main__":
    split_wav("output.wav")
