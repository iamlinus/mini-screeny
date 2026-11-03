#include "link.h"

static uint32_t lastMs = 0;
static bool seen = false;

void linkSeen() { lastMs = millis(); seen = true; }
bool linkAlive() { return seen && millis() - lastMs < LINK_TIMEOUT_MS; }

LinkState linkState(bool hasData, uint32_t dataAgeMs) {
  if (!linkAlive()) return LINK_NONE;
  return hasData && dataAgeMs < DATA_OLD_MS ? LINK_FRESH : LINK_OLD;
}

void drawLinkMark(Arduino_GFX *g, int x, int y, LinkState s) {
  if (s == LINK_NONE) {
    g->setFont(u8g2_font_helvB12_tf);
    g->setTextColor(g->color565(150, 140, 160));
    int16_t x1, y1; uint16_t w, h;
    g->getTextBounds("ø", 0, 0, &x1, &y1, &w, &h);
    g->setCursor(x - w / 2 - x1, y - h / 2 - y1);
    g->print("ø");
  } else {
    g->fillCircle(x, y, 4, s == LINK_FRESH ? g->color565(60, 220, 140) : g->color565(255, 70, 80));
  }
}
