#include "stock.h"
#include "board.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "secrets.h"

// Avanza public quote endpoint (no key). Kjell Group orderbook 1276742.
static const char *QUOTE_URL = "https://www.avanza.se/_api/market-guide/stock/1276742/quote";

struct StockData {
  char priceI[8], priceD[4], chgAbs[12], chgPct[12], volume[16], bidAsk[24], lows[20], highs[20];
  float price, low, high;
  bool down, live;
};

// Start values = closing quote fetched 2026-11-01
static StockData D = {"19", "00", "-0,50", "-2,56%", "75 763", "19,00 / 19,20", "Lägst 19,00", "Högst 19,30",
                      19.00f, 19.00f, 19.30f, true, false};
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool dirty = false;

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

static bool fetchQuote() {
  WiFiClientSecure cl; cl.setInsecure();
  HTTPClient http;
  http.setTimeout(6000);
  if (!http.begin(cl, QUOTE_URL)) return false;
  http.addHeader("User-Agent", "Mozilla/5.0");
  int code = http.GET();
  if (code != 200) { Serial.printf("quote HTTP %d\n", code); http.end(); return false; }
  JsonDocument d;
  DeserializationError e = deserializeJson(d, http.getString());
  http.end();
  if (e || !d["last"].is<float>()) { Serial.println("quote json fail"); return false; }

  StockData n = D;
  n.price = d["last"]; n.high = d["highest"]; n.low = d["lowest"];
  float chg = d["change"], pct = d["changePercent"];
  n.down = chg < 0;
  char buf[16]; fmt2(n.price, buf, sizeof buf);
  char *comma = strchr(buf, ',');
  if (comma) { *comma = 0; strlcpy(n.priceI, buf, sizeof n.priceI); strlcpy(n.priceD, comma + 1, sizeof n.priceD); }
  fmt2(chg, n.chgAbs, sizeof n.chgAbs, true);
  char pb[12]; fmt2(pct, pb, sizeof pb, true); snprintf(n.chgPct, sizeof n.chgPct, "%s%%", pb);
  fmtThousands((long)(float)d["totalVolumeTraded"], n.volume, sizeof n.volume);
  char lo[12], hi[12], b[12], s[12];
  fmt2(n.low, lo, sizeof lo); fmt2(n.high, hi, sizeof hi);
  snprintf(n.lows, sizeof n.lows, "Lägst %s", lo); snprintf(n.highs, sizeof n.highs, "Högst %s", hi);
  fmt2(d["buy"], b, sizeof b); fmt2(d["sell"], s, sizeof s);
  snprintf(n.bidAsk, sizeof n.bidAsk, "%s / %s", b, s);
  n.live = true;
  portENTER_CRITICAL(&mux); D = n; portEXIT_CRITICAL(&mux);
  dirty = true;
  Serial.printf("quote %.2f (%s)\n", n.price, n.chgPct);
  return true;
}

static void netTask(void *) {
  uint32_t nextFetch = 0;
  for (;;) {
    bool conn = WiFi.status() == WL_CONNECTED;
    if (!conn && D.live) { portENTER_CRITICAL(&mux); D.live = false; portEXIT_CRITICAL(&mux); dirty = true; }
    if (conn && millis() >= nextFetch) nextFetch = millis() + (fetchQuote() ? 60000 : 10000);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void stockBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}

bool stockConsumeDirty() { bool d = dirty; dirty = false; return d; }

#define C(r, g, b) gfx->color565(r, g, b)
static void centerText(const char *s, int cx, int y) {
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(cx - w / 2 - x1, y);
  gfx->print(s);
}

void drawStock() {
  StockData d;
  portENTER_CRITICAL(&mux); d = D; portEXIT_CRITICAL(&mux);

  const uint16_t BG = C(8, 10, 16), CARD = C(22, 26, 38), TEXT = C(245, 247, 250), MUTED = C(130, 140, 160);
  const uint16_t TREND = d.down ? C(255, 85, 95) : C(60, 220, 140);
  const uint16_t TREND_BG = d.down ? C(60, 18, 26) : C(14, 56, 40);

  gfx->fillScreen(BG);
  gfx->setFont(u8g2_font_helvB14_tf);
  gfx->setTextColor(TEXT);
  centerText("KJELL GROUP", 120, 44);
  gfx->setFont(u8g2_font_helvR08_tf);
  gfx->setTextColor(MUTED);
  centerText("KJELL  ·  Nasdaq First North", 120, 62);
  gfx->fillCircle(206, 40, 4, d.live ? C(60, 220, 140) : C(90, 96, 110));

  gfx->setFont(u8g2_font_logisoso50_tn);
  gfx->setTextColor(TEXT);
  int16_t x1, y1; uint16_t wi, hi, wd, hd;
  gfx->getTextBounds(d.priceI, 0, 0, &x1, &y1, &wi, &hi);
  gfx->getTextBounds(d.priceD, 0, 0, &x1, &y1, &wd, &hd);
  const int dot = 14, baseY = 128;
  int x0 = 120 - (int)(wi + dot + wd) / 2;
  gfx->setCursor(x0, baseY); gfx->print(d.priceI);
  gfx->fillCircle(x0 + wi + dot / 2, baseY - 4, 3, TEXT);
  gfx->setCursor(x0 + wi + dot, baseY); gfx->print(d.priceD);

  gfx->setFont(u8g2_font_helvR10_tf);
  gfx->setTextColor(MUTED);
  centerText("SEK", 120, 150);

  const int pw = 150, ph = 30, px = 120 - pw / 2, py = 160;
  gfx->fillRoundRect(px, py, pw, ph, 15, TREND_BG);
  int tx = px + 20, ty = py + ph / 2;
  if (d.down) gfx->fillTriangle(tx - 6, ty - 5, tx + 6, ty - 5, tx, ty + 6, TREND);
  else        gfx->fillTriangle(tx - 6, ty + 5, tx + 6, ty + 5, tx, ty - 6, TREND);
  gfx->setFont(u8g2_font_helvB12_tf);
  gfx->setTextColor(TREND);
  gfx->setCursor(px + 34, py + 21); gfx->print(d.chgPct);
  gfx->setFont(u8g2_font_helvR10_tf);
  gfx->setCursor(px + 96, py + 20); gfx->print(d.chgAbs);

  const int bx = 34, bw = 172, by = 214;
  gfx->setFont(u8g2_font_helvR08_tf);
  gfx->setTextColor(MUTED);
  gfx->setCursor(bx, by - 8); gfx->print(d.lows);
  int16_t a; uint16_t w2, h2;
  gfx->getTextBounds(d.highs, 0, 0, &a, &a, &w2, &h2);
  gfx->setCursor(bx + bw - w2, by - 8); gfx->print(d.highs);
  gfx->fillRoundRect(bx, by, bw, 6, 3, CARD);
  float t = (d.high > d.low) ? (d.price - d.low) / (d.high - d.low) : 0.5f;
  int mx = bx + (int)(t * bw);
  gfx->fillRoundRect(bx, by, max(6, mx - bx), 6, 3, TREND);
  gfx->fillCircle(mx, by + 3, 7, TEXT);
  gfx->fillCircle(mx, by + 3, 3, TREND);

  gfx->setFont(u8g2_font_helvR08_tf);
  gfx->setTextColor(MUTED);
  gfx->setCursor(34, 252); gfx->print("VOLYM");
  gfx->getTextBounds("KÖP / SÄLJ", 0, 0, &a, &a, &w2, &h2);
  gfx->setCursor(206 - w2, 252); gfx->print("KÖP / SÄLJ");
  gfx->setFont(u8g2_font_helvB10_tf);
  gfx->setTextColor(TEXT);
  gfx->setCursor(34, 268); gfx->print(d.volume);
  gfx->getTextBounds(d.bidAsk, 0, 0, &a, &a, &w2, &h2);
  gfx->setCursor(206 - w2, 268); gfx->print(d.bidAsk);
}
