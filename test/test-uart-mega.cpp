
#include <Arduino.h>

#define LED_COUNT 20
#define MODE_SWITCH_PIN 12

const uint8_t buttonPins[LED_COUNT] = {
  2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
  22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

const uint8_t ledPins[LED_COUNT] = {
  36, 37, 38, 39, 35, 41, 42, 43, 44, 45,
  A0, A1, A2, A3, A4, A5, A6, A7, A8, A9
};

uint8_t buttonState[LED_COUNT];
uint8_t lastButtonState[LED_COUNT];
uint8_t ledState[LED_COUNT];

unsigned long lastDebounceTime[LED_COUNT];
const unsigned long debounceDelay = 50;

uint8_t manualModeState = HIGH;
int lastModeState = -1;

char rxBuffer[64];
uint8_t rxIndex = 0;

bool dataChanged = false;

unsigned long lastSend = 0;
const unsigned long heartbeatInterval = 1000;

// ============================================
// KIRIM STATUS
// ============================================

void sendData() {

  Serial3.print("DATA,");

  for (int i = 0; i < LED_COUNT; i++) {
    Serial3.print(ledState[i]);
  }

  Serial3.print(",");

  for (int i = 0; i < LED_COUNT; i++) {
    Serial3.print(buttonState[i]);
  }

  Serial3.print(",");
  Serial3.println(manualModeState);

  Serial3.flush();

  Serial.println("[TX] DATA sent");
}

// ============================================
// PERINTAH ESP32
// ============================================

void processCommand(char *cmd) {

  if (strcmp(cmd, "GET") == 0) {
    sendData();
    return;
  }

  int channel, state;

  if (sscanf(cmd, "LED,%d,%d", &channel, &state) == 2) {

    if (channel >= 1 && channel <= LED_COUNT) {

      int i = channel - 1;

      ledState[i] = state ? HIGH : LOW;
      digitalWrite(ledPins[i], ledState[i]);

      Serial3.print("ACK,LED,");
      Serial3.print(channel);
      Serial3.print(",");
      Serial3.println(ledState[i]);

      dataChanged = true;
    }
  }
}

// ============================================
// UART RX
// ============================================

void receiveSerial() {

  while (Serial3.available()) {

    char c = Serial3.read();

    if (c == '\n') {

      rxBuffer[rxIndex] = '\0';

      if (rxIndex > 0) {
        Serial.print("[RX] ");
        Serial.println(rxBuffer);
        processCommand(rxBuffer);
      }

      rxIndex = 0;
    }
    else if (c != '\r') {

      if (rxIndex < sizeof(rxBuffer) - 1) {
        rxBuffer[rxIndex++] = c;
      } else {
        rxIndex = 0;
      }
    }
  }
}

// ============================================
// SETUP
// ============================================

void setup() {

  Serial.begin(115200);
  Serial3.begin(9600);

  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);

  for (int i = 0; i < LED_COUNT; i++) {

    pinMode(buttonPins[i], INPUT_PULLUP);
    pinMode(ledPins[i], OUTPUT);

    buttonState[i] = HIGH;
    lastButtonState[i] = HIGH;
    ledState[i] = LOW;

    digitalWrite(ledPins[i], LOW);

    lastDebounceTime[i] = 0;
  }

  Serial.println("MEGA UART SLAVE READY");

  sendData();
}

// ============================================
// LOOP
// ============================================

void loop() {

  unsigned long now = millis();

  receiveSerial();

  // Mode
  int modeReading = digitalRead(MODE_SWITCH_PIN);

  if (modeReading != lastModeState) {

    lastModeState = modeReading;
    manualModeState = modeReading;

    dataChanged = true;

    Serial.print("[MODE] ");
    Serial.println(manualModeState == HIGH ? "AUTO" : "MANUAL");
  }

  // Tombol
  for (int i = 0; i < LED_COUNT; i++) {

    int reading = digitalRead(buttonPins[i]);

    if (reading != lastButtonState[i]) {
      lastDebounceTime[i] = now;
    }

    if ((now - lastDebounceTime[i]) > debounceDelay) {

      if (reading != buttonState[i]) {

        buttonState[i] = reading;

        if (buttonState[i] == LOW) {

          ledState[i] = !ledState[i];

          digitalWrite(
            ledPins[i],
            ledState[i] ? HIGH : LOW
          );

          dataChanged = true;

          Serial.print("[BUTTON] CH");
          Serial.print(i + 1);
          Serial.print(" -> ");
          Serial.println(ledState[i] ? "ON" : "OFF");
        }
      }
    }

    lastButtonState[i] = reading;
  }

  // Kirim saat ada perubahan + heartbeat berkala
  if (dataChanged || (now - lastSend) >= heartbeatInterval) {
    sendData();
    dataChanged = false;
    lastSend = now;
  }
}
