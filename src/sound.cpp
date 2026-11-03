#include "sound.h"
#include "board.h"
#include <Preferences.h>

static Note q[24];
static uint8_t qn = 0, qi = 0;
static uint32_t noteStart = 0;
static bool playing = false, muted = false;
static uint16_t lastF = 0;

static void out(uint16_t f) {
  if (f == lastF) return;
  lastF = f;
  if (f == 0) { ledcWrite(LEDC_BUZZER, 0); return; }
  ledcWriteTone(LEDC_BUZZER, f);   // sets 50% duty
}

void soundInit() {
  ledcSetup(LEDC_BUZZER, 2000, 10);
  ledcAttachPin(BUZZER_PIN, LEDC_BUZZER);
  ledcWrite(LEDC_BUZZER, 0);
  Preferences p; p.begin("buddy", true); muted = p.getBool("mute", false); p.end();
}

void soundSetMuted(bool m) {
  muted = m;
  if (m) { playing = false; out(0); }
  Preferences p; p.begin("buddy", false); p.putBool("mute", m); p.end();
}
bool soundMuted() { return muted; }
bool soundBusy() { return playing; }

void soundPlay(const Note *seq, uint8_t n, bool interrupt) {
#ifdef QUIET_BUILD
  return;   // development builds never make noise (user is asleep)
#endif
  if (muted) return;
  if (playing && !interrupt) return;
  if (n > 24) n = 24;
  memcpy(q, seq, n * sizeof(Note));
  qn = n; qi = 0; noteStart = millis(); playing = true;
  lastF = 0xFFFF;
}

void soundUpdate() {
  if (!playing) return;
  uint32_t now = millis();
  while (qi < qn && now - noteStart >= q[qi].ms) { noteStart += q[qi].ms; qi++; }
  if (qi >= qn) { playing = false; out(0); return; }
  const Note &n = q[qi];
  if (n.f0 == 0) { out(0); return; }
  float t = (float)(now - noteStart) / n.ms;
  out((uint16_t)(n.f0 + (n.f1 - (float)n.f0) * t));
}

namespace sfx {
void click()  { static const Note s[] = {{2400, 2400, 14}}; soundPlay(s, 1); }
void boing()  { static const Note s[] = {{260, 900, 90}, {900, 500, 70}, {500, 700, 60}}; soundPlay(s, 3); }
void bonk(float k) {
  uint16_t f = 120 + (uint16_t)(k * 220);
  Note s[] = {{(uint16_t)(f * 2), f, (uint16_t)(25 + k * 35)}};
  soundPlay(s, 1, k > 0.5f);
}
void scream() { static const Note s[] = {{700, 1900, 260}, {1900, 1700, 120}, {1700, 1950, 120}, {1950, 1600, 160}}; soundPlay(s, 4); }
void dizzy()  { static const Note s[] = {{900, 600, 90}, {600, 850, 90}, {850, 550, 90}, {550, 800, 90}, {800, 400, 160}}; soundPlay(s, 5, false); }
void chirp()  { static const Note s[] = {{1500, 2300, 50}, {0, 0, 30}, {2000, 2600, 50}}; soundPlay(s, 3); }
void grumble(){ static const Note s[] = {{140, 110, 90}, {0, 0, 40}, {130, 100, 120}}; soundPlay(s, 3, false); }
void knock()  { static const Note s[] = {{420, 300, 22}, {0, 0, 70}, {420, 300, 22}}; soundPlay(s, 3); }
void wiii()   { static const Note s[] = {{900, 1600, 180}, {1600, 1500, 120}}; soundPlay(s, 2, false); }
void yawn()   { static const Note s[] = {{500, 700, 200}, {700, 350, 380}}; soundPlay(s, 2, false); }
void dance() {
  static const Note s[] = {{784, 784, 120}, {0, 0, 30}, {988, 988, 120}, {0, 0, 30}, {1175, 1175, 120}, {0, 0, 30}, {988, 988, 120},
                           {0, 0, 30}, {1319, 1319, 220}, {0, 0, 60}, {1175, 1175, 120}, {0, 0, 30}, {988, 988, 120}, {0, 0, 30},
                           {784, 784, 120}, {0, 0, 30}, {1047, 1568, 260}};
  soundPlay(s, 17);
}
void tada()   { static const Note s[] = {{1047, 1047, 90}, {1319, 1319, 90}, {1568, 1568, 90}, {2093, 2093, 220}}; soundPlay(s, 4); }
}
