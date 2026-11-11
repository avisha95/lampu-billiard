#include <ModbusRTU.h>

// Harus sama dengan SLAVE_ID di sketch master.
// ID 4 dipakai controller utama; ID 1 bentrok dengan modul relay ID 1.
#define SLAVE_ID 4

const int NUM_CHANNELS = 20;
const int MODE_SWITCH_PIN = 12;

// Tombol (INPUT_PULLUP)
const int buttonPins[NUM_CHANNELS] = {
  2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
  22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

// LED
const int ledPins[NUM_CHANNELS] = {
  36, 37, 38, 39, 35, 41, 42, 43, 44, 45,
  A0, A1, A2, A3, A4, A5, A6, A7, A8, A9
};

int buttonState[NUM_CHANNELS];
int lastButtonState[NUM_CHANNELS];
int ledState[NUM_CHANNELS];
unsigned long lastDebounceTime[NUM_CHANNELS];
const unsigned long debounceDelay = 50;

int manualModeState = HIGH;
int lastModeState = -1;

ModbusRTU mb;

// Register:
// 0-19  : LED (0 mati, 1 nyala)
// 20-39 : tombol (0 ditekan, 1 dilepas)
// 40    : mode (0 manual [LOW], 1 otomatis [HIGH])
const int REG_LED_START = 0;
const int REG_BTN_START = 20;
const int REG_MODE_MANUAL = 40;

void setup() {
  Serial.begin(9600);
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);

  for (int i = 0; i < NUM_CHANNELS; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    pinMode(ledPins[i], OUTPUT);

    buttonState[i] = HIGH;
    lastButtonState[i] = HIGH;
    ledState[i] = LOW;
    digitalWrite(ledPins[i], ledState[i]);
    lastDebounceTime[i] = 0;
  }

  // Modbus RTU slave pada Serial1 (TX18 / RX19), tanpa DE/RE
  Serial1.begin(9600, SERIAL_8N1);
  mb.begin(&Serial1);
  mb.setBaudrate(9600);
  mb.slave(SLAVE_ID);

  mb.addHreg(REG_LED_START, 0, NUM_CHANNELS);
  mb.addHreg(REG_BTN_START, 1, NUM_CHANNELS);
  mb.addHreg(REG_MODE_MANUAL, 1, 1);

  Serial.print("Arduino Mega Modbus Slave ID ");
  Serial.print(SLAVE_ID);
  Serial.println(" (Panel 20 Tombol / 20 LED) Siap.");
}

void loop() {
  unsigned long currentTime = millis();

  mb.task();

  int modeReading = digitalRead(MODE_SWITCH_PIN);
  if (modeReading != lastModeState) {
    lastModeState = modeReading;
    manualModeState = modeReading;
    Serial.print("Mode: ");
    Serial.println(manualModeState == HIGH ? "OTOMATIS" : "MANUAL");
  }
  mb.Hreg(REG_MODE_MANUAL, manualModeState);

  for (int i = 0; i < NUM_CHANNELS; i++) {
    int targetLedState = mb.Hreg(REG_LED_START + i);
    if (targetLedState != ledState[i]) {
      ledState[i] = targetLedState;
      digitalWrite(ledPins[i], ledState[i] ? HIGH : LOW);
    }
  }

  for (int i = 0; i < NUM_CHANNELS; i++) {
    int reading = digitalRead(buttonPins[i]);

    if (reading != lastButtonState[i]) {
      lastDebounceTime[i] = currentTime;
    }

    if ((currentTime - lastDebounceTime[i]) > debounceDelay) {
      if (reading != buttonState[i]) {
        buttonState[i] = reading;

        if (buttonState[i] == LOW) {
          ledState[i] = !ledState[i];
          digitalWrite(ledPins[i], ledState[i] ? HIGH : LOW);
          mb.Hreg(REG_LED_START + i, ledState[i]);

          Serial.print("Tombol ");
          Serial.print(i + 1);
          Serial.print(" ditekan -> LED: ");
          Serial.println(ledState[i] ? "NYALA" : "MATI");
        }
      }
    }

    lastButtonState[i] = reading;
    mb.Hreg(REG_BTN_START + i, buttonState[i]);
  }
}
