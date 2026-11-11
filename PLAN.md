# PROJECT PLAN
# Sistem Kontrol Lampu Billiard Berbasis IoT

**Platform:** ESP32 + Arduino Mega 2560 + W5500 Ethernet + RS485 Modbus RTU  
**Komunikasi IoT:** MQTT  
**Jumlah Channel:** 20 Relay  
**Status Dokumen:** Draft Perencanaan Teknis  
**Tanggal:** 31 Oktober 2026

---

## 1. Gambaran Umum Proyek

Proyek ini bertujuan membangun sistem kontrol lampu meja billiard menggunakan teknologi IoT.

Sistem memiliki 20 channel output relay yang dapat dikendalikan melalui dua metode:

1. **Mode Otomatis (LAN/MQTT):** Lampu dikendalikan melalui aplikasi atau website billiard menggunakan MQTT.
2. **Mode Manual:** Lampu dikendalikan secara langsung melalui 20 push button yang terhubung ke Arduino Mega.

Sistem menggunakan ESP32 sebagai pusat komunikasi dan pengendali perangkat, sedangkan Arduino Mega bertugas menangani panel kontrol fisik.

Komunikasi antarperangkat menggunakan Modbus RTU melalui RS485.

## 2. Tujuan Sistem

- Mengendalikan 20 lampu meja billiard melalui aplikasi atau website.
- Menyediakan kontrol manual menggunakan push button.
- Menampilkan status lampu melalui 20 LED indikator.
- Menyediakan mekanisme pemantauan status perangkat.
- Menggunakan koneksi Ethernet agar komunikasi jaringan lebih stabil.
- Memungkinkan integrasi dengan aplikasi billing atau manajemen meja billiard.
- Memastikan kontrol manual tetap dapat digunakan ketika koneksi internet atau MQTT terputus.

## 3. Arsitektur Sistem

### 3.1 Diagram Arsitektur

```text
                 WEBSITE BILLARD
                 (PHP / Laravel)
                        |
                        |
                    MQTT Client
                        |
                        v
                 +-------------+
                 | EMQX BROKER |
                 +-------------+
                        |
                        |
                    Ethernet
                        |
                        v
              +-------------------+
              | ESP32 + W5500     |
              |                   |
              | Main Controller   |
              | MQTT Client       |
              | Modbus Master     |
              +-------------------+
                   |          |
                   |          |
             RS485 Bus 1   RS485 Bus 2
                   |          |
                   v          v
            +-----------+  +----------------+
            | Arduino   |  | Relay Modules  |
            | Mega 2560 |  |                |
            |           |  | Slave ID 2     |
            | 20 Button |  | Slave ID 1     |
            | 20 LED    |  | Slave ID 3     |
            | 1 Switch  |  |                |
            +-----------+  +----------------+
                                |
                                v
                         20 LAMPU BILLIARD
```

### 3.2 Pembagian Fungsi Perangkat

| Perangkat | Fungsi |
|---|---|
| Website Billiard | Mengirim perintah kontrol lampu |
| EMQX Broker | Perantara komunikasi MQTT |
| ESP32 | Pengendali utama sistem |
| W5500 | Koneksi Ethernet |
| Arduino Mega | Pengelola panel kontrol fisik |
| RS485 Bus 1 | Komunikasi ESP32 dengan Mega |
| RS485 Bus 2 | Komunikasi ESP32 dengan modul relay |
| Modul Relay | Mengendalikan 20 lampu |
| Push Button | Kontrol manual setiap channel |
| LED | Indikator status setiap channel |
| Mode Switch | Memilih sumber kendali |

## 4. Spesifikasi Hardware

### 4.1 ESP32

ESP32 bertindak sebagai Main Controller.

Fungsi:
- Menghubungkan perangkat ke jaringan Ethernet.
- Berkomunikasi dengan MQTT Broker EMQX.
- Menerima perintah dari website.
- Mengirim status perangkat ke MQTT Broker.
- Menjadi Modbus Master untuk Arduino Mega.
- Menjadi Modbus Master untuk tiga modul relay.
- Mengatur logika kontrol manual dan otomatis.
- Melakukan sinkronisasi status relay dan LED.

Komponen:
- ESP32 DevKit.
- W5500 Ethernet Module.
- Dua interface RS485.

### 4.2 Arduino Mega 2560

Arduino Mega bertugas mengelola panel kontrol fisik.

| Komponen | Jumlah |
|---|---:|
| Push Button | 20 |
| LED indikator | 20 |
| Mode Switch | 1 |
| Total input | 21 |
| Total output | 20 |

#### Pin Push Button

Menggunakan konfigurasi `INPUT_PULLUP`.

| Tombol | Pin Mega |
|---|---:|
| 1–10 | D2–D11 |
| 11–20 | D22–D31 |

#### Pin LED

| LED | Pin Mega |
|---|---:|
| 1–10 | D36, D37, D38, D39, D35, D41, D42, D43, D44, D45 |
| 11–20 | A0–A9 |

#### Mode Switch

| Parameter | Nilai |
|---|---|
| Pin | D12 |
| Konfigurasi | INPUT_PULLUP |
| LOW | Manual |
| HIGH | Otomatis |

### 4.3 Modul Relay

Total 20 channel relay yang terdiri dari:

| Slave ID | Jumlah Relay |
|---|---:|
| 2 | 8 |
| 1 | 8 |
| 3 | 4 |
| **Total** | **20** |

Pengujian sebelumnya menggunakan urutan:
- Slave ID 2: Relay 1–8.
- Slave ID 1: Relay 1–8.
- Slave ID 3: Relay 4–1.

Urutan tersebut harus dipertahankan dan diverifikasi saat pemetaan channel fisik.

## 5. Komunikasi Modbus RTU

ESP32 menjadi Modbus Master, sedangkan Mega dan modul relay menjadi Modbus Slave.

### 5.1 Modbus ESP32 ke Mega

| Parameter | Konfigurasi |
|---|---|
| Protocol | Modbus RTU |
| Slave ID | 1 |
| Baudrate | 9600 |
| Data Format | 8N1 |
| Library Mega | ModbusRTU |
| Library ESP32 | ModbusRTU |
| Interval polling awal | 200 ms |

#### Holding Register

| Address | Jumlah | Keterangan |
|---|---:|---|
| 0–19 | 20 | Status LED / Relay |
| 20–39 | 20 | Status Push Button |
| 40 | 1 | Status Mode |

Nilai register:

**LED:**
- 0 = OFF
- 1 = ON

**Button:**
- 0 = Ditekan
- 1 = Dilepas

**Mode:**
- 0 = Manual
- 1 = Otomatis

### 5.2 Modbus ESP32 ke Modul Relay

| Parameter | Konfigurasi |
|---|---|
| Protocol | Modbus RTU |
| Baudrate | 9600 |
| Data Format | 8N1 |
| Library | ModbusMaster |
| Fungsi kontrol | Write Single Coil |

Pemetaan awal:

| Logical Channel | Slave ID | Relay Channel |
|---|---:|---:|
| 1–8 | 2 | 1–8 |
| 9–16 | 1 | 1–8 |
| 17–20 | 3 | 1–4 |

Catatan: Pemetaan logical channel merupakan rancangan awal. Posisi fisik relay pada modul ID 3 harus diverifikasi karena program pengujian menggunakan urutan terbalik.

### 5.3 Catatan Wiring RS485

ESP32 membutuhkan dua UART yang berbeda:

- UART untuk komunikasi Mega.
- UART untuk komunikasi modul relay.

Contoh pembagian pin yang dapat digunakan:

| Jalur | ESP32 RX | ESP32 TX |
|---|---:|---:|
| RS485 Mega | GPIO 26 | GPIO 27 |
| RS485 Relay | GPIO 16 | GPIO 17 |

Pin tersebut merupakan usulan awal dan harus disesuaikan dengan board serta wiring aktual.

Pada Arduino Mega:
- RX1 = Pin 19.
- TX1 = Pin 18.

Perlu diperhatikan bahwa W5500 menggunakan SPI. Pada konfigurasi VSPI standar ESP32, GPIO18 dan GPIO19 juga digunakan untuk SPI. Hindari benturan pin dengan UART dan periksa konfigurasi SPI aktual.

Gunakan transceiver RS485 yang sesuai, terminasi bus jika diperlukan, serta referensi ground yang memadai.

## 6. Komunikasi MQTT

MQTT digunakan sebagai protokol komunikasi antara website billiard dan ESP32.

Broker yang direncanakan:

```text
broker.emqx.io
```

Untuk implementasi produksi, disarankan menggunakan broker EMQX milik sendiri atau layanan broker terkelola dengan autentikasi dan ACL.

### 6.1 Struktur Topic

Prefix:

```text
billiard/{device_id}/
```

Topic yang direncanakan:

| Topic | Fungsi |
|---|---|
| `billiard/{device_id}/command` | Perintah kontrol lampu |
| `billiard/{device_id}/status` | Status seluruh lampu |
| `billiard/{device_id}/availability` | Status online/offline |
| `billiard/{device_id}/event` | Informasi perubahan status dan error |
| `billiard/{device_id}/response` | Respons terhadap perintah |

### 6.2 Format Payload Perintah

Menghidupkan lampu:

```json
{
  "command_id": "CMD-001",
  "channel": 1,
  "action": "ON"
}
```

Mematikan lampu:

```json
{
  "command_id": "CMD-002",
  "channel": 1,
  "action": "OFF"
}
```

Mengendalikan beberapa lampu:

```json
{
  "command_id": "CMD-003",
  "channels": [1, 2, 3, 5],
  "action": "ON"
}
```

Mematikan seluruh lampu:

```json
{
  "command_id": "CMD-004",
  "action": "ALL_OFF"
}
```

### 6.3 Format Payload Status

ESP32 mengirimkan status perangkat:

```json
{
  "device_id": "billiard-001",
  "mode": "AUTO",
  "online": true,
  "relays": [
    {
      "channel": 1,
      "state": "ON"
    },
    {
      "channel": 2,
      "state": "OFF"
    }
  ]
}
```

Status yang dikirim harus mencerminkan status relay aktual, bukan hanya perintah yang diterima.

### 6.4 Konfigurasi MQTT

| Parameter | Rekomendasi |
|---|---|
| QoS Command | 1 |
| QoS Status | 1 |
| Retain Command | False |
| Retain Status | True |
| Retain Availability | True |
| Keep Alive | 30–60 detik |
| Reconnect | Otomatis |
| Authentication | Username / Password |
| ACL | Perangkat hanya dapat mengakses topic miliknya |

Perintah harus memiliki `command_id` agar ESP32 dapat mendeteksi duplikasi pesan.

Perintah ON/OFF sebaiknya menggunakan status eksplisit, bukan perintah TOGGLE, sehingga pesan yang diterima berulang tidak membalikkan kondisi lampu secara tidak sengaja.

## 7. Logika Operasional

### 7.1 Mode Otomatis

Ketika saklar Mega berada pada posisi HIGH:

1. Mega mengirimkan informasi mode otomatis melalui register 40.
2. ESP32 mendeteksi mode otomatis.
3. ESP32 menerima perintah MQTT.
4. ESP32 memvalidasi channel dan action.
5. ESP32 mengirim perintah Modbus ke modul relay.
6. ESP32 membaca atau memverifikasi status relay.
7. ESP32 memperbarui register LED pada Mega.
8. LED indikator mengikuti status relay.
9. ESP32 memublikasikan status terbaru ke MQTT.

Pada mode ini, push button tidak boleh mengubah status relay.

### 7.2 Mode Manual

Ketika saklar Mega berada pada posisi LOW:

1. Mega mengirimkan informasi mode manual.
2. ESP32 mendeteksi mode manual.
3. Push button Mega menjadi sumber kontrol.
4. Mega mendeteksi perubahan tombol.
5. Mega mengirimkan status tombol kepada ESP32.
6. ESP32 meneruskan perintah ke modul relay.
7. ESP32 memperbarui status LED berdasarkan status relay.
8. ESP32 memublikasikan perubahan status ke MQTT.

Pada mode ini, perintah kontrol dari website ditolak.

### 7.3 Perubahan Mode

Ketika saklar berubah posisi:

- ESP32 harus mendeteksi perubahan mode.
- Sumber kontrol yang sebelumnya aktif harus dinonaktifkan.
- Status aktual seluruh relay harus dibaca.
- Status LED Mega harus disinkronkan.
- Status mode dan relay harus diperbarui ke MQTT.

Perlu ditentukan kebijakan apakah perubahan mode mempertahankan kondisi lampu terakhir atau mematikan seluruh lampu. Rekomendasi awal: mempertahankan kondisi terakhir dan tidak melakukan perubahan output hanya karena pergantian mode.

## 8. Penanganan Gangguan

### 8.1 MQTT Terputus

Jika koneksi MQTT terputus:
- ESP32 mencoba reconnect secara otomatis.
- Kontrol manual tetap berfungsi.
- Status terakhir tidak boleh dianggap sebagai status aktual tanpa verifikasi.
- Setelah reconnect, ESP32 membaca status relay dan memperbarui status MQTT.

### 8.2 Ethernet Terputus

Jika kabel Ethernet terputus:
- ESP32 mendeteksi status link.
- ESP32 tetap menjalankan task Modbus.
- Relay tidak boleh berubah secara tidak sengaja.
- ESP32 mencoba memulihkan koneksi jaringan.

### 8.3 Modbus Terputus

Jika komunikasi Mega terputus:
- ESP32 menandai status Mega sebagai offline.
- Perintah dari website harus ditolak karena status mode tidak dapat dipastikan.
- Status gangguan dikirimkan ke MQTT.

Jika modul relay tidak merespons:
- ESP32 menandai channel terkait sebagai error.
- Status command tidak boleh langsung dianggap berhasil.
- Sistem mengirimkan informasi kegagalan ke website.

## 9. Keamanan Sistem

Untuk implementasi produksi:

- Gunakan broker MQTT dengan autentikasi.
- Gunakan ACL agar perangkat hanya dapat mengakses topic yang diizinkan.
- Gunakan TLS jika didukung konfigurasi jaringan.
- Website tidak boleh mengekspos kredensial MQTT perangkat secara langsung kepada browser.
- Backend website bertindak sebagai MQTT client.
- Setiap perangkat memiliki identitas unik.
- Validasi seluruh payload yang diterima.
- Batasi perintah berdasarkan mode operasi.
- Gunakan proteksi kelistrikan dan enclosure yang sesuai.

## 10. Tahapan Pengembangan

### Tahap 1 — Validasi Hardware

- [ ] Memastikan semua push button berfungsi.
- [ ] Memastikan semua LED berfungsi.
- [ ] Memastikan switch mode berfungsi.
- [ ] Memastikan 20 relay berfungsi.
- [ ] Memastikan komunikasi RS485 Mega.
- [ ] Memastikan komunikasi RS485 relay.
- [ ] Menentukan pin UART final.
- [ ] Memastikan komunikasi W5500.

### Tahap 2 — Firmware Arduino Mega

- [ ] Implementasi 20 push button.
- [ ] Implementasi debounce.
- [ ] Implementasi 20 LED.
- [ ] Implementasi switch mode.
- [ ] Implementasi Modbus Slave ID 1.
- [ ] Implementasi register 0–40.
- [ ] Implementasi pembatasan kontrol berdasarkan mode.

### Tahap 3 — Firmware ESP32

- [ ] Integrasi W5500 Ethernet.
- [ ] Integrasi MQTT Client.
- [ ] Integrasi Modbus Master ke Mega.
- [ ] Integrasi Modbus Master ke relay.
- [ ] Implementasi pemetaan 20 channel.
- [ ] Implementasi logika manual dan otomatis.
- [ ] Implementasi sinkronisasi status.
- [ ] Implementasi reconnect.
- [ ] Implementasi penanganan error.

### Tahap 4 — Integrasi MQTT

- [ ] Menentukan device ID.
- [ ] Menentukan struktur topic.
- [ ] Menentukan format payload.
- [ ] Implementasi perintah ON/OFF.
- [ ] Implementasi ALL ON/OFF jika diperlukan.
- [ ] Implementasi status relay.
- [ ] Implementasi availability.
- [ ] Implementasi command acknowledgment.

### Tahap 5 — Integrasi Website Billiard

- [ ] Website terhubung ke MQTT melalui backend.
- [ ] Implementasi tombol ON/OFF.
- [ ] Integrasi status meja.
- [ ] Integrasi status perangkat.
- [ ] Integrasi pencatatan aktivitas.
- [ ] Pengujian kontrol dari website.

### Tahap 6 — Pengujian Keseluruhan

- [ ] Uji seluruh 20 channel.
- [ ] Uji mode manual.
- [ ] Uji mode otomatis.
- [ ] Uji pergantian mode ketika lampu menyala.
- [ ] Uji MQTT terputus.
- [ ] Uji Ethernet terputus.
- [ ] Uji ESP32 restart.
- [ ] Uji Mega restart.
- [ ] Uji komunikasi Modbus gagal.
- [ ] Uji perintah duplikat.
- [ ] Uji sinkronisasi status.
- [ ] Uji operasional jangka panjang.

## 11. Keputusan Teknis yang Belum Final

Beberapa detail perlu dikonfirmasi sebelum firmware final dibuat:

| No. | Hal yang Perlu Dipastikan | Status |
|---|---|---|
| 1 | Pemetaan fisik 20 meja ke 20 relay | Belum |
| 2 | Pemetaan relay ID 3 | Perlu verifikasi |
| 3 | Pin UART final ESP32 | Belum |
| 4 | Transceiver RS485 yang digunakan | Perlu konfirmasi |
| 5 | Broker EMQX produksi | Belum |
| 6 | IP dan konfigurasi Ethernet | Belum |
| 7 | Kebijakan saat pergantian mode | Usulan tersedia |
| 8 | Kebijakan saat ESP32 restart | Belum |
| 9 | Format integrasi website | Belum |
| 10 | Mekanisme autentikasi perangkat | Belum |

## 12. Target Akhir Proyek

Sistem dinyatakan berhasil apabila:

1. Semua 20 lampu dapat dikendalikan secara individual.
2. Semua 20 lampu dapat dikendalikan melalui website.
3. Semua 20 lampu dapat dikendalikan melalui push button.
4. Switch dapat memilih sumber kontrol.
5. LED indikator menunjukkan status aktual relay.
6. Komunikasi MQTT dapat berjalan stabil.
7. Sistem mampu pulih setelah koneksi terputus.
8. Sistem tetap menyediakan kontrol manual ketika jaringan bermasalah.
9. Status perangkat dapat dipantau dari website.

---

**Dokumen ini merupakan baseline perencanaan teknis.** Implementasi firmware final dilakukan setelah pemetaan hardware, wiring, dan seluruh aturan operasional dikonfirmasi.