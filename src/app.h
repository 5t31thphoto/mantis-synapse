// ============================================================
//  SYNAPSE — shared declarations
// ============================================================
#pragma once
#include <M5Unified.h>
#include <math.h>
#ifndef HOST
#include <esp_timer.h>
#include <esp_heap_caps.h>
#endif

static const int W = 320, H = 240;
static const int MIC_N = 128;          // scope samples exposed to visuals

// The frame currently being rendered (double-buffered, see main.cpp).
extern M5Canvas *g_cv;
#define canvas (*g_cv)

// ---- shared animation / sensor state (main thread only) ----
extern float g_t, g_dt, g_hue;
extern float g_level, g_peak;          // 0..~1.5 audio energy (mic OR drum loop)
extern float g_lookX, g_lookY;         // tilt, -1.1..1.1
extern int16_t g_mic[MIC_N];           // scope for PULSE
extern bool g_shakeKick;
extern float g_gravX, g_gravY;         // gravity in screen space (downhill)               // true for one frame after a shake

// ---- haptics (main thread only; non-blocking) ----
void hap(uint8_t level, uint16_t ms);
void kickSubHaptic();

// ---- colour helpers ----
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static inline uint16_t hsv565(float h, float s, float v) {
  h = fmodf(h, 360.f);
  if (h < 0) h += 360.f;
  if (v > 1.f) v = 1.f;
  if (v < 0.f) v = 0.f;
  float c = v * s, x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f)), m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else { r = c; b = x; }
  return rgb565((uint8_t)((r + m) * 255), (uint8_t)((g + m) * 255), (uint8_t)((b + m) * 255));
}
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- mantis puppet (mantis.cpp) ----
void mantisBegin();
void mantisDraw(bool sing);
void mantisTap(int x, int y);
// calm.cpp — the physics room
void calmBegin();
void calmDraw();
void calmNext();                       // B: next room
void calmLongPress();                  // long-press: this room's variant
void calmTouch(int x, int y, bool down);
const char *calmName();
