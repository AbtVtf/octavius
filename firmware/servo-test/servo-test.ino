// servo-test.ino
// Sets all 8 servos on D5-D12 to 90 degrees

#include <Servo.h>

#define NUM_SERVOS 8
const uint8_t servoPins[NUM_SERVOS] = { 5, 6, 7, 8, 9, 10, 11, 12 };
Servo servos[NUM_SERVOS];

void setup() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(servoPins[i]);
    servos[i].write(90);
  }
}

void loop() {
}
