#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>        // Thư viện UDP để dò tìm Server
#include <HTTPClient.h>

// ================= 1. CẤU HÌNH WIFI & MẠNG =================
const char* ssid = "Passio Coffee";       // <-- THAY TÊN WIFI NHÀ BẠN
const char* password = "19009434";     // <-- THAY MẬT KHẨU WIFI

const char* IOT_USERNAME = "garden_staff";
const char* IOT_PASSWORD = "Staff@123";
const char* API_KEY = "default_iot_key_for_dev_only";
const char* DEVICE_ID = "arduino-greenhouse-01";

// [NEW] Biến lưu trữ IP và Port dò được từ Radar
String serverIp = ""; 
int serverPort = 8080;
WiFiUDP udp;
const int UDP_PORT = 8888; 
String JWT_TOKEN = ""; 

// ================= 2. CẤU HÌNH CHÂN & BIẾN BƠM =================
#define RXD2 16
#define TXD2 17

const int RELAY_PIN = 4;
const int LED_PIN = 2; // LED tích hợp trên ESP32 NodeMCU

const unsigned long PUMP_DURATION = 5000;
unsigned long pumpStartTime = 0;
bool isPumpRunning = false; 

// [FIX] Biến Cooldown chặn lỗi "bật lên tắt ngay"
bool isCooldown = false;
unsigned long cooldownStartTime = 0;
const unsigned long COOLDOWN_DURATION = 3000; // Nghỉ 3 giây sau khi tắt

unsigned long lastPumpCheckTime = 0;
const long PUMP_POLL_INTERVAL = 2000; // 2 giây hỏi Web 1 lần

// ================= 3. HÀM RADAR TÌM SERVER JAVA =================
bool discoverSpringServer() {
  Serial.println("\n[RADAR] Đang quét mạng LAN tìm GreenSlot Server...");
  
  udp.beginPacket("255.255.255.255", UDP_PORT);
  udp.print("DISCOVER_GREENSLOT_SERVER");
  udp.endPacket();

  unsigned long startTime = millis();
  while (millis() - startTime < 3000) { 
    int packetSize = udp.parsePacket();
    if (packetSize) {
      char reply[255];
      int len = udp.read(reply, 255);
      if (len > 0) reply[len] = 0;

      String response = String(reply);
      if (response.startsWith("GREENSLOT_SERVER:")) {
        serverIp = udp.remoteIP().toString(); 
        String portStr = response.substring(17); 
        serverPort = portStr.length() > 0 ? portStr.toInt() : 8080;

        Serial.println("==========================================");
        Serial.println("✅ [RADAR] ĐÃ TÌM THẤY SERVER JAVA!");
        Serial.println("👉 IP Server : " + serverIp);
        Serial.println("👉 Port      : " + String(serverPort));
        Serial.println("==========================================");
        return true;
      }
    }
    delay(50);
  }
  
  Serial.println("❌ [RADAR] Chưa tìm thấy Server. Đang thử lại...");
  return false;
}

// ================= 4. HÀM TỰ ĐỘNG ĐĂNG NHẬP LẤY TOKEN =================
bool loginToBackend() {
  if (WiFi.status() == WL_CONNECTED && serverIp != "") {
    HTTPClient http;
    String url = "http://" + serverIp + ":" + String(serverPort) + "/api/auth/login";
    
    Serial.println(F("\n[AUTH] Đang gửi yêu cầu đăng nhập lấy Token..."));
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String loginPayload = "{\"username\":\"" + String(IOT_USERNAME) + "\",\"password\":\"" + String(IOT_PASSWORD) + "\"}";
    int httpResponseCode = http.POST(loginPayload);
    
    if (httpResponseCode == 200) {
      String response = http.getString(); 
      int tokenKeyPos = response.indexOf("\"token\"");
      if (tokenKeyPos != -1) {
        int colonPos = response.indexOf(":", tokenKeyPos);
        int firstQuote = response.indexOf("\"", colonPos);
        int secondQuote = response.indexOf("\"", firstQuote + 1);
        
        if (firstQuote != -1 && secondQuote != -1) {
          JWT_TOKEN = response.substring(firstQuote + 1, secondQuote);
          Serial.println(F("✅ [AUTH] Lấy Token tự động thành công!"));
          http.end();
          return true;
        }
      }
    } else {
      Serial.print(F("❌ [AUTH] Lỗi đăng nhập: "));
      Serial.println(httpResponseCode);
    }
    http.end();
  }
  return false;
}

// ================= 5. KHỞI TẠO =================
void setup() {
  Serial.begin(9600);        
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); 

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 
  digitalWrite(LED_PIN, LOW);

  // --- KẾT NỐI WIFI ---
  Serial.println(F("\n[WIFI] Đang kết nối WiFi..."));
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(F("\n✅ [WIFI] Kết nối thành công!"));
  Serial.print(F("🌐 IP Address: "));
  Serial.println(WiFi.localIP());

  // --- MỞ CỔNG UDP VÀ DÒ TÌM SERVER ---
  udp.begin(UDP_PORT);
  while (serverIp == "") {
    discoverSpringServer();
    if (serverIp == "") delay(2000); 
  }

  // --- ĐĂNG NHẬP SAU KHI TÌM ĐƯỢC IP ---
  while (JWT_TOKEN == "") {
    loginToBackend();
    if (JWT_TOKEN == "") delay(3000); 
  }
}

// ================= 6. GỬI DỮ LIỆU CẢM BIẾN LÊN JAVA =================
void postSensorData(String rawJson) {
  if (WiFi.status() == WL_CONNECTED && JWT_TOKEN != "") {
    HTTPClient http;
    String url = "http://" + serverIp + ":" + String(serverPort) + "/api/iot/sensors/data";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-IoT-Api-Key", API_KEY);
    http.addHeader("Authorization", "Bearer " + JWT_TOKEN);
    
    float lightVal = 0, phVal = 0;
    int soilVal = 0;
    
    int lightIdx = rawJson.indexOf("\"light\":");
    int soilIdx = rawJson.indexOf("\"soil\":");
    int phIdx = rawJson.indexOf("\"ph\":");
    
    if (lightIdx != -1 && soilIdx != -1 && phIdx != -1) {
      lightVal = rawJson.substring(lightIdx + 8, rawJson.indexOf(",", lightIdx)).toFloat();
      soilVal = rawJson.substring(soilIdx + 7, rawJson.indexOf(",", soilIdx)).toInt();
      phVal = rawJson.substring(phIdx + 5, rawJson.indexOf("}", phIdx)).toFloat();
    }
    
    String payload = "{\"deviceId\":\"" + String(DEVICE_ID) + "\",\"readings\":[";
    payload += "{\"sensorType\":\"LIGHT_INTENSITY\",\"value\":" + String(lightVal) + ",\"unit\":\"Lux\"},";
    payload += "{\"sensorType\":\"SOIL_MOISTURE\",\"value\":" + String(soilVal) + ",\"unit\":\"%\"},";
    payload += "{\"sensorType\":\"PH\",\"value\":" + String(phVal) + ",\"unit\":\"pH\"}";
    payload += "]}";
    
    int httpResponseCode = http.POST(payload);
    
    if (httpResponseCode == 401) {
      Serial.println("⚠️ [AUTH] Token hết hạn, đang xin cấp lại...");
      JWT_TOKEN = "";
      loginToBackend();
    } else if (httpResponseCode == 200 || httpResponseCode == 201) {
      Serial.println("🌱 [API CẢM BIẾN] Đã gửi thành công!");
    }
    http.end();
  }
}

// ================= 7. KIỂM TRA LỆNH BẬT TẮT BƠM =================
void checkPumpStatus() {
  if (WiFi.status() == WL_CONNECTED && JWT_TOKEN != "") {
    HTTPClient http;
    String url = "http://" + serverIp + ":" + String(serverPort) + "/api/iot/pump/status";
    
    http.begin(url);
    http.addHeader("X-IoT-Api-Key", API_KEY);
    http.addHeader("Authorization", "Bearer " + JWT_TOKEN);
    
    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 401) {
      JWT_TOKEN = ""; 
    } else if (httpResponseCode == 200) {
      String response = http.getString();
      
      if (response.indexOf("\"status\":\"ON\"") != -1 || response.indexOf("\"status\": \"ON\"") != -1) {
        if (!isPumpRunning) {
          Serial.println("💧 [LỆNH WEB] -> BẬT BƠM");
          digitalWrite(RELAY_PIN, HIGH);
          digitalWrite(LED_PIN, HIGH);
          pumpStartTime = millis();
          isPumpRunning = true;
        }
      } 
      else if (response.indexOf("\"status\":\"OFF\"") != -1 || response.indexOf("\"status\": \"OFF\"") != -1) {
        if (isPumpRunning) {
          Serial.println("🛑 [LỆNH WEB] -> TẮT BƠM");
          digitalWrite(RELAY_PIN, LOW);
          digitalWrite(LED_PIN, LOW);
          isPumpRunning = false;
        }
      }
    }
    http.end();
  }
}

// ================= 8. BÁO SERVER RẰNG ĐÃ TẮT BƠM =================
void notifyServerPumpOff() {
  if (WiFi.status() == WL_CONNECTED && JWT_TOKEN != "") {
    HTTPClient http;
    String url = "http://" + serverIp + ":" + String(serverPort) + "/api/iot/pump/status";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-IoT-Api-Key", API_KEY);
    http.addHeader("Authorization", "Bearer " + JWT_TOKEN);
    
    String payload = "{\"deviceId\":\"" + String(DEVICE_ID) + "\",\"status\":\"OFF\"}";
    http.POST(payload);
    http.end();
  }
}

// ================= 9. VÒNG LẶP CHÍNH =================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Quản lý thời gian chờ (Cooldown) sau khi ngắt bơm
  if (isCooldown) {
    if (currentMillis - cooldownStartTime >= COOLDOWN_DURATION) {
      isCooldown = false;
      Serial.println("🔄 [HỆ THỐNG] Đã hết 3s chờ, tiếp tục nhận lệnh mới.");
    }
  }

  // 2. Hỏi Backend xem có lệnh bật bơm không (chỉ hỏi khi KHÔNG bị Cooldown)
  if (!isCooldown && (currentMillis - lastPumpCheckTime >= PUMP_POLL_INTERVAL)) {
    lastPumpCheckTime = currentMillis;
    checkPumpStatus();
  }

  // 3. Tự động tắt bơm sau 5 giây để bảo vệ
  // 3. Tự động tắt bơm sau 5 giây để bảo vệ
  // SỬA Ở DÒNG NÀY: Thay currentMillis bằng millis()
  if (isPumpRunning && (millis() - pumpStartTime >= PUMP_DURATION)) {
    digitalWrite(RELAY_PIN, LOW);   
    digitalWrite(LED_PIN, LOW);          
    isPumpRunning = false;
    Serial.println("⏱️ [HỆ THỐNG] Đã hết 5 giây -> TỰ ĐỘNG TẮT BƠM");
    
    // Kích hoạt Cooldown 3 giây
    isCooldown = true;
    cooldownStartTime = millis(); 
    
    notifyServerPumpOff();
  }

  // 4. Đọc liên tục cảm biến từ Slave Uno gửi sang
  if (Serial2.available()) {
    String sensorData = Serial2.readStringUntil('\n');
    sensorData.trim();
    if (sensorData.length() > 0) {
      Serial.println("[SLAVE] Dữ liệu: " + sensorData);
      postSensorData(sensorData); 
    }
  }
}