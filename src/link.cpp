#include "link.h"
#include "theme.h"
#include "board.h"

static uint32_t lastMs = 0;
static bool seen = false;

void linkSeen() { lastMs = millis(); seen = true; }
bool linkAlive() { return seen && millis() - lastMs < LINK_TIMEOUT_MS; }

LinkState linkState(bool hasData, uint32_t dataAgeMs, uint32_t oldMs) {
  if (!linkAlive()) return LINK_NONE;
  return hasData && dataAgeMs < oldMs ? LINK_FRESH : LINK_OLD;
}

static void center(Arduino_GFX *g, const char *s, int y) {
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(SCREEN_W / 2 - w / 2 - x1, y);
  g->print(s);
}

bool drawNoData(Arduino_GFX *g, bool hasData) {
  if (hasData) return false;
  if (linkAlive()) {
    g->setFont(u8g2_font_helvR10_tf); g->setTextColor(theme::MUTED);
    center(g, "Väntar på datorn ...", SCREEN_H / 2 + 4);
    return true;
  }
  // No companion on this computer (yet): tell how to get it.
  g->setFont(u8g2_font_helvB14_tf); g->setTextColor(theme::TEXT);
  center(g, "Installera på datorn", 64);
  g->setFont(u8g2_font_helvR10_tf); g->setTextColor(theme::MUTED);
  center(g, "Be Claude Code:", 100);
  g->setFont(u8g2_font_helvB10_tf); g->setTextColor(theme::TEXT);
  center(g, "Installera mini screeny från", 130);
  g->setTextColor(theme::ACCENT);
  center(g, REPO_URL, 150);
  g->setFont(u8g2_font_helvR08_tf); g->setTextColor(theme::MUTED);
  center(g, "Skärmen hämtar sin data via ett litet", 190);
  center(g, "program på datorn (companion).", 204);
  return true;
}

void drawLinkMark(Arduino_GFX *g, int x, int y, LinkState s) {
  if (s == LINK_NONE) {
    g->setFont(u8g2_font_helvB12_tf);
    g->setTextColor(theme::MUTED);
    int16_t x1, y1; uint16_t w, h;
    g->getTextBounds("ø", 0, 0, &x1, &y1, &w, &h);
    g->setCursor(x - w / 2 - x1, y - h / 2 - y1);
    g->print("ø");
  } else {
    g->fillCircle(x, y, 4, s == LINK_FRESH ? theme::GOOD : theme::BAD);
  }
}
