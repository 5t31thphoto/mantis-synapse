// ============================================================
//  SYNAPSE — hidden stats (see stats.h)
// ============================================================
#include "app.h"
#include "audio.h"
#include "stats.h"
#include <string.h>
#ifndef HOST
#include <Preferences.h>
#endif

namespace stats {

Save s;
static float s_away = 0;
static float s_saveT = 0;
static const uint32_t MAGIC = 0x4D4E5447;   // "MNTG"

static uint32_t epochOf(int y, int mo, int d, int h, int mi, int se) {
  static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  uint32_t days = (uint32_t)((y - 1970) * 365 + (y - 1969) / 4 + cum[(mo - 1) % 12] + d - 1);
  if (mo > 2 && (y % 4) == 0) days++;
  return days * 86400u + (uint32_t)(h * 3600 + mi * 60 + se);
}
uint32_t now() {
#ifndef HOST
  if (!M5.Rtc.isEnabled()) return 0;
  auto dt = M5.Rtc.getDateTime();
  if (dt.date.year < 2024 || dt.date.year > 2099) return 0;         // clock never set
  return epochOf(dt.date.year, dt.date.month, dt.date.date, dt.time.hours, dt.time.minutes, dt.time.seconds);
#else
  return 1800000000u + millis() / 1000u;
#endif
}
int hourOfDay() { uint32_t t = now(); return t ? (int)((t / 3600u) % 24u) : 12; }
float hoursAway() { return s_away; }

void save() {
  s.magic = MAGIC; s.epoch = now();
#ifndef HOST
  Preferences p;
  if (p.begin("syngarden", false)) { p.putBytes("s", &s, sizeof(s)); p.end(); }
#endif
}

void begin() {
  memset(&s, 0, sizeof(s));
  bool ok = false;
#ifndef HOST
  Preferences p;
  if (p.begin("syngarden", true)) { ok = p.getBytes("s", &s, sizeof(s)) == sizeof(s) && s.magic == MAGIC; p.end(); }
#endif
  if (!ok) {
    memset(&s, 0, sizeof(s));
    s.soil = 0.5f; s.pump = 1.f;
    for (int i = 0; i < 5; i++) { s.grow[i] = 0.15f + i * 0.04f; s.health[i] = 0.9f; s.cxp[i] = 4.f; }
  }
  uint32_t t = now();
  s_away = (ok && t && s.epoch && t > s.epoch) ? (t - s.epoch) / 3600.f : 0.f;
  if (s_away > 24.f * 60.f) s_away = 24.f * 60.f;
  // time passed while off: soil dries, dust settles, the reservoir evaporates a little, plants grow if cared for
  s.soil = clampf(s.soil - s_away * 0.012f, 0.f, 1.f);
  s.dust = clampf(s.dust + s_away * 0.02f, 0.f, 1.f);
  s.pump = clampf(s.pump - s_away * 0.004f, 0.f, 1.f);
  for (int i = 0; i < 5; i++) if (s.health[i] > 0.5f) s.grow[i] = fminf(1.f, s.grow[i] + s_away * 0.0015f * s.health[i]);
}

void event(Ev e, float a) {
  switch (e) {
    case EV_ROOM: s.play += 3.f * a; break;
    case EV_PORTAL: s.play += 2.f * a; break;
    case EV_HOOP: s.play += 0.3f * a; break;
    case EV_POP: s.play += 0.02f * a; break;
    case EV_CARE: s.care += a; break;
    case EV_MEDITATE: s.quiet += 30.f * a; break;
  }
}

void tick(float dt, bool quietMode) {
  // the hidden stats: slow, gentle, lifetime
  if (aud::level < 0.05f && !aud::calibrating() && quietMode) s.quiet += dt / 60.f;
  s.sound += aud::level * dt / 60.f;
  s.motion += clampf(g_jolt * 0.6f + (fabsf(g_gyroX) + fabsf(g_gyroY) + fabsf(g_gyroZ)) / 900.f, 0.f, 2.f) * dt / 60.f;
  // crystals drink from their stat (dust slows them)
  const float *src[5] = {&s.quiet, &s.sound, &s.play, &s.motion, &s.care};
  for (int i = 0; i < 5; i++) {
    float target = 4.f + *src[i] * 0.6f;
    if (s.cxp[i] < target) s.cxp[i] = fminf(target, s.cxp[i] + dt * 0.02f * (1.f - s.dust * 0.8f));
  }
  // real time keeps moving while you play
  s.soil = clampf(s.soil - dt * 0.012f / 3600.f, 0.f, 1.f);
  s.dust = clampf(s.dust + dt * 0.02f / 3600.f, 0.f, 1.f);
  s_saveT += dt;
  if (s_saveT > 60.f) { s_saveT = 0; save(); }
}

}  // namespace stats
