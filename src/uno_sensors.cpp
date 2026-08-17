#include <Wire.h>
#include <BH1750.h>

const int PH_PIN = A0;   
const int SOIL_PIN = A1; 

// Ép cứng địa chỉ I2C mặc định là 0x23 (khi chân ADDR để trống hoặc nối GND)
BH1750 lightMeter(0x23); 
bool isLightSensorReady = false;

void setup() {
  Serial.begin(9600); // Truyền qua chân D1 (TX)
  Wire.begin();
  
  // Khởi tạo cảm biến ánh sáng
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    isLightSensorReady = true;
    Serial.println(F("{\"status\": \"BH1750_OK\"}"));
  } else {
    // In ra JSON báo lỗi để Terminal Python nhận diện được
    Serial.println(F("{\"status\": \"BH1750_ERROR_NOT_FOUND\"}"));
  }
  
  delay(1000);
}

void loop() {
  float lux = 0.0; // Mặc định là 0.0

  // Chỉ gọi hàm đọc ánh sáng nếu cảm biến đã khởi tạo thành công
  if (isLightSensorReady) {
    lux = lightMeter.readLightLevel();
  }

  // Đọc độ ẩm đất
  int soilRaw = analogRead(SOIL_PIN);
  int soilPercent = map(soilRaw, 1023, 350, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

  // Đọc pH
  int phRaw = analogRead(PH_PIN);
  float phValue = 3.5 * (phRaw * (5.0 / 1023.0)); 

  // Gửi nguyên cục JSON sang Master
  Serial.print(F("{\"light\":"));
  Serial.print(lux, 2);
  Serial.print(F(", \"soil\":"));
  Serial.print(soilPercent);
  Serial.print(F(", \"ph\":"));
  Serial.print(phValue, 2);
  Serial.println(F("}"));

  delay(2000);
}