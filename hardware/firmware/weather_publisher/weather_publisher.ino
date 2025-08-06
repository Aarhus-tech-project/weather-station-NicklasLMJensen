#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFiS3.h>            // UNO R4 WiFi’s Wi-Fi library
#include <PubSubClient.h>      // MQTT client

#include "arduino_secrets.h"   // Wi-Fi + MQTT settings

// I²C addresses
#define BME_ADDR      0x76

// Objects
Adafruit_BME280 bme;            // BME280 sensor
WiFiClient    net;             // Network client for MQTT
PubSubClient  mqtt(net);       // MQTT client

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // 1) Initialize BME280
  Wire.begin();  
  if (!bme.begin(BME_ADDR)) {
    Serial.println("❌ BME280 not found! Check wiring.");
    while (1);
  }
  Serial.println("✅ BME280 OK");

  // 2) Connect Wi-Fi
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(SECRET_SSID);
  while (WiFi.begin(SECRET_SSID, SECRET_PASS) != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("✔️  Connected, IP = ");
  Serial.println(WiFi.localIP());

  // 3) Setup MQTT
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  Serial.print("Connecting to MQTT broker: ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  // Reconnect in setup so we start “live”
  while (!mqtt.connected()) {
    if (mqtt.connect("UNO_R4_WeatherClient")) {
      Serial.println("✔️  MQTT connected");
    } else {
      Serial.print("❌ MQTT connect failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" retrying in 2s");
      delay(2000);
    }
  }
}

void loop() {
  // Keep MQTT alive
  if (!mqtt.connected()) {
    // attempt to reconnect
    while (!mqtt.connected()) {
      Serial.print("Reconnecting MQTT… ");
      if (mqtt.connect("UNO_R4_WeatherClient")) {
        Serial.println("connected");
      } else {
        Serial.print("failed, rc=");
        Serial.println(mqtt.state());
        delay(2000);
      }
    }
  }
  mqtt.loop();

  // Read sensor
  float temp = bme.readTemperature();    // °C
  Serial.print("Temp = ");
  Serial.print(temp);
  Serial.println(" °C");

  // Publish as JSON
  char payload[64];
  snprintf(payload, sizeof(payload),
           "{\"temperature\":%.2f}", temp);
  if (mqtt.publish("weather/temperature", payload)) {
    Serial.println("Published! → weather/temperature");
  } else {
    Serial.println("Publish failed");
  }

  delay(120000);  // wait before next reading
}
