"""
GC9A01 Round LCD Display Driver for Octavius Robot Face
-------------------------------------------------------
Drives a 240x240 round GC9A01 display over SPI on a Raspberry Pi.
Renders expressive robot face states using Pillow and pushes
RGB565 framebuffer data to the display.

SPI Pins:
    MOSI  -> GPIO 10
    SCLK  -> GPIO 11
    CS    -> GPIO 8  (CE0)
    DC    -> GPIO 25
    RST   -> GPIO 24
"""

import time
import struct
import spidev
import RPi.GPIO as GPIO
from PIL import Image, ImageDraw


# --- Pin definitions ---
PIN_DC = 25
PIN_RST = 24
PIN_CS = 8

# --- Display dimensions ---
WIDTH = 240
HEIGHT = 240

# --- GC9A01 Commands ---
CMD_SWRESET = 0x01
CMD_SLPOUT = 0x11
CMD_INVON = 0x21
CMD_DISPON = 0x29
CMD_CASET = 0x2A
CMD_RASET = 0x2B
CMD_RAMWR = 0x2C
CMD_MADCTL = 0x36
CMD_COLMOD = 0x3A

# SPI transfer chunk size (bytes). The kernel SPI buffer is typically 4096,
# but many drivers accept larger transfers when DMA is enabled. We use a
# conservative 4096 to stay safe across all Pi models.
_SPI_CHUNK = 4096


class RoundDisplay:
    """Driver for a GC9A01 240x240 round LCD connected via SPI."""

    # Available face expressions
    FACES = ("idle", "happy", "talk", "sad", "angry", "excited")

    def __init__(self, spi_bus: int = 0, spi_device: int = 0,
                 spi_speed_hz: int = 40_000_000, rotation: int = 0):
        """
        Initialise the GC9A01 display.

        Parameters
        ----------
        spi_bus : int
            SPI bus number (default 0).
        spi_device : int
            SPI chip-select index (default 0 = CE0 / GPIO 8).
        spi_speed_hz : int
            SPI clock speed in Hz (default 40 MHz).
        rotation : int
            Display rotation in degrees (0, 90, 180, 270).
        """
        self._rotation = rotation

        # -- GPIO setup --
        GPIO.setwarnings(False)
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(PIN_DC, GPIO.OUT)
        GPIO.setup(PIN_RST, GPIO.OUT)
        GPIO.setup(PIN_CS, GPIO.OUT)
        GPIO.output(PIN_CS, GPIO.HIGH)

        # -- SPI setup (manual CS control) --
        self._spi = spidev.SpiDev()
        self._spi.open(spi_bus, spi_device)
        self._spi.max_speed_hz = spi_speed_hz
        self._spi.mode = 0
        self._spi.no_cs = True

        # -- Initialise the display --
        self._init_display()

    # --------------------------------------------------------------------- #
    #  Low-level SPI helpers                                                 #
    # --------------------------------------------------------------------- #

    def _send_command(self, cmd: int, data: bytes | list | None = None):
        """Send a command byte, optionally followed by data bytes."""
        GPIO.output(PIN_CS, GPIO.LOW)
        GPIO.output(PIN_DC, GPIO.LOW)          # DC low  -> command
        self._spi.writebytes([cmd])
        if data is not None:
            GPIO.output(PIN_DC, GPIO.HIGH)     # DC high -> data
            if isinstance(data, int):
                data = [data]
            self._spi.writebytes(list(data))
        GPIO.output(PIN_CS, GPIO.HIGH)

    def _send_data(self, data: bytes | bytearray | list):
        """Send data bytes (DC high). CS must be held low by caller."""
        GPIO.output(PIN_DC, GPIO.HIGH)
        # Send in chunks to avoid kernel SPI buffer overflow
        buf = bytes(data) if not isinstance(data, (bytes, bytearray)) else data
        offset = 0
        length = len(buf)
        while offset < length:
            end = min(offset + _SPI_CHUNK, length)
            self._spi.writebytes2(buf[offset:end])
            offset = end

    # --------------------------------------------------------------------- #
    #  GC9A01 initialisation sequence                                        #
    # --------------------------------------------------------------------- #

    def _init_display(self):
        """Run the full GC9A01 manufacturer initialisation sequence."""
        # Hardware reset
        GPIO.output(PIN_RST, GPIO.HIGH)
        time.sleep(0.05)
        GPIO.output(PIN_RST, GPIO.LOW)
        time.sleep(0.05)
        GPIO.output(PIN_RST, GPIO.HIGH)
        time.sleep(0.15)

        # Full manufacturer init sequence for GC9A01
        self._send_command(0xEF)
        self._send_command(0xEB, [0x14])
        self._send_command(0xFE)
        self._send_command(0xEF)
        self._send_command(0xEB, [0x14])
        self._send_command(0x84, [0x40])
        self._send_command(0x85, [0xFF])
        self._send_command(0x86, [0xFF])
        self._send_command(0x87, [0xFF])
        self._send_command(0x88, [0x0A])
        self._send_command(0x89, [0x21])
        self._send_command(0x8A, [0x00])
        self._send_command(0x8B, [0x80])
        self._send_command(0x8C, [0x01])
        self._send_command(0x8D, [0x01])
        self._send_command(0x8E, [0xFF])
        self._send_command(0x8F, [0xFF])
        self._send_command(0xB6, [0x00, 0x00])

        # Memory Access Control — rotation
        madctl = self._madctl_for_rotation(self._rotation)
        self._send_command(CMD_MADCTL, [madctl])

        # Pixel format: 16-bit RGB565
        self._send_command(CMD_COLMOD, [0x05])

        self._send_command(0x90, [0x08, 0x08, 0x08, 0x08])
        self._send_command(0xBD, [0x06])
        self._send_command(0xBC, [0x00])
        self._send_command(0xFF, [0x60, 0x01, 0x04])
        self._send_command(0xC3, [0x13])
        self._send_command(0xC4, [0x13])
        self._send_command(0xC9, [0x22])
        self._send_command(0xBE, [0x11])
        self._send_command(0xE1, [0x10, 0x0E])
        self._send_command(0xDF, [0x21, 0x0C, 0x02])
        self._send_command(0xF0, [0x45, 0x09, 0x08, 0x08, 0x26, 0x2A])
        self._send_command(0xF1, [0x43, 0x70, 0x72, 0x36, 0x37, 0x6F])
        self._send_command(0xF2, [0x45, 0x09, 0x08, 0x08, 0x26, 0x2A])
        self._send_command(0xF3, [0x43, 0x70, 0x72, 0x36, 0x37, 0x6F])
        self._send_command(0xED, [0x1B, 0x0B])
        self._send_command(0xAE, [0x77])
        self._send_command(0xCD, [0x63])
        self._send_command(0x70, [0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03])
        self._send_command(0xE8, [0x34])
        self._send_command(0x62, [0x18, 0x0D, 0x71, 0xED, 0x70, 0x70,
                                   0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70])
        self._send_command(0x63, [0x18, 0x11, 0x71, 0xF1, 0x70, 0x70,
                                   0x18, 0x13, 0x71, 0xF3, 0x70, 0x70])
        self._send_command(0x64, [0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07])
        self._send_command(0x66, [0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45,
                                   0x10, 0x00, 0x00, 0x00])
        self._send_command(0x67, [0x00, 0x3C, 0x00, 0x00, 0x00, 0x01,
                                   0x54, 0x10, 0x32, 0x98])
        self._send_command(0x74, [0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00])
        self._send_command(0x98, [0x3E, 0x07])
        self._send_command(0x35)
        self._send_command(0x21)   # Display inversion on

        # Sleep out + Display on
        self._send_command(CMD_SLPOUT)
        time.sleep(0.12)
        self._send_command(CMD_DISPON)
        time.sleep(0.02)

    @staticmethod
    def _madctl_for_rotation(degrees: int) -> int:
        """Return the MADCTL byte for a given rotation."""
        mapping = {
            0:   0x00,
            90:  0x60,
            180: 0xC0,
            270: 0xA0,
        }
        return mapping.get(degrees, 0x00)

    # --------------------------------------------------------------------- #
    #  Framebuffer helpers                                                   #
    # --------------------------------------------------------------------- #

    @staticmethod
    def _convert_to_rgb565(pil_image: Image.Image) -> bytes:
        """
        Convert a PIL RGB image to a bytes object of RGB565 pixel data
        (big-endian, as expected by the GC9A01).
        """
        img = pil_image.convert("RGB")
        pixels = img.tobytes()
        out = bytearray(WIDTH * HEIGHT * 2)
        idx = 0
        for i in range(0, len(pixels), 3):
            r = pixels[i]
            g = pixels[i + 1]
            b = pixels[i + 2]
            # RGB565: RRRRRGGGGGGBBBBB (big-endian)
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out[idx] = (rgb565 >> 8) & 0xFF
            out[idx + 1] = rgb565 & 0xFF
            idx += 2
        return bytes(out)

    def _set_window(self, x0: int = 0, y0: int = 0,
                    x1: int = WIDTH - 1, y1: int = HEIGHT - 1):
        """Set the active drawing window on the display."""
        self._send_command(CMD_CASET, [
            (x0 >> 8) & 0xFF, x0 & 0xFF,
            (x1 >> 8) & 0xFF, x1 & 0xFF,
        ])
        self._send_command(CMD_RASET, [
            (y0 >> 8) & 0xFF, y0 & 0xFF,
            (y1 >> 8) & 0xFF, y1 & 0xFF,
        ])

    # --------------------------------------------------------------------- #
    #  Public API                                                            #
    # --------------------------------------------------------------------- #

    def show_image(self, pil_image: Image.Image):
        """
        Push any 240x240 PIL image to the display.

        The image will be resized and converted as needed.
        """
        img = pil_image.resize((WIDTH, HEIGHT)).convert("RGB")
        buf = self._convert_to_rgb565(img)
        self._set_window()
        # Start RAM write with CS held low for the entire data transfer
        GPIO.output(PIN_CS, GPIO.LOW)
        GPIO.output(PIN_DC, GPIO.LOW)
        self._spi.writebytes([CMD_RAMWR])
        self._send_data(buf)
        GPIO.output(PIN_CS, GPIO.HIGH)

    def clear(self):
        """Fill the display with black."""
        self._set_window()
        GPIO.output(PIN_CS, GPIO.LOW)
        GPIO.output(PIN_DC, GPIO.LOW)
        self._spi.writebytes([CMD_RAMWR])
        self._send_data(bytes(WIDTH * HEIGHT * 2))
        GPIO.output(PIN_CS, GPIO.HIGH)

    def set_face(self, face: str):
        """
        Render and display a face expression.

        Parameters
        ----------
        face : str
            One of 'idle', 'happy', 'talk', 'sad', 'angry', 'excited'.
        """
        face = face.lower().strip()
        render_fn = {
            "idle":    self._render_idle,
            "happy":   self._render_happy,
            "talk":    self._render_talk,
            "sad":     self._render_sad,
            "angry":   self._render_angry,
            "excited": self._render_excited,
        }.get(face)

        if render_fn is None:
            raise ValueError(
                f"Unknown face '{face}'. Choose from: {', '.join(self.FACES)}"
            )

        img = render_fn()
        self.show_image(img)

    def cleanup(self):
        """Release SPI and GPIO resources."""
        try:
            self.clear()
        except Exception:
            pass
        try:
            self._spi.close()
        except Exception:
            pass
        GPIO.cleanup()

    # --------------------------------------------------------------------- #
    #  Face rendering                                                        #
    # --------------------------------------------------------------------- #
    #                                                                        #
    #  All faces are drawn on a 240x240 black canvas.  Because the display   #
    #  is round, visible content should stay within the central ~200x200     #
    #  circle.  Eye positions are referenced from the display centre (120,   #
    #  120) so they remain centred.                                          #
    # --------------------------------------------------------------------- #

    # Shared layout constants
    _CX = WIDTH // 2           # centre X = 120
    _CY = HEIGHT // 2          # centre Y = 120
    _EYE_SPREAD = 42           # half the distance between eye centres
    _LEFT_EYE_X = _CX - _EYE_SPREAD   # 78
    _RIGHT_EYE_X = _CX + _EYE_SPREAD  # 162
    _EYE_Y = _CY - 10         # slightly above centre = 110

    @staticmethod
    def _new_canvas() -> tuple[Image.Image, ImageDraw.ImageDraw]:
        img = Image.new("RGB", (WIDTH, HEIGHT), (0, 0, 0))
        draw = ImageDraw.Draw(img)
        return img, draw

    # -- idle: neutral half-circle eyes --
    def _render_idle(self) -> Image.Image:
        img, draw = self._new_canvas()
        ey = self._EYE_Y
        r = 22  # eye radius

        for ex in (self._LEFT_EYE_X, self._RIGHT_EYE_X):
            # Draw full white circle then cover the bottom half with black
            # to create a half-circle (flat bottom) eye.
            draw.ellipse(
                [ex - r, ey - r, ex + r, ey + r],
                fill="white",
            )
            # Cover bottom half
            draw.rectangle(
                [ex - r - 1, ey, ex + r + 1, ey + r + 1],
                fill="black",
            )

        return img

    # -- happy: squinted upside-down crescent eyes --
    def _render_happy(self) -> Image.Image:
        img, draw = self._new_canvas()
        ey = self._EYE_Y
        r = 24

        for ex in (self._LEFT_EYE_X, self._RIGHT_EYE_X):
            # Upside-down crescent: draw the lower arc of a circle.
            # Full circle, then erase the top portion with a slightly
            # offset black circle to leave a crescent.
            draw.ellipse(
                [ex - r, ey - r, ex + r, ey + r],
                fill="white",
            )
            # Erase the top part with a large overlapping black ellipse
            # shifted upward, leaving only the bottom arc visible.
            draw.ellipse(
                [ex - r - 2, ey - r - 14, ex + r + 2, ey + 4],
                fill="black",
            )

        return img

    # -- talk: round eyes + open mouth --
    def _render_talk(self) -> Image.Image:
        img, draw = self._new_canvas()
        ey = self._EYE_Y - 8  # shift eyes up a bit to make room for mouth
        r = 20

        # Round eyes
        for ex in (self._LEFT_EYE_X, self._RIGHT_EYE_X):
            draw.ellipse(
                [ex - r, ey - r, ex + r, ey + r],
                fill="white",
            )

        # Open mouth — a white oval below the eyes
        mouth_cx = self._CX
        mouth_cy = self._CY + 35
        mw, mh = 18, 14
        draw.ellipse(
            [mouth_cx - mw, mouth_cy - mh, mouth_cx + mw, mouth_cy + mh],
            fill="white",
        )

        return img

    # -- sad: droopy tilted eyes --
    def _render_sad(self) -> Image.Image:
        img, draw = self._new_canvas()
        ey = self._EYE_Y
        rw = 24  # horizontal radius
        rh = 16  # vertical radius (flatter oval)

        for side, ex in (("left", self._LEFT_EYE_X),
                         ("right", self._RIGHT_EYE_X)):
            # Draw an oval eye
            draw.ellipse(
                [ex - rw, ey - rh, ex + rw, ey + rh],
                fill="white",
            )
            # Add a droopy eyelid: cover the outer-upper portion
            # with a black triangle to create the sad tilt.
            if side == "left":
                # Droop outer (left) corner down -> cover upper-left
                draw.polygon(
                    [
                        (ex - rw - 2, ey - rh - 2),
                        (ex + rw + 2, ey - rh - 2),
                        (ex - rw - 2, ey + 4),
                    ],
                    fill="black",
                )
            else:
                # Droop outer (right) corner down -> cover upper-right
                draw.polygon(
                    [
                        (ex - rw - 2, ey - rh - 2),
                        (ex + rw + 2, ey - rh - 2),
                        (ex + rw + 2, ey + 4),
                    ],
                    fill="black",
                )

        return img

    # -- angry: narrow red horizontal slits --
    def _render_angry(self) -> Image.Image:
        img, draw = self._new_canvas()
        ey = self._EYE_Y
        rw = 28   # wide
        rh = 6    # very narrow

        for side, ex in (("left", self._LEFT_EYE_X),
                         ("right", self._RIGHT_EYE_X)):
            # Draw the narrow red slit
            draw.ellipse(
                [ex - rw, ey - rh, ex + rw, ey + rh],
                fill=(255, 40, 40),
            )
            # Add an angry brow: cover the top with a tilted rectangle
            # so the inner edge is lower, creating a V-shaped brow.
            if side == "left":
                # Inner corner is right side, push brow down there
                draw.polygon(
                    [
                        (ex - rw - 2, ey - rh - 12),
                        (ex + rw + 2, ey - rh - 2),
                        (ex + rw + 2, ey - 1),
                        (ex - rw - 2, ey - rh - 2),
                    ],
                    fill="black",
                )
            else:
                draw.polygon(
                    [
                        (ex - rw - 2, ey - rh - 2),
                        (ex + rw + 2, ey - rh - 12),
                        (ex + rw + 2, ey - rh - 2),
                        (ex - rw - 2, ey - 1),
                    ],
                    fill="black",
                )

        return img

    # -- excited: large round eyes --
    def _render_excited(self) -> Image.Image:
        img, draw = self._new_canvas()
        ey = self._EYE_Y
        r = 32  # large radius for a wide-eyed look

        for ex in (self._LEFT_EYE_X, self._RIGHT_EYE_X):
            draw.ellipse(
                [ex - r, ey - r, ex + r, ey + r],
                fill="white",
            )
            # Add a small pupil to sell the "wide-eyed" effect
            pr = 10
            draw.ellipse(
                [ex - pr, ey - pr, ex + pr, ey + pr],
                fill="black",
            )
            # Tiny highlight dot in upper-right of pupil
            hx = ex + 4
            hy = ey - 4
            hr = 3
            draw.ellipse(
                [hx - hr, hy - hr, hx + hr, hy + hr],
                fill="white",
            )

        return img


# --------------------------------------------------------------------- #
#  Convenience: run directly to cycle through all faces                  #
# --------------------------------------------------------------------- #

if __name__ == "__main__":
    display = RoundDisplay()
    try:
        for face_name in RoundDisplay.FACES:
            print(f"Showing face: {face_name}")
            display.set_face(face_name)
            time.sleep(2)
        print("Clearing display.")
        display.clear()
    except KeyboardInterrupt:
        pass
    finally:
        display.cleanup()
