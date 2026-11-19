#pragma once
#include "frame.h"
#include <Arduino_GFX_Library.h>

void buddyInit();
void buddyUpdate(const Sensors &s, float dt);
void buddyDraw(Arduino_GFX *g);
void buddySay(const char *msg, float sec, int prio = 1);
void buddyAlert(const char *msg, float sec, bool wave = false);   // wakes him, beats any chatter, keeps him awake while it shows
void buddyCheer(const char *msg, float sec);   // an alert he is happy about: arms up, a hop, sparkles
// Claude Code sessions on the Mac: while any is thinking he stays awake with "..." over his head,
// while any waits for the user he carries a "!" and says which one now and then.
void buddySetAgents(int thinking, int waiting, const char *waitingTitle);
bool buddySetName(const char *name);   // greeting "Hej <name>!"; false if unchanged
bool buddyAsleep();
const char *buddyDebug();   // one-line state for serial logs
