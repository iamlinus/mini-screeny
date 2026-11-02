#include "shots.h"
#include "board.h"
#include "sound.h"
#include <math.h>

namespace {

const float W = SCREEN_W, H = SCREEN_H, PI_F = 3.14159265f;

struct V2 { float x, y; };

// glass geometry (screen px). Interior is where liquid can be; the rim is the top edge.
const V2 INNER[4] = {{0, 0}, {240, 0}, {240, 280}, {0, 280}};   // the whole screen is the glass; the top edge is the rim

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
    f.x = 12 + frand() * 216; f.y = 140 + frand() * 130;
    f.vx = f.vy = 0; f.kind = frand() < 0.6f ? 0 : 1;
  }
}

uint16_t mix(Arduino_GFX *g, uint8_t r, uint8_t gg, uint8_t b, float k) {   // k<1 darker, >1 lighter
  auto c = [&](float v) { return (uint8_t)clampf(k <= 1 ? v * k : v + (255 - v) * (k - 1), 0, 255); };
  return g->color565(c(r), c(gg), c(b));
}

}  // namespace

void shotsInit() {
  V0 = area(INNER, 4) * 0.58f;
  V = V0; phi = 0; phiV = 0; empty = refilling = drinking = false;
  for (auto &d : drops) d.on = false;
  seedFlakes();
}

void shotsEnter() { say(TYPES[type].name, 1.6f); }

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
      pourRate = fminf(V0 * 0.9f, depth * V0 / 45 + V0 * 0.1f);   // ~1 s to drain at full tilt
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
      else {
        static char m[40];
        if (shotsDrunk >= 5 && shotsDrunk % 5 == 0) snprintf(m, sizeof m, "Ta det lugnt nu!");
        else if (shotsDrunk % 2) snprintf(m, sizeof m, "Skål! #%d", shotsDrunk);
        else snprintf(m, sizeof m, "%s", st.after);
        say(m, 2.2f); sfx::burp();
      }
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
    V += V0 * dt / 1.5f;
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

// blend framebuffer pixels toward white (k>0) or black (k<0): cheap translucent glass effects
static void shade(int x0, int y0, int w, int h, float k) {
  uint16_t *fb = canvas->getFramebuffer();
  if (!fb) return;
  int x1 = min((int)W, x0 + w), y1 = min((int)H, y0 + h);
  x0 = max(0, x0); y0 = max(0, y0);
  for (int y = y0; y < y1; y++) {
    uint16_t *row = fb + y * (int)W;
    for (int x = x0; x < x1; x++) {
      uint16_t v = row[x];
      int r = v >> 11, gg = (v >> 5) & 63, bb = v & 31;
      if (k > 0) { r += (31 - r) * k; gg += (63 - gg) * k; bb += (31 - bb) * k; }
      else { r *= 1 + k; gg *= 1 + k; bb *= 1 + k; }
      row[x] = (r << 11) | (gg << 5) | bb;
    }
  }
}

void shotsDraw(Arduino_GFX *g) {
  const ShotType &st = TYPES[type];
  g->fillScreen(g->color565(20, 22, 32));        // the empty part of the glass

  // tipsy: the world sways a little after a few shots
  float sway = shotsDrunk >= 4 ? sinf(t * 1.3f) * fminf(0.18f, 0.045f * (shotsDrunk - 3)) : 0;
  float ang = phi + sway;
  float nx = sinf(ang), ny = cosf(ang);

  // heat flash in the empty space
  if (heatLeft > 0) {
    float f = fminf(1, heatLeft / 2.8f) * (0.6f + 0.4f * sinf(t * 25));
    g->fillScreen(g->color565(20 + 110 * f, 22 + 12 * f, 32));
  }

  V2 poly[8];
  float d = 0;
  int n = 0;
  if (V > 1) {
    d = solveLevel(nx, ny, V);
    n = clipHalf(INNER, 4, nx, ny, d, poly);
    if (st.layers == 1) {
      // depth gradient: lighter just under the surface, darker toward the bottom
      const int NB = 10;
      float vols[NB], ks[NB];
      for (int i = 0; i < NB; i++) { vols[i] = 1.0f - i * 0.095f; ks[i] = 1.10f - i * 0.036f; }
      for (int i = 0; i < NB; i++) {
        V2 pp[8];
        int m = clipHalf(INNER, 4, nx, ny, solveLevel(nx, ny, V * vols[i]), pp);
        fillPoly(g, pp, m, mix(g, st.r, st.g, st.b, ks[i]), 0);
      }
    } else {
      float cum = 1;
      for (int L = st.layers - 1; L >= 0; L--) {
        V2 pl[8];
        int nl = clipHalf(INNER, 4, nx, ny, solveLevel(nx, ny, V * cum), pl);
        fillPoly(g, pl, nl, g->color565(st.lr[L], st.lg[L], st.lb[L]), 0);
        cum -= st.frac[L];
      }
    }
    if (st.flakes) {
      for (auto &f : flakes) {
        if (f.x * nx + f.y * ny < d + 4) continue;
        int X = lroundf(f.x), Y = lroundf(f.y);
        if (f.kind == 0) g->fillRect(X - 2, Y - 2, 4, 4, g->color565(165, 16, 10));
        else g->fillRect(X - 3, Y, 7, 3, g->color565(250, 218, 125));
      }
    }
    // surface: bright line + a soft lighter band just below it
    V2 a = {0, 0}, b2 = {0, 0}; int found = 0;
    for (int i = 0; i < n; i++)
      if (fabsf(poly[i].x * nx + poly[i].y * ny - d) < 0.6f) { if (found == 0) a = poly[i]; else b2 = poly[i]; found++; }
    if (found >= 2) {
      uint16_t hl = st.layers > 1 ? mix(g, st.lr[2], st.lg[2], st.lb[2], 1.5f) : mix(g, st.r, st.g, st.b, 1.55f);
      for (int k = 0; k < 3; k++)
        g->drawLine(lroundf(a.x + nx * k), lroundf(a.y + ny * k), lroundf(b2.x + nx * k), lroundf(b2.y + ny * k), hl);
    }
  }

  // refill stream pouring in from the rim
  if (refilling) {
    uint16_t c = st.layers > 1 ? g->color565(st.lr[0], st.lg[0], st.lb[0]) : g->color565(st.r, st.g, st.b);
    int sw = 10 + (int)(3 * sinf(t * 30));
    float surf = n ? (d - 120 * nx) / fmaxf(0.2f, ny) : 280;   // where the stream meets the surface (x = 120)
    g->fillRect(120 - sw / 2, 0, sw, (int)clampf(surf, 0, 280) + 4, c);
    for (int k = 0; k < 6; k++) {
      float bx = 120 + sinf(t * 17 + k * 2.1f) * 26, by = clampf(surf, 0, 280) + 6 + fmodf(t * 90 + k * 23, 60);
      g->drawCircle(lroundf(bx), lroundf(by), 2 + k % 3, mix(g, st.r, st.g, st.b, 1.5f));
    }
  }

  // drops leaving over the rim
  for (auto &dp : drops) if (dp.on) {
    uint16_t c = st.layers > 1 ? g->color565(st.lr[2], st.lg[2], st.lb[2]) : g->color565(st.r, st.g, st.b);
    g->fillCircle(lroundf(dp.x), lroundf(dp.y), 3, c);
  }

  // tequila: salt crusted on the rim and a lime wedge hooked over it
  if (st.saltLime) {
    for (int i = 0; i < 60; i++) g->fillRect(2 + i * 4, (i * 7) % 4, 2 + (i % 2), 2, g->color565(250, 250, 248));
    int lx = 206, ly = -2;
    g->fillCircle(lx, ly, 30, g->color565(70, 160, 40));
    g->fillCircle(lx, ly, 26, g->color565(195, 232, 115));
    for (int k = 0; k < 7; k++) {
      float aa = PI_F * 0.05f + k * PI_F / 7;
      g->drawLine(lx, ly, lx + lroundf(cosf(aa) * 24), ly + lroundf(sinf(aa) * 24), g->color565(240, 250, 200));
    }
  }

  // flames when the chili kicks in
  if (heatLeft > 0) {
    for (int i = 0; i < 9; i++) {
      float fx = 14 + i * 26.5f;
      float h = 50 + 30 * sinf(t * 14 + i * 1.7f) + 12 * sinf(t * 23 + i);
      g->fillTriangle(lroundf(fx - 16), 280, lroundf(fx + 16), 280, lroundf(fx + sinf(t * 9 + i) * 5), lroundf(280 - h), g->color565(240, 60, 20));
      g->fillTriangle(lroundf(fx - 9), 280, lroundf(fx + 9), 280, lroundf(fx + sinf(t * 11 + i) * 4), lroundf(280 - h * 0.6f), g->color565(255, 170, 30));
      g->fillTriangle(lroundf(fx - 4), 280, lroundf(fx + 4), 280, fx, lroundf(280 - h * 0.3f), g->color565(255, 240, 140));
    }
  }

  // glass: thick darker walls and two vertical reflections
  shade(0, 0, 6, 280, -0.35f);
  shade(234, 0, 6, 280, -0.35f);
  shade(6, 0, 3, 280, -0.15f);
  shade(231, 0, 3, 280, -0.15f);
  shade(18, 0, 7, 280, 0.22f);
  shade(28, 0, 2, 280, 0.14f);
  shade(206, 0, 4, 280, 0.12f);
  shade(0, 0, 240, 3, 0.35f);     // the rim

  // messages (shot name on switch, cheers, HETT)
  if (msgLeft > 0) {
    bool big = heatLeft > 0;
    int16_t x1, y1; uint16_t w, h;
    g->setFont(big ? u8g2_font_helvB24_tf : u8g2_font_helvB14_tf);
    g->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
    int pw = w + 28, ph = big ? 48 : 34, py = 112;
    g->fillRoundRect(120 - pw / 2, py, pw, ph, 14, big ? g->color565(255, 230, 90) : g->color565(250, 246, 240));
    g->setTextColor(big ? g->color565(200, 30, 10) : g->color565(32, 20, 18));
    g->setCursor(120 - w / 2 - x1 + (big ? lroundf(2 * sinf(t * 40)) : 0), py + (big ? 36 : 24));
    g->print(msg);
  }
}

const char *shotsDebug() {
  static char buf[96];
  snprintf(buf, sizeof buf, "type=%d V=%.0f/%.0f phi=%.2f pour=%.0f empty=%d refill=%d shots=%d msg='%s'", type, V, V0, phi,
           pourRate, empty, refilling, shotsDrunk, msgLeft > 0 ? msg : "");
  return buf;
}
