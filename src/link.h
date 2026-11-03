#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Contact with tools/companion.py on the Mac, and the status mark every data page shows top right:
//   green dot  = fresh data
//   red dot    = data older than the page allows (5 min by default; the Mac is there but gets no new data)
//   "ø"        = no contact with the Mac (no serial line for 25 s; the companion pings every 10 s)
static const uint32_t LINK_TIMEOUT_MS = 25000;
static const uint32_t DATA_OLD_MS = 5 * 60 * 1000UL;

void linkSeen();                   // call for every complete serial line
bool linkAlive();
enum LinkState { LINK_NONE, LINK_OLD, LINK_FRESH };
LinkState linkState(bool hasData, uint32_t dataAgeMs, uint32_t oldMs = DATA_OLD_MS);
void drawLinkMark(Arduino_GFX *g, int x, int y, LinkState s);   // (x, y) = centre of the mark
