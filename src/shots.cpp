#include "shots.h"
#include "board.h"
#include "sound.h"
#include <math.h>

namespace {

const float W = SCREEN_W, H = SCREEN_H, PI_F = 3.14159265f;

struct V2 { float x, y; };

// glass geometry (screen px). Interior is where liquid can be; the rim is the top edge.
const V2 INNER[4] = {{66, 74}, {174, 74}, {160, 206}, {80, 206}};
const V2 OUTER[4] = {{56, 64}, {184, 64}, {170, 236}, {70, 236}};

struct ShotType {
  const char *name;
  uint8_t r, g, b;          // main colour (top layer for layered drinks)
  uint8_t layers;           // 1..3
  uint8_t lr[3], lg[3], lb[3];   // layer colours bottom -> top (layered drinks)
  float frac[3];            // layer volume fractions bottom -> top
  bool flakes, saltLime;
  const char *after;        // said after drinking
};

const ShotType TYPES[] = {
    {"Chili & ingefära", 236, 92, 32, 1, {}, {}, {}, {1}, true, false, "HETT!!!"},
    {"Tequila", 238, 214, 140, 1, {}, {}, {}, {1}, false, true, "Lime! Lime!"},
    {"Jäger", 96, 46, 26, 1, {}, {}, {}, {1}, false, false, "Kryddigt..."},
    {"Salmiak", 62, 58, 66, 1, {}, {}, {}, {1}, false, false, "Salmiakansikte!"},
    {"B-52", 230, 150, 50, 3, {62, 214, 230}, {30, 188, 150}, {16, 150, 50}, {0.36f, 0.34f, 0.30f}, false, false, "Lager på lager!"},
    {"Blå lagun", 40, 124, 232, 1, {}, {}, {}, {1}, false, false, "Blå tunga!"},
    {"Hallon", 232, 62, 112, 1, {}, {}, {}, {1}, false, false, "Sött!"},
};
const int NTYPES = sizeof(TYPES) / sizeof(TYPES[0]);

int type = 0;
float V0 = 0, V = 0;              // liquid area in px^2
float phi = 0, phiV = 0;          // surface angle (0 = gravity straight down the screen) + slosh velocity
float gx_ = 0, gy_ = 1;           // effective gravity unit vector
float t = 0;
bool drinking = false, empty = false, refilling = false;
float emptyT = 0, drained = 0;
int shotsDrunk = 0;
char msg[40] = ""; float msgLeft = 0;
float heatLeft = 0;
float lastLevel = 0, pourRate = 0;
V2 pourAt = {0, 0};

struct Drop { float x, y, vx, vy; bool on; };
Drop drops[40];
struct Flake { float x, y, vx, vy; uint8_t kind; };
Flake flakes[16];

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float frand() { return (esp_random() & 0xFFFF) / 65535.0f; }
inline float wrapPi(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }

int clipHalf(const V2 *in, int n, float nx, float ny, float d, V2 *out) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    const V2 &a = in[i], &b = in[(i + 1) % n];
    float da = a.x * nx + a.y * ny - d, db = b.x * nx + b.y * ny - d;
    if (da >= 0) out[m++] = a;
    if ((da >= 0) != (db >= 0)) {
      float k = da / (da - db);
      out[m++] = {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k};
    }
  }
  return m;
}

float area(const V2 *p, int n) {
  float s = 0;
  for (int i = 0; i < n; i++) { const V2 &a = p[i], &b = p[(i + 1) % n]; s += a.x * b.y - b.x * a.y; }
  return fabsf(s) * 0.5f;
}

// level d such that the part of the interior with p·n >= d has the given area
float solveLevel(float nx, float ny, float vol) {
  float lo = 1e9, hi = -1e9;
  for (auto &p : INNER) { float v = p.x * nx + p.y * ny; lo = fminf(lo, v); hi = fmaxf(hi, v); }
  V2 tmp[8];
  for (int it = 0; it < 22; it++) {
    float mid = (lo + hi) / 2;
    float a = area(tmp, clipHalf(INNER, 4, nx, ny, mid, tmp));
    if (a > vol) lo = mid; else hi = mid;
  }
  return (lo + hi) / 2;
}

void fillPoly(Arduino_GFX *g, const V2 *p, int n, uint16_t col, float ox) {
  for (int i = 1; i + 1 < n; i++)
    g->fillTriangle(lroundf(p[0].x + ox), lroundf(p[0].y), lroundf(p[i].x + ox), lroundf(p[i].y),
                    lroundf(p[i + 1].x + ox), lroundf(p[i + 1].y), col);
}

bool insideInner(float x, float y) {
  for (int i = 0; i < 4; i++) {
    const V2 &a = INNER[i], &b = INNER[(i + 1) % 4];
    if ((b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x) < 0) return false;   // clockwise in screen coords
  }
  return true;
}

void say(const char *m, float sec) { strlcpy(msg, m, sizeof msg); msgLeft = sec; }

void seedFlakes() {
  for (auto &f : flakes) {
    do { f.x = 70 + frand() * 100; f.y = 90 + frand() * 110; } while (!insideInner(f.x, f.y));
    f.vx = f.vy = 0; f.kind = frand() < 0.6f ? 0 : 1;
  }
}

uint16_t mix(Arduino_GFX *g, uint8_t r, uint8_t gg, uint8_t b, float k) {   // k<1 darker, >1 lighter
  auto c = [&](float v) { return (uint8_t)clampf(k <= 1 ? v * k : v + (255 - v) * (k - 1), 0, 255); };
  return g->color565(c(r), c(gg), c(b));
}

}  // namespace

void shotsInit() {
  V0 = area(INNER, 4) * 0.62f;
  V = V0; phi = 0; phiV = 0; empty = refilling = drinking = false;
  for (auto &d : drops) d.on = false;
  seedFlakes();
}

void shotsNext() {
  type = (type + 1) % NTYPES;
  V = 0; empty = false; refilling = true; drinking = false;
  seedFlakes();
  say(TYPES[type].name, 1.6f);
  sfx::click();
}

void shotsUpdate(const Sensors &s, float dt) {
  dt = clampf(dt, 0.001f, 0.05f);
  t += dt;

  // effective gravity (gentle "down" when the board lies flat)
  float pm = sqrtf(s.gx * s.gx + s.gy * s.gy);
  float k = clampf((0.35f - pm) / 0.35f, 0, 1);
  float ex = s.gx, ey = s.gy + 0.55f * k;
  float em = sqrtf(ex * ex + ey * ey);
  if (em > 0.02f) { gx_ = ex / em; gy_ = ey / em; }

  // slosh: surface angle springs toward gravity, jerks kick it
  float target = atan2f(gx_, gy_);
  float diff = wrapPi(target - phi);
  phiV += (diff * 70 - phiV * 3.2f) * dt;
  if (s.jerk > 0.25f) phiV += clampf(s.jx * 3.5f, -6, 6);
  phi = wrapPi(phi + phiV * dt);
  float nx = sinf(phi), ny = cosf(phi);

  // pouring over the rim
  pourRate = 0;
  if (V > 1 && !refilling) {
    float d = solveLevel(nx, ny, V);
    float r1 = INNER[0].x * nx + INNER[0].y * ny, r2 = INNER[1].x * nx + INNER[1].y * ny;
    float depth = fmaxf(r1, r2) - d;
    if (depth > 0) {
      pourRate = fminf(8500, 230 * depth + 900);
      float dv = fminf(V, pourRate * dt);
      V -= dv; drained += dv;
      pourAt = r1 > r2 ? INNER[0] : INNER[1];
      if (!drinking) { drinking = true; }
      int n = 1 + (int)(pourRate / 5000);
      for (int i = 0; i < n; i++)
        for (auto &dp : drops) if (!dp.on) {
          dp = {pourAt.x + (frand() - 0.5f) * 6, pourAt.y + (frand() - 0.5f) * 4,
                nx * 140 + (pourAt.x < 120 ? -40 : 40) + (frand() - 0.5f) * 60, ny * 140 + (frand() - 0.5f) * 60, true};
          break;
        }
    }
  }

  // finished the shot
  if (drinking && V < V0 * 0.03f) {
    V = 0; drinking = false; empty = true; emptyT = 0;
    if (drained > V0 * 0.5f) {
      shotsDrunk++;
      const ShotType &st = TYPES[type];
      if (st.flakes) { heatLeft = 2.8f; say(st.after, 2.8f); sfx::sizzle(); }
      else { say(shotsDrunk >= 5 && shotsDrunk % 5 == 0 ? "Ta det lugnt nu!" : (shotsDrunk % 2 ? "Skål!" : st.after), 2.2f); sfx::burp(); }
    }
    drained = 0;
  }
  if (!drinking && V > V0 * 0.5f) drained = 0;

  // refill: once empty and the glass stands up again
  if (empty) {
    emptyT += dt;
    if (emptyT > 1.3f && ny > 0.8f && heatLeft <= 0) { empty = false; refilling = true; sfx::glug(); }
  }
  if (refilling) {
    V += V0 * dt / 1.3f;
    if (V >= V0) { V = V0; refilling = false; }
  }

  // drops fly off
  for (auto &dp : drops) {
    if (!dp.on) continue;
    dp.vx += gx_ * 900 * dt; dp.vy += gy_ * 900 * dt;
    dp.x += dp.vx * dt; dp.y += dp.vy * dt;
    if (dp.x < -10 || dp.x > W + 10 || dp.y < -10 || dp.y > H + 10) dp.on = false;
  }

  // flakes swirl with the slosh and sink slowly toward gravity
  for (auto &f : flakes) {
    f.vx += (gx_ * 14 + (-ny) * phiV * 9 + (frand() - 0.5f) * 30) * dt;
    f.vy += (gy_ * 14 + (nx) * phiV * 9 + (frand() - 0.5f) * 30) * dt;
    f.vx *= 1 - 2.5f * dt; f.vy *= 1 - 2.5f * dt;
    float nxp = f.x + f.vx * dt, nyp = f.y + f.vy * dt;
    if (insideInner(nxp, nyp)) { f.x = nxp; f.y = nyp; } else { f.vx = -f.vx * 0.5f; f.vy = -f.vy * 0.5f; }
  }

  msgLeft -= dt;
  heatLeft -= dt;
}

void shotsDraw(Arduino_GFX *g) {
  const ShotType &st = TYPES[type];
  const uint16_t BG = g->color565(16, 13, 22);
  g->fillScreen(BG);

  // tipsy sway after a few shots
  float ox = shotsDrunk >= 4 ? sinf(t * 1.6f) * fminf(12, 3.0f * (shotsDrunk - 3)) : 0;

  // heat flash behind everything
  if (heatLeft > 0) {
    float f = fminf(1, heatLeft / 2.8f) * (0.6f + 0.4f * sinf(t * 25));
    g->fillScreen(g->color565(16 + 90 * f, 13 + 10 * f, 22));
  }

  // tequila lime sits on the rim, behind the glass
  if (st.saltLime) {
    int lx = lroundf(176 + ox), ly = 64;
    g->fillCircle(lx, ly, 17, g->color565(70, 160, 40));
    g->fillCircle(lx, ly, 14, g->color565(190, 230, 110));
    for (int k = 0; k < 6; k++) {
      float a = k * PI_F / 3;
      g->drawLine(lx, ly, lx + lroundf(cosf(a) * 13), ly + lroundf(sinf(a) * 13), g->color565(240, 250, 200));
    }
  }

  // glass
  fillPoly(g, OUTER, 4, g->color565(78, 92, 112), ox);
  fillPoly(g, INNER, 4, g->color565(28, 30, 42), ox);

  // liquid
  float nx = sinf(phi), ny = cosf(phi);
  V2 poly[8];
  if (V > 1) {
    float d = solveLevel(nx, ny, V);
    int n = clipHalf(INNER, 4, nx, ny, d, poly);
    if (st.layers == 1) {
      fillPoly(g, poly, n, g->color565(st.r, st.g, st.b), ox);
      // deeper half a bit darker for depth
      float d2 = solveLevel(nx, ny, V * 0.55f);
      V2 p2[8]; int n2 = clipHalf(INNER, 4, nx, ny, d2, p2);
      fillPoly(g, p2, n2, mix(g, st.r, st.g, st.b, 0.9f), ox);
    } else {
      // painter from the top layer down
      float cum = 1;
      for (int L = st.layers - 1; L >= 0; L--) {
        float dl = solveLevel(nx, ny, V * cum);
        V2 pl[8]; int nl = clipHalf(INNER, 4, nx, ny, dl, pl);
        fillPoly(g, pl, nl, g->color565(st.lr[L], st.lg[L], st.lb[L]), ox);
        cum -= st.frac[L];
      }
    }
    // flakes (chili + ginger) inside the liquid only
    if (st.flakes) {
      for (auto &f : flakes) {
        if (f.x * nx + f.y * ny < d + 3) continue;
        int X = lroundf(f.x + ox), Y = lroundf(f.y);
        if (f.kind == 0) g->fillRect(X - 1, Y - 1, 3, 3, g->color565(170, 18, 12));
        else g->fillRect(X - 2, Y, 5, 2, g->color565(248, 214, 120));
      }
    }
    // shiny surface line
    V2 a = {0, 0}, b2 = {0, 0}; int found = 0;
    for (int i = 0; i < n; i++) {
      float v = poly[i].x * nx + poly[i].y * ny - d;
      if (fabsf(v) < 0.6f) { if (found == 0) a = poly[i]; else b2 = poly[i]; found++; }
    }
    if (found >= 2) {
      uint16_t hl = st.layers > 1 ? mix(g, st.lr[2], st.lg[2], st.lb[2], 1.35f) : mix(g, st.r, st.g, st.b, 1.4f);
      g->drawLine(lroundf(a.x + ox), lroundf(a.y), lroundf(b2.x + ox), lroundf(b2.y), hl);
      g->drawLine(lroundf(a.x + ox), lroundf(a.y) + 1, lroundf(b2.x + ox), lroundf(b2.y) + 1, hl);
    }
  }

  // refill stream from above
  if (refilling) {
    uint16_t c = st.layers > 1 ? g->color565(st.lr[0], st.lg[0], st.lb[0]) : g->color565(st.r, st.g, st.b);
    g->fillRect(lroundf(117 + ox), 0, 6, 200, c);
  }

  // glass highlights + rim
  uint16_t hi = g->color565(190, 205, 225);
  g->drawLine(lroundf(70 + ox), 84, lroundf(81 + ox), 196, hi);
  g->drawLine(lroundf(71 + ox), 84, lroundf(82 + ox), 196, hi);
  g->drawLine(lroundf(165 + ox), 90, lroundf(158 + ox), 160, g->color565(120, 135, 155));
  g->fillRect(lroundf(56 + ox), 63, 129, 3, g->color565(160, 176, 198));
  g->fillRect(lroundf(84 + ox), 214, 72, 3, g->color565(110, 124, 146));

  // tequila: salt on the rim and a lime wedge
  if (st.saltLime) {
    for (int i = 0; i < 26; i++) g->fillRect(lroundf(58 + i * 4.8f + ox), 61 + (i * 7) % 3, 2, 2, g->color565(250, 250, 250));
  }

  // drops
  for (auto &dp : drops) if (dp.on) {
    uint16_t c = st.layers > 1 ? g->color565(st.lr[2], st.lg[2], st.lb[2]) : g->color565(st.r, st.g, st.b);
    g->fillCircle(lroundf(dp.x + ox), lroundf(dp.y), 2, c);
  }

  // flames when the chili kicks in
  if (heatLeft > 0) {
    for (int i = 0; i < 9; i++) {
      float fx = 14 + i * 26.5f;
      float h = 34 + 22 * sinf(t * 14 + i * 1.7f) + 10 * sinf(t * 23 + i);
      g->fillTriangle(lroundf(fx - 14), 280, lroundf(fx + 14), 280, lroundf(fx + sinf(t * 9 + i) * 5), lroundf(280 - h), g->color565(240, 60, 20));
      g->fillTriangle(lroundf(fx - 8), 280, lroundf(fx + 8), 280, lroundf(fx + sinf(t * 11 + i) * 4), lroundf(280 - h * 0.6f), g->color565(255, 170, 30));
      g->fillTriangle(lroundf(fx - 3), 280, lroundf(fx + 3), 280, fx, lroundf(280 - h * 0.3f), g->color565(255, 240, 140));
    }
  }

  // header: shot name + count
  g->setFont(u8g2_font_helvB12_tf);
  g->setTextColor(g->color565(245, 240, 235));
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(st.name, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(120 - w / 2 - x1, 40);
  g->print(st.name);
  g->setFont(u8g2_font_helvR08_tf);
  g->setTextColor(g->color565(150, 140, 160));
  char foot[32];
  snprintf(foot, sizeof foot, "BOOT = byt  ·  shots: %d", shotsDrunk);
  g->getTextBounds(foot, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(120 - w / 2 - x1, 262);
  g->print(foot);

  // message
  if (msgLeft > 0) {
    bool big = heatLeft > 0;
    g->setFont(big ? u8g2_font_helvB24_tf : u8g2_font_helvB14_tf);
    g->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
    int pw = w + 28, ph = big ? 48 : 34, py = 120;
    float pop = big ? 1 + 0.06f * sinf(t * 30) : 1;
    g->fillRoundRect(120 - pw / 2, py, pw, ph, 14, big ? g->color565(255, 230, 90) : g->color565(250, 246, 240));
    g->setTextColor(big ? g->color565(200, 30, 10) : g->color565(32, 20, 18));
    g->setCursor(120 - w / 2 - x1 + lroundf((pop - 1) * 40 * sinf(t * 40)), py + (big ? 36 : 24));
    g->print(msg);
  }
}

const char *shotsDebug() {
  static char buf[96];
  snprintf(buf, sizeof buf, "type=%d V=%.0f/%.0f phi=%.2f pour=%.0f empty=%d refill=%d shots=%d msg='%s'", type, V, V0, phi,
           pourRate, empty, refilling, shotsDrunk, msgLeft > 0 ? msg : "");
  return buf;
}
