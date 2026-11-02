#pragma once
#include "imu.h"

// Maps IMU axes to screen axes: X right, Y down (pixels), Z out of the screen toward the viewer.
// Found with a two-step calibration (flat, then upright) and stored in NVS.
struct ScreenFrame {
  float X[3], Y[3], Z[3];
  bool valid;
};

extern ScreenFrame screenFrame;

bool frameLoad();
void frameSave();
void frameClear();
// u1 = up-vector while lying flat (screen up), u2 = up-vector while upright (portrait, facing viewer)
bool frameFromCalibration(const float u1[3], const float u2[3]);
void frameSetDirect(const float X[3], const float Y[3], const float Z[3]);

// Sensor values in screen terms. Everything the buddy reacts to; can also be injected over serial.
struct Sensors {
  float gx, gy;      // gravity in the screen plane (g); (0,1) = screen held upright
  float gz;          // up·Z: +1 screen faces the sky, -1 screen faces the floor
  float wz;          // spin around the screen normal toward the viewer (deg/s, + = counter-clockwise as seen by the viewer)
  float wmag;        // total rotation speed (deg/s)
  float jerk;        // largest acceleration spike this frame (g)
  float jx, jy;      // spike direction in the screen plane (g)
  float freefallMs;
  float stillSec;
};

Sensors sensorsFromImu(const ImuSnap &s, float dt);
