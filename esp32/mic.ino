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
 
// --- HAVA DURUMU (OPEN-METEO) AYARLARI ---
// API Key yok! Sadece şehrinizin enlem (latitude) ve boylam (longitude) değerleri.
// Şu an Ankara (39.92, 32.85) için ayarlıdır. Başka şehir için değiştirebilirsiniz.
const char* latitude = "39.92"; 
const char* longitude = "32.85";

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

// --- OPEN-METEO ÜCRETSİZ HAVA DURUMU SORGULAMA ---
int fetchRainProbability() {
  if(WiFi.status() != WL_CONNECTED) return 0;
  
  HTTPClient http;
  
  // forecast_hours=1 ile sadece içinde bulunduğumuz saatin yağış ihtimalini (% olarak) çekiyoruz.
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(latitude) + "&longitude=" + String(longitude) + "&hourly=precipitation_probability&forecast_hours=1";
  
  http.begin(url);
  int httpCode = http.GET();
  int prob = 0;
  
  if (httpCode == 200) {
    String payload = http.getString();
    
    // Open-Meteo JSON yanıtı için bellek ayırma
    DynamicJsonDocument doc(1024); 
    deserializeJson(doc, payload);
    
    // JSON içerisinden hourly -> precipitation_probability -> ilk elemanı (0. index) alıyoruz
    prob = doc["hourly"]["precipitation_probability"][0]; 
    Serial.println("Yagmur Ihtimali (Open-Meteo): %" + String(prob));
  } else {
    Serial.println("Hava durumu API hatasi! HTTP Kodu: " + String(httpCode));
  }
  
  http.end();
  return prob;
}

// --- KARAR MOTORLARI ---

void processOnlineMode(float m, bool r, float bv, float bp) {
  HTTPClient http;
  http.begin(backendUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["moisture"] = m;
  doc["temperature"] = 24.0; 
  doc["humidity"] = 50.0;
  
  // Fonksiyonu çağır ve gelen %'lik veriyi backend'e ilet
  doc["rain_probability"] = fetchRainProbability(); 
  
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