// SenseCAP Watcher USB-UART Bridge
// Bridges USB serial (UART0) to Nano serial (UART2 on GPIO 19/20)
// GPIO 20 = TX (out from Watcher to Nano D4), GPIO 19 = RX (in to Watcher from Nano D3)

#define WATCHER_UART_TX 20
#define WATCHER_UART_RX 19
#define NANO_BAUD 9600

HardwareSerial NanoSerial(2);

void setup() {
  Serial.begin(115200);  // USB to PC
  delay(500);
  NanoSerial.begin(NANO_BAUD, SERIAL_8N1, WATCHER_UART_RX, WATCHER_UART_TX);
  Serial.println("Bridge ready");
}

void loop() {
  while (Serial.available()) {
    NanoSerial.write(Serial.read());
  }
  while (NanoSerial.available()) {
    Serial.write(NanoSerial.read());
  }
}
