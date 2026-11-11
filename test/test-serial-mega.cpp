
#include <Arduino.h>

#define LED_PIN 36
#define BUTTON_PIN 2

void setup() {
  Serial.begin(115200);
  Serial3.begin(9600);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.println("==============================");
  Serial.println("ARDUINO MEGA SERIAL3");
  Serial.println("RX3: Pin 15");
  Serial.println("TX3: Pin 14");
  Serial.println("==============================");
}

void loop() {
  if (Serial3.available()) {
    String command = Serial3.readStringUntil('\n');
    command.trim();

    if (command.startsWith("CNT:")) {
      uint8_t counter = command.substring(4).toInt();

      digitalWrite(LED_PIN, counter % 2);

      Serial.print("[RX] Counter: ");
      Serial.println(counter);

      // Kirim respons ke ESP32
      Serial3.print("ACK:");
      Serial3.print(counter);
      Serial3.print(",");
      Serial3.print(digitalRead(BUTTON_PIN) == LOW ? 1 : 0);
      Serial3.print(",");
      Serial3.print(digitalRead(LED_PIN));
      Serial3.println(",1");

      Serial.println("[TX] Response sent");
    }
  }
}
