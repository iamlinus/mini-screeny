// Interactive orange buddy. Physics in screen pixels; gravity and motion come from Sensors.
#include "buddy.h"
#include "board.h"
#include "sound.h"
#include <math.h>

namespace {

const float W = SCREEN_W, H = SCREEN_H;
const float PI_F = 3.14159265f;
const float GPX = 1500.0f;                 // px/s^2 per g
const float BW = 96, BH = 66;              // body size
const float CY_L = 8, HX = 56, HY = 41;    // collision box (local center y, half extents)
const float MARGIN = 3;

// ---------- small helpers ----------
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float wrapPi(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }
inline float frand() { return (esp_random() & 0xFFFF) / 65535.0f; }
inline float frange(float a, float b) { return a + (b - a) * frand(); }
template <size_t N> const char *pick(const char *const (&a)[N]) { return a[esp_random() % N]; }

uint16_t rgb(Arduino_GFX *g, float r, float gr, float b) {
  return g->color565((uint8_t)clampf(r, 0, 255), (uint8_t)clampf(gr, 0, 255), (uint8_t)clampf(b, 0, 255));
}

// ---------- messages ----------
const char *const M_IDLE[] = {"Hej Linus!", "Luta på mig!", "Snurra mig!", "Knacka på mig!", "Vänd mig upp och ner!",
                              "Jag bor i en ESP32", "Vad bygger vi idag?", "PWR = nästa sida", "Skaka inte... eller?"};
const char *const M_OUCH[] = {"Aj!", "Oj!", "Bonk!", "Mitt huvud...", "Det där kändes", "Aj aj aj"};
const char *const M_SLIDE[] = {"Wiii!", "Hoppsan!", "Jag glider!", "Lutar det inte lite?", "Neeej!"};
const char *const M_DIZZY[] = {"Snurrrrigt...", "Världen snurrar!", "Sluta snurra!", "Jag mår illa...", "Wooooah"};
const char *const M_DIZZY_END[] = {"Uhh... var är jag?", "Vilket håll är upp?", "Aldrig mer...", "Puh."};
const char *const M_SHAKE[] = {"HALLÅ!", "Jordbävning!", "Skaka mig inte!", "Hjäääälp!", "Vad händer?!"};
const char *const M_ANGRY[] = {"NU räcker det!", "Grrrr!", "Sluta skaka!", "Jag blir arg nu!"};
const char *const M_TAP[] = {"Boing!", "Vem knackar?", "Kom in!", "Hallå?", "Hoppla!"};
const char *const M_FALL[] = {"AAAAAH!", "Jag flyyyger!", "Fånga mig!"};
const char *const M_DARK[] = {"Det var mörkt där!", "Hej igen!", "Vem släckte?"};
const char *const M_CEIL[] = {"Allt är upp och ner!", "Jag står i taket!", "Vem vände på världen?"};
const char *const M_WALL[] = {"Nu står jag på väggen!", "Sidledes är också ett håll", "Konstig vinkel..."};
const char *const M_WAKE[] = {"Va?! Jag var vaken!", "Jag vilade bara ögonen", "Mm... god morgon"};
const char *const M_FLOAT[] = {"Jag svävar!", "Viktlös!", "Flyter runt..."};

// ---------- state ----------
struct Body { float x, y, vx, vy, th, om; } b;
float squash = 0, squashV = 0;
float t = 0;
float dizzy = 0, fright = 0, anger = 0, joy = 0, shake = 0;
bool dizzyPeak = false;
bool asleep = false, yawned = false;
float blinkIn = 2.5f, blinkLeft = 0;
float faceDownT = 0; bool faceDown = false;
bool grounded = false; float groundT = 0, airT = 0;
float gnx = 0, gny = -1;                  // normal of the supporting wall
float gdx = 0, gdy = 1, gmag = 1;         // effective gravity direction
float sayLeft = 0; char sayBuf[48] = ""; int sayPrio = 0;
float chatIn = 6;
float wanderIn = 4, wanderGoal = -1, walkPhase = 0, walkSpeed = 0;
float waveLeft = 0;
float screamLeft = 0;
float tapPending = 0, tapCooldown = 0;
float cdOuch = 0, cdSlide = 0, cdCeil = 0, cdWall = 0, cdShake = 0, cdFloat = 0;
float ceilT = 0, wallT = 0, floatT = 0;
float lookX = 0, lookY = 0;
float zzzIn = 0;
float lastSpin = 0;

// ---------- particles ----------
enum PType : uint8_t { P_DUST, P_STAR, P_ZZZ, P_SWEAT, P_HEART, P_SPARK };
struct Part { float x, y, vx, vy, life, max; PType type; bool on; };
Part parts[48];

void spawn(PType ty, float x, float y, float vx, float vy, float life) {
  for (auto &p : parts) if (!p.on) { p = {x, y, vx, vy, life, life, ty, true}; return; }
}

void say(const char *m, float sec, int prio) {
  if (sayLeft > 0 && prio < sayPrio) return;
  strlcpy(sayBuf, m, sizeof sayBuf);
  sayLeft = sec; sayPrio = prio;
}

// ---------- drawing helpers ----------
struct Xf {
  float x, y, c, s, sx, sy;
  void apply(float lx, float ly, float &X, float &Y) const {
    lx *= sx; ly *= sy;
    X = x + lx * c - ly * s;
    Y = y + lx * s + ly * c;
  }
  float rs() const { return sqrtf(sx * sy); }
};

void quad(Arduino_GFX *g, const float px[4], const float py[4], uint16_t col) {
  g->fillTriangle(lroundf(px[0]), lroundf(py[0]), lroundf(px[1]), lroundf(py[1]), lroundf(px[2]), lroundf(py[2]), col);
  g->fillTriangle(lroundf(px[0]), lroundf(py[0]), lroundf(px[2]), lroundf(py[2]), lroundf(px[3]), lroundf(py[3]), col);
}

// Rounded rect in local coords: centre (cx,cy), size w x h, corner r, extra rotation a.
void rrect(Arduino_GFX *g, const Xf &xf, float cx, float cy, float w, float h, float r, float a, uint16_t col) {
  r = fminf(r, fminf(w, h) * 0.5f);
  float ca = cosf(a), sa = sinf(a);
  auto L = [&](float u, float v, float &X, float &Y) { xf.apply(cx + u * ca - v * sa, cy + u * sa + v * ca, X, Y); };
  float px[4], py[4];
  float hw = w / 2, hh = h / 2;
  if (r < 1.5f) {
    L(-hw, -hh, px[0], py[0]); L(hw, -hh, px[1], py[1]); L(hw, hh, px[2], py[2]); L(-hw, hh, px[3], py[3]);
    quad(g, px, py, col);
    return;
  }
  L(-hw + r, -hh, px[0], py[0]); L(hw - r, -hh, px[1], py[1]); L(hw - r, hh, px[2], py[2]); L(-hw + r, hh, px[3], py[3]);
  quad(g, px, py, col);
  L(-hw, -hh + r, px[0], py[0]); L(hw, -hh + r, px[1], py[1]); L(hw, hh - r, px[2], py[2]); L(-hw, hh - r, px[3], py[3]);
  quad(g, px, py, col);
  float rr = r * xf.rs();
  const float cxs[4] = {-hw + r, hw - r, hw - r, -hw + r}, cys[4] = {-hh + r, -hh + r, hh - r, hh - r};
  for (int i = 0; i < 4; i++) { float X, Y; L(cxs[i], cys[i], X, Y); g->fillCircle(lroundf(X), lroundf(Y), lroundf(rr), col); }
}

void circleL(Arduino_GFX *g, const Xf &xf, float lx, float ly, float r, uint16_t col) {
  float X, Y; xf.apply(lx, ly, X, Y);
  g->fillCircle(lroundf(X), lroundf(Y), lroundf(r * xf.rs()), col);
}

void thickLineL(Arduino_GFX *g, const Xf &xf, float x0, float y0, float x1, float y1, float th, uint16_t col) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 0.01f) return;
  rrect(g, xf, (x0 + x1) / 2, (y0 + y1) / 2, len + th, th, th / 2, atan2f(dy, dx), col);
}

void sparkle(Arduino_GFX *g, float x, float y, float r, uint16_t col) {
  int X = lroundf(x), Y = lroundf(y), R = lroundf(r), q = max(1, R / 4);
  g->fillTriangle(X, Y - R, X - q, Y, X + q, Y, col);
  g->fillTriangle(X, Y + R, X - q, Y, X + q, Y, col);
  g->fillTriangle(X - R, Y, X, Y - q, X, Y + q, col);
  g->fillTriangle(X + R, Y, X, Y - q, X, Y + q, col);
}

void heart(Arduino_GFX *g, float x, float y, float r, uint16_t col) {
  int X = lroundf(x), Y = lroundf(y), R = max(2, (int)lroundf(r));
  g->fillCircle(X - R / 2, Y, R / 2 + 1, col);
  g->fillCircle(X + R / 2, Y, R / 2 + 1, col);
  g->fillTriangle(X - R, Y + 1, X + R, Y + 1, X, Y + R + 2, col);
}

void centerText(Arduino_GFX *g, const char *s, int cx, int y) {
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(cx - w / 2 - x1, y);
  g->print(s);
}

// background stars (fixed seeds)
struct Star { uint8_t x, y, depth; };
Star stars[30];

}  // namespace

// =====================================================================

void buddyInit() {
  b = {W / 2, H - 60, 0, 0, 0, 0};
  uint32_t seed = 12345;
  for (auto &s : stars) {
    seed = seed * 1103515245 + 12345; s.x = (seed >> 16) % 240;
    seed = seed * 1103515245 + 12345; s.y = (seed >> 16) % 280;
    seed = seed * 1103515245 + 12345; s.depth = 1 + (seed >> 16) % 3;
  }
  for (auto &p : parts) p.on = false;
  say("Hej Linus!", 2.5f, 1);
  waveLeft = 2.0f;
}

void buddySay(const char *msg, float sec, int prio) { say(msg, sec, prio); }
bool buddyAsleep() { return asleep; }

void buddyBootShort() {
  soundSetMuted(!soundMuted());
  say(soundMuted() ? "Ljud av" : "Ljud på!", 1.5f, 3);
  if (!soundMuted()) sfx::chirp();
}

static void wake(bool loud) {
  if (!asleep) return;
  asleep = false; yawned = false;
  fright = fmaxf(fright, 0.5f);
  say(pick(M_WAKE), 2.2f, 2);
  if (loud) sfx::chirp();
}

void buddyUpdate(const Sensors &s, float dt) {
  dt = clampf(dt, 0.001f, 0.05f);
  t += dt;

  // ---- effective gravity (when lying flat, fall back to a gentle "screen-down") ----
  float pm = sqrtf(s.gx * s.gx + s.gy * s.gy);
  float k = clampf((0.35f - pm) / 0.35f, 0, 1);
  float gex = s.gx, gey = s.gy + 0.55f * k;
  if (s.freefallMs > 60) { gex = s.gx; gey = s.gy; }   // weightless while falling
  gmag = sqrtf(gex * gex + gey * gey);
  if (gmag > 0.02f) { gdx = gex / gmag; gdy = gey / gmag; }

  // ---- event detection ----
  float spin = fabsf(s.wz);
  lastSpin = spin;
  bool moving = s.stillSec < 0.05f;

  // dizziness from spinning in the screen plane (and a bit from tumbling)
  if (spin > 80) dizzy += (spin - 80) / 600.0f * dt;
  float other = fmaxf(0, s.wmag - spin);
  if (other > 150) dizzy += (other - 150) / 1400.0f * dt;
  if (spin < 60 && other < 120) dizzy -= 0.13f * dt;
  dizzy = clampf(dizzy, 0, 1.6f);
  if (dizzy > 0.5f && !dizzyPeak) { dizzyPeak = true; say(pick(M_DIZZY), 2.5f, 2); sfx::dizzy(); wake(false); }
  if (dizzyPeak && dizzy < 0.3f) { dizzyPeak = false; say(pick(M_DIZZY_END), 2.5f, 1); }

  // shaking
  if (s.jerk > 0.5f) shake += (s.jerk - 0.5f) * dt * 3.0f;
  shake = clampf(shake - 0.9f * dt, 0, 5);
  if (shake > 0.9f && cdShake <= 0) { say(pick(M_SHAKE), 1.8f, 2); cdShake = 4; fright = 1; sfx::bonk(0.8f); wake(false); }
  if (shake > 2.0f) anger += dt * 0.7f;
  anger = clampf(anger - 0.18f * dt, 0, 1.5f);
  static bool angryOn = false;
  if (anger > 0.9f && !angryOn) { angryOn = true; say(pick(M_ANGRY), 2.5f, 3); sfx::grumble(); }
  if (anger < 0.4f) angryOn = false;
  if (shake > 0.6f && frand() < dt * 8) spawn(P_SWEAT, b.x + frange(-40, 40), b.y - 30, frange(-60, 60), -80, 0.7f);

  // knock / tap: one spike without more spikes right after
  if (tapPending > 0) {
    tapPending -= dt;
    if (s.jerk > 0.45f) tapPending = -1;   // more spikes: that's shaking, not a tap
    else if (tapPending <= 0) {
      tapCooldown = 0.6f;
      if (asleep) wake(true);
      else {
        if (grounded) { b.vx -= gdx * 560; b.vy -= gdy * 560; }
        say(pick(M_TAP), 1.6f, 2);
        sfx::boing();
        joy = fmaxf(joy, 0.7f);
        for (int i = 0; i < 4; i++) spawn(P_HEART, b.x + frange(-30, 30), b.y - 40, frange(-40, 40), -70, 1.0f);
      }
    }
  } else if (s.jerk > 0.7f && shake < 0.5f && tapCooldown <= 0) {
    tapPending = 0.15f;
  }
  tapCooldown -= dt;

  // shake moves the buddy: the screen accelerates, the buddy lags behind
  if (s.jerk > 0.25f) { b.vx -= s.jx * 520; b.vy -= s.jy * 520; }

  // free fall
  if (s.freefallMs > 90) {
    if (screamLeft <= 0) { say(pick(M_FALL), 1.8f, 3); sfx::scream(); wake(false); }
    screamLeft = 0.8f;
  }
  screamLeft -= dt;

  // face down (screen toward the floor)
  if (s.gz < -0.7f) { faceDownT += dt; if (faceDownT > 0.6f) faceDown = true; }
  else { faceDownT = 0; if (faceDown && s.gz > 0.1f) { faceDown = false; say(pick(M_DARK), 2.2f, 2); sfx::chirp(); wake(false); } }

  // sleep after a long still period (only while calm)
  bool calm = dizzy < 0.2f && shake < 0.2f && anger < 0.3f && screamLeft <= 0;
  if (!asleep && calm && grounded && s.stillSec > 22 && !yawned) { yawned = true; say("*gäsp*", 2.5f, 1); sfx::yawn(); }
  if (!asleep && calm && grounded && s.stillSec > 27) { asleep = true; sayLeft = 0; }
  if (asleep && (moving || s.jerk > 0.12f)) wake(true);
  if (!asleep && s.stillSec < 1) yawned = false;

  // ---- physics ----
  float ax = gex * GPX, ay = gey * GPX;
  // walking: a little self-propulsion along the floor when calm
  walkSpeed = 0;
  if (grounded && calm && !asleep && sayLeft <= 0.5f) {
    wanderIn -= dt;
    if (wanderIn <= 0 && wanderGoal < 0) {
      wanderGoal = frange(0.2f, 0.8f);
      wanderIn = frange(4, 9);
    }
  }
  if (grounded && wanderGoal >= 0 && !asleep) {
    // tangent of the floor
    float tx = -gny, ty = gnx;
    // position along tangent mapped to 0..1 across the wall
    float span = fabsf(tx) > 0.5f ? W : H;
    float pos = fabsf(tx) > 0.5f ? b.x : b.y;
    float sgn = (fabsf(tx) > 0.5f ? tx : ty) > 0 ? 1 : -1;
    float goal = wanderGoal * span;
    float d = goal - pos;
    if (fabsf(d) < 6 || !calm) wanderGoal = -1;
    else {
      float dir = (d > 0 ? 1 : -1) * sgn;
      walkSpeed = 50 * dir;
      float vt = b.vx * tx + b.vy * ty;
      float dv = (walkSpeed - vt) * fminf(1, dt * 6);
      b.vx += tx * dv; b.vy += ty * dv;
      walkPhase += dt * 14;
    }
  }

  b.vx += ax * dt; b.vy += ay * dt;
  float drag = 1 - 0.4f * dt;
  b.vx *= drag; b.vy *= drag;
  b.x += b.vx * dt; b.y += b.vy * dt;

  // collision with the screen edges (oriented box support extents)
  float c = cosf(b.th), sn = sinf(b.th);
  bool newGround = false;
  for (int pass = 0; pass < 2; pass++) {
    float bcx = b.x - CY_L * sn, bcy = b.y + CY_L * c;
    float ex = fabsf(c) * HX + fabsf(sn) * HY, ey = fabsf(sn) * HX + fabsf(c) * HY;
    struct Wall { float pen, nx, ny; } walls[4] = {
        {MARGIN - (bcx - ex), 1, 0}, {(bcx + ex) - (W - MARGIN), -1, 0},
        {MARGIN - (bcy - ey), 0, 1}, {(bcy + ey) - (H - MARGIN), 0, -1}};
    for (auto &w : walls) {
      if (w.pen < -0.5f) continue;
      if (w.pen > 0) { b.x += w.nx * w.pen; b.y += w.ny * w.pen; }
      float vn = b.vx * w.nx + b.vy * w.ny;
      float support = -(gex * w.nx + gey * w.ny);   // how much gravity pushes into this wall
      if (vn < 0) {
        float impact = -vn;
        float e = impact > 90 ? 0.42f : 0;
        b.vx -= (1 + e) * vn * w.nx; b.vy -= (1 + e) * vn * w.ny;
        if (pass == 0 && impact > 200) {
          squash = fminf(0.38f, squash + impact / 2400.0f);
          float cxp = bcx - w.nx * ex, cyp = bcy - w.ny * ey;
          int n = impact > 600 ? 6 : 3;
          for (int i = 0; i < n; i++)
            spawn(P_DUST, cxp + frange(-30, 30) * fabsf(w.ny), cyp + frange(-30, 30) * fabsf(w.nx),
                  w.nx * frange(20, 70) + frange(-50, 50) * fabsf(w.ny), w.ny * frange(20, 70) + frange(-50, 50) * fabsf(w.nx), 0.5f);
          sfx::bonk(clampf(impact / 1400.0f, 0, 1));
          if (!grounded) b.om += frange(-1, 1) * impact * 0.004f;
          if (impact > 650) {
            fright = fmaxf(fright, 0.8f);
            for (int i = 0; i < 4; i++) spawn(P_SPARK, cxp, cyp, frange(-120, 120), frange(-120, 120), 0.35f);
            if (cdOuch <= 0) { say(pick(M_OUCH), 1.5f, 2); cdOuch = 2.5f; }
            wake(false);
          }
        }
      }
      // Coulomb friction along the wall
      if (support > 0.05f) {
        float tx = -w.ny, ty = w.nx;
        float vt = b.vx * tx + b.vy * ty;
        float fr = 0.38f * support * GPX * dt;
        float nv = fabsf(vt) <= fr ? 0 : vt - (vt > 0 ? fr : -fr);
        if (walkSpeed != 0) nv = vt;   // walking overrides friction
        b.vx += tx * (nv - vt); b.vy += ty * (nv - vt);
      }
      if (support > 0.5f * gmag && gmag > 0.15f) { newGround = true; gnx = w.nx; gny = w.ny; }
    }
  }
  grounded = newGround;
  if (grounded) { groundT += dt; airT = 0; } else { airT += dt; groundT = 0; }

  // ---- rotation: feet toward gravity, lagging while airborne ----
  float target = atan2f(-gdx, gdy);
  float diff = wrapPi(target - b.th);
  if (grounded) b.om += (diff * 95 - b.om * 15) * dt;
  else {
    b.om += (diff * 6 - b.om * 0.9f) * dt;
    b.th -= s.wz * (PI_F / 180) * dt;     // the screen spins under a floating buddy
  }
  if (gmag < 0.15f) b.om *= (1 - 0.5f * dt);     // weightless: keep tumbling slowly
  b.th = wrapPi(b.th + b.om * dt);

  // squash spring
  squashV += (-squash * 260 - squashV * 16) * dt;
  squash += squashV * dt;
  squash = clampf(squash, -0.25f, 0.4f);

  // ---- situational messages ----
  float rel = wrapPi(b.th);
  bool onCeil = grounded && fabsf(rel) > 2.5f;
  bool onWall = grounded && fabsf(fabsf(rel) - PI_F / 2) < 0.45f;
  ceilT = onCeil ? ceilT + dt : 0;
  wallT = onWall ? wallT + dt : 0;
  if (ceilT > 0.5f && cdCeil <= 0) { say(pick(M_CEIL), 2.4f, 2); cdCeil = 12; }
  if (wallT > 0.7f && cdWall <= 0) { say(pick(M_WALL), 2.4f, 1); cdWall = 15; }
  float vt = b.vx * -gny + b.vy * gnx;
  if (grounded && fabsf(vt) > 110 && walkSpeed == 0 && cdSlide <= 0) { say(pick(M_SLIDE), 1.6f, 1); sfx::wiii(); cdSlide = 6; joy = fmaxf(joy, 0.5f); }
  floatT = (gmag < 0.2f && s.freefallMs <= 0 && !grounded) ? floatT + dt : 0;
  if (floatT > 1.2f && cdFloat <= 0) { say(pick(M_FLOAT), 2.0f, 1); cdFloat = 12; }

  // idle chatter
  if (calm && grounded && !asleep) {
    chatIn -= dt;
    if (chatIn <= 0) {
      const char *m = pick(M_IDLE);
      say(m, 2.6f, 0);
      if (m == M_IDLE[0]) waveLeft = 1.8f;
      chatIn = frange(11, 19);
    }
  }

  // cooldowns & decays
  cdOuch -= dt; cdSlide -= dt; cdCeil -= dt; cdWall -= dt; cdShake -= dt; cdFloat -= dt;
  fright = fmaxf(0, fright - 0.6f * dt);
  joy = fmaxf(0, joy - 0.35f * dt);
  sayLeft -= dt;
  waveLeft -= dt;

  // blinking
  blinkIn -= dt;
  if (blinkIn <= 0) { blinkLeft = 0.13f; blinkIn = frange(2.2f, 5.0f); }
  blinkLeft -= dt;

  // eyes look toward motion, otherwise drift around
  float lx = clampf(b.vx / 250, -1, 1) * 5 + sinf(t * 0.7f) * 2.5f;
  float ly = clampf(b.vy / 250, -1, 1) * 3;
  if (walkSpeed != 0) lx = (walkSpeed > 0 ? 4 : -4);
  lookX += (lx - lookX) * fminf(1, dt * 6);
  lookY += (ly - lookY) * fminf(1, dt * 6);

  // particles
  for (auto &p : parts) {
    if (!p.on) continue;
    p.life -= dt;
    if (p.life <= 0) { p.on = false; continue; }
    switch (p.type) {
      case P_DUST:  p.vx *= 1 - 3 * dt; p.vy *= 1 - 3 * dt; break;
      case P_SWEAT: p.vx += gex * 900 * dt; p.vy += gey * 900 * dt + 0; break;
      case P_ZZZ:   p.vx = -gdx * 22 + sinf(t * 3 + p.max) * 12; p.vy = -gdy * 22; break;
      case P_HEART: p.vy -= 20 * dt; break;
      default: break;
    }
    p.x += p.vx * dt; p.y += p.vy * dt;
  }
  if (asleep) {
    zzzIn -= dt;
    if (zzzIn <= 0) { zzzIn = 1.3f; spawn(P_ZZZ, b.x + 30, b.y - 50, 0, 0, 2.6f); }
  }
}

// =====================================================================

void buddyDraw(Arduino_GFX *g) {
  const uint16_t BG = rgb(g, 16, 13, 22);
  g->fillScreen(BG);

  // parallax stars: shift against gravity for depth
  for (auto &s : stars) {
    float d = s.depth;
    int x = (int)(s.x - gdx * gmag * d * 6) % 240, y = (int)(s.y - gdy * gmag * d * 6 + 280) % 280;
    if (x < 0) x += 240;
    uint8_t v = 40 + d * 22;
    g->drawPixel(x, y, rgb(g, v, v, v + 20));
    if (d == 3) g->drawPixel(x + 1, y, rgb(g, v, v, v + 20));
  }

  // soft glow behind the buddy
  g->fillCircle(lroundf(b.x), lroundf(b.y + 6), 70, rgb(g, 30, 21, 30));
  g->fillCircle(lroundf(b.x), lroundf(b.y + 6), 52, rgb(g, 38, 25, 32));

  // ---- expression ----
  enum { E_NORMAL, E_BLINK, E_SLEEP, E_SCREAM, E_DIZZY, E_ANGRY, E_WIDE, E_HAPPY } ex = E_NORMAL;
  if (asleep) ex = E_SLEEP;
  else if (screamLeft > 0) ex = E_SCREAM;
  else if (dizzy > 0.45f) ex = E_DIZZY;
  else if (anger > 0.8f) ex = E_ANGRY;
  else if (fright > 0.45f || shake > 0.6f) ex = E_WIDE;
  else if (joy > 0.45f) ex = E_HAPPY;
  else if (blinkLeft > 0) ex = E_BLINK;

  // ---- body transform ----
  float wob = dizzy > 0.2f ? sinf(t * 6.5f) * 0.16f * fminf(dizzy, 1.2f) : 0;
  float jitter = anger > 0.8f ? sinf(t * 70) * 0.04f : 0;
  float breathe = asleep ? sinf(t * 1.8f) * 0.035f : 0;
  float sq = squash + breathe;
  Xf xf{b.x, b.y, cosf(b.th + wob + jitter), sinf(b.th + wob + jitter), 1 + sq * 0.6f, 1 - sq};

  // colours
  float ar = clampf(anger, 0, 1), dz = clampf(dizzy * 0.5f, 0, 0.35f);
  float br = 217 + (232 - 217) * ar, bg = 119 + (70 - 119) * ar, bb = 87 + (58 - 87) * ar;
  br += (150 - br) * dz; bg += (175 - bg) * dz; bb += (90 - bb) * dz;   // a bit green when dizzy
  if (asleep) { br *= 0.8f; bg *= 0.8f; bb *= 0.8f; }
  const uint16_t BODY = rgb(g, br, bg, bb);
  const uint16_t BODY_D = rgb(g, br * 0.8f, bg * 0.74f, bb * 0.7f);
  const uint16_t BODY_L = rgb(g, br * 1.1f + 10, bg * 1.2f + 10, bb * 1.25f + 10);
  const uint16_t INK = rgb(g, 32, 20, 18);
  const uint16_t WHITE_ = rgb(g, 250, 246, 240);
  const uint16_t GOLD = rgb(g, 255, 214, 120);

  // ---- legs ----
  bool walking = walkSpeed != 0;
  float dangle = grounded ? 0 : 4;
  const float legX[4] = {-32, -12, 12, 32};
  for (int i = 0; i < 4; i++) {
    float lift = walking ? fmaxf(0, sinf(walkPhase + i * PI_F)) * 6 : 0;
    if (screamLeft > 0 || (!grounded && airT > 0.2f)) lift = sinf(t * 30 + i * 1.7f) * 3;
    float h = 18 + dangle - lift;
    rrect(g, xf, legX[i], BH / 2 - 6 + h / 2, 12, h, 4, 0, BODY_D);
  }

  // ---- arms ----
  for (int side = -1; side <= 1; side += 2) {
    float el;   // elevation, 0 = straight out, + = up
    if (asleep) el = -0.6f;
    else if (screamLeft > 0) el = 1.25f + sinf(t * 28 + side) * 0.35f;
    else if (!grounded && airT > 0.15f) el = 0.9f + sinf(t * 22 + side * 1.3f) * 0.5f;
    else if (anger > 0.8f) el = -0.7f + sinf(t * 40) * 0.08f;
    else if (dizzy > 0.45f) el = sinf(t * 5 + side) * 0.5f;
    else if (grounded && fabsf(b.vx * -gny + b.vy * gnx) > 90 && !walking) el = sinf(t * 16 + side * 1.6f) * 1.2f + 0.3f;
    else if (side == 1 && waveLeft > 0) el = 1.0f + sinf(t * 14) * 0.45f;
    else if (walking) el = -0.2f + sinf(walkPhase + (side > 0 ? 0 : PI_F)) * 0.35f;
    else el = -0.25f + sinf(t * 2 + side) * 0.08f;
    float dx = side * cosf(el), dy = -sinf(el);
    float sx = side * (BW / 2 - 3), sy = 2;
    float cx = sx + dx * 9, cy = sy + dy * 9;
    rrect(g, xf, cx, cy, 20, 14, 6, atan2f(dy, dx), BODY);
  }

  // ---- body ----
  rrect(g, xf, 0, 0, BW, BH, 18, 0, BODY);
  rrect(g, xf, 0, -BH / 2 + 10, BW - 22, 9, 4, 0, BODY_L);   // highlight
  // cheeks
  uint16_t cheek = ex == E_HAPPY ? rgb(g, 255, 140, 140) : BODY_L;
  circleL(g, xf, -36, 12, 5, cheek);
  circleL(g, xf, 36, 12, 5, cheek);

  // ---- eyes ----
  const float eyeX[2] = {-19, 19}, eyeY = -6;
  for (int i = 0; i < 2; i++) {
    float ex0 = eyeX[i] + lookX, ey0 = eyeY + lookY;
    switch (ex) {
      case E_SLEEP:
      case E_BLINK:
        rrect(g, xf, ex0, ey0 + 6, 15, 4, 2, 0, INK); break;
      case E_HAPPY:   // ^ ^
        thickLineL(g, xf, ex0 - 7, ey0 + 4, ex0, ey0 - 4, 4, INK);
        thickLineL(g, xf, ex0, ey0 - 4, ex0 + 7, ey0 + 4, 4, INK);
        break;
      case E_DIZZY: {  // spinning spirals
        float px = 0, py = 0;
        float dir = i == 0 ? 1 : -1;
        for (int k = 0; k <= 22; k++) {
          float a = k * 0.62f + t * 9 * dir, r = 0.6f + k * 0.42f;
          float x = ex0 + cosf(a) * r, y = ey0 + sinf(a) * r;
          if (k) thickLineL(g, xf, px, py, x, y, 2.6f, INK);
          px = x; py = y;
        }
        break;
      }
      case E_SCREAM:
      case E_WIDE:
        rrect(g, xf, ex0, ey0, 16, 26, 7, 0, INK);
        circleL(g, xf, ex0 + 2, ey0 - 6, 3, WHITE_);
        break;
      case E_ANGRY:
        rrect(g, xf, ex0, ey0 + 3, 13, 16, 4, 0, INK);
        rrect(g, xf, ex0 - (i == 0 ? -1 : 1) * 1, ey0 - 11, 18, 5, 2, i == 0 ? 0.45f : -0.45f, INK);
        break;
      default:
        rrect(g, xf, ex0, ey0, 13, 23, 4, 0, INK);
        break;
    }
  }

  // ---- mouth ----
  switch (ex) {
    case E_SCREAM: circleL(g, xf, 0 + lookX * 0.5f, 17, 7 + sinf(t * 30) * 1.5f, INK); break;
    case E_HAPPY:
      thickLineL(g, xf, -7, 14, 0, 19, 3.5f, INK);
      thickLineL(g, xf, 0, 19, 7, 14, 3.5f, INK);
      break;
    case E_DIZZY: {
      float px = -11, py = 17;
      for (int k = 1; k <= 8; k++) {
        float x = -11 + k * 2.75f, y = 17 + sinf(k * 1.6f + t * 8) * 2.5f;
        thickLineL(g, xf, px, py, x, y, 2.5f, INK);
        px = x; py = y;
      }
      break;
    }
    case E_ANGRY: rrect(g, xf, 0, 18, 16, 4, 2, 0, INK); break;
    case E_WIDE: circleL(g, xf, 0, 18, 4, INK); break;
    default: break;
  }

  // ---- dizzy stars orbiting the head ----
  if (dizzy > 0.45f) {
    for (int k = 0; k < 3; k++) {
      float a = t * 5 + k * 2.094f;
      float X, Y;
      xf.apply(cosf(a) * 40, -BH / 2 - 14 + sinf(a) * 9, X, Y);
      sparkle(g, X, Y, 7 + 2 * sinf(t * 9 + k), GOLD);
    }
  }
  // steam when angry
  if (anger > 0.8f) {
    for (int k = 0; k < 2; k++) {
      float ph = fmodf(t * 1.6f + k * 0.5f, 1.0f);
      float X, Y;
      xf.apply((k ? 30 : -30) + sinf(t * 6 + k) * 3, -BH / 2 - 8 - ph * 26, X, Y);
      g->fillCircle(lroundf(X), lroundf(Y), lroundf(3 + ph * 6), rgb(g, 200 - ph * 120, 200 - ph * 120, 210 - ph * 120));
    }
  }

  // ---- particles ----
  for (auto &p : parts) {
    if (!p.on) continue;
    float f = p.life / p.max;
    switch (p.type) {
      case P_DUST: g->fillCircle(lroundf(p.x), lroundf(p.y), lroundf(2 + (1 - f) * 5), rgb(g, 70 + 60 * f, 60 + 50 * f, 70 + 50 * f)); break;
      case P_SPARK: sparkle(g, p.x, p.y, 3 + f * 5, GOLD); break;
      case P_SWEAT: g->fillCircle(lroundf(p.x), lroundf(p.y), 3, rgb(g, 120, 190, 255)); break;
      case P_HEART: heart(g, p.x, p.y, 4 + 2 * f, rgb(g, 255, 90 + 60 * (1 - f), 120)); break;
      case P_ZZZ: {
        g->setFont(f > 0.5f ? u8g2_font_helvB12_tf : u8g2_font_helvB10_tf);
        g->setTextColor(rgb(g, 140 + 100 * f, 150 + 90 * f, 220));
        g->setCursor(lroundf(p.x), lroundf(p.y));
        g->print("z");
        break;
      }
      default: break;
    }
  }

  // ---- speech bubble (always upright to the screen) ----
  if (sayLeft > 0 && sayBuf[0]) {
    g->setFont(u8g2_font_helvB10_tf);
    int16_t x1, y1; uint16_t w, h;
    g->getTextBounds(sayBuf, 0, 0, &x1, &y1, &w, &h);
    int pw = w + 24, ph = 30;
    float topY = b.y - 64 - ph - 8;     // prefer above
    bool above = topY > 10;
    int by = above ? (int)topY : (int)fminf(b.y + 62, H - ph - 10);
    int bx = (int)clampf(b.x - pw / 2.0f, 12, W - pw - 12);
    if (!above && by < b.y + 40) by = 10;   // no room anywhere near: park at the top
    uint16_t bubble = WHITE_;
    g->fillRoundRect(bx, by, pw, ph, 13, bubble);
    int tx = (int)clampf(b.x, bx + 16, bx + pw - 16);
    if (above) g->fillTriangle(tx - 7, by + ph - 1, tx + 7, by + ph - 1, tx, by + ph + 9, bubble);
    else if (by > b.y) g->fillTriangle(tx - 7, by + 1, tx + 7, by + 1, tx, by - 9, bubble);
    g->setTextColor(INK);
    centerText(g, sayBuf, bx + pw / 2, by + 20);
  }

  // mute marker
  if (soundMuted()) {
    g->fillRoundRect(210, 22, 14, 10, 3, rgb(g, 90, 80, 95));
    g->drawLine(208, 20, 226, 34, rgb(g, 230, 90, 90));
  }
}

const char *buddyDebug() {
  static char buf[160];
  snprintf(buf, sizeof buf, "pos=(%.0f,%.0f) v=(%.0f,%.0f) th=%.2f gr=%d dz=%.2f sh=%.2f an=%.2f sl=%d say='%s'",
           b.x, b.y, b.vx, b.vy, b.th, grounded, dizzy, shake, anger, asleep, sayLeft > 0 ? sayBuf : "");
  return buf;
}
