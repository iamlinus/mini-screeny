#include "pulse.h"
#include "board.h"
#include "link.h"
#include "theme.h"
#include <ArduinoJson.h>

// One quota window. Missing or null in the payload = unknown, drawn as dashes, never as 0.
struct Quota {
  bool has = false, hasReset = false;
  float pct = 0;
  long resetMin = 0;
};

struct PulseData {
  Quota cSess, cWeek, cModel;
  char modelLabel[24] = "";
  uint32_t rxMs = 0;
  uint32_t ageAtRxMs = 0;   // how old the companion said the numbers were when it sent them
  bool any = false;         // at least one payload received since boot
};

static PulseData P;
static bool dirty = true;

static void readQuota(JsonDocument &d, const char *pctKey, const char *resetKey, Quota &q) {
  JsonVariant p = d[pctKey], r = d[resetKey];
  q.has = p.is<float>();
  q.pct = q.has ? constrain(p.as<float>(), 0.0f, 100.0f) : 0;
  q.hasReset = r.is<long>() || r.is<float>();
  q.resetMin = q.hasReset ? (long)r.as<float>() : 0;
}

bool pulseFeed(const char *json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  if (!d.is<JsonObject>()) return false;
  PulseData n;
  readQuota(d, "claudeSessionPct", "claudeSessionResetMin", n.cSess);
  readQuota(d, "claudeWeekPct", "claudeWeekResetMin", n.cWeek);
  readQuota(d, "claudeModelWeekPct", "claudeModelWeekResetMin", n.cModel);
  const char *lbl = d["claudeModelWeekLabel"] | "";
  strlcpy(n.modelLabel, lbl, sizeof n.modelLabel);
  n.ageAtRxMs = (uint32_t)(d["claudeAgeSec"] | 0.0f) * 1000UL;
  n.rxMs = millis();
  n.any = true;
  P = n;
  dirty = true;
  return true;
}

bool pulseWantsDraw() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (dirty || now - last >= 1000) { dirty = false; last = now; return true; }
  return false;
}

const char *pulseDebug() {
  static char b[160];
  snprintf(b, sizeof b, "vp any=%d age=%lus sess=%s%.0f week=%s%.0f model=%s%.0f '%s'", P.any,
           P.any ? (unsigned long)((millis() - P.rxMs) / 1000) : 0UL, P.cSess.has ? "" : "-", P.cSess.pct,
           P.cWeek.has ? "" : "-", P.cWeek.pct, P.cModel.has ? "" : "-", P.cModel.pct, P.modelLabel);
  return b;
}

// ---------------- drawing ----------------

static LinkState mark() { return linkState(P.any, P.ageAtRxMs + (millis() - P.rxMs)); }

static uint16_t levelColor(Arduino_GFX *g, float pct) {
  if (pct >= 90) return theme::BAD;
  if (pct >= 70) return theme::WARN;
  return theme::ACCENT;
}

static void centerText(Arduino_GFX *g, const char *s, int cx, int y) {
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(cx - w / 2 - x1, y);
  g->print(s);
}

static void rightText(Arduino_GFX *g, const char *s, int rx, int y) {
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(rx - w - x1, y);
  g->print(s);
}

// Ring segment clockwise from 12 o'clock. fillArc uses 0 deg = 3 o'clock; split so no arc wraps past 360.
static void ring(Arduino_GFX *g, int cx, int cy, int r1, int r2, float sweep, uint16_t col) {
  if (sweep <= 0) return;
  if (sweep > 360) sweep = 360;
  float a = 270, b = 270 + sweep;
  if (b <= 360) g->fillArc(cx, cy, r1, r2, a, fminf(b, 359.99f), col);   // 360 would wrap to 0
  else { g->fillArc(cx, cy, r1, r2, a, 359.99f, col); g->fillArc(cx, cy, r1, r2, 0, b - 360, col); }
}

// Minutes left now, counted down from the value received.
static long minutesLeft(const Quota &q) {
  long left = q.resetMin - (long)((millis() - P.rxMs) / 60000);
  return left < 0 ? 0 : left;
}

static void fmtDuration(long m, char *out, size_t n) {
  if (m >= 1440) snprintf(out, n, "%ld d %ld h", m / 1440, (m % 1440) / 60);
  else if (m >= 60) snprintf(out, n, "%ld h %ld min", m / 60, m % 60);
  else snprintf(out, n, "%ld min", m);
}

// One weekly window: label, bar, time to reset under it. The percentage sits after the label, or
// (pctBelow, for the narrow side-by-side columns) at the end of the "om ..." line.
static void barRow(Arduino_GFX *g, int x0, int x1, int y, const char *label, const Quota &q, bool pctBelow,
                   uint16_t track, uint16_t text, uint16_t muted) {
  char right[8];
  if (q.has) snprintf(right, sizeof right, "%d%%", (int)q.pct);  // floor: 99.6 is not "100"
  else strcpy(right, "--");
  const int by = y + 6;

  g->setFont(u8g2_font_helvR08_tf); g->setTextColor(muted);
  g->setCursor(x0, y); g->print(label);
  g->fillRoundRect(x0, by, x1 - x0, 6, 3, track);
  if (q.has && q.pct > 0) g->fillRoundRect(x0, by, max(6, (int)((x1 - x0) * q.pct / 100)), 6, 3, levelColor(g, q.pct));
  if (q.has && q.hasReset) {
    char dur[16], ln[24];
    fmtDuration(minutesLeft(q), dur, sizeof dur);
    snprintf(ln, sizeof ln, "om %s", dur);
    g->setCursor(x0, by + 19); g->print(ln);
  }
  g->setFont(u8g2_font_helvB10_tf); g->setTextColor(q.has ? text : muted);
  rightText(g, right, x1, pctBelow ? by + 20 : y + 1);
}

// Landscape layout for the mounted screen (SCREEN_W x SCREEN_H = 280 x 240): the session ring
// centred on top with its reset time inside, the week bar(s) underneath. The per-model week only
// exists while Anthropic runs a separate weekly cap for a model; then the two bars sit side by side.
void pulseDraw(Arduino_GFX *g) {
  using namespace theme;
  const uint16_t TRACK = SURFACE;

  g->fillScreen(BG);
  drawLinkMark(g, SCREEN_W - 24, 22, mark());
  if (drawNoData(g, P.any)) return;

  // Session ring (Claude 5 h window)
  const int cx = SCREEN_W / 2, cy = 90, ro = 76, ri = 62;
  g->fillArc(cx, cy, ro, ri, 0, 360, TRACK);
  const Quota &s = P.cSess;
  if (s.has) ring(g, cx, cy, ro, ri, s.pct * 3.6f, levelColor(g, s.pct));
  char big[8];
  if (s.has) snprintf(big, sizeof big, "%d", (int)s.pct); else strcpy(big, "--");
  g->setFont(u8g2_font_logisoso42_tn); g->setTextColor(s.has ? TEXT : MUTED);
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(big, 0, 0, &x1, &y1, &w, &h);
  int bx = cx - (int)w / 2 - x1 - (s.has ? 8 : 0);
  g->setCursor(bx, cy + 12); g->print(big);
  if (s.has) { g->setFont(u8g2_font_helvB14_tf); g->setCursor(bx + x1 + w + 3, cy + 12); g->print("%"); }
  g->setFont(u8g2_font_helvR08_tf); g->setTextColor(MUTED);
  centerText(g, "SESSION", cx, cy + 30);
  if (s.has && s.hasReset) {
    char dur[16]; fmtDuration(minutesLeft(s), dur, sizeof dur);
    centerText(g, dur, cx, cy + 44);
  }

  // Weekly window(s) under the ring
  const int y = 190;
  if (P.cModel.has) {
    barRow(g, 30, 132, y, "VECKA", P.cWeek, true, TRACK, TEXT, MUTED);
    barRow(g, 148, SCREEN_W - 30, y, P.modelLabel[0] ? P.modelLabel : "MODELL · VECKA", P.cModel, true, TRACK, TEXT, MUTED);
  } else {
    barRow(g, 44, SCREEN_W - 44, y, "VECKA", P.cWeek, false, TRACK, TEXT, MUTED);
  }
}
