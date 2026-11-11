# AGENTS.md

## Commands

- Build ESP32: `pio run -e esp32dev`
- Build Arduino Mega: `pio run -e mega`
- Flash ESP32: `pio run -e esp32dev -t upload`
- Flash Arduino Mega: `pio run -e mega -t upload`
- Serial monitor: `pio device monitor -e esp32dev` (115200) / `pio device monitor -e mega` (9600)

## Source layout

- `src/main_esp32.cpp` — controller: W5500 Ethernet + built-in HTTP server on port 80. No mDNS: akses dashboard lewat IP ESP32 (DHCP; IP dicetak di serial sebagai `[HTTP] Buka http://.../`). JSON API: `GET /api/status`, `GET /api/scan` (probe relay IDs 1–20), `POST /api/relay` (`{"channel":n,"action":"ON|OFF"}`, `{"channels":[...]}`, `{"action":"ALL_OFF"}`). One shared Modbus RTU bus (UART2 RX16/TX17): Mega slave 4 (regs 0–40) and relay modules (ID2 ch1–8, ID1 ch9–16, ID3 ch17–20 mapped relay 4→1, write single coil FC05). Manual mode: Mega buttons drive relays; auto mode: HTTP commands; HTTP commands rejected while manual. `POST /api/relay` only enqueues (`{"ok":true,"queued":n}`); the relay task applies it asynchronously.
- `src/main_mega.cpp` — Modbus RTU slave ID 4 (Serial1 TX18/RX19): 20 buttons (D2–D11, D22–D31), 20 LEDs (D36–D39, D35, D41–D45, A0–A9), mode switch D12 (0 = LOW/manual, 1 = HIGH/auto). Reports buttons/mode; applies LEDs from master-written regs; no local toggle.
- `web/` — dashboard (HTML/CSS/JS, no external deps), edited freely. `scripts/embed_web.py` embeds it into auto-generated `src/web_assets.h` at build time (never edit that header). The ESP32 serves these files itself.

Each env picks its source via `build_src_filter` in `platformio.ini`. Full spec in `PLAN.md` (note: PLAN still describes MQTT; current implementation uses HTTP instead).

## Gotchas

- No MQTT and no mDNS. Open the dashboard via the ESP32's IP so the UI is same-origin; serving `web/` with `python3 -m http.server` will not reach the API.
- Firmware ESP32 sekarang FreeRTOS: `taskWeb` (core 0; Ethernet/HTTP), `taskMega` (core 1; poll + tulis LED), `taskRelay` (core 1; konsumsi `relayQueue`). `busMutex` mengunci Serial2, `stateMutex` melindungi state relay/LED, dan queue menyimpan perintah relay sehingga HTTP tidak menunggu bus. Ethernet connect dipindah ke `taskWeb` agar mode manual tetap jalan tanpa LAN.
- FSM relay per channel: `relayDesired` (perintah terakhir), `relayState` (terkonfirmasi ACK FC05), `relayPending`, `relayFault`. Hanya `taskRelay` yang mengubah status terkonfirmasi via `finishRelay()`; LED panel mengikuti status terkonfirmasi, bukan perintah. `/api/status` per relay berisi `state`, `desired`, `pending`, `fault`.
- Hanya `emelianov/modbus-esp8266` (ESP32↔Mega ID 4) yang dipakai. Relay ID 1/2/3 ditulis dengan frame Modbus FC05 mentah (`writeCoilRaw`, timeout 200 ms) — tidak ada lagi `4-20ma/ModbusMaster` (2 s fixed). Setiap transaksi diawali `settleBus(5,60)` (jeda senyap >=3.5 karakter) supaya frame tidak menyatu dengan respons sebelumnya; frame FC05 dicari di mana pun dalam buffer, dan frame rusak di-retry sekali. Cooldown **per slave** (`slaveCooldownUntil[]`), jadi modul mati (mis. ID3) tidak memblokir ID1/ID2.
- Mega polling: manual mode reads regs 20–40 (20 tombol + mode, 21 regs) every 150 ms; register LED 0–19 tidak dibaca karena master yang menulis. Auto mode reads only mode reg 40 every 500 ms. `taskMega` memprioritaskan poll di atas tulis LED, dan LED write timeout 400 ms (retry 2x). Shared UART selalu di-settle (`settleBus`) sebelum/sesudah transaksi agar byte telat/echo tidak merusak frame.
- Mega `Serial1`: TX = 18, RX = 19. ESP32 shared bus: UART2 16/17; W5500 CS = GPIO5 (VSPI 18/19/23).
- AVR (Mega) has no `Serial.printf` — use `Serial.print`/`Serial.println`.
- ArduinoJson pinned to v6 (`bblanchon/ArduinoJson@^6.21.5`); use `StaticJsonDocument`.
- Relay ID 3 mapping follows `test/test-modbus.cpp`: logical channels 17–20 drive relay 4→1 (coil 3→0). Relay write failure = no valid FC05 echo within 200 ms (wiring/address/baud/power); modul yang diam ditandai `fault` dan di-cooldown 3 s. `GET /api/scan` memprobe ID 1–20 (FC01/FC03, fallback FC05 no-change) untuk cek modul mana yang benar-benar menjawab.

## Tests

- No automated test suite. `test/` holds reference sketches (ESP32 master/slave, Mega slave, relay test, Ethernet); `src/` is the integrated firmware.
