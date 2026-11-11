#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

// ============================================================
// KONFIGURASI
// ============================================================

// Link ke Arduino Mega (UART1) - protokol teks "DATA,..."
#define MEGA_RX 21
#define MEGA_TX 22
#define MEGA_BAUD 9600

// Bus relay RS485 (UART2) - Modbus RTU FC05
#define RELAY_RX 16
#define RELAY_TX 17
#define RELAY_BAUD 9600

// W5500 Ethernet
#define W5500_CS 5
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
const char* DEVICE_ID = "billiard-001";

#define LED_COUNT 20

#define BUS_GAP_MS 40
#define WRITE_TIMEOUT_MS 250
#define WRITE_RETRY_GAP_MS 60
#define WRITE_MAX_TRIES 3
#define READ_TIMEOUT_MS 150

#define MEGA_POLL_MS 150
#define MEGA_TIMEOUT_MS 2000

#define RELAY_QUEUE_LENGTH 32

#define HTTP_INIT_TIMEOUT_MS 250
#define HTTP_HEADER_TIMEOUT_MS 500
#define HTTP_BODY_TIMEOUT_MS 700
#define HTTP_LINE_TIMEOUT_MS 150
#define HTTP_MAX_BODY 2048

#define LOOP_WDT_TIMEOUT_MS 20000
#define DHCP_TIMEOUT_MS 10000
#define DHCP_RESPONSE_TIMEOUT_MS 2000

HardwareSerial MegaSerial(1);
HardwareSerial RelaySerial(2);
EthernetServer httpServer(80);

// ============================================================
// STATE DARI MEGA
// ============================================================

uint8_t megaLeds[LED_COUNT];
uint8_t megaButtons[LED_COUNT];
uint8_t prevButtons[LED_COUNT];

bool buttonsSeeded = false;
bool megaAuto = false;
bool lastMegaAuto = false;
bool haveMode = false;
bool megaWarned = false;

unsigned long lastMegaRx = 0;
unsigned long lastMegaPoll = 0;

// ============================================================
// STATE RELAY
// ============================================================

bool relayState[LED_COUNT];    // status terkonfirmasi ACK FC05
bool relayDesired[LED_COUNT];  // target perintah terakhir
bool relayPending[LED_COUNT];
bool relayFault[LED_COUNT];

struct RelayCmd {
  uint8_t channel;
  bool on;
};

RelayCmd relayQueue[RELAY_QUEUE_LENGTH];
uint8_t relayQHead = 0;
uint8_t relayQTail = 0;
uint8_t relayQCount = 0;

// Buffer perintah Serial Monitor
char cmdBuf[64];
uint8_t cmdIndex = 0;

// ============================================================
// STATE ETHERNET
// ============================================================

bool ethernetConnected = false;
bool httpStarted = false;
unsigned long lastEthernetCheck = 0;

// ============================================================
// PROTOTYPES
// ============================================================

bool connectEthernet();
void maintainEthernet();
void handleHttpClient();

// ============================================================
// MODBUS CRC16
// ============================================================

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
    if (RelaySerial.available()) {
      while (RelaySerial.available()) RelaySerial.read();
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
  RelaySerial.write(req, sizeof(req));
  RelaySerial.flush();

  uint8_t resp[16];
  uint8_t n = 0;
  uint32_t start = millis();
  uint32_t last = start;

  while ((uint32_t)(millis() - start) < WRITE_TIMEOUT_MS) {
    while (RelaySerial.available() && n < sizeof(resp)) {
      resp[n++] = RelaySerial.read();
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

// ============================================================
// PEMETAAN CHANNEL -> SLAVE / COIL (PLAN.md)
// ============================================================

bool channelMap(uint8_t channel, uint8_t &slaveId, uint16_t &coil) {
  if (channel < 1 || channel > LED_COUNT) return false;

  if (channel <= 8) {
    slaveId = 2;
    coil = channel - 1;          // ID2: relay 1-8 -> coil 0-7
  } else if (channel <= 16) {
    slaveId = 1;
    coil = channel - 9;          // ID1: relay 1-8 -> coil 0-7
  } else {
    slaveId = 3;
    coil = 20 - channel;         // ID3: relay 4->1 -> coil 3->0
  }
  return true;
}

// ============================================================
// SINKRONISASI LED MEGA
// ============================================================

void syncLeds() {
  for (uint8_t i = 0; i < LED_COUNT; i++) {
    uint8_t want = relayState[i] ? 1 : 0;
    if (megaLeds[i] == want) continue;

    MegaSerial.print("LED,");
    MegaSerial.print(i + 1);
    MegaSerial.print(",");
    MegaSerial.println(want);

    megaLeds[i] = want;  // optimistic; frame berikutnya akan mengonfirmasi
  }
}

// ============================================================
// BACA STATUS RELAY SAAT RESET (FC01)
// ============================================================

bool readCoilsRaw(uint8_t slaveId, uint16_t start, uint8_t qty,
                  uint8_t* bits, uint8_t* lastError) {
  uint8_t req[8] = {
    slaveId,
    0x01,  // FC01 = read coils
    (uint8_t)(start >> 8),
    (uint8_t)(start & 0xFF),
    0x00,
    qty,
    0x00,
    0x00
  };
  uint16_t crc = mbCrc16(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  settleBus();
  RelaySerial.write(req, sizeof(req));
  RelaySerial.flush();

  uint8_t resp[32];
  uint8_t n = 0;
  uint32_t startMs = millis();
  uint32_t last = startMs;

  while ((uint32_t)(millis() - startMs) < READ_TIMEOUT_MS) {
    while (RelaySerial.available() && n < sizeof(resp)) {
      resp[n++] = RelaySerial.read();
      last = millis();
    }
    if (n && (uint32_t)(millis() - last) >= 10) break;
    delay(1);
  }
  settleBus(5, 20);

  for (uint8_t i = 0; i + 5 <= n; i++) {
    if (resp[i] != slaveId) continue;

    if (resp[i + 1] == 0x81) {
      uint16_t got = resp[i + 3] | ((uint16_t)resp[i + 4] << 8);
      if (mbCrc16(&resp[i], 3) == got) {
        if (lastError) *lastError = 2;
        return false;
      }
    }

    if (resp[i + 1] != 0x01) continue;
    uint8_t byteCount = resp[i + 2];
    if (byteCount < (uint8_t)((qty + 7) / 8)) continue;
    if ((uint16_t)(i + 3 + byteCount + 2) > n) continue;

    uint16_t got = resp[i + 3 + byteCount] |
                   ((uint16_t)resp[i + 4 + byteCount] << 8);
    if (mbCrc16(&resp[i], 3 + byteCount) != got) continue;

    for (uint8_t b = 0; b < qty; b++) {
      bits[b] = (resp[i + 3 + (b >> 3)] >> (b & 7)) & 1;
    }
    return true;
  }

  if (n) {
    Serial.printf("[RELAY] ID%u FC01 resp:", slaveId);
    for (uint8_t k = 0; k < n; k++) Serial.printf(" %02X", resp[k]);
    Serial.println();
  }

  if (lastError) *lastError = n ? 3 : 1;
  return false;
}

bool readRegistersRaw(uint8_t slaveId, uint16_t start, uint8_t qty,
                      uint16_t* regs, uint8_t* lastError) {
  uint8_t req[8] = {
    slaveId,
    0x03,  // FC03 = read holding registers
    (uint8_t)(start >> 8),
    (uint8_t)(start & 0xFF),
    0x00,
    qty,
    0x00,
    0x00
  };
  uint16_t crc = mbCrc16(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  settleBus();
  RelaySerial.write(req, sizeof(req));
  RelaySerial.flush();

  uint8_t resp[32];
  uint8_t n = 0;
  uint32_t startMs = millis();
  uint32_t last = startMs;

  while ((uint32_t)(millis() - startMs) < READ_TIMEOUT_MS) {
    while (RelaySerial.available() && n < sizeof(resp)) {
      resp[n++] = RelaySerial.read();
      last = millis();
    }
    if (n && (uint32_t)(millis() - last) >= 10) break;
    delay(1);
  }
  settleBus(5, 20);

  for (uint8_t i = 0; i + 5 <= n; i++) {
    if (resp[i] != slaveId) continue;

    if (resp[i + 1] == 0x83) {
      uint16_t got = resp[i + 3] | ((uint16_t)resp[i + 4] << 8);
      if (mbCrc16(&resp[i], 3) == got) {
        if (lastError) *lastError = 2;
        return false;
      }
    }

    if (resp[i + 1] != 0x03) continue;
    uint8_t byteCount = resp[i + 2];
    if (byteCount < (uint8_t)(qty * 2)) continue;
    if ((uint16_t)(i + 3 + byteCount + 2) > n) continue;

    uint16_t got = resp[i + 3 + byteCount] |
                   ((uint16_t)resp[i + 4 + byteCount] << 8);
    if (mbCrc16(&resp[i], 3 + byteCount) != got) continue;

    for (uint8_t r = 0; r < qty; r++) {
      regs[r] = ((uint16_t)resp[i + 3 + (r * 2)] << 8) |
                resp[i + 4 + (r * 2)];
    }
    return true;
  }

  if (n) {
    Serial.printf("[RELAY] ID%u FC03 resp:", slaveId);
    for (uint8_t k = 0; k < n; k++) Serial.printf(" %02X", resp[k]);
    Serial.println();
  }

  if (lastError) *lastError = n ? 3 : 1;
  return false;
}

// Coba FC01 dulu; kalau modul tidak mendukung, fallback FC03 register 0.
bool readModuleCoils(uint8_t slaveId, uint8_t qty, uint8_t* bits,
                     uint8_t* lastError) {
  uint8_t err = 1;

  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    if (readCoilsRaw(slaveId, 0, qty, bits, &err)) return true;
    if (err == 2) break;  // exception: FC01 tidak didukung
    delay(20);
  }

  uint16_t reg = 0;
  if (readRegistersRaw(slaveId, 0, 1, &reg, &err)) {
    for (uint8_t b = 0; b < qty; b++) bits[b] = (reg >> b) & 1;
    return true;
  }

  if (lastError) *lastError = err;
  return false;
}

// Sinkronkan relayState + relayDesired dengan kondisi coil fisik.
bool readRelayStates() {
  bool changed = false;
  uint8_t bits[8];
  uint8_t err = 1;

  if (readModuleCoils(2, 8, bits, &err)) {
    for (uint8_t c = 0; c < 8; c++) {
      bool on = bits[c] != 0;
      if (relayState[c] != on) {
        relayState[c] = on;
        relayDesired[c] = on;
        changed = true;
      }
    }
  } else {
    Serial.printf("[RELAY] baca ID2 gagal: %s\n", resultText(err));
  }

  if (readModuleCoils(1, 8, bits, &err)) {
    for (uint8_t c = 0; c < 8; c++) {
      uint8_t i = 8 + c;
      bool on = bits[c] != 0;
      if (relayState[i] != on) {
        relayState[i] = on;
        relayDesired[i] = on;
        changed = true;
      }
    }
  } else {
    Serial.printf("[RELAY] baca ID1 gagal: %s\n", resultText(err));
  }

  if (readModuleCoils(3, 4, bits, &err)) {
    for (uint8_t coil = 0; coil < 4; coil++) {
      uint8_t i = 19 - coil;  // coil 0 -> CH20, coil 3 -> CH17
      bool on = bits[coil] != 0;
      if (relayState[i] != on) {
        relayState[i] = on;
        relayDesired[i] = on;
        changed = true;
      }
    }
  } else {
    Serial.printf("[RELAY] baca ID3 gagal: %s\n", resultText(err));
  }

  if (changed) {
    Serial.println("[RELAY] status awal disinkronkan dari modul");
    syncLeds();
  }
  return changed;
}

// ============================================================
// ANTRIAN PERINTAH RELAY
// ============================================================

bool enqueueRelay(uint8_t channel, bool on) {
  if (channel < 1 || channel > LED_COUNT) return false;
  if (relayQCount >= RELAY_QUEUE_LENGTH) return false;

  relayDesired[channel - 1] = on;
  relayPending[channel - 1] = true;

  relayQueue[relayQTail].channel = channel;
  relayQueue[relayQTail].on = on;
  relayQTail = (relayQTail + 1) % RELAY_QUEUE_LENGTH;
  relayQCount++;
  return true;
}

void clearRelayQueue() {
  relayQHead = 0;
  relayQTail = 0;
  relayQCount = 0;
}

// Tulis satu relay ke bus Modbus (blocking).
bool applyRelayNow(uint8_t channel, bool on) {
  uint8_t slaveId;
  uint16_t coil;

  if (!channelMap(channel, slaveId, coil)) return false;

  uint8_t err = 1;
  bool ok = writeCoil(slaveId, coil, on, &err);

  if (ok) {
    Serial.printf("[RELAY] CH%u %s OK (ID%u coil %u)\n",
                  channel, on ? "ON" : "OFF", slaveId, coil);
  } else {
    Serial.printf("[RELAY] CH%u %s GAGAL: %s (ID%u coil %u)\n",
                  channel, on ? "ON" : "OFF", resultText(err), slaveId, coil);
  }
  return ok;
}

void processRelayQueue() {
  if (relayQCount == 0) return;

  RelayCmd cmd = relayQueue[relayQHead];
  relayQHead = (relayQHead + 1) % RELAY_QUEUE_LENGTH;
  relayQCount--;

  uint8_t i = cmd.channel - 1;

  // Jangan eksekusi kalau mode/Mega belum pasti.
  if (!haveMode || megaWarned) {
    relayPending[i] = false;
    relayDesired[i] = relayState[i];
    return;
  }

  bool ok = applyRelayNow(cmd.channel, cmd.on);

  if (ok) {
    relayState[i] = cmd.on;
    relayFault[i] = false;
  } else {
    relayFault[i] = true;
  }

  relayPending[i] = false;
  syncLeds();
}

void toggleChannel(uint8_t channel) {
  enqueueRelay(channel, !relayDesired[channel - 1]);
}

// ============================================================
// DATA DARI MEGA
// ============================================================

// Format: DATA,<20 LED>,<20 BUTTON>,<MODE>
// LED/button: 0/1. Mode: 0 = MANUAL, 1 = AUTO.
bool parseMegaData(const String &line) {
  if (!line.startsWith("DATA,")) return false;
  if (line.length() < 48) return false;

  uint8_t leds[LED_COUNT];
  uint8_t buttons[LED_COUNT];

  for (uint8_t i = 0; i < LED_COUNT; i++) {
    char c = line.charAt(5 + i);
    if (c != '0' && c != '1') return false;
    leds[i] = c - '0';
  }

  for (uint8_t i = 0; i < LED_COUNT; i++) {
    char c = line.charAt(26 + i);
    if (c != '0' && c != '1') return false;
    buttons[i] = c - '0';
  }

  char mode = line.charAt(47);
  if (mode != '0' && mode != '1') return false;

  for (uint8_t i = 0; i < LED_COUNT; i++) {
    megaLeds[i] = leds[i];
    megaButtons[i] = buttons[i];
  }
  megaAuto = (mode == '1');
  return true;
}

void handleMegaFrame() {
  bool modeChanged = !haveMode || (megaAuto != lastMegaAuto);

  if (modeChanged) {
    haveMode = true;
    lastMegaAuto = megaAuto;

    // Buang perintah lama supaya tidak dieksekusi di mode baru.
    clearRelayQueue();
    for (uint8_t i = 0; i < LED_COUNT; i++) {
      relayPending[i] = false;
      relayDesired[i] = relayState[i];
      relayFault[i] = false;
    }

    Serial.printf("[MODE] %s\n", megaAuto ? "AUTO" : "MANUAL");

    // Hindari tombol yang sedang ditekan dianggap event saat ganti mode.
    for (uint8_t i = 0; i < LED_COUNT; i++) prevButtons[i] = megaButtons[i];
    buttonsSeeded = true;
  } else if (!buttonsSeeded) {
    for (uint8_t i = 0; i < LED_COUNT; i++) prevButtons[i] = megaButtons[i];
    buttonsSeeded = true;
  } else if (!megaAuto) {
    // Mode manual: tombol Mega mengontrol relay.
    for (uint8_t i = 0; i < LED_COUNT; i++) {
      if (prevButtons[i] != megaButtons[i]) {
        if (megaButtons[i] == 0) {  // LOW = ditekan
          Serial.printf("[BUTTON] CH%u ditekan\n", i + 1);
          toggleChannel(i + 1);
        }
        prevButtons[i] = megaButtons[i];
      }
    }
  } else {
    // Mode auto: tombol tidak mengubah relay; LED dikoreksi ke status relay.
    for (uint8_t i = 0; i < LED_COUNT; i++) prevButtons[i] = megaButtons[i];
  }

  syncLeds();
}

void pollMega() {
  unsigned long now = millis();

  if (now - lastMegaPoll >= MEGA_POLL_MS) {
    lastMegaPoll = now;
    MegaSerial.println("GET");
  }

  while (MegaSerial.available()) {
    String line = MegaSerial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    lastMegaRx = millis();

    // ACK LED / potongan frame akibat buffer UART bukan frame status.
    if (!line.startsWith("DATA,")) continue;

    if (megaWarned) {
      megaWarned = false;
      Serial.println("[MEGA] online kembali");
    }

    if (!parseMegaData(line)) {
      Serial.print("[MEGA] frame tidak valid: ");
      Serial.println(line);
      continue;
    }

    handleMegaFrame();
  }

  if (!megaWarned && (millis() - lastMegaRx) > MEGA_TIMEOUT_MS) {
    megaWarned = true;
    haveMode = false;
    buttonsSeeded = false;

    clearRelayQueue();
    for (uint8_t i = 0; i < LED_COUNT; i++) {
      relayPending[i] = false;
      relayDesired[i] = relayState[i];
    }

    Serial.println("[MEGA] timeout. Mode tidak diketahui, perintah ditolak.");
  }
}

// ============================================================
// PERINTAH SERIAL (debug / pengganti web sementara)
// ============================================================

void printStatus() {
  Serial.printf("[STATUS] Mega=%s, Mode=%s, IP=%s\n",
                megaWarned ? "OFFLINE" : "online",
                haveMode ? (megaAuto ? "AUTO" : "MANUAL") : "UNKNOWN",
                ethernetConnected ? Ethernet.localIP().toString().c_str() : "-");

  for (uint8_t ch = 1; ch <= LED_COUNT; ch++) {
    uint8_t slaveId;
    uint16_t coil;
    channelMap(ch, slaveId, coil);
    Serial.printf("  CH%02u %s%s%s | ID%u coil %u\n",
                  ch,
                  relayState[ch - 1] ? "ON " : "OFF",
                  relayPending[ch - 1] ? " PENDING" : "",
                  relayFault[ch - 1] ? " FAULT" : "",
                  slaveId, coil);
  }
}

void handleSerialCommand(char *cmd) {
  char *arg = strchr(cmd, ' ');
  if (arg) {
    *arg = '\0';
    arg++;
    while (*arg == ' ') arg++;
  }

  for (char *p = cmd; *p; p++) *p = toupper(*p);
  if (arg) {
    for (char *p = arg; *p; p++) *p = toupper(*p);
  }

  if (strcmp(cmd, "STATUS") == 0) {
    printStatus();
    return;
  }

  bool wantOn = false;
  bool all = false;
  bool validAction = false;

  if (strcmp(cmd, "ON") == 0) {
    wantOn = true;
    validAction = true;
  } else if (strcmp(cmd, "OFF") == 0) {
    wantOn = false;
    validAction = true;
  } else if (strcmp(cmd, "ALL_ON") == 0) {
    wantOn = true;
    all = true;
    validAction = true;
  } else if (strcmp(cmd, "ALL_OFF") == 0) {
    wantOn = false;
    all = true;
    validAction = true;
  }

  if (!validAction) {
    Serial.println("[CMD] Tidak dikenal. Pakai: ON <1-20> | OFF <1-20> | ON ALL | OFF ALL | STATUS");
    return;
  }

  int channel = -1;

  if (!all) {
    if (arg && strcmp(arg, "ALL") == 0) {
      all = true;
    } else if (arg) {
      channel = atoi(arg);
    } else {
      Serial.println("[CMD] Channel belum diisi.");
      return;
    }
  }

  if (!haveMode || megaWarned) {
    Serial.println("[CMD] Ditolak: status Mega/mode belum diketahui.");
    return;
  }

  if (!megaAuto) {
    Serial.println("[CMD] Ditolak: switch di posisi MANUAL.");
    return;
  }

  if (all) {
    for (uint8_t ch = 1; ch <= LED_COUNT; ch++) {
      enqueueRelay(ch, wantOn);
    }
    return;
  }

  if (channel < 1 || channel > LED_COUNT) {
    Serial.println("[CMD] Channel harus 1-20.");
    return;
  }

  enqueueRelay((uint8_t)channel, wantOn);
}

void readSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (cmdIndex > 0) {
        cmdBuf[cmdIndex] = '\0';
        handleSerialCommand(cmdBuf);
        cmdIndex = 0;
      }
    } else if (cmdIndex < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdIndex++] = c;
    }
  }
}

// ============================================================
// ETHERNET
// ============================================================

bool connectEthernet() {
  Serial.println("[NET] DHCP start");
  Ethernet.init(W5500_CS);

  if (Ethernet.begin(mac, DHCP_TIMEOUT_MS, DHCP_RESPONSE_TIMEOUT_MS) == 0) {
    ethernetConnected = false;
    Serial.println("[NET] DHCP gagal");
    return false;
  }

  delay(250);

  if (!httpStarted) {
    httpServer.begin();
    httpStarted = true;
  }

  ethernetConnected = true;
  Serial.printf("[NET] Buka http://%s/\n", Ethernet.localIP().toString().c_str());
  return true;
}

void maintainEthernet() {
  unsigned long now = millis();
  if (now - lastEthernetCheck < 5000) return;
  lastEthernetCheck = now;

  if (!ethernetConnected) {
    connectEthernet();
    return;
  }

  Ethernet.maintain();

  if (Ethernet.linkStatus() == LinkOFF) {
    ethernetConnected = false;
    Serial.println("[NET] link down");
  }
}

// ============================================================
// HTTP SERVER
// ============================================================

void sendResponse(EthernetClient& client, int code, const char* type,
                  const char* body, size_t len, bool cors) {
  const char* status = "OK";
  if (code == 400) status = "Bad Request";
  else if (code == 404) status = "Not Found";
  else if (code == 409) status = "Conflict";
  else if (code == 413) status = "Payload Too Large";
  else if (code == 500) status = "Internal Server Error";

  client.print("HTTP/1.1 "); client.print(code); client.print(' '); client.println(status);
  client.print("Content-Type: "); client.println(type);
  client.print("Content-Length: "); client.println(len);
  client.println("Connection: close");
  client.println("Cache-Control: no-store");
  if (cors) {
    client.println("Access-Control-Allow-Origin: *");
    client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
    client.println("Access-Control-Allow-Headers: Content-Type");
  }
  client.println();
  if (len) client.write((const uint8_t*)body, len);
  client.flush();
}

void sendText(EthernetClient& client, int code, const char* type,
              const String& body, bool cors) {
  sendResponse(client, code, type, body.c_str(), body.length(), cors);
}

void sendJsonError(EthernetClient& client, int code, const char* reason) {
  String body = "{\"ok\":false,\"reason\":\"";
  body += reason;
  body += "\"}";
  sendText(client, code, "application/json", body, true);
}

String buildStatusJson() {
  String out;
  out.reserve(2000);
  out = "{\"device_id\":\"";
  out += DEVICE_ID;
  out += "\",\"mode\":\"";
  out += haveMode ? (megaAuto ? "AUTO" : "MANUAL") : "UNKNOWN";
  out += "\",\"mode_known\":";
  out += haveMode ? "true" : "false";
  out += ",\"online\":true,\"mega_online\":";
  out += (megaWarned || !haveMode) ? "false" : "true";
  out += ",\"relays\":[";

  for (uint8_t i = 0; i < LED_COUNT; i++) {
    if (i) out += ',';
    out += "{\"channel\":"; out += String(i + 1);
    out += ",\"state\":\""; out += relayState[i] ? "ON" : "OFF";
    out += "\",\"desired\":\""; out += relayDesired[i] ? "ON" : "OFF";
    out += "\",\"pending\":"; out += relayPending[i] ? "true" : "false";
    out += ",\"fault\":"; out += relayFault[i] ? "true" : "false";
    out += "}";
  }

  out += "]}";
  return out;
}

void handleRelayPost(EthernetClient& client, const String& body) {
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { sendJsonError(client, 400, "invalid_json"); return; }

  if (!haveMode) { sendJsonError(client, 409, "mode_unknown"); return; }
  if (!megaAuto) { sendJsonError(client, 409, "manual_mode"); return; }
  if (megaWarned) { sendJsonError(client, 409, "mega_offline"); return; }

  const char* action = doc["action"] | "";
  int queued = 0, failed = 0, invalid = 0;

  if (strcmp(action, "ALL_OFF") == 0) {
    for (uint8_t ch = 1; ch <= LED_COUNT; ch++) {
      if (enqueueRelay(ch, false)) queued++; else failed++;
    }
  } else {
    bool on;
    if (strcmp(action, "ON") == 0) on = true;
    else if (strcmp(action, "OFF") == 0) on = false;
    else { sendJsonError(client, 400, "invalid_action"); return; }

    if (doc["channels"].is<JsonArray>()) {
      JsonArray arr = doc["channels"].as<JsonArray>();
      if (arr.size() == 0) { sendJsonError(client, 400, "no_channel"); return; }
      for (JsonVariant v : arr) {
        if (!v.is<int>()) { invalid++; continue; }
        int ch = v.as<int>();
        if (ch < 1 || ch > LED_COUNT) { invalid++; continue; }
        if (enqueueRelay((uint8_t)ch, on)) queued++; else failed++;
      }
    } else if (doc.containsKey("channel")) {
      if (!doc["channel"].is<int>()) { sendJsonError(client, 400, "invalid_channel"); return; }
      int ch = doc["channel"].as<int>();
      if (ch < 1 || ch > LED_COUNT) { sendJsonError(client, 400, "invalid_channel"); return; }
      if (enqueueRelay((uint8_t)ch, on)) queued++; else failed++;
    } else {
      sendJsonError(client, 400, "no_channel"); return;
    }
  }

  String out = "{\"ok\":";
  out += (failed == 0 && invalid == 0) ? "true" : "false";
  out += ",\"queued\":"; out += String(queued);
  out += ",\"failed\":"; out += String(failed);
  out += ",\"invalid\":"; out += String(invalid);
  if (failed) out += ",\"reason\":\"queue_full\"";
  out += "}";
  sendText(client, 200, "application/json", out, true);
}

// Bounded line reader. Returns -1 on timeout, -2 on overflow.
static int readLineBounded(EthernetClient& client, char* buf, size_t cap,
                           uint32_t timeoutMs) {
  if (cap < 2) return -2;
  size_t n = 0;
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < timeoutMs) {
    if (!client.available()) { delay(1); continue; }
    int c = client.read();
    if (c < 0) continue;
    if (c == '\r') continue;
    if (c == '\n') { buf[n] = 0; return (int)n; }
    if (n + 1 >= cap) { buf[n] = 0; return -2; }
    buf[n++] = (char)c;
  }
  buf[n] = 0;
  return n ? (int)n : -1;
}

// ============================================================
// SCAN ID RELAY
// ============================================================

bool probeFrame(uint8_t id, uint8_t fc, uint32_t timeoutMs) {
  uint8_t req[8] = {id, fc, 0, 0, 0, 1, 0, 0};
  uint16_t crc = mbCrc16(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  settleBus(BUS_GAP_MS, 100);
  RelaySerial.write(req, 8);
  RelaySerial.flush();

  uint8_t resp[32];
  uint8_t n = 0;
  uint32_t start = millis();
  uint32_t last = start;

  while ((uint32_t)(millis() - start) < timeoutMs) {
    while (RelaySerial.available() && n < sizeof(resp)) {
      resp[n++] = RelaySerial.read();
      last = millis();
    }
    if (n && (uint32_t)(millis() - last) >= 8) break;
    delay(1);
  }
  settleBus(5, 20);

  for (uint8_t i = 0; i < n; i++) {
    if (resp[i] != id) continue;

    if (i + 5 <= n && resp[i + 1] == (uint8_t)(fc | 0x80)) {
      uint16_t got = resp[i + 3] | ((uint16_t)resp[i + 4] << 8);
      if (mbCrc16(&resp[i], 3) == got) return true;
    }
    if (fc == 0x01 && i + 6 <= n && resp[i + 1] == 0x01 && resp[i + 2] == 1) {
      uint16_t got = resp[i + 4] | ((uint16_t)resp[i + 5] << 8);
      if (mbCrc16(&resp[i], 4) == got) return true;
    }
    if (fc == 0x03 && i + 7 <= n && resp[i + 1] == 0x03 && resp[i + 2] == 2) {
      uint16_t got = resp[i + 5] | ((uint16_t)resp[i + 6] << 8);
      if (mbCrc16(&resp[i], 5) == got) return true;
    }
  }
  return false;
}

bool probeModbusId(uint8_t id) {
  return probeFrame(id, 0x01, 100) || probeFrame(id, 0x03, 100);
}

void scanRelayIds(String& out) {
  out = "{\"ok\":true,\"baud\":9600,\"found\":[";
  bool first = true;
  for (uint8_t id = 1; id <= 20; id++) {
    if (probeModbusId(id)) {
      if (!first) out += ',';
      out += String(id);
      first = false;
    }
  }
  out += "],\"note\":\"Hanya modul yang menjawab FC01/FC03 yang terdeteksi.\"}";
}

void handleHttpClient() {
  EthernetClient client = httpServer.available();
  if (!client) return;
  client.setTimeout(HTTP_LINE_TIMEOUT_MS);

  uint32_t start = millis();
  while (client.connected() && !client.available() &&
         (uint32_t)(millis() - start) < HTTP_INIT_TIMEOUT_MS) delay(1);
  if (!client.available()) { client.stop(); return; }

  char line[256];
  int lineLen = readLineBounded(client, line, sizeof(line), HTTP_LINE_TIMEOUT_MS);
  if (lineLen <= 0) { client.stop(); return; }

  String requestLine(line);
  int sp1 = requestLine.indexOf(' ');
  int sp2 = requestLine.indexOf(' ', sp1 + 1);
  if (sp1 <= 0 || sp2 <= sp1) { client.stop(); return; }

  String method = requestLine.substring(0, sp1);
  String path = requestLine.substring(sp1 + 1, sp2);
  int q = path.indexOf('?');
  if (q >= 0) path.remove(q);

  int contentLength = 0;
  uint32_t headerStart = millis();
  bool headersComplete = false;

  while (client.connected() &&
         (uint32_t)(millis() - headerStart) < HTTP_HEADER_TIMEOUT_MS) {
    int n = readLineBounded(client, line, sizeof(line), HTTP_LINE_TIMEOUT_MS);
    if (n == -1) break;
    if (n == -2) { sendJsonError(client, 400, "header_too_long"); client.stop(); return; }
    if (n == 0) { headersComplete = true; break; }

    String h(line);
    h.trim();
    if (h.startsWith("Content-Length:") || h.startsWith("content-length:")) {
      contentLength = h.substring(h.indexOf(':') + 1).toInt();
    }
  }

  if (!headersComplete) {
    sendJsonError(client, 400, "incomplete_headers");
    client.stop();
    return;
  }

  if (contentLength < 0 || contentLength > HTTP_MAX_BODY) {
    sendJsonError(client, 413, "body_too_large");
    client.stop();
    return;
  }

  String body;
  if (contentLength > 0) {
    body.reserve(contentLength);
    uint32_t bodyStart = millis();
    while ((int)body.length() < contentLength &&
           (uint32_t)(millis() - bodyStart) < HTTP_BODY_TIMEOUT_MS) {
      if (client.available()) body += (char)client.read();
      else delay(1);
    }
    if ((int)body.length() != contentLength) {
      sendJsonError(client, 400, "incomplete_body");
      client.stop();
      return;
    }
  }

  if (method == "OPTIONS") {
    sendResponse(client, 200, "text/plain", "", 0, true);
  } else if (method == "GET" && (path == "/" || path == "/api")) {
    sendText(client, 200, "application/json",
             "{\"ok\":true,\"service\":\"billiard-controller\","
             "\"api\":[\"/api/status\",\"/api/relay\",\"/api/scan\"]}", true);
  } else if (method == "GET" && path == "/api/status") {
    sendText(client, 200, "application/json", buildStatusJson(), true);
  } else if (method == "POST" && path == "/api/relay") {
    handleRelayPost(client, body);
  } else if (method == "GET" && path == "/api/scan") {
    String out;
    scanRelayIds(out);
    sendText(client, 200, "application/json", out, true);
  } else {
    sendText(client, 404, "text/plain", "Not found", false);
  }

  delay(1);
  client.stop();
}

// ============================================================
// WATCHDOG & RESET REASON
// ============================================================

const char* resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    default: return "UNKNOWN";
  }
}

void enableLoopWatchdog() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = LOOP_WDT_TIMEOUT_MS;
  cfg.idle_core_mask = (1 << 0);
  cfg.trigger_panic = true;

  esp_err_t err = esp_task_wdt_reconfigure(&cfg);
  if (err == ESP_ERR_INVALID_STATE) {
    err = esp_task_wdt_init(&cfg);
  }
  if (err != ESP_OK) {
    Serial.printf("[WDT] gagal konfigurasi: %d\n", (int)err);
    return;
  }

  enableLoopWDT();
  Serial.printf("[WDT] loop watchdog %u ms aktif\n", (unsigned)LOOP_WDT_TIMEOUT_MS);
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.printf("[BOOT] reset: %s\n", resetReasonText(esp_reset_reason()));

  MegaSerial.begin(MEGA_BAUD, SERIAL_8N1, MEGA_RX, MEGA_TX);
  MegaSerial.setTimeout(50);

  RelaySerial.begin(RELAY_BAUD, SERIAL_8N1, RELAY_RX, RELAY_TX);
  settleBus(10, 200);

  for (uint8_t i = 0; i < LED_COUNT; i++) {
    megaLeds[i] = 0;
    megaButtons[i] = 1;
    prevButtons[i] = 1;
    relayState[i] = false;
    relayDesired[i] = false;
    relayPending[i] = false;
    relayFault[i] = false;
  }

  Serial.println();
  Serial.println("================================");
  Serial.println(" ESP32 BILLIARD CONTROLLER");
  Serial.println("================================");
  Serial.println("Mega : UART1 RX21 TX22 @9600");
  Serial.println("Relay: UART2 RX16 TX17 @9600");
  Serial.println("ID2: CH1-8 | ID1: CH9-16 | ID3: CH17-20");
  Serial.println("W5500: CS GPIO5, DHCP");
  Serial.println("================================");

  readRelayStates();

  while (MegaSerial.available()) MegaSerial.read();
  lastMegaRx = millis();

  connectEthernet();

  enableLoopWatchdog();
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  readSerialCommands();
  pollMega();
  processRelayQueue();

  if (ethernetConnected) handleHttpClient();

  maintainEthernet();
}
