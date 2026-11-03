#include "football.h"
#include "board.h"
#include "link.h"
#include "theme.h"
#include <ArduinoJson.h>

struct Match { bool has = false, home = false; char opp[20] = ""; int gf = 0, ga = 0, days = 0; char when[24] = "", date[12] = ""; };
struct TeamData {
  bool hasTable = false; int pos = 0, of = 0, pts = 0, gp = 0; char league[16] = "", me[10] = "";
  Match next, last;
  bool any = false; uint32_t rxMs = 0;
};
static TeamData H;
static bool dirty = true;
static int lastMark = -1;
static const uint32_t OLD_MS = 3UL * 3600 * 1000;   // fetched hourly: red after 3 h without new data

static LinkState mark() { return linkState(H.any, millis() - H.rxMs, OLD_MS); }

bool footballFeed(const char *json) {
  JsonDocument d;
  if (deserializeJson(d, json) || !d.is<JsonObject>()) return false;
  TeamData n;
  strlcpy(n.me, d["me"] | "", sizeof n.me);
  JsonObject t = d["table"];
  if (!t.isNull() && t["pos"].is<int>()) {
    n.hasTable = true; n.pos = t["pos"]; n.of = t["of"] | 0; n.pts = t["pts"] | 0; n.gp = t["gp"] | 0;
    strlcpy(n.league, t["league"] | "", sizeof n.league);
  }
  JsonObject nx = d["next"];
  if (!nx.isNull()) {
    n.next.has = true; n.next.home = nx["home"] | false; n.next.days = nx["days"] | 0;
    strlcpy(n.next.opp, nx["opp"] | "?", sizeof n.next.opp); strlcpy(n.next.when, nx["when"] | "", sizeof n.next.when);
  }
  JsonObject ls = d["last"];
  if (!ls.isNull() && ls["gf"].is<int>() && ls["ga"].is<int>()) {
    n.last.has = true; n.last.home = ls["home"] | false; n.last.gf = ls["gf"]; n.last.ga = ls["ga"];
    strlcpy(n.last.opp, ls["opp"] | "?", sizeof n.last.opp); strlcpy(n.last.date, ls["date"] | "", sizeof n.last.date);
  }
  n.any = true; n.rxMs = millis();
  H = n;
  dirty = true;
  return true;
}

bool footballConsumeDirty() {
  int m = mark();
  bool d = dirty || m != lastMark;
  dirty = false; lastMark = m;
  return d;
}

const char *footballDebug() {
  static char b[128];
  snprintf(b, sizeof b, "fb any=%d %s pos=%d next=%s %dd last=%d-%d %s", H.any, H.me, H.pos, H.next.opp, H.next.days, H.last.gf, H.last.ga, H.last.opp);
  return b;
}

// ---------------- drawing ----------------
static int textW(Arduino_GFX *g, const char *s, int16_t *x1 = nullptr) {
  int16_t a, b; uint16_t w, h;
  g->getTextBounds(s, 0, 0, &a, &b, &w, &h);
  if (x1) *x1 = a;
  return w;
}

// small rounded tag with a letter, e.g. "H"/"B" or the result "V"/"O"/"F"
// returns the tag's width
static int tag(Arduino_GFX *g, int x, int y, const char *s, uint16_t fg, uint16_t bg) {
  g->setFont(u8g2_font_helvB10_tf);
  int16_t x1; int w = textW(g, s, &x1);
  g->fillRoundRect(x, y - 13, w + 12, 18, 6, bg);
  g->setTextColor(fg);
  g->setCursor(x + 6 - x1, y);
  g->print(s);
  return w + 12;
}

// Landscape 280 x 240: table position (left) and next match (right) on top, last result underneath.
void footballDraw(Arduino_GFX *g) {
  using namespace theme;
  g->fillScreen(BG);
  drawLinkMark(g, SCREEN_W - 24, 22, mark());
  if (drawNoData(g, H.any)) return;
  const int L = 26, R = 150;

  // ---- table position ----
  g->setFont(u8g2_font_helvR08_tf); g->setTextColor(MUTED);
  g->setCursor(L, 34); g->print("TABELLEN");
  char big[8];
  if (H.hasTable) snprintf(big, sizeof big, "%d", H.pos); else strcpy(big, "-");
  g->setFont(u8g2_font_logisoso50_tn); g->setTextColor(H.hasTable ? TEXT : MUTED);
  int16_t x1; int w = textW(g, big, &x1);
  g->setCursor(L - x1, 98); g->print(big);
  if (H.hasTable) {
    char ln[32];
    g->setFont(u8g2_font_helvR10_tf); g->setTextColor(MUTED);
    snprintf(ln, sizeof ln, "av %d", H.of);
    g->setCursor(L + w + 8, 98); g->print(ln);
    g->setFont(u8g2_font_helvR08_tf);
    snprintf(ln, sizeof ln, "%s · %d p", H.league, H.pts);
    g->setCursor(L, 118); g->print(ln);
  }

  // ---- next match ----
  g->setFont(u8g2_font_helvR08_tf); g->setTextColor(MUTED);
  g->setCursor(R, 34); g->print("NÄSTA MATCH");
  if (H.next.has) {
    if (H.next.days <= 1) {
      g->setFont(u8g2_font_helvB14_tf); g->setTextColor(TEXT);
      g->setCursor(R, 92); g->print(H.next.days == 0 ? "IDAG" : "IMORGON");
    } else {
      snprintf(big, sizeof big, "%d", H.next.days);
      g->setFont(u8g2_font_logisoso50_tn); g->setTextColor(TEXT);
      w = textW(g, big, &x1);
      g->setCursor(R - x1, 98); g->print(big);
      g->setFont(u8g2_font_helvR10_tf); g->setTextColor(MUTED);
      g->setCursor(R + w + 8, 98); g->print("dagar");
    }
    // [H]/[B] + opponent, then the kick-off time
    int tw = tag(g, R, 119, H.next.home ? "H" : "B", INK, ACCENT);
    char ln[32];
    g->setFont(u8g2_font_helvB10_tf); g->setTextColor(TEXT);
    strlcpy(ln, H.next.opp, sizeof ln);
    g->setCursor(R + tw + 6, 119); g->print(ln);
    g->setFont(u8g2_font_helvR08_tf); g->setTextColor(MUTED);
    g->setCursor(R, 136); g->print(H.next.when);
  } else {
    g->setFont(u8g2_font_helvR10_tf); g->setTextColor(MUTED);
    g->setCursor(R, 92); g->print("Inget spelschema");
  }

  // ---- last result ----
  g->drawFastHLine(L, 152, SCREEN_W - 2 * L, SURFACE);
  g->setFont(u8g2_font_helvR08_tf); g->setTextColor(MUTED);
  g->setCursor(L, 174); g->print("SENASTE MATCH");
  if (H.last.has) {
    const Match &m = H.last;
    bool win = m.gf > m.ga, draw = m.gf == m.ga;
    tag(g, L, 202, win ? "V" : draw ? "O" : "F", win ? GOOD : draw ? WARN : BAD, win ? GOOD_BG : draw ? WARN_BG : BAD_BG);
    char ln[48];   // home team first, as in every table
    if (m.home) snprintf(ln, sizeof ln, "%s  %d - %d  %s", H.me, m.gf, m.ga, m.opp);
    else snprintf(ln, sizeof ln, "%s  %d - %d  %s", m.opp, m.ga, m.gf, H.me);
    g->setFont(u8g2_font_helvB12_tf); g->setTextColor(TEXT);
    g->setCursor(L + 34, 202); g->print(ln);
    g->setFont(u8g2_font_helvR08_tf); g->setTextColor(MUTED);
    snprintf(ln, sizeof ln, "%s · %s", m.home ? "Hemma" : "Borta", m.date);
    g->setCursor(L + 34, 219); g->print(ln);
  }
}
