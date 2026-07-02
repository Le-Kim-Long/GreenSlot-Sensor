#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>

// --- CẤU HÌNH CẢM BIẾN ĐẤT ---
const int SOIL_VCC = 7; // Nguồn nuôi cảm biến đất 
const int SOIL_AO = A1; // Chân đọc tín hiệu đất 
const int SOIL_AIR_VALUE = 1023; // Khô ngoài trời 
const int SOIL_WATER_VALUE = 240; // Ướt trong đất

// --- CẤU HÌNH CẢM BIẾN ÁNH SÁNG ---
BH1750 lightMeter;

void setup() {
  Serial.begin(9600);
  
  // Khởi tạo chân độ ẩm đất
  pinMode(SOIL_VCC, OUTPUT); 
  digitalWrite(SOIL_VCC, LOW); 

  // Khởi tạo I2C cho cảm biến ánh sáng (Mặc định SDA=A4, SCL=A5)
  Wire.begin();
  if (lightMeter.begin()) {
    Serial.println(F("[SYSTEM] Cam bien anh sang BH1750 OK!"));
  } else {
    Serial.println(F("[ERROR] Khong tim thay BH1750. Kiem tra lai day SDA/SCL!"));
  }
}

void loop() {
  // 1. ĐỌC ĐỘ ẨM ĐẤT
  digitalWrite(SOIL_VCC, HIGH); 
  delay(100); 
  int rawSoil = analogRead(SOIL_AO);
  digitalWrite(SOIL_VCC, LOW); 
  
  int moisturePercent = map(rawSoil, SOIL_AIR_VALUE, SOIL_WATER_VALUE, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  // 2. ĐỌC ÁNH SÁNG (Đơn vị: Lux)
  float lux = lightMeter.readLightLevel();

  // 3. ĐÓNG GÓI JSON KÉP
  // Kết quả: {"moisture": 65, "light": 254.12}
  Serial.print(F("{\"moisture\":")); 
  Serial.print(moisturePercent);
  Serial.print(F(", \"light\":"));
  Serial.print(lux);
  Serial.println(F("}")); 
  
  delay(5000); // Tránh spam server
}