#include "frame.h"
#include "board.h"
#include <Preferences.h>
#include <math.h>

ScreenFrame screenFrame = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, false};

// Logical (mounted) screen axes expressed in panel pixel axes: right = MOUNT_R, down = MOUNT_D (see MOUNT_ROT in board.h)
#if MOUNT_ROT == 1
static const float MOUNT_R[2] = {0, 1}, MOUNT_D[2] = {-1, 0};
#elif MOUNT_ROT == 2
static const float MOUNT_R[2] = {-1, 0}, MOUNT_D[2] = {0, -1};
#elif MOUNT_ROT == 3
static const float MOUNT_R[2] = {0, -1}, MOUNT_D[2] = {1, 0};
#else
static const float MOUNT_R[2] = {1, 0}, MOUNT_D[2] = {0, 1};
#endif

// screenFrame (panel axes) -> logical screen axes
static void logicalAxes(float Xl[3], float Yl[3]) {
  const ScreenFrame &f = screenFrame;
  for (int i = 0; i < 3; i++) {
    Xl[i] = MOUNT_R[0] * f.X[i] + MOUNT_R[1] * f.Y[i];
    Yl[i] = MOUNT_D[0] * f.X[i] + MOUNT_D[1] * f.Y[i];
  }
}

static float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void norm(float a[3]) { float m = sqrtf(dot(a, a)); if (m > 1e-6f) for (int i = 0; i < 3; i++) a[i] /= m; }
static void cross(const float a[3], const float b[3], float o[3]) {
  o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}

bool frameLoad() {
  Preferences p; p.begin("buddy", true);
  bool ok = p.getBool("cal", false) && p.getBytes("frame", &screenFrame, sizeof screenFrame) == sizeof screenFrame;
  p.end();
  if (ok) cross(screenFrame.Z, screenFrame.Y, screenFrame.X);   // re-derive X (fixes frames saved with the old handedness bug)
  screenFrame.valid = ok;
  return ok;
}
void frameSave() {
  Preferences p; p.begin("buddy", false);
  p.putBytes("frame", &screenFrame, sizeof screenFrame); p.putBool("cal", true);
  p.end();
}
void frameClear() {
  Preferences p; p.begin("buddy", false); p.putBool("cal", false); p.end();
  screenFrame.valid = false;
}

bool frameFromCalibration(const float u1[3], const float u2[3]) {
  float Z[3] = {u1[0], u1[1], u1[2]}; norm(Z);
  // upright as mounted: up = -Yl (logical down)  =>  Yl = -u2, made orthogonal to Z
  float Yl[3] = {-u2[0], -u2[1], -u2[2]}; norm(Yl);
  float d = dot(Yl, Z);
  if (fabsf(d) > 0.8f) return false;   // second pose was not upright enough (~53 deg back-tilt allowed)
  for (int i = 0; i < 3; i++) Yl[i] -= d * Z[i];
  norm(Yl);
  // Y points down and Z toward the viewer, so (X, Y, Z) is left-handed: X = Z x Y (not Y x Z)
  float Xl[3]; cross(Z, Yl, Xl);
  // back to panel axes (the inverse of logicalAxes: transpose of the rotation)
  float X[3], Y[3];
  for (int i = 0; i < 3; i++) {
    X[i] = MOUNT_R[0] * Xl[i] + MOUNT_D[0] * Yl[i];
    Y[i] = MOUNT_R[1] * Xl[i] + MOUNT_D[1] * Yl[i];
  }
  frameSetDirect(X, Y, Z);
  return true;
}

void frameSetDirect(const float X[3], const float Y[3], const float Z[3]) {
  for (int i = 0; i < 3; i++) { screenFrame.X[i] = X[i]; screenFrame.Y[i] = Y[i]; screenFrame.Z[i] = Z[i]; }
  screenFrame.valid = true;
}

Sensors sensorsFromImu(const ImuSnap &s, float dt) {
  const ScreenFrame &f = screenFrame;
  float LX[3], LY[3];
  logicalAxes(LX, LY);
  Sensors o;
  // gravity points opposite to the measured "up" vector
  o.gx = -dot(s.up, LX);
  o.gy = -dot(s.up, LY);
  o.gz = dot(s.up, f.Z);
  float wz = dot(s.gyro, f.Z);
  // prefer the integrated rotation over the frame (does not miss fast spins)
  if (dt > 0) wz = dot(s.rot, f.Z) / dt;
  o.wz = wz;
  float rm = sqrtf(dot(s.rot, s.rot));
  o.wmag = dt > 0 ? rm / dt : 0;
  o.jerk = s.hpPeakMag;
  o.jx = dot(s.hpPeak, LX);
  o.jy = dot(s.hpPeak, LY);
  o.freefallMs = s.freefallMs;
  o.stillSec = s.stillSec;
  return o;
}
