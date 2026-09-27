// ============================================================
//  SYNAPSE — FX engine
//  160x120 8-bit indexed buffers (internal RAM) + 256-colour palette,
//  MilkDrop-style grid warp for feedback, and a 2x present straight into
//  the 16-bit framebuffer. Per-pixel psychedelia at Core2 speed.
// ============================================================
#pragma once
#include <stdint.h>

#include <M5Unified.h>

namespace fx {

static const int LW = 160, LH = 120;
static const int GW = 21, GH = 16;           // warp grid nodes (8x8 px blocks)

extern uint8_t *buf;                         // draw target
extern uint8_t *back;                        // previous frame (warp source)
extern float gx[GH][GW], gy[GH][GW];         // warp: source coords per node

// LUTs over offsets dx in [-160,159], dy in [-120,119]  (index (dy+120)*320 + dx+160)
extern uint16_t *tun;                        // (angle << 8) | depth   depth = 900/r
extern uint8_t *rad;                         // r (lores pixels)
extern int8_t sn[256];                       // sin * 127

bool begin();
void swap();
void clear(uint8_t v = 0);
void present(M5Canvas &c);
void warp(uint8_t fade, bool smooth);        // buf <- back sampled through grid
void warpIdentity();

// palettes. index 0 is always near-black.
void palCosine(float ar, float ag, float ab, float br, float bg, float bb,
               float cr, float cg, float cb, float dr, float dg, float db, float shift,
               float gamma = 1.f);
void palSet(int i, uint8_t r, uint8_t g, uint8_t b);
void palFlash(float amt);                    // lift towards white

// primitives (brightness = index, max-blend)
void plot(int x, int y, uint8_t v);
void addp(int x, int y, int v);              // saturating add
void line(int x0, int y0, int x1, int y1, uint8_t v);
void disc(int cx, int cy, int r, uint8_t v);
void ring(int cx, int cy, int r, uint8_t v);

// ---- dual-core raster: rows [0,LH/2) on this core, [LH/2,LH) on core 0 ----
typedef void (*RowTramp)(void *ctx, int y0, int y1);
void parallelRaw(RowTramp t, void *ctx);
template <class F> inline void parallel(F &f) {
  parallelRaw([](void *c, int y0, int y1) { (*(F *)c)(y0, y1); }, (void *)&f);
}

static inline uint16_t tunAt(int dx, int dy) {
  if (dx < -160) dx = -160; else if (dx > 159) dx = 159;
  if (dy < -120) dy = -120; else if (dy > 119) dy = 119;
  return tun[(dy + 120) * 320 + dx + 160];
}
static inline uint8_t radAt(int dx, int dy) {
  if (dx < -160) dx = -160; else if (dx > 159) dx = 159;
  if (dy < -120) dy = -120; else if (dy > 119) dy = 119;
  return rad[(dy + 120) * 320 + dx + 160];
}

}  // namespace fx
