#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- DONANIM PİN TANIMLARI ---
#define MOISTURE_PIN 34   
#define RAIN_PIN     35   
#define RELAY_PIN    26   
#define BATTERY_PIN  32   

// --- SİSTEM & AĞ AYARLARI ---
const char* ssid = "Zeynep"; 
const char* password = "bisifrebul00"; 
const char* backendUrl = "http://192.168.36.251/api/sensors/data";
 
// NOT: Hava durumu (Open-Meteo) işlemleri ESP32'nin RAM'ini yormaması 
// ve daha kararlı çalışması için sunucu (Node.js) tarafına taşınmıştır.

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(115200);

  // Pin Modları
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(RAIN_PIN, INPUT_PULLUP);
  pinMode(BATTERY_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);

  // Röle Low-Level Trigger olduğu için HIGH komutu pompaya giden elektriği KESER.
  digitalWrite(RELAY_PIN, HIGH);

  // Ekranı Başlat
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sistem Basliyor");

  // WiFi Bağlantısı
  WiFi.begin(ssid, password);
  Serial.print("WiFi Baglaniyor...");
}

void loop() {
  // 1. SENSÖR VERİLERİNİ TOPLA
  float moisture = readMoisture();
  bool isRaining = (digitalRead(RAIN_PIN) == LOW); // LOW = Yağmur algılandı
  float batteryV = readBattery();
  float batteryPct = constrain((batteryV - 3.0) / (4.2 - 3.0) * 100.0, 0, 100);

  // 2. LCD EKRANI GÜNCELLE
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Nem: %" + String((int)moisture));
  lcd.setCursor(10, 0);
  lcd.print(isRaining ? "YAGMUR" : "KURU");

  // 3. KARAR MEKANİZMASI
  if (WiFi.status() == WL_CONNECTED) {
    processOnlineMode(moisture, isRaining, batteryV, batteryPct);
  } else {
    processOfflineMode(moisture, isRaining);
  }

  // 1 dakika bekle ve tekrarla
  delay(60000);
}

// --- KARAR MOTORLARI ---

void processOnlineMode(float m, bool r, float bv, float bp) {
  HTTPClient http;
  http.begin(backendUrl);
  http.addHeader("Content-Type", "application/json");

  // Yalnızca donanım verilerini gönderiyoruz (Bellek tasarrufu)
  StaticJsonDocument<256> doc;
  doc["moisture"] = m;
  doc["is_raining"] = r;
  doc["battery_voltage"] = bv;
  doc["battery_level"] = bp;

  String requestBody;
  serializeJson(doc, requestBody);
  
  int httpCode = http.POST(requestBody);
  if (httpCode == 200) {
    String response = http.getString();
    StaticJsonDocument<256> resDoc;
    deserializeJson(resDoc, response);

    String action = resDoc["action"];
    int duration = resDoc["duration"];

    // Backend "SULA" derse pompayı çalıştır
    if (action == "IRRIGATE") {
      runPump(duration);
    }
  } else {
    Serial.print("Sunucuya baglanilamadi, HTTP Kodu: ");
    Serial.println(httpCode);
  }
  http.end();
}

void processOfflineMode(float m, bool r) {
  if (r) return; // Fiziksel yağmur varsa sulama yapma

  if (m < 30.0) {
    runPump(10); // Acil durum sulaması
  }
}

// --- YARDIMCI FONKSİYONLAR ---

void runPump(int seconds) {
  seconds = constrain(seconds, 0, 30); 
  lcd.setCursor(0, 1);
  lcd.print("POMPA AKTIF!    "); 
  
  digitalWrite(RELAY_PIN, LOW); // Röleyi AÇ
  delay(seconds * 1000);
  
  digitalWrite(RELAY_PIN, HIGH); // Röleyi KAPAT
  
  lcd.setCursor(0, 1);
  lcd.print("                "); 
}

float readMoisture() {
  int raw = analogRead(MOISTURE_PIN);
  float pct = map(raw, 4095, 1000, 0, 100);
  return constrain(pct, 0, 100);
}

float readBattery() {
  int raw = analogRead(BATTERY_PIN);
  return (raw / 4095.0) * 3.3 * 2.0;
}