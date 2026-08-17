import serial
import argparse
import wave
import struct

def main():
    parser = argparse.ArgumentParser(description="Stream audio from ESP32")
    parser.add_argument('--port', default='/dev/ttyACM1', help="Serial port")
    parser.add_argument('--baud', type=int, default=921600, help="Baud rate")
    parser.add_argument('--output', default='output.wav', help="Output WAV file")
    parser.add_argument('--swap', action='store_true', help="Swap bytes (Endianness fix)")
    parser.add_argument('--gain', type=float, default=1.0, help="Digital gain multiplier")
    parser.add_argument('--channels', type=int, default=1, help="Number of channels (1=Mono, 2=Stereo)")
    args = parser.parse_args()

    print(f"Connecting to {args.port} at {args.baud}...")
    try:
        ser = serial.Serial(args.port, args.baud)
    except Exception as e:
        print(f"Error opening serial port: {e}")
        return

    print("Connected! Scanning for audio packets...")
    if args.swap:
        print("Byte Swapping ENABLED.")
    if args.gain != 1.0:
        print(f"Digital Gain: x{args.gain}")
    
    print("Hold the device button to stream.")
    print("Press Ctrl+C to stop.")

    # WAV Settings
    sample_rate = 16000
    channels = args.channels
    width = 2 # 16-bit

    frames = []
    
    # Magic Header: DE AD BE EF
    HEADER = b'\xDE\xAD\xBE\xEF'

    try:
        buffer = b''
        while True:
            # Read available data
            if ser.in_waiting > 0:
                new_data = ser.read(ser.in_waiting)
                buffer += new_data
                
                # Look for headers
                while len(buffer) > 6: # Header (4) + Length (2)
                    # Find header index
                    start_idx = buffer.find(HEADER)
                    
                    if start_idx == -1:
                        # No header found, keep last 3 bytes just in case partial header
                        buffer = buffer[-3:]
                        break
                    
                    # If we have junk before the header, discard it
                    if start_idx > 0:
                        buffer = buffer[start_idx:]
                        
                    # Check if we have enough bytes for header + length
                    if len(buffer) < 6:
                        break
                        
                    # Parse Length
                    payload_len = struct.unpack('<H', buffer[4:6])[0]
                    
                    # Check if we have the full payload
                    total_packet_size = 6 + payload_len
                    if len(buffer) < total_packet_size:
                        break # Wait for more data
                        
                    # Extract Audio Data
                    audio_data = buffer[6:total_packet_size]
                    
                    # Handle Byte Swapping
                    if args.swap:
                        # Swap every 2 bytes
                        count = len(audio_data) // 2
                        shorts = struct.unpack('>' + 'h' * count, audio_data)
                        audio_data = struct.pack('<' + 'h' * count, *shorts)

                    # Handle Gain
                    if args.gain != 1.0:
                        count = len(audio_data) // 2
                        shorts = struct.unpack('<' + 'h' * count, audio_data)
                        boosted = []
                        for s in shorts:
                            val = int(s * args.gain)
                            if val > 32767: val = 32767
                            if val < -32768: val = -32768
                            boosted.append(val)
                        audio_data = struct.pack('<' + 'h' * count, *boosted)

                    frames.append(audio_data)
                    
                    # Print status occasionally
                    if len(frames) % 50 == 0:
                        print(f"\rCaptured {len(frames)} chunks...", end='', flush=True)
                        
                    # Remove processed packet from buffer
                    buffer = buffer[total_packet_size:]
                    
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        ser.close()

        # Save WAV file
        if frames:
            raw_bytes = b''.join(frames)
            print(f"\nCaptured {len(raw_bytes)} bytes total.")
            
            # Standard Stereo Processing
            total_samples = len(raw_bytes) // 2
            all_samples = struct.unpack('<' + 'h' * total_samples, raw_bytes)
            
            # Apply Gain to ALL samples
            if args.gain != 1.0:
                boosted = []
                for s in all_samples:
                    val = int(s * args.gain)
                    if val > 32767: val = 32767
                    if val < -32768: val = -32768
                    boosted.append(val)
                all_samples = boosted

            packed_bytes = struct.pack('<' + 'h' * len(all_samples), *all_samples)

            print(f"Saving to {args.output}...")
            wf = wave.open(args.output, 'wb')
            wf.setnchannels(channels)
            wf.setsampwidth(width)
            wf.setframerate(sample_rate)
            wf.writeframes(packed_bytes)
            wf.close()
            print("Done.")
        else:
            print("No audio captured.")

if __name__ == "__main__":
    main()