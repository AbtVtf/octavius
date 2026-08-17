# Flashing Guide — SenseCAP Watcher & Arduino Nano

## Hardware Overview

- **SenseCAP Watcher**: ESP32-S3 with CH342 USB-UART chip. Shows up as two serial ports: `/dev/ttyACM0` (Himax AI chip) and `/dev/ttyACM1` (ESP32-S3 — this is the one we flash).
- **Arduino Nano**: ATmega328P clone with CH340 USB-serial chip. Shows up as `/dev/ttyUSB0`.

---

## Arduino Nano

### Prerequisites

- `arduino-cli` installed with `arduino:avr` core
- Board: clone Nano with old bootloader

### FQBN

```
arduino:avr:nano:cpu=atmega328old
```

### Compile

```bash
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old firmware/nano-servo/nano-servo.ino
```

### Upload

```bash
arduino-cli upload --fqbn arduino:avr:nano:cpu=atmega328old --port /dev/ttyUSB0 firmware/nano-servo/nano-servo.ino
```

### Troubleshooting

**"programmer is not responding" / sync errors:**

1. **Check for zombie avrdude processes**: `ps aux | grep avrdude` — kill any stuck ones
2. **Check port**: `ls /dev/ttyUSB*` — the port number can change after unplug/replug
3. **Try the 1200 baud trick**: `stty -F /dev/ttyUSB0 1200 hupcl && sleep 0.5 && arduino-cli upload ...`
4. **Disconnect Watcher UART**: If the Watcher is connected to D4 (purple wire), it can interfere with upload. Disconnect it before flashing.
5. **Nothing else can hold the port**: Close Arduino IDE serial monitor, any other serial tools, etc.

**Port keeps changing (ttyUSB0 → ttyUSB1):**

This happens after unplug/replug. Just use whatever `ls /dev/ttyUSB*` shows.

---

## SenseCAP Watcher (ESP32-S3)

### Prerequisites

- PlatformIO installed (via the venv in yoyogochi project)
- The firmware source lives in `firmware/watcher-face/src/main_face.c`
- Build happens from the yoyogochi project dir (symlinked source)

### Build

```bash
cd /home/mafuu/Documents/GitHub/yoyogochi/model-projects/my-app
/home/mafuu/Documents/GitHub/yoyogochi/venv/bin/pio run
```

### Flash via PlatformIO

```bash
/home/mafuu/Documents/GitHub/yoyogochi/venv/bin/pio run --target upload --upload-port /dev/ttyACM1
```

### Flash via esptool (alternative)

If PlatformIO upload fails with "port busy":

```bash
stty -F /dev/ttyACM1 115200 raw
/home/mafuu/.arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool \
  --port /dev/ttyACM1 --baud 115200 --before default-reset --chip esp32s3 \
  write-flash 0x0 .pio/build/esp32-s3-devkitc-1/firmware.merged.bin
```

### Critical Settings

- **FQBN equivalent**: `esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=32M,PSRAM=opi`
- **CDCOnBoot MUST be `default`**, NOT `cdc`. The Watcher uses an external CH342 USB-UART chip, not native USB CDC. With `cdc`, Serial goes to native USB which is not physically connected.
- **PSRAM must be OCTAL mode** (`CONFIG_SPIRAM_MODE_OCT=y`), not QUAD.

### WiFi Configuration

Edit `firmware/watcher-face/src/main_face.c`, lines 30-31:

```c
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"
```

Rebuild and reflash after changing.

### Troubleshooting

**Upload fails with "port busy":**

1. Run `stty -F /dev/ttyACM1 115200 raw` first, then retry
2. Stop ModemManager: `sudo systemctl stop ModemManager`
3. Use esptool directly (see above)

**Upload fails with "Packet content transfer stopped":**

The Watcher might be in a bad state (boot loop). Just retry the upload — it usually works on the second attempt.

**Face doesn't show / display blank:**

- Check that the build succeeded (look for `[SUCCESS]` in output)
- The init order matters: I2C → IO expander → wait 500ms → display → WiFi → audio → UART
- The IO expander must be initialized to power on the LCD

**WiFi won't connect:**

- Check SSID/password are correct (case-sensitive)
- The Watcher's WiFi init blocks until connected. If the network is unavailable, it will retry forever.
- Check serial logs: `timeout 10 cat /dev/ttyACM1` (at 115200 baud)

---

## USB Port Reference

| Port | Device | Chip |
|------|--------|------|
| `/dev/ttyACM0` | Himax HX6538 (AI chip) | — |
| `/dev/ttyACM1` | SenseCAP Watcher ESP32-S3 | CH342 |
| `/dev/ttyUSB0` | Arduino Nano | CH340 |

Ports can shift (e.g., `ttyUSB0` → `ttyUSB1`) after unplug/replug. Always check with `ls /dev/ttyACM* /dev/ttyUSB*`.

---

## UART Connection (Watcher ↔ Nano)

The Watcher communicates with the Nano over UART at 9600 baud:

- **Watcher GPIO 20 (TX)** → **Nano D4 (RX)** — purple wire, direct connection (3.3V is fine for Nano input)
- **Nano D3 (TX)** → **1K resistor → junction → 2K resistor → GND** — white wire, voltage divider (5V → 3.3V for Watcher input). Junction connects to **Watcher GPIO 19 (RX)**.
- **Nano GND** → **Watcher GND** — orange wire

**Important**: Disconnect the purple wire (D4) before flashing the Nano, or the Watcher's UART output may interfere with the upload.
