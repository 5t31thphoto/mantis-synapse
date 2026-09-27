// ============================================================
//  SYNAPSE — audio (listen-only)
//  The mic is always on: every mode hears the room continuously.
//  sfx() borrows the speaker for a moment (Core2 shares one I2S port).
// ============================================================
#pragma once
#include <stdint.h>

namespace aud {
void begin();
void service(float dt);                // main thread, once per frame

extern float level, peak;              // energy 0..~1.5
extern float calm;                     // slow, soft energy for the calm room
extern float zcr;                      // 0..1 brightness
extern float onset;                    // onset strength this frame (0 = none)
extern float beatPos, beatConf;        // beat PLL
extern int16_t scope[256];
extern float bands[32];
extern float bass, mid, treble, centroid;
bool speakerLive();                    // true only while an sfx is sounding
void sfx(float pitch);                 // comic squeak
void calibrateAmbient(float cap = 1000.f);   // listen ~0.9 s to learn the room's noise floor (capped)
bool calibrating();
void playBuf(const int16_t *d, int n, uint32_t rate, uint8_t vol);   // borrow the speaker for a buffer
void bell(int which, uint8_t vol);     // singing-bowl bells 0..2
void speakerHold(bool on);             // keep the speaker (mic off) - meditation
void echo(bool on);                    // yakback: records when you talk, plays you back funny
int echoState();                       // 0 listening, 1 recording, 2 playing
int echoFx();                          // 0 chipmunk, 1 monster, 2 backwards
}
