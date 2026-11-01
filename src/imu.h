#pragma once
#include <Arduino.h>

// QMI8658 read at ~200 Hz in a background task (core 0). All vectors in IMU axes.
struct ImuSnap {
  float up[3];        // low-passed accel (g): points away from the earth when still
  float acc[3];       // latest raw accel (g)
  float gyro[3];      // latest gyro, bias-corrected (deg/s)
  float hpPeak[3];    // high-passed accel vector at the largest spike since last take (g)
  float hpPeakMag;    // |hpPeak|
  float rot[3];       // integrated rotation since last take (deg)
  float freefallMs;   // current continuous free-fall duration
  float stillSec;     // how long the board has been completely still
  bool  ok;
};

bool imuBegin();
ImuSnap imuTake();     // copy + reset peaks/integrals
