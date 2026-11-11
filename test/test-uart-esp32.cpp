
#include <Arduino.h>

#define RX_PIN 21
#define TX_PIN 22

#define LED_COUNT 20

HardwareSerial MySerial(1);

uint8_t slaveData[41];

unsigned long lastRequest = 0;
const unsigned long pollInterval = 200;

unsigned long lastRx = 0;
bool rxWarned = false;
const unsigned long rxTimeout = 1500;

void processResponse(String response) {

  response.trim();

  if (!response.startsWith("DATA,")) {
    Serial.println("[UNKNOWN] " + response);
    return;
  }

  if (response.length() < 48) {
    Serial.println("[ERROR] Data tidak lengkap");
    return;
  }

  // LED
  for (int i = 0; i < 20; i++) {
    slaveData[i] = response.charAt(5 + i) - '0';
  }

  // Tombol
  for (int i = 0; i < 20; i++) {
    slaveData[20 + i] = response.charAt(26 + i) - '0';
  }

  // Mode
  slaveData[40] = response.charAt(47) - '0';

  Serial.printf(
    "[DATA] MODE=%s BUTTON=",
    slaveData[40] == LOW ? "MANUAL" : "AUTO"
  );

  for (int i = 0; i < 20; i++) {
    Serial.print(slaveData[20 + i] == LOW ? '1' : '0');
  }

  Serial.print(" LED=");

  for (int i = 0; i < 20; i++) {
    Serial.print(slaveData[i]);
  }

  Serial.println();
}

void setLed(uint8_t channel, bool state) {

  if (channel < 1 || channel > 20) return;

  MySerial.print("LED,");
  MySerial.print(channel);
  MySerial.print(",");
  MySerial.println(state ? 1 : 0);
}

void setup() {

  Serial.begin(115200);

  MySerial.begin(
    9600,
    SERIAL_8N1,
    RX_PIN,
    TX_PIN
  );
  MySerial.setTimeout(50);

  Serial.println("ESP32 UART MASTER READY");
  Serial.println("RX: GPIO21  <- TX3 Mega (pin 14)");
  Serial.println("TX: GPIO22  -> RX3 Mega (pin 15)");
  Serial.println("GND kedua board harus tersambung");
}

void loop() {

  // Request berkala
  if (millis() - lastRequest >= pollInterval) {

    lastRequest = millis();

    MySerial.println("GET");
  }

  // Baca respons
  if (MySerial.available()) {

    String response = MySerial.readStringUntil('\n');
    response.trim();

    if (response.length() > 0) {

      lastRx = millis();
      rxWarned = false;

      Serial.print("[RX MEGA] ");
      Serial.println(response);

      processResponse(response);
    }
  }

  if (!rxWarned && (millis() - lastRx) > rxTimeout) {
    rxWarned = true;
    Serial.println("[TIMEOUT] Tidak ada data dari Mega. Cek kabel TX3(14)->GPIO21, GND, dan level 5V->3.3V.");
  }
}
