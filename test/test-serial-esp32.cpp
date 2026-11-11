
#include <Arduino.h>

#define RX_PIN 21
#define TX_PIN 22

HardwareSerial MySerial(1);

uint8_t counter = 0;

void setup() {
  Serial.begin(115200);

  MySerial.begin(
    9600,
    SERIAL_8N1,
    RX_PIN,
    TX_PIN
  );

  Serial.println("==============================");
  Serial.println("ESP32 UART MASTER");
  Serial.println("UART1 RX: GPIO21");
  Serial.println("UART1 TX: GPIO22");
  Serial.println("==============================");
}

void loop() {
  static uint32_t lastSend = 0;

  if (millis() - lastSend >= 1000) {
    lastSend = millis();

    MySerial.print("CNT:");
    MySerial.println(counter);

    Serial.print("[TX] Counter: ");
    Serial.println(counter);

    counter++;
  }

  if (MySerial.available()) {
    String response = MySerial.readStringUntil('\n');
    response.trim();

    if (response.length() > 0) {
      Serial.print("[RX] Mega: ");
      Serial.println(response);

      if (response.startsWith("ACK:")) {
        Serial.println("[SUCCESS] TWO WAY COMMUNICATION");
      }
    }
  }
}
