// ============================================================
//  SYNAPSE — FX engine (see fx.h)
// ============================================================
#include "app.h"
#include "fx.h"
#include <string.h>

namespace fx {

uint8_t *buf = nullptr, *back = nullptr;
float gx[GH][GW], gy[GH][GW];
uint16_t *tun = nullptr;
uint8_t *rad = nullptr;
int8_t sn[256];
static uint32_t s_pal[256];      // swapped RGB565, duplicated into both halves
static uint8_t s_rgb[256][3];

static inline uint16_t sw565(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = rgb565(r, g, b);
  return (uint16_t)((c >> 8) | (c << 8));
}

static RowTramp s_job = nullptr;
static void *s_ctx = nullptr;
#ifndef HOST
static TaskHandle_t s_worker = nullptr;
static SemaphoreHandle_t s_done = nullptr;
static void workerTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (s_job) s_job(s_ctx, LH / 2, LH);
    xSemaphoreGive(s_done);
  }
}
#endif
void parallelRaw(RowTramp t, void *ctx) {
#ifndef HOST
  if (s_worker) {
    s_job = t; s_ctx = ctx;
    xTaskNotifyGive(s_worker);
    t(ctx, 0, LH / 2);
    xSemaphoreTake(s_done, portMAX_DELAY);
    return;
  }
#endif
  t(ctx, 0, LH);
}

bool begin() {
#ifndef HOST
  s_done = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(workerTask, "fxw", 4096, nullptr, 3, &s_worker, 0);
#endif
  buf = (uint8_t *)heap_caps_malloc(LW * LH, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  back = (uint8_t *)heap_caps_malloc(LW * LH, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) buf = (uint8_t *)malloc(LW * LH);
  if (!back) back = (uint8_t *)malloc(LW * LH);
  tun = (uint16_t *)heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  rad = (uint8_t *)heap_caps_malloc(320 * 240, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!tun) tun = (uint16_t *)malloc(320 * 240 * 2);
  if (!rad) rad = (uint8_t *)malloc(320 * 240);
  if (!buf || !back || !tun || !rad) return false;
  memset(buf, 0, LW * LH); memset(back, 0, LW * LH);
  for (int i = 0; i < 256; i++) sn[i] = (int8_t)(sinf(i * 6.2831853f / 256.f) * 127.f);
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      float dx = x - 160 + 0.5f, dy = y - 120 + 0.5f;
      float r = sqrtf(dx * dx + dy * dy);
      int a = (int)(atan2f(dy, dx) * (256.f / 6.2831853f)) & 255;
      int d = (int)(900.f / (r + 0.35f));
      if (d > 255) d = 255;
      tun[y * 320 + x] = (uint16_t)((a << 8) | d);
      rad[y * 320 + x] = (uint8_t)(r > 255.f ? 255 : (int)r);
    }
  warpIdentity();
  palCosine(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1, 1, 1, 0.f, 0.33f, 0.67f, 0);
  return true;
}

void swap() { uint8_t *t = buf; buf = back; back = t; }
void clear(uint8_t v) { memset(buf, v, LW * LH); }

void present(M5Canvas &c) {
  uint16_t *fb = (uint16_t *)c.getBuffer();
  if (!fb) return;
  for (int y = 0; y < LH; y++) {
    const uint8_t *s = buf + y * LW;
    uint32_t *r0 = (uint32_t *)(fb + (y * 2) * W);
    uint32_t *r1 = r0 + W / 2;
    for (int x = 0; x < LW; x++) { uint32_t v = s_pal[s[x]]; r0[x] = v; r1[x] = v; }
  }
}

void warpIdentity() {
  for (int j = 0; j < GH; j++)
    for (int i = 0; i < GW; i++) { gx[j][i] = i * 8.f; gy[j][i] = j * 8.f; }
}

void warp(uint8_t fade, bool smooth) {
  static int32_t fx_[GH][GW], fy_[GH][GW];
  for (int j = 0; j < GH; j++)
    for (int i = 0; i < GW; i++) {
      fx_[j][i] = (int32_t)(gx[j][i] * 65536.f);
      fy_[j][i] = (int32_t)(gy[j][i] * 65536.f);
    }
  const int32_t maxX = (LW - 2) << 16, maxY = (LH - 2) << 16;
  for (int bj = 0; bj < GH - 1; bj++)
    for (int bi = 0; bi < GW - 1; bi++) {
      int32_t lx = fx_[bj][bi], ly = fy_[bj][bi];
      int32_t rx = fx_[bj][bi + 1], ry = fy_[bj][bi + 1];
      int32_t dlx = (fx_[bj + 1][bi] - lx) >> 3, dly = (fy_[bj + 1][bi] - ly) >> 3;
      int32_t drx = (fx_[bj + 1][bi + 1] - rx) >> 3, dry = (fy_[bj + 1][bi + 1] - ry) >> 3;
      for (int py = 0; py < 8; py++) {
        int32_t sx = lx, sy = ly;
        int32_t stx = (rx - lx) >> 3, sty = (ry - ly) >> 3;
        uint8_t *o = buf + (bj * 8 + py) * LW + bi * 8;
        if (smooth) {
          for (int px = 0; px < 8; px++) {        // bilinear: no blocky zoom artefacts
            int32_t cx = sx < 0 ? 0 : (sx > maxX ? maxX : sx);
            int32_t cy = sy < 0 ? 0 : (sy > maxY ? maxY : sy);
            const uint8_t *s = back + (cy >> 16) * LW + (cx >> 16);
            int fx = (cx >> 8) & 255, fy = (cy >> 8) & 255;
            int top = (s[0] << 8) + (s[1] - s[0]) * fx;
            int bot = (s[LW] << 8) + (s[LW + 1] - s[LW]) * fx;
            int v = ((top << 8) + (bot - top) * fy) >> 16;
            v -= fade + (v >> 6);
            o[px] = (uint8_t)(v < 0 ? 0 : v);
            sx += stx; sy += sty;
          }
        } else {
          for (int px = 0; px < 8; px++) {
            int32_t cx = sx < 0 ? 0 : (sx > maxX ? maxX : sx);
            int32_t cy = sy < 0 ? 0 : (sy > maxY ? maxY : sy);
            int v = back[(cy >> 16) * LW + (cx >> 16)] - fade;
            o[px] = (uint8_t)(v < 0 ? 0 : v);
            sx += stx; sy += sty;
          }
        }
        lx += dlx; ly += dly; rx += drx; ry += dry;
      }
    }
}

void palSet(int i, uint8_t r, uint8_t g, uint8_t b) {
  s_rgb[i][0] = r; s_rgb[i][1] = g; s_rgb[i][2] = b;
  uint32_t v = sw565(r, g, b);
  s_pal[i] = v | (v << 16);
}

void palCosine(float ar, float ag, float ab, float br, float bg, float bb,
               float cr, float cg, float cb, float dr, float dg, float db, float shift, float gamma) {
  for (int i = 0; i < 256; i++) {
    float t = i / 255.f;
    float ramp = t < 0.16f ? t / 0.16f : 1.f;
    if (gamma != 1.f) ramp *= powf(t, gamma - 1.f) > 1.f ? 1.f : powf(t, gamma - 1.f);
    float r = ar + br * cosf(6.2831853f * (cr * t + dr + shift));
    float g = ag + bg * cosf(6.2831853f * (cg * t + dg + shift));
    float b = ab + bb * cosf(6.2831853f * (cb * t + db + shift));
    palSet(i, (uint8_t)(clampf(r * ramp, 0, 1) * 255), (uint8_t)(clampf(g * ramp, 0, 1) * 255),
           (uint8_t)(clampf(b * ramp, 0, 1) * 255));
  }
}

void palFlash(float amt) {
  if (amt <= 0.01f) return;
  amt = clampf(amt, 0, 1);
  for (int i = 1; i < 256; i++) {
    uint8_t r = s_rgb[i][0], g = s_rgb[i][1], b = s_rgb[i][2];
    palSet(i, (uint8_t)(r + (255 - r) * amt), (uint8_t)(g + (255 - g) * amt), (uint8_t)(b + (255 - b) * amt));
  }
}

void plot(int x, int y, uint8_t v) {
  if ((unsigned)x >= (unsigned)LW || (unsigned)y >= (unsigned)LH) return;
  uint8_t &p = buf[y * LW + x];
  if (v > p) p = v;
}
void addp(int x, int y, int v) {
  if ((unsigned)x >= (unsigned)LW || (unsigned)y >= (unsigned)LH) return;
  uint8_t &p = buf[y * LW + x];
  int n = p + v; p = (uint8_t)(n > 255 ? 255 : (n < 0 ? 0 : n));
}
void line(int x0, int y0, int x1, int y1, uint8_t v) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, guard = 400;
  while (guard--) {
    plot(x0, y0, v);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void disc(int cx, int cy, int r, uint8_t v) {
  if (r <= 0) { plot(cx, cy, v); return; }
  for (int y = -r; y <= r; y++) {
    int yy = cy + y;
    if ((unsigned)yy >= (unsigned)LH) continue;
    int w = (int)sqrtf((float)(r * r - y * y));
    int x0 = cx - w < 0 ? 0 : cx - w, x1 = cx + w >= LW ? LW - 1 : cx + w;
    uint8_t *row = buf + yy * LW;
    for (int x = x0; x <= x1; x++) if (v > row[x]) row[x] = v;
  }
}
void ring(int cx, int cy, int r, uint8_t v) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    plot(cx + x, cy + y, v); plot(cx - x, cy + y, v); plot(cx + x, cy - y, v); plot(cx - x, cy - y, v);
    plot(cx + y, cy + x, v); plot(cx - y, cy + x, v); plot(cx + y, cy - x, v); plot(cx - y, cy - x, v);
    y++;
    if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
  }
}

}  // namespace fx
