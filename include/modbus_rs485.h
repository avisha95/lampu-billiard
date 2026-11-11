#ifndef MODBUS_RS485_H
#define MODBUS_RS485_H

#include <ModbusRTU.h>

#define SLAVE_ID    1
#define BAUD_RATE   9600
#define RS485_RX    16
#define RS485_TX    17

// 8 relay pins
const uint8_t RELAY_PINS[8] = {25, 26, 27, 14, 12, 13, 5, 4};
const uint8_t N_RELAY = 8;

class ModbusRS485 {
  public:
    ModbusRS485();
    void begin();
    void task();
    uint16_t readHreg(uint16_t reg);
    void writeHreg(uint16_t reg, uint16_t val);
    uint16_t readIreg(uint16_t reg);
    void writeIreg(uint16_t reg, uint16_t val);
    bool readCoil(uint16_t coil);
    void writeCoil(uint16_t coil, bool state);
    bool readIsts(uint16_t coil);
    void writeIsts(uint16_t coil, bool state);

  private:
    ModbusRTU mb;
    uint16_t tick = 0;
    uint32_t lastMs = 0;
};

#endif