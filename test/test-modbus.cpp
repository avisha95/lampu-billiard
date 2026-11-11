#include <Arduino.h>

// ==============================
// KONFIGURASI RS485 ESP32
// ==============================
#define RS485_RX 16
#define RS485_TX 17
#define MODBUS_BAUD 9600

#define BUS_GAP_MS 40
#define WRITE_TIMEOUT_MS 250
#define WRITE_RETRY_GAP_MS 60
#define WRITE_MAX_TRIES 3

HardwareSerial RS485(1);

// ==============================
// CRC16 MODBUS
// ==============================
uint16_t mbCrc16(const uint8_t* data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
  }
  return crc;
}

// Buang byte sisa / echo agar frame baru tidak menyatu dengan respons lama.
void settleBus(uint32_t quietMs = BUS_GAP_MS, uint32_t maxMs = 150) {
  uint32_t start = millis();
  uint32_t lastByte = start;
  while ((uint32_t)(millis() - start) < maxMs) {
    if (RS485.available()) {
      while (RS485.available()) RS485.read();
      lastByte = millis();
    } else if ((uint32_t)(millis() - lastByte) >= quietMs) {
      break;
    }
    delay(1);
  }
}

// Hasil: 0 = ACK, 1 = timeout, 2 = exception, 3 = frame rusak.
uint8_t writeCoilRaw(uint8_t slaveId, uint16_t coil, bool on) {
  uint8_t req[8] = {
    slaveId,
    0x05,  // FC05 = write single coil
    (uint8_t)(coil >> 8),
    (uint8_t)(coil & 0xFF),
    (uint8_t)(on ? 0xFF : 0x00),
    0x00,
    0x00,
    0x00
  };
  uint16_t crc = mbCrc16(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  settleBus();
  RS485.write(req, sizeof(req));
  RS485.flush();

  uint8_t resp[16];
  uint8_t n = 0;
  uint32_t start = millis();
  uint32_t last = start;

  while ((uint32_t)(millis() - start) < WRITE_TIMEOUT_MS) {
    while (RS485.available() && n < sizeof(resp)) {
      resp[n++] = RS485.read();
      last = millis();
    }
    if (n && (uint32_t)(millis() - last) >= 10) break;
    delay(1);
  }
  settleBus(5, 20);

  // Cari echo FC05 yang valid di posisi mana pun dalam buffer.
  for (uint8_t i = 0; i + 8 <= n; i++) {
    if (resp[i] != slaveId || resp[i + 1] != 0x05) continue;
    uint16_t got = resp[i + 6] | ((uint16_t)resp[i + 7] << 8);
    if (mbCrc16(&resp[i], 6) == got) return 0;
  }

  // Exception FC05: [id, 0x85, code, crcLo, crcHi]
  for (uint8_t i = 0; i + 5 <= n; i++) {
    if (resp[i] != slaveId || resp[i + 1] != 0x85) continue;
    uint16_t got = resp[i + 3] | ((uint16_t)resp[i + 4] << 8);
    if (mbCrc16(&resp[i], 3) == got) return 2;
  }

  return n ? 3 : 1;
}

bool writeCoil(uint8_t slaveId, uint16_t coil, bool on, uint8_t* lastError) {
  uint8_t result = 1;

  for (uint8_t attempt = 0; attempt < WRITE_MAX_TRIES; attempt++) {
    result = writeCoilRaw(slaveId, coil, on);
    if (result == 0) return true;  // ACK
    if (result == 2) break;        // exception: alamat salah, jangan diulang
    if (attempt + 1 < WRITE_MAX_TRIES) delay(WRITE_RETRY_GAP_MS);
  }

  if (lastError) *lastError = result;
  return false;
}

const char* resultText(uint8_t result) {
  switch (result) {
    case 0: return "OK";
    case 1: return "TIMEOUT (tidak ada balasan)";
    case 2: return "EXCEPTION (alamat/coil tidak valid)";
    default: return "FRAME RUSAK";
  }
}

// ==============================
// TES SATU RELAY
// ==============================
void testRelay(uint8_t slaveId, uint8_t relayNumber, uint16_t coil,
               uint32_t onMs = 2000) {
  uint8_t err = 1;

  Serial.printf("\n[ID %u] Relay %u (coil %u) ON\n", slaveId, relayNumber, coil);
  if (!writeCoil(slaveId, coil, true, &err)) {
    Serial.printf("[ID %u] Relay %u gagal ON: %s\n",
                  slaveId, relayNumber, resultText(err));
    delay(500);
    return;
  }
  Serial.printf("[ID %u] Relay %u ON OK\n", slaveId, relayNumber);

  delay(onMs);

  Serial.printf("[ID %u] Relay %u OFF\n", slaveId, relayNumber);
  if (!writeCoil(slaveId, coil, false, &err)) {
    Serial.printf("[ID %u] Relay %u gagal OFF: %s\n",
                  slaveId, relayNumber, resultText(err));
  } else {
    Serial.printf("[ID %u] Relay %u OFF OK\n", slaveId, relayNumber);
  }

  delay(500);
}

// ==============================
// TES SATU MODUL
// ==============================
void testModule(uint8_t slaveId, uint8_t relayCount, bool reverse) {
  Serial.println();
  Serial.printf("========== MODBUS ID %u ==========\n", slaveId);

  if (reverse) {
    // ID 3: urutan fisik relay 4 -> 1 (coil 3 -> 0)
    for (int relay = relayCount; relay >= 1; relay--) {
      testRelay(slaveId, (uint8_t)relay, (uint16_t)(relay - 1));
    }
  } else {
    // ID 2 / ID 1: urutan relay 1 -> 8 (coil 0 -> 7)
    for (uint8_t relay = 1; relay <= relayCount; relay++) {
      testRelay(slaveId, relay, (uint16_t)(relay - 1));
    }
  }
}

// ==============================
// SETUP
// ==============================
void setup() {
  Serial.begin(115200);

  RS485.begin(
    MODBUS_BAUD,
    SERIAL_8N1,
    RS485_RX,
    RS485_TX
  );

  settleBus(10, 200);

  Serial.println();
  Serial.println("================================");
  Serial.println(" ESP32 MODBUS RELAY AUTO TEST");
  Serial.println("================================");
  Serial.println("Baudrate : 9600");
  Serial.println("RX       : GPIO16");
  Serial.println("TX       : GPIO17");
  Serial.println("Urutan ID: 2 -> 1 -> 3");
  Serial.println("ID 2     : Relay 1 -> 8 (coil 0 -> 7)");
  Serial.println("ID 1     : Relay 1 -> 8 (coil 0 -> 7)");
  Serial.println("ID 3     : Relay 4 -> 1 (coil 3 -> 0)");
  Serial.println("================================");
}

// ==============================
// LOOP OTOMATIS
// ==============================
void loop() {
  testModule(2, 8, false);
  delay(1000);

  testModule(1, 8, false);
  delay(1000);

  testModule(3, 4, true);

  Serial.println();
  Serial.println("Semua perangkat selesai dites.");
  Serial.println("Mengulang dalam 3 detik...");

  delay(3000);
}
