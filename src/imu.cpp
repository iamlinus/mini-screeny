#include "imu.h"
#include "board.h"
#include <Wire.h>

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static ImuSnap S;
static float bias[3] = {0, 0, 0};
static float lp[3] = {0, 0, -1};
static bool imuOk = false;

static void wr(uint8_t r, uint8_t v) {
  Wire.beginTransmission(IMU_ADDR); Wire.write(r); Wire.write(v); Wire.endTransmission();
}
static uint8_t rd8(uint8_t r) {
  Wire.beginTransmission(IMU_ADDR); Wire.write(r); Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}
static bool readRaw(float a[3], float g[3]) {
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x35);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)12) != 12) return false;
  int16_t v[6];
  for (int i = 0; i < 6; i++) { uint8_t l = Wire.read(), h = Wire.read(); v[i] = (int16_t)(h << 8 | l); }
  for (int i = 0; i < 3; i++) { a[i] = v[i] / 4096.0f; g[i] = v[i + 3] / 32.0f; }
  return true;
}

static void task(void *) {
  const float dt = 0.005f;
  float stillT = 0;
  float biasAcc[3] = {0, 0, 0}; int biasN = 0;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(5));
    float a[3], g[3];
    if (!readRaw(a, g)) continue;
    float am = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float hp[3], hm = 0;
    for (int i = 0; i < 3; i++) {
      lp[i] += (a[i] - lp[i]) * 0.06f;       // tau ~80 ms
      hp[i] = a[i] - lp[i];
      hm += hp[i] * hp[i];
    }
    hm = sqrtf(hm);
    float gc[3], gm = 0;
    for (int i = 0; i < 3; i++) { gc[i] = g[i] - bias[i]; gm += gc[i] * gc[i]; }
    gm = sqrtf(gm);

    // gyro bias: learn while truly still
    bool still = hm < 0.025f && gm < 6.0f && fabsf(am - 1.0f) < 0.06f;
    stillT = still ? stillT + dt : 0;
    if (stillT > 0.5f) {
      for (int i = 0; i < 3; i++) biasAcc[i] += g[i];
      if (++biasN >= 100) {
        for (int i = 0; i < 3; i++) bias[i] += (biasAcc[i] / biasN - bias[i]) * 0.5f;
        biasN = 0; biasAcc[0] = biasAcc[1] = biasAcc[2] = 0;
      }
    } else { biasN = 0; biasAcc[0] = biasAcc[1] = biasAcc[2] = 0; }

    portENTER_CRITICAL(&mux);
    for (int i = 0; i < 3; i++) {
      S.up[i] = lp[i]; S.acc[i] = a[i]; S.gyro[i] = gc[i];
      S.rot[i] += gc[i] * dt;
    }
    if (hm > S.hpPeakMag) { S.hpPeakMag = hm; for (int i = 0; i < 3; i++) S.hpPeak[i] = hp[i]; }
    S.freefallMs = am < 0.35f ? S.freefallMs + dt * 1000 : 0;
    S.stillSec = (hm < 0.04f && gm < 10.0f) ? S.stillSec + dt : 0;
    portEXIT_CRITICAL(&mux);
  }
}

bool imuBegin() {
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  if (rd8(0x00) != 0x05) { Serial.println("IMU: WHO_AM_I mismatch"); return false; }
  wr(0x60, 0xB0); delay(20);   // soft reset
  wr(0x02, 0x40);              // CTRL1: address auto-increment, little endian
  wr(0x03, 0x25);              // CTRL2: accel ±8 g, ~235 Hz
  wr(0x04, 0x65);              // CTRL3: gyro ±1024 dps
  wr(0x08, 0x03);              // CTRL7: enable accel + gyro
  delay(60);
  // initial gyro bias + gravity estimate (2 s budget, keeps best effort if moving)
  float acc[3] = {0, 0, 0}, ga[3] = {0, 0, 0}; int n = 0;
  for (int k = 0; k < 120; k++) {
    float a[3], g[3];
    if (readRaw(a, g)) { for (int i = 0; i < 3; i++) { acc[i] += a[i]; ga[i] += g[i]; } n++; }
    delay(5);
  }
  if (n) for (int i = 0; i < 3; i++) { lp[i] = acc[i] / n; bias[i] = ga[i] / n; }
  memset(&S, 0, sizeof S);
  for (int i = 0; i < 3; i++) S.up[i] = lp[i];
  imuOk = true;
  S.ok = true;
  xTaskCreatePinnedToCore(task, "imu", 4096, nullptr, 3, nullptr, 0);
  return true;
}

ImuSnap imuTake() {
  ImuSnap out;
  portENTER_CRITICAL(&mux);
  out = S;
  S.hpPeakMag = 0; S.hpPeak[0] = S.hpPeak[1] = S.hpPeak[2] = 0;
  S.rot[0] = S.rot[1] = S.rot[2] = 0;
  portEXIT_CRITICAL(&mux);
  out.ok = imuOk;
  return out;
}
