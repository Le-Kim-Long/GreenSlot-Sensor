#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>        // <-- [NEW] Thư viện UDP để dò tìm Server
#include "esp_http_server.h"
#include <HTTPClient.h>     // Thư viện để gửi API lên Java Spring Boot

// ================= 1. CẤU HÌNH WIFI & ĐỊNH DANH CAMERA =================
const char* ssid = "Hoang Dung";       // <-- THAY TÊN WIFI NHÀ BẠN
const char* password = "90909090";     // <-- THAY MẬT KHẨU WIFI

// Định danh cho con Camera này
const char* CAM_ID = "CAM_SVIET_01"; 
const char* CAM_NAME = "Vườn Rau Tầng 1"; 

// [NEW] Cấu hình Radar dò tìm tự động
WiFiUDP udp;
const int UDP_PORT = 8888;          // Cổng UDP quy ước với Java
String serverPingUrl = "";          // Link API sẽ được tự động điền sau khi dò thấy Java

unsigned long lastPingTime = 0;
const long PING_INTERVAL = 30000;   // 30 giây báo cáo IP lên Java 1 lần

// ================= 2. CẤU HÌNH CHÂN CHO MẪU AI-THINKER =================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

httpd_handle_t camera_httpd = NULL; 
httpd_handle_t stream_httpd = NULL; 

// ================= 3. GIAO DIỆN WEB CỦA CAMERA =================
const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>GreenSlot Camera</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background-color: #2c3e50; color: white; margin: 0; padding: 20px; }
    .container { display: flex; flex-direction: column; align-items: center; gap: 20px; }
    h1 { color: #ecf0f1; margin-bottom: 5px; }
    .btn { padding: 15px 30px; font-size: 18px; font-weight: bold; cursor: pointer; background-color: #e74c3c; color: white; border: none; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); transition: 0.2s; }
    .btn:hover { background-color: #c0392b; }
    .btn:active { transform: translateY(2px); box-shadow: none; }
    .box { background: #34495e; padding: 15px; border-radius: 10px; width: 100%; max-width: 500px; }
    img { max-width: 100%; border-radius: 5px; background-color: #000; }
  </style>
</head>
<body>
  <h1>Camera Giám Sát GreenSlot</h1>
  <p>Live Stream & Tự Động Báo Cáo IP Về Java</p>
  
  <div class="container">
    <div class="box">
      <h3>🔴 Trực Tiếp (Live Stream)</h3>
      <img id="stream" width="480">
    </div>
    <button class="btn" onclick="downloadPhoto()">📸 Chụp & Lưu Ảnh Về Máy</button>
  </div>

  <script>
    window.onload = function() {
      var streamUrl = window.location.protocol + "//" + window.location.hostname + ":81/stream";
      document.getElementById('stream').src = streamUrl;
    };
    function downloadPhoto() {
      window.location.href = '/capture?t=' + new Date().getTime();
    }
  </script>
</body>
</html>
)rawliteral";

// ================= 4. CÁC HÀM XỬ LÝ RADAR & BÁO CÁO IP =================

// [NEW] Hàm Radar: Hét vào mạng LAN để tìm máy tính chạy Spring Boot
bool discoverSpringServer() {
  Serial.println("\n[RADAR] Đang quét mạng LAN tìm GreenSlot Server...");
  
  // Gửi gói tin Broadcast tới tất cả các máy trong mạng WiFi
  udp.beginPacket("255.255.255.255", UDP_PORT);
  udp.print("DISCOVER_GREENSLOT_SERVER");
  udp.endPacket();

  unsigned long startTime = millis();
  // Lắng nghe câu trả lời trong tối đa 3 giây
  while (millis() - startTime < 3000) { 
    int packetSize = udp.parsePacket();
    if (packetSize) {
      char reply[255];
      int len = udp.read(reply, 255);
      if (len > 0) reply[len] = 0;

      String response = String(reply);
      // Nếu nghe thấy câu trả lời chuẩn từ Java Spring Boot
      if (response.startsWith("GREENSLOT_SERVER:")) {
        String discoveredIp = udp.remoteIP().toString(); 
        String portStr = response.substring(17); 
        int discoveredPort = portStr.length() > 0 ? portStr.toInt() : 8080;

        // Tự động lắp ghép thành link Ping hoàn chỉnh
        serverPingUrl = "http://" + discoveredIp + ":" + String(discoveredPort) + "/api/cameras/ping";
        
        Serial.println("==========================================");
        Serial.println("[RADAR] => ĐÃ TÌM THẤY SERVER JAVA!");
        Serial.println("[RADAR] => IP Máy Tính: " + discoveredIp);
        Serial.println("[RADAR] => URL API: " + serverPingUrl);
        Serial.println("==========================================");
        return true;
      }
    }
    delay(50);
  }
  
  Serial.println("[RADAR] Chưa tìm thấy Server (Có thể Java chưa bật hoặc bị Tường lửa chặn).");
  return false;
}

// Hàm gửi báo cáo điểm danh lên Java Spring Boot (Heartbeat)
void sendHeartbeat() {
  if (WiFi.status() == WL_CONNECTED && serverPingUrl != "") {
    HTTPClient http;
    http.begin(serverPingUrl); // <-- Sử dụng link tự động tìm được
    http.addHeader("Content-Type", "application/json");

    String currentIP = WiFi.localIP().toString();
    String streamUrl = "http://" + currentIP + ":81/stream";
    String captureUrl = "http://" + currentIP + "/capture";
    
    String jsonPayload = "{";
    jsonPayload += "\"cam_id\":\"" + String(CAM_ID) + "\",";
    jsonPayload += "\"name\":\"" + String(CAM_NAME) + "\",";
    jsonPayload += "\"ip\":\"" + currentIP + "\",";
    jsonPayload += "\"stream_url\":\"" + streamUrl + "\",";
    jsonPayload += "\"capture_url\":\"" + captureUrl + "\"";
    jsonPayload += "}";

    Serial.print("[HEARTBEAT] Đang gửi báo cáo IP lên Java... ");
    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      Serial.printf("Thành công! Mã phản hồi: %d\n", httpResponseCode);
    } else {
      Serial.printf("Lỗi kết nối (%s).\n", http.errorToString(httpResponseCode).c_str());
      Serial.println("[WARNING] Có thể Server đã đổi IP hoặc tắt. Sẽ bật Radar quét lại!");
      serverPingUrl = ""; // Xóa link cũ để vòng lặp sau tự động quét lại
    }
    http.end();
  }
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[ERROR] Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"GreenSlot_Capture.jpg\"");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len;
  uint8_t * _jpg_buf;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[ERROR] Camera capture failed");
      res = ESP_FAIL;
    } else {
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }
    
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) break;
  }
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768; 
  httpd_uri_t index_uri = { "/", HTTP_GET, index_handler, NULL };
  httpd_uri_t capture_uri = { "/capture", HTTP_GET, capture_handler, NULL };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
  }

  httpd_config_t config_stream = HTTPD_DEFAULT_CONFIG();
  config_stream.server_port = 81; 
  config_stream.ctrl_port = 32769; 
  httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };

  if (httpd_start(&stream_httpd, &config_stream) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

// ================= 5. SETUP =================
void setup() {
  Serial.begin(9600);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2; 
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("[ERROR] Khởi tạo camera thất bại!");
    return;
  }

  WiFi.begin(ssid, password);
  Serial.print("[SYSTEM] Đang kết nối WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[SYSTEM] WiFi kết nối thành công!");
  Serial.print("[SYSTEM] TRUY CẬP IP WEB: http://");
  Serial.println(WiFi.localIP());

  startCameraServer();

  // [NEW] Mở cổng UDP và quét tìm Server ngay lần khởi động đầu tiên
  udp.begin(UDP_PORT);
  if (discoverSpringServer()) {
    sendHeartbeat(); // Nếu tìm thấy thì gửi điểm danh luôn
  }
  lastPingTime = millis();
}

// ================= 6. LOOP =================
void loop() {
  unsigned long currentMillis = millis();
  
  // Kiểm tra mỗi 30 giây
  if (currentMillis - lastPingTime >= PING_INTERVAL) {
    lastPingTime = currentMillis;
    
    // Nếu chưa có link Server (hoặc bị mất kết nối trước đó), hãy quét Radar lại!
    if (serverPingUrl == "") {
      if (discoverSpringServer()) {
        sendHeartbeat();
      }
    } else {
      // Nếu đã có link, tiến hành gửi Heartbeat
      sendHeartbeat();
    }
  }
}