// ============================================================
//  SYNAPSE — hidden stats (never shown as numbers)
//  Lifetime accumulators fed by everything you do in Synapse; the gardens
//  grow from them. Saved in NVS; the Core2's RTC tells real time passed,
//  so the gardens also change while the device is off.
// ============================================================
#pragma once
#include <stdint.h>

namespace stats {
enum Ev : uint8_t { EV_ROOM = 0, EV_PORTAL, EV_HOOP, EV_POP, EV_CARE, EV_MEDITATE };
struct Save {
  uint32_t magic, epoch;                     // last save, seconds (0 = RTC unknown)
  float quiet, sound, motion, play, care;    // lifetime hidden stats
  // crystal garden
  float cxp[5];                              // xp per crystal: quartz, amethyst, bismuth, fluorite, opal
  float dust;                                // 0 clean .. 1 dusty
  // succulent garden
  float soil;                                // 0 bone dry .. 1 soaked
  float grow[5], health[5];                  // per plant
  uint8_t pups[5], bloom[5];
  float pump;                                // waterfall reservoir 0..1
  float rake[4];                             // (unused, reserved)
};
extern Save s;
void begin();
void tick(float dt, bool quietMode);         // call every frame
void event(Ev e, float amount = 1.f);
void save();
float hoursAway();                           // real hours passed since last time (0 if unknown)
uint32_t now();                              // epoch seconds from RTC (0 if unknown)
int hourOfDay();                             // 0..23 (12 if unknown)
}
