/*
 * Sesame Robot - Watcher Assistant Firmware
 *
 * Features:
 * - QSPI display with animated face (SPD2010, 412x412)
 * - WiFi TCP server for receiving commands from PC
 * - UART bridge to Arduino Nano for servo control
 * - I2S audio capture (ES7243E mic) and playback (ES8311 speaker)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_display_panel.hpp>
// Audio and codec I2C deferred for later

using namespace esp_panel::drivers;

// ---- WiFi ----
const char* ssid = "TP-Link_C084";
const char* password = "22236371";
#define TCP_PORT 8080

// ---- Display pins (QSPI SPD2010) ----
#define LCD_CS    45
#define LCD_SCK   7
#define LCD_D0    9
#define LCD_D1    1
#define LCD_D2    14
#define LCD_D3    13
#define LCD_BL    8
#define LCD_W     412
#define LCD_H     412

// ---- I2S Audio pins ----
#define I2S_MCLK  10
#define I2S_BCLK  11
#define I2S_LRCK  12
#define I2S_DOUT  16  // to speaker (ES8311)
#define I2S_DIN   15  // from mic (ES7243E)

// ---- Codec I2C ----
#define CODEC_SDA 47
#define CODEC_SCL 48

// ---- UART to Nano ----
#define NANO_TX   20
#define NANO_RX   19
HardwareSerial NanoSerial(2);

// ---- TCP ----
WiFiServer server(TCP_PORT);
WiFiClient client;

// ---- Display ----
LCD *lcd = nullptr;
BacklightPWM_LEDC *backlight = nullptr;

// Face frame buffer (412 * 2 bytes per pixel = 824 bytes per line)
// We draw the face line by line to save RAM
#define FACE_SIZE 412
uint16_t lineBuffer[FACE_SIZE];

// Face state
enum FaceState { FACE_IDLE, FACE_HAPPY, FACE_TALK, FACE_SAD, FACE_ANGRY };
volatile FaceState currentFace = FACE_IDLE;
unsigned long lastBlinkMs = 0;
bool eyesClosed = false;
unsigned long nextBlinkTime = 0;

// ---- Color helpers (RGB565) ----
#define RGB565(r,g,b) ((((r)>>3)<<11) | (((g)>>2)<<5) | ((b)>>3))
#define COL_BG       RGB565(10, 10, 30)
#define COL_FACE     RGB565(255, 140, 66)
#define COL_EYE      RGB565(40, 40, 40)
#define COL_EYE_W    RGB565(255, 255, 255)
#define COL_MOUTH    RGB565(40, 40, 40)
#define COL_PUPIL    RGB565(20, 20, 20)

void drawCircleFilled(int cx, int cy, int r, uint16_t color);
void drawFace();

// Audio streaming
WiFiClient audioClient;
bool audioStreaming = false;

void setup() {
  Serial.begin(115200);

  // Connect WiFi first
  Serial.print("WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // UART to Nano (AFTER WiFi, GPIO 19/20 issue)
  NanoSerial.begin(115200, SERIAL_8N1, NANO_RX, NANO_TX);

  // Init display
  initDisplay();

  // Simple test: fill orange
  Serial.println("Drawing...");
  for (int y = 0; y < LCD_H; y++) {
    for (int x = 0; x < LCD_W; x++) lineBuffer[x] = COL_FACE;
    lcd->drawBitmap(0, y, LCD_W, 1, (const uint8_t*)lineBuffer);
    if (y % 50 == 0) yield();  // feed watchdog
  }
  Serial.println("Draw done");

  // TCP server
  server.begin();
  Serial.print("TCP port ");
  Serial.println(TCP_PORT);

  // Audio codec init deferred

  // Randomize blink
  randomSeed(analogRead(0));
  nextBlinkTime = millis() + random(2000, 5000);

  Serial.println("Ready");
}

void initDisplay() {
  // Backlight off during init
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LOW);

  // QSPI bus - use simple constructor
  BusQSPI *bus = new BusQSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
  bus->configQSPI_FreqHz(40000000);

  // LCD
  lcd = new LCD_SPD2010(bus, LCD_W, LCD_H, 16, -1);

  // RGB order

  if (!lcd->begin()) {
    Serial.println("LCD init failed!");
    return;
  }
  Serial.println("LCD init OK");

  // Mirror/rotation if needed
  lcd->swapXY(false);
  lcd->mirrorX(false);
  lcd->mirrorY(false);
  lcd->displayOn();
  Serial.println("Display ON");

  // Color bar test from library
  lcd->colorBarTest();
  Serial.println("Color bar drawn");

  // Turn on backlight with LEDC PWM
  ledcAttach(LCD_BL, 5000, 8);  // 5kHz, 8-bit
  ledcWrite(LCD_BL, 255);       // full brightness
  Serial.println("Backlight ON");
}

// ---- Simple face drawing ----
// Draw face to the round 412x412 display
// Eyes are two white circles with dark pupils
// Mouth varies by expression

inline bool inCircle(int x, int y, int cx, int cy, int r) {
  int dx = x - cx, dy = y - cy;
  return (dx*dx + dy*dy) <= (r*r);
}

void drawFace() {
  int centerX = LCD_W / 2;
  int centerY = LCD_H / 2;

  // Eye parameters
  int eyeY = centerY - 30;
  int eyeSpacing = 60;
  int eyeW = 30;  // eye radius
  int eyeH = eyesClosed ? 3 : 25;
  int pupilR = 10;

  // Mouth parameters
  int mouthY = centerY + 50;
  int mouthW = 40;
  int mouthH = 8;

  switch (currentFace) {
    case FACE_HAPPY:
      mouthH = 15;  // bigger smile
      break;
    case FACE_TALK:
      mouthH = 20;  // open mouth
      mouthW = 30;
      break;
    case FACE_SAD:
      eyeY = centerY - 20;
      mouthH = 5;
      break;
    case FACE_ANGRY:
      eyeY = centerY - 35;
      mouthW = 50;
      mouthH = 5;
      break;
    default:
      break;
  }

  // Draw line by line
  for (int y = 0; y < LCD_H; y++) {
    for (int x = 0; x < LCD_W; x++) {
      uint16_t col = COL_BG;

      // Check if inside display circle (round display)
      if (inCircle(x, y, centerX, centerY, 200)) {
        col = COL_FACE;

        // Left eye
        int lex = centerX - eyeSpacing;
        if (!eyesClosed) {
          if (inCircle(x, y, lex, eyeY, eyeW)) {
            col = COL_EYE_W;
            if (inCircle(x, y, lex, eyeY, pupilR))
              col = COL_PUPIL;
          }
        } else {
          // Closed eye = horizontal line
          if (abs(y - eyeY) <= 2 && abs(x - lex) <= eyeW)
            col = COL_EYE;
        }

        // Right eye
        int rex = centerX + eyeSpacing;
        if (!eyesClosed) {
          if (inCircle(x, y, rex, eyeY, eyeW)) {
            col = COL_EYE_W;
            if (inCircle(x, y, rex, eyeY, pupilR))
              col = COL_PUPIL;
          }
        } else {
          if (abs(y - eyeY) <= 2 && abs(x - rex) <= eyeW)
            col = COL_EYE;
        }

        // Mouth
        if (currentFace == FACE_HAPPY || currentFace == FACE_IDLE) {
          // Smile arc
          int my = mouthY + (int)(((float)(x - centerX) * (x - centerX)) / (mouthW * 8));
          if (abs(y - my) <= mouthH/2 && abs(x - centerX) <= mouthW)
            col = COL_MOUTH;
        } else if (currentFace == FACE_TALK) {
          // Open oval
          if (inCircle(x, y, centerX, mouthY, mouthW) &&
              !inCircle(x, y, centerX, mouthY, mouthW - mouthH))
            col = COL_MOUTH;
          if (inCircle(x, y, centerX, mouthY, mouthW - mouthH))
            col = RGB565(80, 30, 30);  // inside mouth
        } else if (currentFace == FACE_SAD) {
          // Frown
          int my = mouthY - (int)(((float)(x - centerX) * (x - centerX)) / (mouthW * 8));
          if (abs(y - my) <= mouthH/2 && abs(x - centerX) <= mouthW)
            col = COL_MOUTH;
        } else {
          // Straight line
          if (abs(y - mouthY) <= mouthH/2 && abs(x - centerX) <= mouthW)
            col = COL_MOUTH;
        }
      }

      lineBuffer[x] = col;
    }
    lcd->drawBitmap(0, y, LCD_W, 1, (const uint8_t*)lineBuffer);
  }
}

void loop() {
  // Accept TCP clients
  WiFiClient newClient = server.available();
  if (newClient) {
    if (client) client.stop();
    client = newClient;
  }

  // TCP -> Nano
  if (client && client.connected()) {
    while (client.available()) {
      // Read a full line
      String line = client.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        // Check for face commands
        if (line.startsWith("face ")) {
          String face = line.substring(5);
          if (face == "idle") currentFace = FACE_IDLE;
          else if (face == "happy") currentFace = FACE_HAPPY;
          else if (face == "talk") currentFace = FACE_TALK;
          else if (face == "sad") currentFace = FACE_SAD;
          else if (face == "angry") currentFace = FACE_ANGRY;
          drawFace();
        } else {
          // Forward to Nano
          NanoSerial.println(line);
        }
      }
    }
  } else if (client) {
    client.stop();
  }

  // USB Serial -> Nano
  while (Serial.available()) {
    NanoSerial.write(Serial.read());
  }

  // Nano -> TCP + USB
  while (NanoSerial.available()) {
    char c = NanoSerial.read();
    Serial.write(c);
    if (client && client.connected()) client.write(c);
  }

  // Blink animation
  if (millis() > nextBlinkTime) {
    eyesClosed = true;
    drawFace();
    delay(150);
    eyesClosed = false;
    drawFace();
    nextBlinkTime = millis() + random(2000, 6000);
  }
}
