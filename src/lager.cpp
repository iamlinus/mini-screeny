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

// A change in kr as "+123 tkr" / "-1,25 Mkr"; under 500 kr counts as unchanged.
static void fmtDelta(double d, char *out, size_t n) {
  double a = fabs(d);
  const char *sg = d < 0 ? "-" : "+";
  if (a < 500) snprintf(out, n, "0 kr");
  else if (a >= 1e6) snprintf(out, n, "%s%.2f Mkr", sg, a / 1e6);
  else snprintf(out, n, "%s%.0f tkr", sg, a / 1e3);
  swComma(out);
}

// A stock value in kr as "12,4 Mkr" / "830 tkr"
static void fmtValue(double v, char *out, size_t n) {
  if (v >= 1e7) snprintf(out, n, "%.1f Mkr", v / 1e6);
  else if (v >= 1e6) snprintf(out, n, "%.2f Mkr", v / 1e6);
  else snprintf(out, n, "%.0f tkr", v / 1e3);
  swComma(out);
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

  // total, big, with the unit after it
  double tot = d.e + d.o;
  char big[16], unit[6];
  if (tot >= 1e7) { snprintf(big, sizeof big, "%.1f", tot / 1e6); strcpy(unit, "Mkr"); }
  else if (tot >= 1e6) { snprintf(big, sizeof big, "%.2f", tot / 1e6); strcpy(unit, "Mkr"); }
  else { snprintf(big, sizeof big, "%.0f", tot / 1e3); strcpy(unit, "tkr"); }
  char *dot = strchr(big, '.');
  char dec[4] = "";
  if (dot) { *dot = 0; strlcpy(dec, dot + 1, sizeof dec); }
  int16_t xi, xd, xs, y1; uint16_t wi, hi, wd = 0, hd, ws, hs;
  g->setFont(u8g2_font_logisoso38_tn);
  g->getTextBounds(big, 0, 0, &xi, &y1, &wi, &hi);
  if (dec[0]) g->getTextBounds(dec, 0, 0, &xd, &y1, &wd, &hd);
  g->setFont(u8g2_font_helvR10_tf);
  g->getTextBounds(unit, 0, 0, &xs, &y1, &ws, &hs);
  const int dotW = dec[0] ? 12 : 0, gap = 8, baseY = 100;
  int x0 = cx - (int)(wi + dotW + wd + gap + ws) / 2;
  g->setFont(u8g2_font_logisoso38_tn);
  g->setTextColor(TEXT);
  g->setCursor(x0 - xi, baseY); g->print(big);
  if (dec[0]) {
    g->fillCircle(x0 + wi + dotW / 2, baseY - 3, 3, TEXT);   // the comma, drawn (the digits-only font has none)
    g->setCursor(x0 + wi + dotW - xd, baseY); g->print(dec);
  }
  g->setFont(u8g2_font_helvR10_tf);
  g->setTextColor(MUTED);
  g->setCursor(x0 + wi + dotW + wd + gap - xs, baseY); g->print(unit);

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
  drawRow(g, "E", d.e, d.pe, d.hasPrev, 182, L, R);
  g->drawFastHLine(L, 194, R - L, SURFACE);
  drawRow(g, "Övriga", d.o, d.po, d.hasPrev, 218, L, R);
  if (d.hasPrev && d.prevDay[0]) {
    char f[24]; snprintf(f, sizeof f, "mot %s", d.prevDay);
    g->setFont(u8g2_font_helvR08_tf);
    g->setTextColor(MUTED);
    rightText(g, f, W - 30, 232);
  }
}
