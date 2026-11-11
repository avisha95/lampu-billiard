
#include <SPI.h>
#include <Ethernet.h>
#include <MQTT.h>

// Konfigurasi W5500
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};

#define W5500_CS 5

// Konfigurasi MQTT
const char* mqttBroker = "broker.emqx.io";
const char* mqttClientID = "espw5500";
const char* mqttTopic = "/EthernetW500";

EthernetClient net;
MQTTClient client;

unsigned long lastMillis = 0;
unsigned long lastEthernetCheck = 0;

bool ethernetConnected = false;

// =====================================
// Koneksi Ethernet menggunakan DHCP
// =====================================
bool connectEthernet() {
  Serial.println("\nMenghubungkan Ethernet via DHCP...");

  Ethernet.init(W5500_CS);

  // Mencoba mendapatkan IP otomatis
  if (Ethernet.begin(mac) == 0) {
    Serial.println("DHCP gagal mendapatkan IP!");
    ethernetConnected = false;
    return false;
  }

  delay(1000);

  Serial.println("Ethernet terhubung!");
  Serial.print("IP Address : ");
  Serial.println(Ethernet.localIP());

  Serial.print("Subnet Mask: ");
  Serial.println(Ethernet.subnetMask());

  Serial.print("Gateway    : ");
  Serial.println(Ethernet.gatewayIP());

  ethernetConnected = true;
  return true;
}

// =====================================
// Koneksi MQTT
// =====================================
bool connectMQTT() {
  Serial.print("Menghubungkan MQTT...");

  if (client.connect(mqttClientID, "", "")) {
    Serial.println("\nMQTT terhubung!");

    if (client.subscribe(mqttTopic)) {
      Serial.print("Subscribe: ");
      Serial.println(mqttTopic);
    }

    return true;
  }

  Serial.println(" gagal!");
  return false;
}

// =====================================
// Callback saat menerima pesan MQTT
// =====================================
void messageReceived(String &topic, String &payload) {
  Serial.print("Pesan masuk [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(payload);
}

// =====================================
// Setup
// =====================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==============================");
  Serial.println("ESP32 + W5500 + MQTT");
  Serial.println("==============================");

  // Inisialisasi Ethernet dan DHCP
  if (connectEthernet()) {
    client.begin(mqttBroker, net);
    client.onMessage(messageReceived);

    connectMQTT();
  }
}

// =====================================
// Loop
// =====================================
void loop() {
  // Periksa status Ethernet setiap 5 detik
  if (millis() - lastEthernetCheck >= 5000) {
    lastEthernetCheck = millis();

    Ethernet.maintain();

    if (Ethernet.linkStatus() == LinkOFF) {
      if (ethernetConnected) {
        Serial.println("Kabel Ethernet terputus!");
        ethernetConnected = false;
      }
    } else {
      if (!ethernetConnected) {
        Serial.println("Link Ethernet aktif, mencoba DHCP...");
        connectEthernet();
      }
    }
  }

  // Jika Ethernet belum tersambung, tunggu
  if (!ethernetConnected) {
    delay(10);
    return;
  }

  // Proses komunikasi MQTT
  client.loop();

  // Hubungkan ulang MQTT jika terputus
  if (!client.connected()) {
    static unsigned long lastMQTTAttempt = 0;

    if (millis() - lastMQTTAttempt >= 3000) {
      lastMQTTAttempt = millis();
      connectMQTT();
    }
  }

  // Publish pesan setiap 1 detik
  if (client.connected() && millis() - lastMillis >= 1000) {
    lastMillis = millis();

    if (client.publish(mqttTopic, "world")) {
      Serial.println("Publish: world");
    } else {
      Serial.println("Publish gagal!");
    }
  }

  delay(10);
}
