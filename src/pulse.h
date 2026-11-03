#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// VibePulse page: Claude quota windows (session, week, per-model week) read by tools/companion.py on the Mac.
// The board has no network path to it; tools/vibepulse_bridge.py polls the server and
// sends the payload over USB serial as one line: "vp {json}".
bool pulseFeed(const char *json);   // parse a (subset of a) /api/tokens v2 payload; false on bad JSON
bool pulseWantsDraw();              // true when new data arrived or the once-a-second refresh is due
void pulseDraw(Arduino_GFX *g);
const char *pulseDebug();
const char *pulseTakeAlert();      // a quota reset / passed 90 % since the last call, or nullptr
