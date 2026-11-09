#include "lager.h"
#include "board.h"
#include "link.h"
#include "theme.h"
#include <ArduinoJson.h>

// The stock changes when the stock Excel is uploaded to Airtable (not every day), so a fetch every hour
// is plenty and the dot only turns red after 3 hours without a fresh fetch.
static const uint32_t OLD_MS = 3UL * 60 * 60 * 1000;

struct LagerData {
  double e, o, pe, po;      // kr now (E, other) and at the baseline
  bool hasPrev, any;
  char prevDay[8];          // "8/11": the date the comparison is against
  uint32_t rxMs;
};

static LagerData D = {};
static bool dirty = false;
static int lastMark = -1;

bool lagerFeed(const char *json) {
  JsonDocument d;
  if (deserializeJson(d, json) || !d["e"].is<double>() || !d["o"].is<double>()) return false;
  LagerData n = {};
  n.e = d["e"]; n.o = d["o"];
  n.hasPrev = d["pe"].is<double>() && d["po"].is<double>();
  if (n.hasPrev) { n.pe = d["pe"]; n.po = d["po"]; }
  strlcpy(n.prevDay, d["pd"] | "", sizeof n.prevDay);
  n.any = true; n.rxMs = millis();
  D = n;
  dirty = true;
  return true;
}

static LinkState mark() { return linkState(D.any, millis() - D.rxMs, OLD_MS); }

bool lagerConsumeDirty() {
  int m = mark();
  bool d = dirty || m != lastMark;
  dirty = false; lastMark = m;
  return d;
}

const char *lagerDebug() {
  static char b[96];
  snprintf(b, sizeof b, "lag any=%d age=%lus e=%.0f o=%.0f prev=%d", D.any, D.any ? (unsigned long)((millis() - D.rxMs) / 1000) : 0UL,
           D.e, D.o, D.hasPrev);
  return b;
}

static void swComma(char *s) { for (; *s; s++) if (*s == '.') *s = ','; }

// kr as whole Kkr with a space between thousands: 12010000 -> "12 010"
static void fmtKkr(double kr, char *out, size_t n) {
  long k = lround(fabs(kr) / 1000.0);
  char t[16]; snprintf(t, sizeof t, "%ld", k);
  int len = strlen(t), o = 0;
  for (int i = 0; i < len && o < (int)n - 2; i++) {
    if (i && (len - i) % 3 == 0) out[o++] = ' ';
    out[o++] = t[i];
  }
  out[o] = 0;
}

// A change in kr as "+110 Kkr" / "-1 250 Kkr"; under 500 kr counts as unchanged.
static void fmtDelta(double d, char *out, size_t n) {
  if (fabs(d) < 500) { snprintf(out, n, "0 Kkr"); return; }
  char k[16]; fmtKkr(d, k, sizeof k);
  snprintf(out, n, "%s%s Kkr", d < 0 ? "-" : "+", k);
}

// A stock value in kr as "4 310 Kkr"
static void fmtValue(double v, char *out, size_t n) {
  char k[16]; fmtKkr(v, k, sizeof k);
  snprintf(out, n, "%s Kkr", k);
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

static uint16_t trend(double d) { return fabs(d) < 500 ? theme::MUTED : d > 0 ? theme::BAD : theme::GOOD; }
static uint16_t trendBg(double d) { return fabs(d) < 500 ? theme::SURFACE : d > 0 ? theme::BAD_BG : theme::GOOD_BG; }

// One group row: label left, value in the middle, change right.
static void drawRow(Arduino_GFX *g, const char *label, double now, double prev, bool hasPrev, int y, int L, int R) {
  using namespace theme;
  char v[16], dl[16];
  fmtValue(now, v, sizeof v);
  g->setFont(u8g2_font_helvB12_tf);
  g->setTextColor(TEXT);
  g->setCursor(L, y); g->print(label);
  g->setFont(u8g2_font_helvR12_tf);
  rightText(g, v, L + 128, y);
  if (hasPrev) {
    double d = now - prev;
    fmtDelta(d, dl, sizeof dl);
    g->setFont(u8g2_font_helvB10_tf);
    g->setTextColor(trend(d));
    rightText(g, dl, R, y);
  } else {
    g->setFont(u8g2_font_helvR10_tf);
    g->setTextColor(MUTED);
    rightText(g, "-", R, y);
  }
}

void drawLager(Arduino_GFX *g) {
  const LagerData &d = D;
  using namespace theme;
  const int W = SCREEN_W, cx = W / 2, L = 30, R = W - 30;

  g->fillScreen(BG);
  drawLinkMark(g, W - 30, 31, mark());
  if (drawNoData(g, d.any)) return;

  g->setFont(u8g2_font_helvB14_tf);
  g->setTextColor(TEXT);
  centerText(g, "LAGERVÄRDE", cx, 42);

  // total in Kkr, big, thousands drawn as separate groups (the digits-only font has no space), unit after
  double tot = d.e + d.o;
  char big[16], grp[4][8]; int ng = 0;
  fmtKkr(tot, big, sizeof big);
  for (char *tok = strtok(big, " "); tok && ng < 4; tok = strtok(nullptr, " ")) strlcpy(grp[ng++], tok, sizeof grp[0]);
  int16_t gx[4], y1, xs; uint16_t gw[4], gh, ws, hs;
  const int sp = 16, gap = 8, baseY = 100;
  g->setFont(u8g2_font_logisoso38_tn);
  int totalW = 0;
  for (int i = 0; i < ng; i++) { g->getTextBounds(grp[i], 0, 0, &gx[i], &y1, &gw[i], &gh); totalW += gw[i] + (i ? sp : 0); }
  g->setFont(u8g2_font_helvR10_tf);
  g->getTextBounds("Kkr", 0, 0, &xs, &y1, &ws, &hs);
  int x = cx - (totalW + gap + ws) / 2;
  g->setFont(u8g2_font_logisoso38_tn);
  g->setTextColor(TEXT);
  for (int i = 0; i < ng; i++) { g->setCursor(x - gx[i], baseY); g->print(grp[i]); x += gw[i] + sp; }
  g->setFont(u8g2_font_helvR10_tf);
  g->setTextColor(MUTED);
  g->setCursor(x - sp + gap - xs, baseY); g->print("Kkr");

  // change against the last day we have a reading for
  const int pw = 190, ph = 28, px = cx - pw / 2, py = 114;
  if (d.hasPrev) {
    double dt = tot - (d.pe + d.po);
    char dl[16], pc[12], line[40];
    fmtDelta(dt, dl, sizeof dl);
    double pct = (d.pe + d.po) > 0 ? dt / (d.pe + d.po) * 100.0 : 0;
    snprintf(pc, sizeof pc, "%+.1f %%", pct); swComma(pc);
    g->fillRoundRect(px, py, pw, ph, 14, trendBg(dt));
    int tx = px + 20, ty = py + ph / 2;
    uint16_t c = trend(dt);
    if (fabs(dt) < 500) g->fillRect(tx - 6, ty - 1, 12, 3, c);
    else if (dt < 0) g->fillTriangle(tx - 6, ty - 5, tx + 6, ty - 5, tx, ty + 6, c);
    else g->fillTriangle(tx - 6, ty + 5, tx + 6, ty + 5, tx, ty - 6, c);
    g->setFont(u8g2_font_helvB12_tf);
    g->setTextColor(c);
    if (fabs(dt) < 500) snprintf(line, sizeof line, "Oförändrat");
    else snprintf(line, sizeof line, "%s   %s", dl, pc);
    g->setCursor(px + 36, py + 20); g->print(line);
  } else {
    g->setFont(u8g2_font_helvR10_tf);
    g->setTextColor(MUTED);
    centerText(g, "Jämförelse från imorgon", cx, py + 19);
  }

  // the two groups
  g->drawFastHLine(L, 158, R - L, SURFACE);
  drawRow(g, "EMV", d.e, d.pe, d.hasPrev, 182, L, R);
  g->drawFastHLine(L, 194, R - L, SURFACE);
  drawRow(g, "A/B", d.o, d.po, d.hasPrev, 218, L, R);
  if (d.hasPrev && d.prevDay[0]) {
    char f[24]; snprintf(f, sizeof f, "mot %s", d.prevDay);
    g->setFont(u8g2_font_helvR08_tf);
    g->setTextColor(MUTED);
    rightText(g, f, W - 30, 232);
  }
}
