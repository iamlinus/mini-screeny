#include <Arduino.h>
#include <Wire.h>
#define IMU 0x6B
void wr(uint8_t r, uint8_t v) { Wire.beginTransmission(IMU); Wire.write(r); Wire.write(v); Wire.endTransmission(); }
void setup() {
  Serial.begin(115200); delay(1500);
  Wire.begin(11, 10, 400000);
  wr(0x60, 0xB0); delay(20);       // soft reset
  wr(0x02, 0x40);                  // CTRL1: auto-increment, little endian
  wr(0x03, 0x25);                  // CTRL2: accel ±8g, ODR ~235Hz
  wr(0x04, 0x65);                  // CTRL3: gyro ±1024dps
  wr(0x08, 0x03);                  // CTRL7: accel+gyro enable
  delay(50);
}
void loop() {
  Wire.beginTransmission(IMU); Wire.write(0x35); Wire.endTransmission(false);
  Wire.requestFrom(IMU, 12);
  int16_t v[6]; for (int i = 0; i < 6; i++) { uint8_t l = Wire.read(), h = Wire.read(); v[i] = (int16_t)(h << 8 | l); }
  float ax = v[0] / 4096.f, ay = v[1] / 4096.f, az = v[2] / 4096.f;
  Serial.printf("a=(%.2f %.2f %.2f) |a|=%.2f  g=(%.1f %.1f %.1f)\n", ax, ay, az, sqrtf(ax*ax+ay*ay+az*az), v[3]/32.f, v[4]/32.f, v[5]/32.f);
  delay(400);
}
