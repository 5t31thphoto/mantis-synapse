#include "app.h"
#include "wire.h"

namespace wire {

static uint32_t s_rng = 0x9E3779B9u;
static inline uint32_t rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static inline float rndf() { return (rnd() & 0xFFFF) / 65535.f; }

void line(int x0, int y0, int x1, int y1, uint16_t col, float glitch) {
  if (glitch > 0 && rndf() < glitch * 0.25f) return;                       // dropout
  int gx = 0;
  if (glitch > 0 && rndf() < glitch * 0.35f) gx = (int)(rnd() % 17) - 8;  // slice tear
  canvas.drawLine(x0 + gx - 1, y0, x1 + gx - 1, y1, TEAL);                 // chromatic fringe
  canvas.drawLine(x0 + gx + 1, y0, x1 + gx + 1, y1, PLUM);
  canvas.drawLine(x0 + gx, y0, x1 + gx, y1, col);
}

void tesseract(float cx, float cy, float size, float a1, float a2, float a3, float glitch, uint16_t col) {
  float px[16], py[16];
  float c1 = cosf(a1), s1 = sinf(a1), c2 = cosf(a2), s2 = sinf(a2), c3 = cosf(a3), s3 = sinf(a3);
  for (int i = 0; i < 16; i++) {
    float x = (i & 1) ? 1.f : -1.f, y = (i & 2) ? 1.f : -1.f, z = (i & 4) ? 1.f : -1.f, w = (i & 8) ? 1.f : -1.f;
    float t = x * c1 - w * s1; w = x * s1 + w * c1; x = t;                  // xw
    t = y * c2 - w * s2; w = y * s2 + w * c2; y = t;                          // yw
    t = x * c3 - z * s3; z = x * s3 + z * c3; x = t;                          // xz
    float k4 = 1.f / (2.6f - w), k3 = 1.f / (3.2f - z * k4);                  // 4D -> 3D -> 2D
    float j = glitch > 0 && rndf() < glitch * 0.2f ? (rndf() - 0.5f) * 10.f : 0.f;
    px[i] = cx + x * k4 * k3 * size * 3.f + j;
    py[i] = cy + y * k4 * k3 * size * 3.f;
  }
  for (int i = 0; i < 16; i++)
    for (int b = 0; b < 4; b++) {
      int k = i ^ (1 << b);
      if (k > i) line((int)px[i], (int)py[i], (int)px[k], (int)py[k], b == 3 ? LIME : col, glitch);
    }
}

void tunnelRings(float vpX, float vpY, float phase, float twist, float energy, float glitch, bool recede) {
  const int RINGS = 7, SIDES = 6;
  float prevX[SIDES], prevY[SIDES];
  bool havePrev = false;
  float fr = phase - floorf(phase);
  for (int r = RINGS - 1; r >= 0; r--) {                                       // far to near
    float z = (r + (recede ? fr : 1.f - fr)) * 0.9f + 0.35f;
    float sc = 120.f / z;
    if (sc > 420.f) { havePrev = false; continue; }
    float rot = twist * z + phase * 0.3f;
    float ox = (vpX - W * 0.5f) / (z * 0.8f + 0.6f), oy = (vpY - H * 0.5f) / (z * 0.8f + 0.6f);
    float cx = W * 0.5f + ox, cy = H * 0.5f + oy;
    float x[SIDES], y[SIDES];
    for (int s = 0; s < SIDES; s++) {
      float a = rot + s * 1.0471976f;
      float wob = 1.f + energy * 0.12f * sinf(phase * 9.f + s * 2.f + r);
      x[s] = cx + cosf(a) * sc * wob; y[s] = cy + sinf(a) * sc * 0.82f * wob;
    }
    float fade = clampf(1.2f - z * 0.18f, 0.2f, 1.f);
    uint16_t c = hsv565(160.f + r * 22.f + phase * 20.f, 0.8f, fade * (0.55f + energy * 0.45f));
    for (int s = 0; s < SIDES; s++) {
      int n = (s + 1) % SIDES;
      line((int)x[s], (int)y[s], (int)x[n], (int)y[n], c, glitch);
      if (havePrev && (s & 1) == 0) line((int)x[s], (int)y[s], (int)prevX[s], (int)prevY[s], LIME, glitch * 1.5f);
    }
    for (int s = 0; s < SIDES; s++) { prevX[s] = x[s]; prevY[s] = y[s]; }
    havePrev = true;
  }
}

// ---- engraving: lift pixels along a lattice (reads the image, so it never echoes) ----
static uint16_t *s_fb = nullptr;
static int s_k = 2;
static inline void lift(int x, int y) {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
  uint16_t v = s_fb[y * W + x];
  v = (uint16_t)((v >> 8) | (v << 8));
  int r = (v >> 11) + s_k, g = ((v >> 5) & 63) + s_k * 3, b = (v & 31) + s_k;
  if (r > 31) r = 31; if (g > 63) g = 63; if (b > 31) b = 31;
  v = (uint16_t)((r << 11) | (g << 5) | b);
  s_fb[y * W + x] = (uint16_t)((v >> 8) | (v << 8));
}
static void liftLine(float x0, float y0, float x1, float y1) {
  int n = (int)(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0))) + 1;
  if (n > 800) n = 800;
  float dx = (x1 - x0) / n, dy = (y1 - y0) / n;
  for (int i = 0; i <= n; i++) lift((int)(x0 + dx * i), (int)(y0 + dy * i));
}
static void liftCircle(float cx, float cy, float r) {
  int n = (int)(r * 6.3f) + 8;
  for (int i = 0; i < n; i++) { float a = i * 6.2831853f / n; lift((int)(cx + cosf(a) * r), (int)(cy + sinf(a) * r)); }
}
void engrave(float cx, float cy, float d, float rot, int strength) {
  s_fb = (uint16_t *)canvas.getBuffer();
  if (!s_fb || strength <= 0) return;
  s_k = strength;
  const float L = 420.f;
  for (int dir = 0; dir < 3; dir++) {                                          // triangular lattice
    float a = rot + dir * 1.0471976f, ux = cosf(a), uy = sinf(a), nx = -uy, ny = ux;
    for (int k = -7; k <= 7; k++) {
      float px = cx + nx * k * d * 0.8660254f, py = cy + ny * k * d * 0.8660254f;
      liftLine(px - ux * L, py - uy * L, px + ux * L, py + uy * L);
    }
  }
  liftCircle(cx, cy, d);                                                        // flower of life rosette
  for (int i = 0; i < 6; i++) { float a = rot + i * 1.0471976f + 0.5235988f; liftCircle(cx + cosf(a) * d * 0.577f * 1.732f, cy + sinf(a) * d, d); }
  liftCircle(cx, cy, d * 2.f);
}

}  // namespace wire
