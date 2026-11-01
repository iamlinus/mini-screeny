#pragma once
#include <Arduino.h>

// Non-blocking buzzer sequencer. A note slides linearly from f0 to f1 over ms; f0 == 0 is a rest.
struct Note { uint16_t f0, f1, ms; };

void soundInit();
void soundUpdate();                       // call every loop
void soundPlay(const Note *seq, uint8_t n, bool interrupt = true);
bool soundBusy();
void soundSetMuted(bool m);
bool soundMuted();

// Ready-made effects
namespace sfx {
void click();
void boing();
void bonk(float strength);   // 0..1
void scream();
void dizzy();
void chirp();
void grumble();
void knock();
void wiii();
void yawn();
void tada();
}
