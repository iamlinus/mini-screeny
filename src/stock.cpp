#include "stock.h"
#include "board.h"
#include "link.h"
#include <ArduinoJson.h>

// Kjell Group quote. The board has no network of its own: tools/companion.py on the Mac fetches
// Avanza's public quote (orderbook 1276742) and sends the raw fields over USB as "stk {json}".

struct StockData {
  char priceI[8], priceD[4], chgAbs[12], chgPct[12], volume[16], bidAsk[24], lows[20], highs[20];
  float price, low, high;
  bool down, any;
  uint32_t rxMs;
};

// Start values = closing quote fetched 2026-11-01 (shown grey until the companion sends data)
static StockData D = {"19", "00", "-0,50", "-2,56%", "75 763", "19,00 / 19,20", "Lägst 19,00", "Högst 19,30",
                      19.00f, 19.00f, 19.30f, true, false, 0};
static bool dirty = false;
static int lastMark = -1;

static void fmt2(float v, char *out, size_t n, bool sign = false) {
  snprintf(out, n, sign ? "%+.2f" : "%.2f", v);
  for (char *c = out; *c; c++) if (*c == '.') *c = ',';
}
static void fmtThousands(long v, char *out, size_t n) {
  char t[16]; snprintf(t, sizeof t, "%ld", v);
  int len = strlen(t), o = 0;
  for (int i = 0; i < len && o < (int)n - 2; i++) {
    if (i && (len - i) % 3 == 0) out[o++] = ' ';
    out[o++] = t[i];
  }
  out[o] = 0;
}

bool stockFeed(const char *json) {
  JsonDocument d;
  if (deserializeJson(d, json) || !d["last"].is<float>()) return false;
  StockData n = D;
  n.price = d["last"];
  n.high = d["highest"] | n.price;
  n.low = d["lowest"] | n.price;
  float chg = d["change"] | 0.0f, pct = d["changePercent"] | 0.0f;
  n.down = chg < 0;
  char buf[16]; fmt2(n.price, buf, sizeof buf);
  char *comma = strchr(buf, ',');
  if (comma) { *comma = 0; strlcpy(n.priceI, buf, sizeof n.priceI); strlcpy(n.priceD, comma + 1, sizeof n.priceD); }
  fmt2(chg, n.chgAbs, sizeof n.chgAbs, true);
  char pb[12]; fmt2(pct, pb, sizeof pb, true); snprintf(n.chgPct, sizeof n.chgPct, "%s%%", pb);
  fmtThousands((long)(d["totalVolumeTraded"] | 0.0f), n.volume, sizeof n.volume);
  char lo[12], hi[12], b[12], s[12];
  fmt2(n.low, lo, sizeof lo); fmt2(n.high, hi, sizeof hi);
  snprintf(n.lows, sizeof n.lows, "Lägst %s", lo); snprintf(n.highs, sizeof n.highs, "Högst %s", hi);
  fmt2(d["buy"] | 0.0f, b, sizeof b); fmt2(d["sell"] | 0.0f, s, sizeof s);
  snprintf(n.bidAsk, sizeof n.bidAsk, "%s / %s", b, s);
  n.any = true; n.rxMs = millis();
  D = n;
  dirty = true;
  return true;
}

static LinkState mark() { return linkState(D.any, millis() - D.rxMs); }

bool stockConsumeDirty() {
  int m = mark();
  bool d = dirty || m != lastMark;   // also redraw when the status mark changes
  dirty = false; lastMark = m;
  return d;
}

const char *stockDebug() {
  static char b[96];
  snprintf(b, sizeof b, "stk any=%d age=%lus price=%s,%s %s", D.any, D.any ? (unsigned long)((millis() - D.rxMs) / 1000) : 0UL,
           D.priceI, D.priceD, D.chgPct);
  return b;
}

#define C(r_, g_, b_) g->color565(r_, g_, b_)
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

// Landscape layout for the mounted screen (SCREEN_W x SCREEN_H = 280 x 240)
void drawStock(Arduino_GFX *g) {
  const StockData &d = D;

  const uint16_t BG = C(8, 10, 16), CARD = C(22, 26, 38), TEXT = C(245, 247, 250), MUTED = C(130, 140, 160);
  const uint16_t TREND = d.down ? C(255, 85, 95) : C(60, 220, 140);
  const uint16_t TREND_BG = d.down ? C(60, 18, 26) : C(14, 56, 40);
  const int W = SCREEN_W, cx = W / 2, L = 34, R = W - 34;

  g->fillScreen(BG);
  g->setFont(u8g2_font_helvB14_tf);
  g->setTextColor(TEXT);
  centerText(g, "KJELL GROUP", cx, 36);
  g->setFont(u8g2_font_helvR08_tf);
  g->setTextColor(MUTED);
  centerText(g, "KJELL  ·  Nasdaq First North", cx, 53);
  drawLinkMark(g, W - 30, 31, mark());

  // price with "SEK" after it, centred as one group
  g->setFont(u8g2_font_logisoso50_tn);
  // place by ink boxes (x1 = left bearing), otherwise the digits overlap and hide the decimal point
  int16_t xi, xd, xs, y1; uint16_t wi, hi, wd, hd, ws, hs;
  g->getTextBounds(d.priceI, 0, 0, &xi, &y1, &wi, &hi);
  g->getTextBounds(d.priceD, 0, 0, &xd, &y1, &wd, &hd);
  g->setFont(u8g2_font_helvR10_tf);
  g->getTextBounds("SEK", 0, 0, &xs, &y1, &ws, &hs);
  const int dot = 14, gap = 8, baseY = 118;
  int x0 = cx - (int)(wi + dot + wd + gap + ws) / 2;
  g->setFont(u8g2_font_logisoso50_tn);
  g->setTextColor(TEXT);
  g->setCursor(x0 - xi, baseY); g->print(d.priceI);
  g->fillCircle(x0 + wi + dot / 2, baseY - 4, 3, TEXT);
  g->setCursor(x0 + wi + dot - xd, baseY); g->print(d.priceD);
  g->setFont(u8g2_font_helvR10_tf);
  g->setTextColor(MUTED);
  g->setCursor(x0 + wi + dot + wd + gap - xs, baseY); g->print("SEK");

  const int pw = 150, ph = 28, px = cx - pw / 2, py = 130;
  g->fillRoundRect(px, py, pw, ph, 14, TREND_BG);
  int tx = px + 20, ty = py + ph / 2;
  if (d.down) g->fillTriangle(tx - 6, ty - 5, tx + 6, ty - 5, tx, ty + 6, TREND);
  else        g->fillTriangle(tx - 6, ty + 5, tx + 6, ty + 5, tx, ty - 6, TREND);
  g->setFont(u8g2_font_helvB12_tf);
  g->setTextColor(TREND);
  g->setCursor(px + 34, py + 20); g->print(d.chgPct);
  g->setFont(u8g2_font_helvR10_tf);
  g->setCursor(px + 96, py + 19); g->print(d.chgAbs);

  // day range
  const int by = 182, bw = R - L;
  g->setFont(u8g2_font_helvR08_tf);
  g->setTextColor(MUTED);
  g->setCursor(L, by - 8); g->print(d.lows);
  rightText(g, d.highs, R, by - 8);
  g->fillRoundRect(L, by, bw, 6, 3, CARD);
  float t = (d.high > d.low) ? (d.price - d.low) / (d.high - d.low) : 0.5f;
  int mx = L + (int)(t * bw);
  g->fillRoundRect(L, by, max(6, mx - L), 6, 3, TREND);
  g->fillCircle(mx, by + 3, 7, TEXT);
  g->fillCircle(mx, by + 3, 3, TREND);

  g->setFont(u8g2_font_helvR08_tf);
  g->setTextColor(MUTED);
  g->setCursor(L, 212); g->print("VOLYM");
  rightText(g, "KÖP / SÄLJ", R, 212);
  g->setFont(u8g2_font_helvB10_tf);
  g->setTextColor(TEXT);
  g->setCursor(L, 228); g->print(d.volume);
  rightText(g, d.bidAsk, R, 228);
}
