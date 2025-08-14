#include <WiFiS3.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include "Arduino_secrets.h"

WiFiSSLClient ssl;

// ---- Sensor state ----
enum SensorType { NONE, BME_280, BMP_280 };
SensorType sensorType = NONE;
Adafruit_BME280 bme;
Adafruit_BMP280 bmp;
uint8_t i2cAddr = 0;

// Small container for readings (define before use)
struct Readings { float tC, h, p_hPa; bool ok; };

// ---- Helpers ----
String urlEncode(const String &s){
  String o; const char*h="0123456789ABCDEF";
  for (size_t i=0;i<s.length();i++){
    char c=s[i];
    bool ok=(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~';
    if (ok) o+=c; else { o+='%'; o+=h[(c>>4)&0xF]; o+=h[c&0xF]; }
  }
  return o;
}

void i2cScan() {
  Serial.println("\n[I2C] scanning...");
  byte found=0;
  for (byte addr=1; addr<127; addr++){
    Wire.beginTransmission(addr);
    if (Wire.endTransmission()==0){
      Serial.print(" - device at 0x");
      if (addr<16) Serial.print("0");
      Serial.println(addr, HEX);
      found++;
    }
    delay(2);
  }
  if (!found) Serial.println(" (no I2C devices found)");
}

uint8_t readChipId(uint8_t addr){
  Wire.beginTransmission(addr);
  Wire.write(0xD0); // chip id register
  if (Wire.endTransmission(false)!=0) return 0;
  Wire.requestFrom((int)addr, 1);
  if (Wire.available()) return Wire.read();
  return 0;
}

bool connectWifiStrict() {
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0=millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-t0<25000) { delay(500); Serial.print("."); }
  if (WiFi.status()!=WL_CONNECTED) { Serial.println("\nWi-Fi link failed"); return false; }
  unsigned long t1=millis();
  while (WiFi.localIP()==IPAddress(0,0,0,0) && millis()-t1<10000) delay(200);
  Serial.print("\nIP: "); Serial.println(WiFi.localIP());
  return WiFi.localIP()!=IPAddress(0,0,0,0);
}

// ---- NEW: network diagnostics (DNS + TCP 443) ----
void netDiag() {
  Serial.print("WiFi IP: "); Serial.println(WiFi.localIP());

  IPAddress ip;
  if (WiFi.hostByName(INFLUX_HOST, ip)) {
    Serial.print("DNS -> "); Serial.println(ip);
  } else {
    Serial.println("DNS resolve FAILED");
  }

  WiFiClient probe;
  if (probe.connect(INFLUX_HOST, INFLUX_PORT)) {
    Serial.println("TCP 443 to host: OK");
    probe.stop();
  } else {
    Serial.println("TCP 443 to host: FAILED");
  }
}

bool httpsGetHealth(){
  if (!ssl.connect(INFLUX_HOST, INFLUX_PORT)) { Serial.println("TLS connect failed (health)"); return false; }
  String req = String("GET /health HTTP/1.1\r\n") +
               "Host: " + String(INFLUX_HOST) + "\r\n" +
               "Connection: close\r\n\r\n";
  ssl.print(req);
  String status = ssl.readStringUntil('\n'); status.trim();
  Serial.print("[Influx] Health: "); Serial.println(status);
  while (ssl.connected()) while (ssl.available()) ssl.read();
  ssl.stop();
  return status.startsWith("HTTP/1.1 200");
}

bool writeToInfluxHTTPS(const String &lp){
  String path = "/api/v2/write?org="+urlEncode(INFLUX_ORG)+
                "&bucket="+urlEncode(INFLUX_BUCKET)+
                "&precision=ns";

  if (!ssl.connect(INFLUX_HOST, INFLUX_PORT)) {
    Serial.println("TLS connect failed");
    return false;
  }

  String hdr = String("POST ")+path+" HTTP/1.1\r\n"+
               "Host: "+String(INFLUX_HOST)+"\r\n"+
               "Authorization: Token "+String(INFLUX_TOKEN)+"\r\n"+
               "Content-Type: text/plain; charset=utf-8\r\n"+
               "Content-Length: "+String(lp.length())+"\r\n"+
               "Connection: close\r\n\r\n";
  ssl.print(hdr);
  ssl.print(lp);

  String status = ssl.readStringUntil('\n'); // expect: HTTP/1.1 204 No Content
  status.trim();
  Serial.print("[Influx] Write status: "); Serial.println(status);

  while (ssl.connected()) while (ssl.available()) ssl.read();
  ssl.stop();
  return status.startsWith("HTTP/1.1 204");
}

// ---- Sensor init & read ----
void initSensors(){
  Wire.begin();
  Wire.setClock(100000);
  i2cScan();

  // Try BME280 first (0x76/0x77)
  uint8_t addrs[2] = {0x76, 0x77};
  for (uint8_t i=0;i<2;i++){
    if (bme.begin(addrs[i])) {
      uint8_t id = readChipId(addrs[i]);
      Serial.print("Found device at 0x"); Serial.print(addrs[i], HEX);
      Serial.print("  chipID=0x"); Serial.println(id, HEX);
      if (id==0x60) {
        sensorType = BME_280; i2cAddr = addrs[i];
        Serial.println("Confirmed: BME280 (has humidity).");
        bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                        Adafruit_BME280::SAMPLING_X2, // temp
                        Adafruit_BME280::SAMPLING_X2, // pressure
                        Adafruit_BME280::SAMPLING_X2, // humidity
                        Adafruit_BME280::FILTER_X4,
                        Adafruit_BME280::STANDBY_MS_500);
        return;
      }
    }
  }
  // Try BMP280
  for (uint8_t i=0;i<2;i++){
    if (bmp.begin(addrs[i])) {
      uint8_t id = readChipId(addrs[i]);
      Serial.print("Found device at 0x"); Serial.print(addrs[i], HEX);
      Serial.print("  chipID=0x"); Serial.println(id, HEX);
      if (id==0x58) {
        sensorType = BMP_280; i2cAddr = addrs[i];
        Serial.println("Detected BMP280 (no humidity).");
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::FILTER_X4,
                        Adafruit_BMP280::STANDBY_MS_500);
        return;
      }
    }
  }

  Serial.println("No BME280/BMP280 detected at 0x76/0x77. Check wiring: VIN->3.3V, GND, SDA/SCL.");
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

// ---- Arduino lifecycle ----
void setup(){
  Serial.begin(115200); delay(300);

  if (!connectWifiStrict()) { Serial.println("No DHCP IP."); while(true){ delay(1000);} }

  // NEW: run the diagnostics once so we see DNS & TCP status
  netDiag();

  httpsGetHealth();   // optional: prints HTTP/1.1 200 OK if tunnel+Influx are good
  initSensors();
}

void loop(){
  Readings r = readSensors();

  // Print to Serial
  if (sensorType==NONE) {
    Serial.println("Sensor not detected.");
  } else {
    Serial.print(sensorType==BME_280 ? "BME280" : "BMP280");
    Serial.print(" @0x"); Serial.print(i2cAddr, HEX);
    Serial.print("  T="); Serial.print(r.tC,2); Serial.print("°C  ");
    if (sensorType==BME_280) { Serial.print("H="); Serial.print(r.h,1); Serial.print("%  "); }
    Serial.print("P="); Serial.print(r.p_hPa,1); Serial.println(" hPa");
  }

  // Build line protocol (only valid fields)
  String lp = "weather,device=uno-r4,location=school ";
  bool first = true;
  if (!isnan(r.tC))     { lp += "temperature=" + String(r.tC,2); first=false; }
  if (sensorType==BME_280 && !isnan(r.h)) {
    lp += (first?"":","); lp += "humidity=" + String(r.h,1); first=false;
  }
  if (!isnan(r.p_hPa))  { lp += (first?"":","); lp += "pressure=" + String(r.p_hPa,1); }

  if (sensorType!=NONE && !lp.endsWith(" ")) {
    if (writeToInfluxHTTPS(lp)) Serial.println("Write OK");
    else                        Serial.println("Write FAILED");
  } else {
    Serial.println("Skip write (no sensor or invalid data).");
  }

  delay(5000); // 5s
}
