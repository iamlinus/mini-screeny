#include "frame.h"
#include <Preferences.h>
#include <math.h>

ScreenFrame screenFrame = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, false};

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
  // upright portrait: up = -Y  =>  Y = -u2, made orthogonal to Z
  float Y[3] = {-u2[0], -u2[1], -u2[2]}; norm(Y);
  float d = dot(Y, Z);
  if (fabsf(d) > 0.8f) return false;   // second pose was not upright enough (~53 deg back-tilt allowed)
  for (int i = 0; i < 3; i++) Y[i] -= d * Z[i];
  norm(Y);
  // Y points down and Z toward the viewer, so (X, Y, Z) is left-handed: X = Z x Y (not Y x Z)
  float X[3]; cross(Z, Y, X);
  frameSetDirect(X, Y, Z);
  return true;
}

void frameSetDirect(const float X[3], const float Y[3], const float Z[3]) {
  for (int i = 0; i < 3; i++) { screenFrame.X[i] = X[i]; screenFrame.Y[i] = Y[i]; screenFrame.Z[i] = Z[i]; }
  screenFrame.valid = true;
}

Sensors sensorsFromImu(const ImuSnap &s, float dt) {
  const ScreenFrame &f = screenFrame;
  Sensors o;
  // gravity points opposite to the measured "up" vector
  o.gx = -dot(s.up, f.X);
  o.gy = -dot(s.up, f.Y);
  o.gz = dot(s.up, f.Z);
  float wz = dot(s.gyro, f.Z);
  // prefer the integrated rotation over the frame (does not miss fast spins)
  if (dt > 0) wz = dot(s.rot, f.Z) / dt;
  o.wz = wz;
  float rm = sqrtf(dot(s.rot, s.rot));
  o.wmag = dt > 0 ? rm / dt : 0;
  o.jerk = s.hpPeakMag;
  o.jx = dot(s.hpPeak, f.X);
  o.jy = dot(s.hpPeak, f.Y);
  o.freefallMs = s.freefallMs;
  o.stillSec = s.stillSec;
  return o;
}
