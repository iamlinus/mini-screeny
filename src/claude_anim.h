#pragma once
#include <Arduino_GFX_Library.h>
#include <math.h>

// Animated orange mascot, drawn procedurally. Call drawClaude(g, millis()) each frame.
static void centerIn(Arduino_GFX *g, const char *s, int cx, int y) {
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(cx - w / 2 - x1, y);
  g->print(s);
}

static void sparkle(Arduino_GFX *g, int x, int y, int r, uint16_t col) {
  g->fillTriangle(x, y - r, x - r / 4, y, x + r / 4, y, col);
  g->fillTriangle(x, y + r, x - r / 4, y, x + r / 4, y, col);
  g->fillTriangle(x - r, y, x, y - r / 4, x, y + r / 4, col);
  g->fillTriangle(x + r, y, x, y - r / 4, x, y + r / 4, col);
}

static void drawClaude(Arduino_GFX *g, uint32_t ms) {
  const float t = ms / 1000.0f;
  const uint16_t K_BG     = g->color565(14, 12, 20);
  const uint16_t K_GLOW   = g->color565(30, 20, 26);
  const uint16_t K_ORANGE = g->color565(217, 119, 87);
  const uint16_t K_ORANGE_D = g->color565(176, 88, 60);
  const uint16_t K_ORANGE_L = g->color565(240, 150, 115);
  const uint16_t K_DARK   = g->color565(30, 20, 18);
  const uint16_t K_WHITE  = g->color565(250, 246, 240);
  const uint16_t K_GOLD   = g->color565(255, 214, 120);

  g->fillScreen(K_BG);
  g->fillCircle(120, 175, 100, K_GLOW);                      // soft backdrop

  // bounce
  float ph = t * 3.2f;
  float hop = fabsf(sinf(ph)) * 14.0f;                     // 0..14 px up
  float squash = 1.0f - 0.06f * (1.0f - fabsf(sinf(ph)));  // landing squash
  const int cx = 120, ground = 232;
  int bw = (int)(108 * (2.0f - squash)), bh = (int)(78 * squash);
  int bx = cx - bw / 2, by = ground - 22 - bh - (int)hop;

  // shadow
  int sw = 56 - (int)(hop * 1.4f);
  g->fillRoundRect(cx - sw, ground - 3, sw * 2, 8, 4, g->color565(8, 6, 12));

  // legs (4)
  int legTop = by + bh - 4, legH = 22 + (int)hop;
  if (legH > 26) legH = 26;
  for (int i = 0; i < 4; i++) {
    int lx = bx + 10 + i * (bw - 36) / 3;
    g->fillRoundRect(lx, legTop, 16, legH, 4, K_ORANGE_D);
  }

  // arms: left sways, right waves every ~6 s
  int armY = by + bh / 2;
  float sway = sinf(ph + 1.0f) * 5.0f;
  g->fillRoundRect(bx - 16, armY - 6 + (int)sway, 20, 16, 6, K_ORANGE);
  float waveCycle = fmodf(t, 6.0f);
  int rY = armY - 6;
  if (waveCycle > 4.2f && waveCycle < 5.6f) rY = armY - 34 + (int)(sinf(t * 18.0f) * 7.0f);
  else rY = armY - 6 - (int)sway;
  g->fillRoundRect(bx + bw - 4, rY, 20, 16, 6, K_ORANGE);

  // body
  g->fillRoundRect(bx, by, bw, bh, 20, K_ORANGE);
  g->fillRoundRect(bx + 8, by + 6, bw - 16, 12, 6, K_ORANGE_L);   // top highlight

  // eyes: look around, blink every ~3.4 s
  float look = sinf(t * 0.9f) * 6.0f;
  float bl = fmodf(t, 3.4f);
  int eyeH = (bl > 3.25f) ? 3 : 24;
  int ey = by + bh / 2 - 14 + (24 - eyeH) / 2;
  g->fillRoundRect(cx - 30 + (int)look, ey, 13, eyeH, 4, K_DARK);
  g->fillRoundRect(cx + 17 + (int)look, ey, 13, eyeH, 4, K_DARK);
  // tiny cheeks
  g->fillCircle(cx - 40, by + bh / 2 + 14, 5, K_ORANGE_L);
  g->fillCircle(cx + 40, by + bh / 2 + 14, 5, K_ORANGE_L);

  // sparkles
  for (int i = 0; i < 4; i++) {
    float p = fmodf(t * 0.8f + i * 0.25f, 1.0f);
    int r = (int)(sinf(p * 3.14159f) * (6 + i * 2));
    if (r > 1) {
      static const int sx[4] = {34, 206, 52, 192};
      static const int sy[4] = {120, 138, 206, 96};
      sparkle(g, sx[i], sy[i], r, K_GOLD);
    }
  }

  // speech bubble, message changes every 3.5 s
  static const char *MSG[] = {"Hej Linus!", "Vad bygger vi idag?", "Jag bor i en ESP32", "Tryck PWR!"};
  const char *m = MSG[((uint32_t)(t / 3.5f)) % 4];
  g->setFont(u8g2_font_helvB10_tf);
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(m, 0, 0, &x1, &y1, &w, &h);
  int pw = w + 28, px = cx - pw / 2;
  g->fillRoundRect(px, 36, pw, 34, 14, K_WHITE);
  g->fillTriangle(cx - 8, 69, cx + 8, 69, cx, 80, K_WHITE);
  g->setTextColor(K_DARK);
  centerIn(g, m, cx, 58);
}
