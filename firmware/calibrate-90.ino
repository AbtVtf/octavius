// Sesame Servo Calibration - All servos to 90 degrees
// Upload this BEFORE attaching joints to servo shafts.
//
// Custom pinout:
//   R1=GPIO18, R2=GPIO19, L1=GPIO17, L2=GPIO16
//   R3=GPIO26, R4=GPIO27, L3=GPIO25, L4=GPIO33
//
// Servo order in array: {R1, R2, L1, L2, R4, R3, L3, L4}
// (matches firmware enum order)

#include <ESP32Servo.h>

Servo servos[8];

// Your pinout: Index order is {R1, R2, L1, L2, R4, R3, L3, L4}
const int servoPins[8] = {18, 19, 17, 16, 27, 26, 25, 33};
const char* servoNames[8] = {"R1", "R2", "L1", "L2", "R4", "R3", "L3", "L4"};

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Sesame Servo Calibration ===");
  Serial.println("Setting all servos to 90 degrees...\n");

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (int i = 0; i < 8; i++) {
    servos[i].setPeriodHertz(50);
    servos[i].attach(servoPins[i], 732, 2929);
    delay(50);
  }

  // Set all to 90 degrees with staggered timing to prevent brownout
  for (int i = 0; i < 8; i++) {
    servos[i].write(90);
    Serial.printf("  %s (GPIO %d) -> 90 deg\n", servoNames[i], servoPins[i]);
    delay(100);
  }

  Serial.println("\nAll servos at 90 degrees!");
  Serial.println("You can now attach the joints to the servo shafts.");
  Serial.println("\nCommands via Serial Monitor:");
  Serial.println("  'all <angle>'     - set all servos to angle (0-180)");
  Serial.println("  '<index> <angle>' - set servo by index (0-7) to angle");
  Serial.println("  'sweep'           - sweep all servos 45-135 slowly");
  Serial.println("  '90'              - reset all to 90");
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input == "90") {
      for (int i = 0; i < 8; i++) {
        servos[i].write(90);
        delay(50);
      }
      Serial.println("All servos -> 90 deg");
    }
    else if (input.startsWith("all ")) {
      int angle = input.substring(4).toInt();
      angle = constrain(angle, 0, 180);
      for (int i = 0; i < 8; i++) {
        servos[i].write(angle);
        delay(50);
      }
      Serial.printf("All servos -> %d deg\n", angle);
    }
    else if (input == "sweep") {
      Serial.println("Sweeping 45 -> 135 -> 90...");
      for (int a = 90; a <= 135; a += 5) {
        for (int i = 0; i < 8; i++) servos[i].write(a);
        delay(200);
      }
      for (int a = 135; a >= 45; a -= 5) {
        for (int i = 0; i < 8; i++) servos[i].write(a);
        delay(200);
      }
      for (int a = 45; a <= 90; a += 5) {
        for (int i = 0; i < 8; i++) servos[i].write(a);
        delay(200);
      }
      Serial.println("Sweep done, back to 90");
    }
    else {
      // Parse "<index> <angle>"
      int spaceIdx = input.indexOf(' ');
      if (spaceIdx > 0) {
        int idx = input.substring(0, spaceIdx).toInt();
        int angle = input.substring(spaceIdx + 1).toInt();
        if (idx >= 0 && idx < 8) {
          angle = constrain(angle, 0, 180);
          servos[idx].write(angle);
          Serial.printf("%s (index %d) -> %d deg\n", servoNames[idx], idx, angle);
        }
      }
    }
  }
}
