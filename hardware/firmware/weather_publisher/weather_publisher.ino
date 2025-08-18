#include <WiFiS3.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include "Arduino_secrets.h"



#ifndef MEASUREMENT_NAME
#define MEASUREMENT_NAME   "weather"
#endif
#ifndef TAG_DEVICE
#define TAG_DEVICE         "uno-r4"
#endif
#ifndef TAG_LOCATION
#define TAG_LOCATION       "school"
#endif

const unsigned long SENSOR_INTERVAL_MS = 1000;   //  1s
const unsigned long SEND_INTERVAL_MS   = 5000;   //  5s

WiFiSSLClient ssl;

enum SensorType { NONE, BME_280, BMP_280 };
SensorType sensorType = NONE;
Adafruit_BME280 bme;
Adafruit_BMP280 bmp;
uint8_t i2cAddr = 0;

struct Readings { float tC, h, p_hPa; bool ok; };

unsigned long lastSensorMs = 0;
unsigned long lastSendMs   = 0;
Readings latest{NAN,NAN,NAN,false};


String urlEncode(const String &s){
  String o; o.reserve(s.length()+8);
  static const char HEX_CHARS[] = "0123456789ABCDEF";  
  for (size_t i=0;i<s.length();i++){
    unsigned char c = static_cast<unsigned char>(s[i]);
    bool ok=(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~';
    if (ok) o += (char)c;
    else {
      o += '%';
      o += HEX_CHARS[(c>>4)&0xF];
      o += HEX_CHARS[c&0xF];
    }
  }
  return o;
}

void i2cScan() {
  Serial.println(F("\n[I2C] scanning..."));
  byte found=0;
  for (byte addr=1; addr<127; addr++){
    Wire.beginTransmission(addr);
    if (Wire.endTransmission()==0){
      Serial.print(F(" - device at 0x"));
      if (addr<16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
    delay(2);
  }
  if (!found) Serial.println(F(" (no I2C devices found)"));
}

uint8_t readChipId(uint8_t addr){
  Wire.beginTransmission(addr);
  Wire.write(0xD0); 
  if (Wire.endTransmission(false)!=0) return 0;
  Wire.requestFrom((int)addr, 1);
  if (Wire.available()) return Wire.read();
  return 0;
}

bool connectWifiStrict() {
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0=millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-t0<25000) { delay(500); Serial.print('.'); }
  if (WiFi.status()!=WL_CONNECTED) { Serial.println(F("\nWi-Fi link failed")); return false; }
  unsigned long t1=millis();
  while (WiFi.localIP()==IPAddress(0,0,0,0) && millis()-t1<10000) delay(200);
  Serial.print(F("\nIP: ")); Serial.println(WiFi.localIP());
  return WiFi.localIP()!=IPAddress(0,0,0,0);
}

void netDiag() {
  Serial.print(F("WiFi IP: ")); Serial.println(WiFi.localIP());
  IPAddress ip;
  if (WiFi.hostByName(INFLUX_HOST, ip)) {
    Serial.print(F("DNS -> ")); Serial.println(ip);
  } else {
    Serial.println(F("DNS resolve FAILED"));
  }
  WiFiClient probe;
  if (probe.connect(INFLUX_HOST, INFLUX_PORT)) {
    Serial.println(F("TCP 443 to host: OK"));
    probe.stop();
  } else {
    Serial.println(F("TCP 443 to host: FAILED"));
  }
}


bool ensureTLS() {
  if (ssl.connected()) return true;
  delay(50); 
  return ssl.connect(INFLUX_HOST, INFLUX_PORT);
}


void drainClient(WiFiSSLClient &c, unsigned long maxMs=500) {
  unsigned long t=millis();
  while (c.available() && millis()-t<maxMs) { c.read(); }
}

// -------------------- Influx --------------------
bool httpsGetHealth(){
  if (!ensureTLS()) { Serial.println(F("TLS connect failed (health)")); return false; }
  String req; req.reserve(96);
  req  = "GET /health HTTP/1.1\r\nHost: ";
  req += INFLUX_HOST;
  req += "\r\nConnection: keep-alive\r\n\r\n";
  ssl.print(req);
  String status = ssl.readStringUntil('\n'); status.trim();
  Serial.print(F("[Influx] Health: ")); Serial.println(status);
  drainClient(ssl);
  return status.startsWith("HTTP/1.1 200");
}

bool writeToInfluxHTTPS(const Readings &r){

  String lp; lp.reserve(96);
  lp  = MEASUREMENT_NAME;
  lp += ",device="; lp += TAG_DEVICE;
  lp += ",location="; lp += TAG_LOCATION;
  lp += ' ';
  bool first=true;
  if (!isnan(r.tC))     { lp += "temperature="; lp += String(r.tC,2); first=false; }
  if (sensorType==BME_280 && !isnan(r.h)) {
    if (!first) lp+=','; lp += "humidity="; lp += String(r.h,1); first=false;
  }
  if (!isnan(r.p_hPa))  { if (!first) lp+=','; lp += "pressure="; lp += String(r.p_hPa,1); }

  String path; path.reserve(96);
  path  = "/api/v2/write?org=";
  path += urlEncode(String(INFLUX_ORG));
  path += "&bucket=";
  path += urlEncode(String(INFLUX_BUCKET));
  path += "&precision=ns";

  if (!ensureTLS()) { Serial.println(F("TLS connect failed")); return false; }

  
  String hdr; hdr.reserve(200);
  hdr  = "POST ";
  hdr += path;
  hdr += " HTTP/1.1\r\nHost: ";
  hdr += INFLUX_HOST;
  hdr += "\r\nAuthorization: Token ";
  hdr += INFLUX_TOKEN;
  hdr += "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: ";
  hdr += String(lp.length());
  hdr += "\r\nConnection: keep-alive\r\nKeep-Alive: timeout=30\r\n\r\n";

  ssl.print(hdr);
  ssl.print(lp);

  String status = ssl.readStringUntil('\n'); status.trim();
  Serial.print(F("[Influx] Write status: ")); Serial.println(status);

  drainClient(ssl, 600);
  bool ok = status.startsWith("HTTP/1.1 204");
  if (!ok) { ssl.stop(); delay(100); } 
  return ok;
}

// -------------------- Sensors --------------------
void initSensors(){
  Wire.begin();
  Wire.setClock(100000);
  i2cScan();

  uint8_t addrs[2] = {0x76, 0x77};
  for (uint8_t i=0;i<2;i++){
    if (bme.begin(addrs[i])) {
      uint8_t id = readChipId(addrs[i]);
      Serial.print(F("Found device at 0x")); Serial.print(addrs[i], HEX);
      Serial.print(F("  chipID=0x")); Serial.println(id, HEX);
      if (id==0x60) {
        sensorType = BME_280; i2cAddr = addrs[i];
        Serial.println(F("Confirmed: BME280 (has humidity)."));
        bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                        Adafruit_BME280::SAMPLING_X2, 
                        Adafruit_BME280::SAMPLING_X2, 
                        Adafruit_BME280::SAMPLING_X2, 
                        Adafruit_BME280::FILTER_X4,
                        Adafruit_BME280::STANDBY_MS_500);
        return;
      }
    }
  }
  for (uint8_t i=0;i<2;i++){
    if (bmp.begin(addrs[i])) {
      uint8_t id = readChipId(addrs[i]);
      Serial.print(F("Found device at 0x")); Serial.print(addrs[i], HEX);
      Serial.print(F("  chipID=0x")); Serial.println(id, HEX);
      if (id==0x58) {
        sensorType = BMP_280; i2cAddr = addrs[i];
        Serial.println(F("Detected BMP280 (no humidity)."));
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::FILTER_X4,
                        Adafruit_BMP280::STANDBY_MS_500);
        return;
      }
    }
  }

  Serial.println(F("No BME280/BMP280 detected at 0x76/0x77. Check wiring: VIN->3.3V, GND, SDA/SCL."));
  sensorType = NONE;
}

Readings readSensors(){
  Readings r{NAN,NAN,NAN,false};
  if (sensorType==BME_280) {
    bme.takeForcedMeasurement();
    r.tC = bme.readTemperature();
    r.h  = bme.readHumidity();
    float pPa = bme.readPressure();
    r.p_hPa = isnan(pPa) ? NAN : pPa/100.0f;
    r.ok = !(isnan(r.tC) || isnan(r.h) || isnan(r.p_hPa));
  } else if (sensorType==BMP_280) {
    r.tC = bmp.readTemperature();
    float pPa = bmp.readPressure();
    r.p_hPa = isnan(pPa) ? NAN : pPa/100.0f;
    r.ok = !(isnan(r.tC) || isnan(r.p_hPa));
  }
  return r;
}

// -------------------- Arduino lifecycle --------------------
void setup(){
  Serial.begin(115200);
  delay(300);

  if (!connectWifiStrict()) {
    Serial.println(F("No DHCP IP."));
    while (true) { delay(1000); }
  }

  netDiag();
  httpsGetHealth();
  initSensors();
}

void loop(){
  const unsigned long now = millis();


  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    latest = readSensors();
    if (sensorType==NONE) {
      Serial.println(F("Sensor not detected."));
    } else {
      Serial.print(sensorType==BME_280 ? F("BME280") : F("BMP280"));
      Serial.print(F(" @0x")); Serial.print(i2cAddr, HEX);
      Serial.print(F("  T=")); Serial.print(latest.tC,2); Serial.print(F("°C  "));
      if (sensorType==BME_280) { Serial.print(F("H=")); Serial.print(latest.h,1); Serial.print(F("%  ")); }
      Serial.print(F("P=")); Serial.print(latest.p_hPa,1); Serial.println(F(" hPa"));
    }
    lastSensorMs = now;
  }

 
  if (now - lastSendMs >= SEND_INTERVAL_MS) {
    if (sensorType!=NONE && latest.ok) {
      if (writeToInfluxHTTPS(latest)) Serial.println(F("Write OK"));
      else                            Serial.println(F("Write FAILED"));
    } else {
      Serial.println(F("Skip write (no sensor or invalid data)."));
    }
    lastSendMs = now;
  }


  if (WiFi.status()!=WL_CONNECTED) {
    Serial.println(F("Wi-Fi dropped, reconnecting..."));
    connectWifiStrict();
  }

  delay(25);
}
