#include <Arduino.h>
#include <SoftwareSerial.h>

// Nối với mạch Slave
SoftwareSerial slaveSerial(A2, A3); // RX, TX

const int RELAY_PIN = 4;
const unsigned long PUMP_DURATION = 5000;
unsigned long pumpStartTime = 0;
bool isPumpRunning = false; 

void setup() {
  Serial.begin(9600);        
  slaveSerial.begin(9600); 

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(13, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 
  digitalWrite(13, LOW);
}

void loop() {
  // 1. NHẬN LỆNH TỪ PYTHON ĐỂ BẬT/TẮT BƠM
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim(); 
    
    if (cmd == "ON") {
      if (!isPumpRunning) {
        digitalWrite(RELAY_PIN, HIGH);  
        digitalWrite(13, HIGH);        
        pumpStartTime = millis();      
        isPumpRunning = true;          
      }
      Serial.println("{\"pump_status\": \"ON\"}"); 
    } 
    else if (cmd == "OFF") {
      if (isPumpRunning) {
        digitalWrite(RELAY_PIN, LOW);   
        digitalWrite(13, LOW);          
        isPumpRunning = false;
      }
      Serial.println("{\"pump_status\": \"OFF\"}"); 
    }
  }

  // 2. TỰ ĐỘNG TẮT BƠM (5 GIÂY)
  if (isPumpRunning && (millis() - pumpStartTime >= PUMP_DURATION)) {
    digitalWrite(RELAY_PIN, LOW);   
    digitalWrite(13, LOW);          
    isPumpRunning = false;
    Serial.println("{\"pump_status\": \"TIMEOUT_OFF\"}");
  }

  // 3. ĐỌC CẢM BIẾN VÀ ĐẨY LÊN PYTHON
  if (slaveSerial.available()) {
    String sensorData = slaveSerial.readStringUntil('\n');
    sensorData.trim();
    if (sensorData.length() > 0) {
      // IN ĐÚNG RAW JSON, KHÔNG THÊM BẤT KỲ CHỮ NÀO KHÁC ĐỂ PYTHON KHÔNG BỊ LỖI
      Serial.println(sensorData); 
    }
  }
}