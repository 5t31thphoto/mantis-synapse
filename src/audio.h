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
}
