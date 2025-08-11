#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFiS3.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"

// ---- BME280 ----
#define BME_ADDR 0x76            // change to 0x77 if your module uses that
Adafruit_BME280 bme;

// ---- Wi-Fi + MQTT ----
WiFiClient net;
PubSubClient mqtt(net);

const unsigned PUBLISH_MS = 5000; // publish interval
unsigned long lastPub = 0;

void connectWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(SECRET_SSID);

  // Use the SAME loop style that worked for you
  while (WiFi.begin(SECRET_SSID, SECRET_PASS) != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.print("\nWi-Fi connected. IP: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  while (!mqtt.connected()) {
    String clientId = String("gr12-uno-") + String(millis() & 0xFFFF, HEX);
    Serial.print("MQTT connecting… ");
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("OK");
    } else {
      Serial.print("fail rc=");
      Serial.print(mqtt.state());  // -4 timeout, -2 connect fail, 5 not authorized, etc.
      Serial.println(" (retry in 2s)");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Wire.begin();

  // BME280 init (try 0x76 first; switch to 0x77 if needed)
  if (!bme.begin(BME_ADDR)) {
    Serial.println("❌ BME280 not found at 0x76. Trying 0x77…");
    if (!bme.begin(0x77)) {
      Serial.println("❌ BME280 not found. Check wiring/address.");
      while (true) delay(1000);
    }
  }
  Serial.println("✅ BME280 ready");

  connectWiFi();
  connectMQTT();
}

void loop() {
  // keep connections alive
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi dropped, reconnecting…");
    connectWiFi();
  }
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();

  // publish periodically
  unsigned long now = millis();
  if (now - lastPub >= PUBLISH_MS) {
    lastPub = now;

    float t = bme.readTemperature();          // °C
    float h = bme.readHumidity();             // %RH
    float p = bme.readPressure() / 100.0F;    // hPa

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"group\":12,\"temp\":%.2f,\"hum\":%.2f,\"pres\":%.2f}", t, h, p);

    const char* topic = "h4prog/g12/telemetry";

    bool ok = mqtt.publish(topic, payload);
    Serial.print("PUB "); Serial.print(topic);
    Serial.print(" -> "); Serial.println(ok ? "OK" : "FAIL");
  }
}
