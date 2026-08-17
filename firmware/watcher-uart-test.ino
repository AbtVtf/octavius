#include <SoftwareSerial.h>

// D2 = RX from Watcher IO_20
// D3 = TX to Watcher IO_19 (through voltage divider)
SoftwareSerial watcherSerial(2, 3);

void setup() {
  Serial.begin(115200);
  watcherSerial.begin(115200);
  Serial.println("UART bridge ready. Anything from Watcher will show here.");
}

void loop() {
  // Watcher -> PC (show what Watcher sends)
  while (watcherSerial.available()) {
    char c = watcherSerial.read();
    Serial.write(c);
  }

  // PC -> Watcher (forward what we type)
  while (Serial.available()) {
    char c = Serial.read();
    watcherSerial.write(c);
  }
}
