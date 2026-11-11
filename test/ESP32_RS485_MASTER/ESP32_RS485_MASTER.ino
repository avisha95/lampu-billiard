#include <ModbusRTU.h>

// Harus sama dengan SLAVE_ID di sketch Mega.
// ID 4 dipakai controller utama; ID 1 bentrok dengan modul relay ID 1.
#define SLAVE_ID 4

// Pin RS485 ESP32
#define RXD2 16
#define TXD2 17

ModbusRTU mb;

unsigned long previousMillis = 0;
const long interval = 200;

// 41 register: 0-19 LED, 20-39 tombol, 40 mode
uint16_t slaveData[41];

bool cbRead(Modbus::ResultCode event, uint16_t transactionId, void* data) {
  (void)transactionId;
  (void)data;

  if (event != Modbus::EX_SUCCESS) {
    Serial.printf("Gagal baca ID %u! Error code: 0x%02X\n", SLAVE_ID, event);
    return true;
  }

  bool isManualMode = (slaveData[40] == LOW);
  Serial.printf("OK ID %u -> mode: %s | tombol:",
                SLAVE_ID, isManualMode ? "MANUAL" : "OTOMATIS");
  for (int i = 0; i < 20; i++) {
    Serial.print(slaveData[20 + i] == LOW ? '1' : '0');
  }
  Serial.println();
  return true;
}

void setup() {
  Serial.begin(115200);

  // Tanpa DE/RE
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  mb.begin(&Serial2);
  mb.master();

  Serial.printf("ESP32 Modbus Master mulai, poll slave ID %u\n", SLAVE_ID);
}

void loop() {
  mb.task();

  if (!mb.slave()) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      mb.readHreg(SLAVE_ID, 0, slaveData, 41, cbRead);
    }
  }
}
