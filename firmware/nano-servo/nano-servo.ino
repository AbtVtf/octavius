// nano-servo.ino
// Arduino Nano servo driver for Sesame Robot
// Hybrid: angle commands + on-board animations
// Protocol (newline-terminated):
//   L1:45,R2:90,L3:135   — set specific servos
//   ALL:90                — set all servos to same angle
//   B a0 a1 ... a7       — bulk set 8 angles in pin order D5-D12
//   DETACH                — detach all servos
//   stand / rest / walk / back / left / right / wave / dance
// Accepts commands from USB serial and SoftwareSerial (Watcher on D4/D3)

#include <Servo.h>
#include <SoftwareSerial.h>

#define WATCHER_RX 4
#define WATCHER_TX 3
SoftwareSerial watcherSerial(WATCHER_RX, WATCHER_TX);

#define NUM_SERVOS 8
#define AUTO_DETACH_MS 2000

const uint8_t servoPins[NUM_SERVOS] = { 5, 6, 7, 8, 9, 10, 11, 12 };

#define S_L1 0   // D5
#define S_L4 1   // D6
#define S_R2 2   // D7
#define S_R4 3   // D8
#define S_R3 4   // D9
#define S_L3 5   // D10
#define S_R1 6   // D11
#define S_L2 7   // D12

Servo servos[NUM_SERVOS];
bool attached = false;
unsigned long lastCmdMs = 0;

char cmdBuf[64];
uint8_t cmdIdx = 0;

int8_t legIndex(char side, char num) {
  if (side == 'L' || side == 'l') {
    switch (num) {
      case '1': return S_L1;
      case '2': return S_L2;
      case '3': return S_L3;
      case '4': return S_L4;
    }
  } else if (side == 'R' || side == 'r') {
    switch (num) {
      case '1': return S_R1;
      case '2': return S_R2;
      case '3': return S_R3;
      case '4': return S_R4;
    }
  }
  return -1;
}

void attachAll() {
  if (!attached) {
    for (int i = 0; i < NUM_SERVOS; i++) {
      servos[i].attach(servoPins[i]);
    }
    attached = true;
  }
}

void detachAll() {
  if (attached) {
    for (int i = 0; i < NUM_SERVOS; i++) {
      servos[i].detach();
    }
    attached = false;
  }
}

// ====== ON-BOARD ANIMATIONS ======

void standPose() {
  //       L1  L4  R2  R4  R3   L3  R1   L2
  int a[] = {45, 180, 45, 0, 180, 0, 135, 135};
  for (int i = 0; i < NUM_SERVOS; i++) servos[i].write(a[i]);
}

void restPose() {
  // Slowly interpolate from current position to 90
  int current[NUM_SERVOS];
  for (int i = 0; i < NUM_SERVOS; i++) {
    current[i] = servos[i].read();
  }
  for (int step = 1; step <= 20; step++) {
    float t = step / 20.0;
    for (int i = 0; i < NUM_SERVOS; i++) {
      servos[i].write(current[i] + (int)((90 - current[i]) * t));
    }
    delay(40);
  }
}

void walkForward() {
  // Ported from original sesame-firmware movement-sequences.h
  standPose();
  delay(200);

  // Initial step
  servos[S_R3].write(135); servos[S_L3].write(45);
  servos[S_R2].write(100); servos[S_L1].write(25);
  delay(100);

  for (int c = 0; c < 10; c++) {
    servos[S_R3].write(135); servos[S_L3].write(0);
    delay(100);
    servos[S_L4].write(135); servos[S_L2].write(90);
    servos[S_R4].write(0);   servos[S_R1].write(180);
    delay(100);
    servos[S_R2].write(45);  servos[S_L1].write(90);
    delay(100);
    servos[S_R4].write(45);  servos[S_L4].write(180);
    delay(100);
    servos[S_R3].write(180); servos[S_L3].write(45);
    servos[S_R2].write(90);  servos[S_L1].write(0);
    delay(100);
    servos[S_L2].write(135); servos[S_R1].write(90);
    delay(100);
  }
  standPose();
}

void walkBackward() {
  // Ported from original sesame-firmware movement-sequences.h
  standPose();
  delay(200);

  for (int c = 0; c < 10; c++) {
    servos[S_R3].write(135); servos[S_L3].write(0);
    delay(100);
    servos[S_L4].write(135); servos[S_L2].write(135);
    servos[S_R4].write(0);   servos[S_R1].write(90);
    delay(100);
    servos[S_R2].write(90);  servos[S_L1].write(0);
    delay(100);
    servos[S_R4].write(45);  servos[S_L4].write(180);
    delay(100);
    servos[S_R3].write(180); servos[S_L3].write(45);
    servos[S_R2].write(45);  servos[S_L1].write(90);
    delay(100);
    servos[S_L2].write(90);  servos[S_R1].write(180);
    delay(100);
  }
  standPose();
}

void turnLeft() {
  // Ported from original sesame-firmware movement-sequences.h
  standPose();
  delay(200);

  for (int c = 0; c < 10; c++) {
    // Legset 1 (R1 L2)
    servos[S_R3].write(135); servos[S_L4].write(135);
    delay(100);
    servos[S_R1].write(180); servos[S_L2].write(180);
    delay(100);
    servos[S_R3].write(180); servos[S_L4].write(180);
    delay(100);
    servos[S_R1].write(135); servos[S_L2].write(135);
    delay(100);
    // Legset 2 (R2 L1)
    servos[S_R4].write(45);  servos[S_L3].write(45);
    delay(100);
    servos[S_R2].write(90);  servos[S_L1].write(90);
    delay(100);
    servos[S_R4].write(0);   servos[S_L3].write(0);
    delay(100);
    servos[S_R2].write(45);  servos[S_L1].write(45);
    delay(100);
  }
  standPose();
}

void turnRight() {
  // Ported from original sesame-firmware movement-sequences.h
  standPose();
  delay(200);

  for (int c = 0; c < 10; c++) {
    // Legset 2 (R2 L1)
    servos[S_R4].write(45);  servos[S_L3].write(45);
    delay(100);
    servos[S_R2].write(0);   servos[S_L1].write(0);
    delay(100);
    servos[S_R4].write(0);   servos[S_L3].write(0);
    delay(100);
    servos[S_R2].write(45);  servos[S_L1].write(45);
    delay(100);
    // Legset 1 (R1 L2)
    servos[S_R3].write(135); servos[S_L4].write(135);
    delay(100);
    servos[S_R1].write(90);  servos[S_L2].write(90);
    delay(100);
    servos[S_R3].write(180); servos[S_L4].write(180);
    delay(100);
    servos[S_R1].write(135); servos[S_L2].write(135);
    delay(100);
  }
  standPose();
}

void wavePose() {
  standPose();
  delay(200);
  // Shift weight: R4 up, hips lean right (from original)
  servos[S_R4].write(80);
  servos[S_L2].write(60);
  servos[S_R1].write(100);
  delay(300);
  // Lift L3 fully up
  servos[S_L3].write(180);
  delay(300);
  // Wave L3 back and forth 4 times
  for (int i = 0; i < 4; i++) {
    servos[S_L3].write(180);
    delay(300);
    servos[S_L3].write(100);
    delay(300);
  }
  standPose();
}

void dancePose() {
  // Ported from original
  servos[S_R1].write(90);  servos[S_R2].write(90);
  servos[S_L1].write(90);  servos[S_L2].write(90);
  servos[S_R4].write(160); servos[S_R3].write(160);
  servos[S_L3].write(10);  servos[S_L4].write(10);
  delay(300);
  for (int i = 0; i < 5; i++) {
    servos[S_R4].write(115); servos[S_R3].write(115);
    servos[S_L3].write(10);  servos[S_L4].write(10);
    delay(300);
    servos[S_R4].write(160); servos[S_R3].write(160);
    servos[S_L3].write(65);  servos[S_L4].write(65);
    delay(300);
  }
  standPose();
}

void swimPose() {
  for (int i = 0; i < NUM_SERVOS; i++) servos[i].write(90);
  for (int i = 0; i < 4; i++) {
    servos[S_R1].write(135); servos[S_R2].write(45);
    servos[S_L1].write(45);  servos[S_L2].write(135);
    delay(400);
    servos[S_R1].write(90);  servos[S_R2].write(90);
    servos[S_L1].write(90);  servos[S_L2].write(90);
    delay(400);
  }
  standPose();
}

void pointPose() {
  servos[S_L2].write(60);   servos[S_R1].write(135);
  servos[S_R2].write(100);  servos[S_L4].write(180);
  servos[S_L1].write(25);   servos[S_L3].write(145);
  servos[S_R4].write(80);   servos[S_R3].write(170);
  delay(2000);
  standPose();
}

void pushupPose() {
  standPose();
  delay(200);
  servos[S_L1].write(0);    servos[S_R1].write(180);
  servos[S_L3].write(90);   servos[S_R3].write(90);
  delay(500);
  for (int i = 0; i < 4; i++) {
    servos[S_L3].write(0);   servos[S_R3].write(180);
    delay(600);
    servos[S_L3].write(90);  servos[S_R3].write(90);
    delay(500);
  }
  standPose();
}

void bowPose() {
  standPose();
  delay(200);
  servos[S_L1].write(0);    servos[S_R1].write(180);
  servos[S_L3].write(0);    servos[S_R3].write(180);
  servos[S_L2].write(180);  servos[S_R2].write(0);
  servos[S_R4].write(0);    servos[S_L4].write(180);
  delay(600);
  servos[S_L3].write(90);   servos[S_R3].write(90);
  delay(3000);
  standPose();
}

void cutePose() {
  standPose();
  delay(200);
  servos[S_L2].write(160);  servos[S_R2].write(20);
  servos[S_R4].write(180);  servos[S_L4].write(0);
  servos[S_L1].write(0);    servos[S_R1].write(180);
  servos[S_L3].write(180);  servos[S_R3].write(0);
  delay(200);
  for (int i = 0; i < 5; i++) {
    servos[S_R4].write(180); servos[S_L4].write(45);
    delay(300);
    servos[S_R4].write(135); servos[S_L4].write(0);
    delay(300);
  }
  standPose();
}

void freakyPose() {
  standPose();
  delay(200);
  servos[S_L1].write(0);    servos[S_R1].write(180);
  servos[S_L2].write(180);  servos[S_R2].write(0);
  servos[S_R4].write(90);   servos[S_R3].write(0);
  delay(200);
  for (int i = 0; i < 3; i++) {
    servos[S_R3].write(25);
    delay(400);
    servos[S_R3].write(0);
    delay(400);
  }
  standPose();
}

void wormPose() {
  standPose();
  delay(200);
  servos[S_R1].write(180);  servos[S_R2].write(0);
  servos[S_L1].write(0);    servos[S_L2].write(180);
  servos[S_R4].write(90);   servos[S_R3].write(90);
  servos[S_L3].write(90);   servos[S_L4].write(90);
  delay(200);
  for (int i = 0; i < 5; i++) {
    servos[S_R3].write(45);  servos[S_L3].write(135);
    servos[S_R4].write(45);  servos[S_L4].write(135);
    delay(300);
    servos[S_R3].write(135); servos[S_L3].write(45);
    servos[S_R4].write(135); servos[S_L4].write(45);
    delay(300);
  }
  standPose();
}

void shakePose() {
  standPose();
  delay(200);
  servos[S_R1].write(135);  servos[S_L1].write(45);
  servos[S_L3].write(90);   servos[S_R3].write(90);
  servos[S_L2].write(90);   servos[S_R2].write(90);
  delay(200);
  for (int i = 0; i < 5; i++) {
    servos[S_R4].write(45);  servos[S_L4].write(135);
    delay(300);
    servos[S_R4].write(0);   servos[S_L4].write(180);
    delay(300);
  }
  standPose();
}

void shrugPose() {
  standPose();
  delay(200);
  servos[S_R3].write(90);   servos[S_R4].write(90);
  servos[S_L3].write(90);   servos[S_L4].write(90);
  delay(1000);
  servos[S_R3].write(0);    servos[S_R4].write(180);
  servos[S_L3].write(180);  servos[S_L4].write(0);
  delay(1500);
  standPose();
}

void deadPose() {
  standPose();
  delay(200);
  servos[S_R3].write(90);   servos[S_R4].write(90);
  servos[S_L3].write(90);   servos[S_L4].write(90);
  // Stay dead — no standPose, auto-detach will handle it
}

void crabPose() {
  standPose();
  delay(200);
  servos[S_R1].write(90);   servos[S_R2].write(90);
  servos[S_L1].write(90);   servos[S_L2].write(90);
  servos[S_R4].write(0);    servos[S_R3].write(180);
  servos[S_L3].write(45);   servos[S_L4].write(135);
  for (int i = 0; i < 5; i++) {
    servos[S_R4].write(45);  servos[S_R3].write(135);
    servos[S_L3].write(0);   servos[S_L4].write(180);
    delay(300);
    servos[S_R4].write(0);   servos[S_R3].write(180);
    servos[S_L3].write(45);  servos[S_L4].write(135);
    delay(300);
  }
  standPose();
}

// ====== COMMAND HANDLER ======

void handleCommand(const char* cmd) {
  if (strcmp(cmd, "DETACH") == 0 || strcmp(cmd, "detach") == 0) {
    detachAll();
    return;
  }

  // On-board animations
  if (strcmp(cmd, "stand") == 0)  { attachAll(); standPose();    lastCmdMs = millis(); return; }
  if (strcmp(cmd, "rest") == 0 || strcmp(cmd, "stop") == 0) { attachAll(); restPose(); lastCmdMs = millis(); return; }
  if (strcmp(cmd, "walk") == 0 || strcmp(cmd, "forward") == 0) { attachAll(); walkForward(); lastCmdMs = millis(); return; }
  if (strcmp(cmd, "back") == 0 || strcmp(cmd, "backward") == 0) { attachAll(); walkBackward(); lastCmdMs = millis(); return; }
  if (strcmp(cmd, "left") == 0)  { attachAll(); turnLeft();     lastCmdMs = millis(); return; }
  if (strcmp(cmd, "right") == 0) { attachAll(); turnRight();    lastCmdMs = millis(); return; }
  if (strcmp(cmd, "wave") == 0)  { attachAll(); wavePose();     lastCmdMs = millis(); return; }
  if (strcmp(cmd, "dance") == 0) { attachAll(); dancePose();    lastCmdMs = millis(); return; }
  if (strcmp(cmd, "swim") == 0)  { attachAll(); swimPose();     lastCmdMs = millis(); return; }
  if (strcmp(cmd, "point") == 0) { attachAll(); pointPose();    lastCmdMs = millis(); return; }
  if (strcmp(cmd, "pushup") == 0){ attachAll(); pushupPose();   lastCmdMs = millis(); return; }
  if (strcmp(cmd, "bow") == 0)   { attachAll(); bowPose();      lastCmdMs = millis(); return; }
  if (strcmp(cmd, "cute") == 0)  { attachAll(); cutePose();     lastCmdMs = millis(); return; }
  if (strcmp(cmd, "freaky") == 0){ attachAll(); freakyPose();   lastCmdMs = millis(); return; }
  if (strcmp(cmd, "worm") == 0)  { attachAll(); wormPose();     lastCmdMs = millis(); return; }
  if (strcmp(cmd, "shake") == 0) { attachAll(); shakePose();    lastCmdMs = millis(); return; }
  if (strcmp(cmd, "shrug") == 0) { attachAll(); shrugPose();    lastCmdMs = millis(); return; }
  if (strcmp(cmd, "dead") == 0)  { attachAll(); deadPose();     lastCmdMs = millis(); return; }
  if (strcmp(cmd, "crab") == 0)  { attachAll(); crabPose();     lastCmdMs = millis(); return; }

  // Bulk command: "B a0 a1 a2 a3 a4 a5 a6 a7"
  if (cmd[0] == 'B' && cmd[1] == ' ') {
    attachAll();
    const char* p = cmd + 2;
    for (int i = 0; i < NUM_SERVOS && *p; i++) {
      int angle = atoi(p);
      if (angle >= 0 && angle <= 180) servos[i].write(angle);
      while (*p && *p != ' ') p++;
      if (*p == ' ') p++;
    }
    lastCmdMs = millis();
    return;
  }

  // ALL:angle
  if ((cmd[0] == 'A' || cmd[0] == 'a') &&
      (cmd[1] == 'L' || cmd[1] == 'l') &&
      (cmd[2] == 'L' || cmd[2] == 'l') && cmd[3] == ':') {
    int angle = atoi(cmd + 4);
    if (angle >= 0 && angle <= 180) {
      attachAll();
      for (int i = 0; i < NUM_SERVOS; i++) {
        servos[i].write(angle);
      }
      lastCmdMs = millis();
    }
    return;
  }

  // Parse comma-separated pairs: L1:45,R2:90,...
  attachAll();
  const char* p = cmd;
  while (*p) {
    char side = *p++;
    if (!*p) break;
    char num = *p++;
    if (*p != ':') break;
    p++;
    int angle = atoi(p);
    while (*p >= '0' && *p <= '9') p++;
    if (*p == ',') p++;

    int8_t idx = legIndex(side, num);
    if (idx >= 0 && angle >= 0 && angle <= 180) {
      servos[idx].write(angle);
    }
  }
  lastCmdMs = millis();
}

void processChar(char c) {
  if (c == '\n' || c == '\r') {
    if (cmdIdx > 0) {
      cmdBuf[cmdIdx] = '\0';
      handleCommand(cmdBuf);
      cmdIdx = 0;
    }
  } else {
    if (cmdIdx < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdIdx++] = c;
    }
  }
}

void setup() {
  Serial.begin(9600);
  watcherSerial.begin(9600);

  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(servoPins[i]);
    servos[i].write(90);
  }
  delay(500);
  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].detach();
  }
  attached = false;

  cmdIdx = 0;
  cmdBuf[0] = '\0';
}

void loop() {
  while (Serial.available()) {
    processChar(Serial.read());
  }

  while (watcherSerial.available()) {
    processChar(watcherSerial.read());
  }

  if (attached && (millis() - lastCmdMs > AUTO_DETACH_MS)) {
    detachAll();
  }
}
