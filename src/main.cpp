#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Waveshare ESP32-S3-LCD-1.69 (ST7789V2, 240x280, 4-wire SPI)
#define LCD_DC  4
#define LCD_CS  5
#define LCD_CLK 6
#define LCD_DIN 7
#define LCD_RST 8
#define LCD_BL  15

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_CLK, LCD_DIN, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, 240, 280, 0, 20, 0, 20);

// ---- Data (uppdateras live via Wi-Fi, startvärden = stängning 2026-11-01) ----
static const char *NAME   = "KJELL GROUP";
static const char *TICKER = "KJELL  ·  Nasdaq First North";
char  PRICE_I[8] = "19", PRICE_D[4] = "00";
char  CHG_ABS[12] = "-0,50", CHG_PCT[12] = "-2,56%";
char  VOLUME[16] = "75 763", MCAP[24] = "19,00 / 19,20";
char  LOWS[20] = "Lägst 19,00", HIGHS[20] = "Högst 19,30";
float PRICE = 19.00f, DAY_LOW = 19.00f, DAY_HIGH = 19.30f;
bool  DOWN = true;
bool  LIVE = false;

// ---- Palette ----
#define C(r,g,b) gfx->color565(r,g,b)

void centerText(const char *s, int cx, int y) {
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(cx - w / 2 - x1, y);
  gfx->print(s);
}

void drawStock() {
  const uint16_t BG    = C(8, 10, 16);
  const uint16_t CARD  = C(22, 26, 38);
  const uint16_t TEXT  = C(245, 247, 250);
  const uint16_t MUTED = C(130, 140, 160);
  const uint16_t TREND = DOWN ? C(255, 85, 95) : C(60, 220, 140);
  const uint16_t TREND_BG = DOWN ? C(60, 18, 26) : C(14, 56, 40);

  gfx->fillScreen(BG);

  // Header
  gfx->setFont(u8g2_font_helvB14_tf);
  gfx->setTextColor(TEXT);
  centerText(NAME, 120, 44);
  gfx->setFont(u8g2_font_helvR08_tf);
  gfx->setTextColor(MUTED);
  centerText(TICKER, 120, 62);

  gfx->fillCircle(206, 40, 4, LIVE ? C(60, 220, 140) : C(90, 96, 110));

  // Big price
  gfx->setFont(u8g2_font_logisoso50_tn);
  gfx->setTextColor(TEXT);
  int16_t x1, y1; uint16_t wi, hi, wd, hd;
  gfx->getTextBounds(PRICE_I, 0, 0, &x1, &y1, &wi, &hi);
  gfx->getTextBounds(PRICE_D, 0, 0, &x1, &y1, &wd, &hd);
  const int dot = 14;                      // plats för kommatecknet
  int total = wi + dot + wd;
  int x0 = 120 - total / 2;
  const int baseY = 128;
  gfx->setCursor(x0, baseY);               gfx->print(PRICE_I);
  gfx->fillCircle(x0 + wi + dot / 2, baseY - 4, 3, TEXT);  // komma/punkt
  gfx->setCursor(x0 + wi + dot, baseY);    gfx->print(PRICE_D);

  gfx->setFont(u8g2_font_helvR10_tf);
  gfx->setTextColor(MUTED);
  centerText("SEK", 120, 150);

  // Change pill
  const int pw = 150, ph = 30, px = 120 - pw / 2, py = 160;
  gfx->fillRoundRect(px, py, pw, ph, 15, TREND_BG);
  // triangle
  int tx = px + 20, ty = py + ph / 2;
  if (DOWN) gfx->fillTriangle(tx - 6, ty - 5, tx + 6, ty - 5, tx, ty + 6, TREND);
  else      gfx->fillTriangle(tx - 6, ty + 5, tx + 6, ty + 5, tx, ty - 6, TREND);
  gfx->setFont(u8g2_font_helvB12_tf);
  gfx->setTextColor(TREND);
  gfx->setCursor(px + 34, py + 21);
  gfx->print(CHG_PCT);
  gfx->setFont(u8g2_font_helvR10_tf);
  gfx->setCursor(px + 96, py + 20);
  gfx->print(CHG_ABS);

  // Day range bar
  const int bx = 34, bw = 172, by = 214;
  gfx->setFont(u8g2_font_helvR08_tf);
  gfx->setTextColor(MUTED);
  gfx->setCursor(bx, by - 8);        gfx->print(LOWS);
  int16_t a; uint16_t w2, h2;
  gfx->getTextBounds(HIGHS, 0, 0, &a, &a, &w2, &h2);
  gfx->setCursor(bx + bw - w2, by - 8); gfx->print(HIGHS);
  gfx->fillRoundRect(bx, by, bw, 6, 3, CARD);
  float t = (DAY_HIGH > DAY_LOW) ? (PRICE - DAY_LOW) / (DAY_HIGH - DAY_LOW) : 0.5f;
  int mx = bx + (int)(t * bw);
  gfx->fillRoundRect(bx, by, max(6, mx - bx), 6, 3, TREND);
  gfx->fillCircle(mx, by + 3, 7, TEXT);
  gfx->fillCircle(mx, by + 3, 3, TREND);

  // Footer stats
  gfx->setFont(u8g2_font_helvR08_tf);
  gfx->setTextColor(MUTED);
  gfx->setCursor(34, 252);   gfx->print("VOLYM");
  gfx->getTextBounds("KÖP / SÄLJ", 0, 0, &a, &a, &w2, &h2);
  gfx->setCursor(206 - w2, 252); gfx->print("KÖP / SÄLJ");
  gfx->setFont(u8g2_font_helvB10_tf);
  gfx->setTextColor(TEXT);
  gfx->setCursor(34, 268);   gfx->print(VOLUME);
  gfx->getTextBounds(MCAP, 0, 0, &a, &a, &w2, &h2);
  gfx->setCursor(206 - w2, 268); gfx->print(MCAP);
}

#if __has_include("photo.h")
#include "photo.h"
#endif

#include "claude_anim.h"
Arduino_Canvas *canvas = new Arduino_Canvas(240, 280, gfx);

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "secrets.h"

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

bool fetchQuote() {
  WiFiClientSecure cl; cl.setInsecure();
  HTTPClient http;
  http.setTimeout(6000);
  if (!http.begin(cl, "https://www.avanza.se/_api/market-guide/stock/1276742/quote")) return false;
  http.addHeader("User-Agent", "Mozilla/5.0");
  int code = http.GET();
  if (code != 200) { Serial.printf("HTTP %d\n", code); http.end(); return false; }
  JsonDocument d;
  DeserializationError e = deserializeJson(d, http.getString());
  http.end();
  if (e || !d["last"].is<float>()) { Serial.println("json fail"); return false; }
  PRICE = d["last"]; DAY_HIGH = d["highest"]; DAY_LOW = d["lowest"];
  float chg = d["change"], pct = d["changePercent"];
  DOWN = chg < 0;
  char buf[16]; fmt2(PRICE, buf, sizeof buf);
  char *comma = strchr(buf, ',');
  if (comma) { *comma = 0; strlcpy(PRICE_I, buf, sizeof PRICE_I); strlcpy(PRICE_D, comma + 1, sizeof PRICE_D); }
  fmt2(chg, CHG_ABS, sizeof CHG_ABS, true);
  char pb[12]; fmt2(pct, pb, sizeof pb, true); snprintf(CHG_PCT, sizeof CHG_PCT, "%s%%", pb);
  fmtThousands((long)(float)d["totalVolumeTraded"], VOLUME, sizeof VOLUME);
  char lo[12], hi[12], b[12], se[12];
  fmt2(DAY_LOW, lo, sizeof lo); fmt2(DAY_HIGH, hi, sizeof hi);
  snprintf(LOWS, sizeof LOWS, "Lägst %s", lo); snprintf(HIGHS, sizeof HIGHS, "Högst %s", hi);
  fmt2(d["buy"], b, sizeof b); fmt2(d["sell"], se, sizeof se);
  snprintf(MCAP, sizeof MCAP, "%s / %s", b, se);
  LIVE = true;
  Serial.printf("quote %.2f (%s)\n", PRICE, CHG_PCT);
  return true;
}

#define PWR_PIN 40
#define BUZZER_PIN 42

void beep(int freq, int ms) { tone(BUZZER_PIN, freq, ms); delay(ms); noTone(BUZZER_PIN); }
void startupChime() { beep(1319, 70); beep(1760, 110); }   // E6 -> A6
void click() { beep(2400, 18); }
int page = 0;
int idleLevel;

void drawPhoto() {
#ifdef HAS_PHOTO
  gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)PHOTO, 240, 280);
#else
  gfx->fillScreen(gfx->color565(8, 10, 16));
  gfx->setFont(u8g2_font_helvB14_tf);
  gfx->setTextColor(WHITE);
  gfx->setCursor(40, 130); gfx->print("Foto saknas");
  gfx->setFont(u8g2_font_helvR10_tf);
  gfx->setTextColor(gfx->color565(130, 140, 160));
  gfx->setCursor(30, 156); gfx->print("kör tools/make_photo.py");
#endif
}

void drawPage() { if (page == 0) drawStock(); else if (page == 1) drawPhoto(); }

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  pinMode(PWR_PIN, INPUT_PULLUP);
  delay(50);
  idleLevel = digitalRead(PWR_PIN);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  if (!gfx->begin()) Serial.println("gfx->begin() failed");
  gfx->setUTF8Print(true);   // åäö
  canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
  canvas->setUTF8Print(true);
  drawPage();
  startupChime();
}

void loop() {
  static uint32_t lastPress = 0;
  if (digitalRead(PWR_PIN) != idleLevel && millis() - lastPress > 300) {
    lastPress = millis();
    page = (page + 1) % 3;
    click();
    drawPage();
    while (digitalRead(PWR_PIN) != idleLevel) delay(10);   // vänta på släpp
  }
  static uint32_t nextFetch = 0, nextLog = 0;
  if (millis() > nextLog) {
    nextLog = millis() + 5000;
    Serial.printf("wifi status=%d ip=%s rssi=%d live=%d\n", WiFi.status(),
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(), LIVE);
    static uint32_t nextScan = 0;
    if (WiFi.status() == WL_NO_SSID_AVAIL && millis() > nextScan) {
      nextScan = millis() + 30000;
      WiFi.disconnect();
      delay(200);
      int n = WiFi.scanNetworks();
      Serial.printf("scan: %d nät\n", n);
      for (int i = 0; i < n; i++) {
        String ss = WiFi.SSID(i);
        Serial.printf("  [%s] ch%d %ddBm hex:", ss.c_str(), WiFi.channel(i), WiFi.RSSI(i));
        for (size_t k = 0; k < ss.length(); k++) Serial.printf("%02X", (uint8_t)ss[k]);
        Serial.println();
      }
      WiFi.scanDelete();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }
  if (page != 2 && WiFi.status() == WL_CONNECTED && millis() > nextFetch) {
    bool ok = fetchQuote();
    nextFetch = millis() + (ok ? 60000 : 10000);
    if (ok && page == 0) drawPage();
  } else if (WiFi.status() != WL_CONNECTED && LIVE) { LIVE = false; if (page == 0) drawPage(); }
  if (page == 2) {
    drawClaude(canvas, millis());
    canvas->flush();
  } else {
    delay(10);
  }
}
