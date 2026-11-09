#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Lagervärde page: total stock value from Airtable, split Brand Group Code E / other, against the last
// day the Mac has a reading for. Higher is BAD (red), lower is GOOD (green). Data from tools/companion.py
// over USB: "lag {json}" (see tools/lager.py), fetched hourly.
bool lagerFeed(const char *json);   // false on bad JSON / missing totals
void drawLager(Arduino_GFX *g);
bool lagerConsumeDirty();           // true once after new data arrived or the status mark changed
const char *lagerDebug();
