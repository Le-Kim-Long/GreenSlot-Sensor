#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ================= 1. CẤU HÌNH WIFI & MẠNG =================
const char* ssid = "Hoang Dung";       
const char* password = "90909090";     

const char* IOT_USERNAME = "admin";
const char* IOT_PASSWORD = "GreenSlot@2024";
const char* API_KEY = "test_key";
const char* DEVICE_ID = "arduino-greenhouse-01";

// ================= 1. CẤU HÌNH MÔI TRƯỜNG =================
const bool USE_LOCAL_SERVER = false; // Đổi thành 'true' nếu chạy Local, 'false' nếu chạy Deploy (Render)

const String localIp = "192.168.1.15";
const int localPort = 8080;
const String deployHost = "greenslot-backend.onrender.com";
String JWT_TOKEN = ""; 

String getBaseUrl() {
  if (USE_LOCAL_SERVER) {
    return "http://" + localIp + ":" + String(localPort);
  } else {
    return "https://" + deployHost;
  }
}

// ================= 2. CẤU HÌNH CHÂN & BIẾN BƠM/CẢM BIẾN =================
#define RXD2 16
#define TXD2 17

const int RELAY_PIN = 4;
const int LED_PIN = 2;

const unsigned long PUMP_DURATION = 5000;
unsigned long pumpStartTime = 0;
bool isPumpRunning = false; 

bool isCooldown = false;
unsigned long cooldownStartTime = 0;
const unsigned long COOLDOWN_DURATION = 3000; 

unsigned long lastPumpCheckTime = 0;
const long PUMP_POLL_INTERVAL = 10000; 

// THÊM BIẾN QUẢN LÝ THỜI GIAN GỬI DỮ LIỆU CẢM BIẾN
unsigned long lastSensorPostTime = 0;
const long SENSOR_POST_INTERVAL = 30000; // 30 giây
String latestSensorData = ""; // Biến lưu trữ dữ liệu mới nhất từ Slave

// ================= 3. CÁC HÀM GIAO TIẾP VỚI SERVER =================

bool loginToBackend() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client; 
    client.setInsecure();
    
    HTTPClient http;
    String url = getBaseUrl() + "/api/auth/login";
    
    Serial.println(F("\n[AUTH] Đang gửi yêu cầu đăng nhập..."));
    
    if (USE_LOCAL_SERVER) {
      http.begin(url);
    } else {
      http.begin(client, url); 
    }
    
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close"); 

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
          Serial.println(F("✅ [AUTH] Lấy Token thành công!"));
          
          http.end();
          client.stop();
          delay(150); 
          return true;
        }
      }
    } else {
      Serial.printf("❌ [AUTH] Lỗi đăng nhập: %d\n", httpResponseCode);
    }
    
    http.end();
    client.stop();
    delay(150); 
  }
  return false;
}

void postSensorData(String rawJson) {
  if (WiFi.status() == WL_CONNECTED && JWT_TOKEN != "") {
    HTTPClient http;
    String url = getBaseUrl() + "/api/iot/sensors/data";
    
    // Tách biệt rõ ràng Client cho HTTP (Local) và HTTPS (Deploy)
    WiFiClient clientHTTP;
    WiFiClientSecure clientHTTPS;
    clientHTTPS.setInsecure();
    
    if (USE_LOCAL_SERVER) {
      http.begin(clientHTTP, url); 
    } else {
      http.begin(clientHTTPS, url);
    }
    
    // ĐIỂM QUAN TRỌNG: Tăng thời gian chờ Server phản hồi lên 15 giây
    http.setTimeout(15000); 
    
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-IoT-Api-Key", API_KEY);
    http.addHeader("Authorization", "Bearer " + JWT_TOKEN);
    http.addHeader("Connection", "close"); 
    
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
    } else if (httpResponseCode == 200 || httpResponseCode == 201) {
      Serial.println("🌱 [API CẢM BIẾN] Đã gửi Data thành công!");
    } else {
      Serial.printf("❌ [API LỖI] Mã: %d\n", httpResponseCode);
    }
    
    http.end();
    delay(150); 
  }
}

void checkPumpStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    String url = getBaseUrl() + "/api/iot/pump/status";
    
    if (USE_LOCAL_SERVER) {
      http.begin(url);
    } else {
      http.begin(client, url);
    }
    
    http.addHeader("X-IoT-Api-Key", API_KEY);
    if (JWT_TOKEN != "") {
      http.addHeader("Authorization", "Bearer " + JWT_TOKEN);
    }
    http.addHeader("Connection", "close"); 
    
    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 401) {
      JWT_TOKEN = ""; 
    } else if (httpResponseCode == 200) {
      String response = http.getString();
      
      if (response.indexOf("\"status\":\"ON\"") != -1 || response.indexOf("\"status\": \"ON\"") != -1) {
        if (!isPumpRunning) {
          Serial.println("💧 [HỆ THỐNG] KÍCH HOẠT BẬT BƠM!");
          digitalWrite(RELAY_PIN, HIGH);
          digitalWrite(LED_PIN, HIGH);
          pumpStartTime = millis();
          isPumpRunning = true;
        }
      } 
      else if (response.indexOf("\"status\":\"OFF\"") != -1 || response.indexOf("\"status\": \"OFF\"") != -1) {
        if (isPumpRunning) {
          Serial.println("🛑 [HỆ THỐNG] TẮT BƠM");
          digitalWrite(RELAY_PIN, LOW);
          digitalWrite(LED_PIN, LOW);
          isPumpRunning = false;
        }
      }
    }
    
    http.end();
    client.stop();
    delay(150); 
  }
}

void notifyServerPumpOff() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    String url = getBaseUrl() + "/api/iot/pump/status";
    
    if (USE_LOCAL_SERVER) {
      http.begin(url);
    } else {
      http.begin(client, url);
    }
    
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-IoT-Api-Key", API_KEY);
    if (JWT_TOKEN != "") {
      http.addHeader("Authorization", "Bearer " + JWT_TOKEN);
    }
    http.addHeader("Connection", "close");
    
    String payload = "{\"status\":\"OFF\"}";
    http.POST(payload);
    
    http.end();
    client.stop();
    delay(150); 
  }
}

// ================= 4. KHỞI TẠO =================
void setup() {
  Serial.begin(9600);        
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); 

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 
  digitalWrite(LED_PIN, LOW);

  Serial.println(F("\n[WIFI] Đang kết nối WiFi..."));
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(F("\n✅ [WIFI] Kết nối thành công!"));
  Serial.print(F("🌐 IP Address: "));
  Serial.println(WiFi.localIP());

  while (JWT_TOKEN == "") {
    loginToBackend();
    if (JWT_TOKEN == "") delay(3000); 
  }
}

// ================= 8. VÒNG LẶP CHÍNH =================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ [WIFI] Mất kết nối! Đang thử kết nối lại...");
    WiFi.disconnect();
    WiFi.reconnect();
    
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ [WIFI] Đã kết nối lại thành công!");
    } else {
      return; 
    }
  }

  if (JWT_TOKEN == "") {
    loginToBackend();
    return;
  }

  unsigned long currentMillis = millis();

  // 1. Quản lý trạng thái chờ của máy bơm
  if (isCooldown) {
    if (currentMillis - cooldownStartTime >= COOLDOWN_DURATION) {
      isCooldown = false;
      Serial.println("🔄 [HỆ THỐNG] Đã hết 3s chờ, tiếp tục nhận lệnh mới.");
    }
  }

  // 2. Hỏi máy chủ trạng thái bơm mỗi 10 giây
  if (!isCooldown && (currentMillis - lastPumpCheckTime >= PUMP_POLL_INTERVAL)) {
    lastPumpCheckTime = currentMillis;
    checkPumpStatus();
  }

  // 3. Tắt bơm tự động sau 5 giây
  if (isPumpRunning && (currentMillis - pumpStartTime >= PUMP_DURATION)) {
    digitalWrite(RELAY_PIN, LOW);   
    digitalWrite(LED_PIN, LOW);          
    isPumpRunning = false;
    Serial.println("⏱️ [HỆ THỐNG] Đã hết 5 giây -> TỰ ĐỘNG TẮT BƠM");
    
    isCooldown = true;
    cooldownStartTime = currentMillis; 
    
    notifyServerPumpOff();
  }

  // 4. CẬP NHẬT: Đọc liên tục để lấy dữ liệu mới nhất (tránh đầy buffer)
  if (Serial2.available()) {
    String tempSensorData = Serial2.readStringUntil('\n');
    tempSensorData.trim();
    if (tempSensorData.length() > 0) {
      latestSensorData = tempSensorData; // Lưu lại dữ liệu mới nhất
    }
  }

  // 5. CẬP NHẬT: Cứ mỗi 10 giây sẽ bắn API gửi lên Server 1 lần
// 5. CẬP NHẬT: Cứ mỗi 30 giây sẽ kiểm tra và bắn API gửi lên Server 1 lần
  if (currentMillis - lastSensorPostTime >= SENSOR_POST_INTERVAL) {
    lastSensorPostTime = currentMillis;
    
    Serial.println("\n--- [DEBUG] Đã qua 30s. Bắt đầu chu kỳ xử lý ---");
    
    if (latestSensorData.length() > 0) {
      Serial.println("[SLAVE] Đã nhận được dữ liệu: " + latestSensorData);
      postSensorData(latestSensorData); 
      
      // Xóa dữ liệu cũ sau khi gửi xong để tránh gửi lại dữ liệu cũ nếu Slave chết
      latestSensorData = ""; 
    } else {
      Serial.println("❌ [SLAVE CẢNH BÁO] Không có dữ liệu cảm biến mới!");
      Serial.println("-> Vui lòng kiểm tra lại dây cắm RX(16)-TX(17) và code mạch Slave.");
    }
  }
}