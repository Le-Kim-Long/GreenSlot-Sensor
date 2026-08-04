#include <Wire.h>
#include <BH1750.h>

const int PH_PIN = A0;   
const int SOIL_PIN = A1; 
BH1750 lightMeter;

void setup() {
  Serial.begin(9600); // Truyền qua chân D1 (TX)
  Wire.begin();
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println(F("{\"status\": \"BH1750_OK\"}"));
  }
  delay(1000);
}

void loop() {
  float lux = lightMeter.readLightLevel();

  int soilRaw = analogRead(SOIL_PIN);
  int soilPercent = map(soilRaw, 1023, 350, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

  int phRaw = analogRead(PH_PIN);
  float phValue = 3.5 * (phRaw * (5.0 / 1023.0)); 

  // Gửi JSON sang Master
  Serial.print(F("{\"light\":"));
  Serial.print(lux, 2);
  Serial.print(F(", \"soil\":"));
  Serial.print(soilPercent);
  Serial.print(F(", \"ph\":"));
  Serial.print(phValue, 2);
  Serial.println(F("}"));

  delay(2000);
}