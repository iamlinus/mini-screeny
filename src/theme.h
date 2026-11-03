#pragma once
#include <stdint.h>

// One palette for every page: warm near-black background, warm white text, lilac-grey secondary text
// (the Claude buddy's colours). Status/trend colours carry meaning and are the same everywhere.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {
constexpr uint16_t BG      = rgb565(16, 13, 22);     // page background
constexpr uint16_t SURFACE = rgb565(40, 34, 50);     // bar tracks, rings, cards
constexpr uint16_t TEXT    = rgb565(245, 240, 235);  // primary text and numbers
constexpr uint16_t MUTED   = rgb565(150, 140, 160);  // labels, secondary text
constexpr uint16_t ACCENT  = rgb565(217, 119, 87);   // Claude orange
constexpr uint16_t INK     = rgb565(32, 20, 18);     // text on light/orange fills

constexpr uint16_t GOOD    = rgb565(60, 220, 140);   // up, win, fresh
constexpr uint16_t BAD     = rgb565(255, 85, 95);    // down, loss, old data
constexpr uint16_t WARN    = rgb565(255, 196, 90);   // draw, getting full
constexpr uint16_t GOOD_BG = rgb565(20, 52, 40);     // dark fills behind GOOD / BAD text
constexpr uint16_t BAD_BG  = rgb565(62, 22, 32);
constexpr uint16_t WARN_BG = rgb565(60, 46, 26);
}
