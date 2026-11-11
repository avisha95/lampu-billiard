#include <Arduino.h>
#include <ModbusRTU.h>

#define SLAVE_ID 4

ModbusRTU mb;
uint16_t data[41];
uint32_t lastPoll = 0;

bool cb(Modbus::ResultCode ev, uint16_t, void*) {
  if (ev != Modbus::EX_SUCCESS) {
    Serial.printf("POLL ID%u FAIL event=0x%02X\n", SLAVE_ID, ev);
    return true;
  }
  Serial.printf("POLL ID%u OK mode=%u btn=", SLAVE_ID, data[40]);
  for (int i = 0; i < 20; i++) Serial.print(data[20 + i] == 0 ? '1' : '0');
  Serial.print(" led=");
  for (int i = 0; i < 20; i++) Serial.print(data[i] ? '1' : '0');
  Serial.println();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  mb.begin(&Serial2);
  mb.master();
  Serial.println("=== MEGA LINK TEST: poll slave ID4, 41 regs @9600 ===");
}

void loop() {
  mb.task();
  if (!mb.slave() && millis() - lastPoll >= 500) {
    lastPoll = millis();
    mb.readHreg(SLAVE_ID, 0, data, 41, cb);
  }
}
