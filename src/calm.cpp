// ============================================================
//  SYNAPSE — CALM: the physics room
//  Slow, soft, glowing, deeply interactive. Everything still listens,
//  but through aud::calm (slow attack, slower release) so sound nudges
//  instead of shoves. B = next room, long-press = this room's variant.
//    FLOW    glow fluid; sound sets viscosity + colour   (LP: neon/honey/mercury)
//    SPLASH  tap the pool, drop water from your finger   (LP: gentle rain)
//    SAND    sand-art between glass panes, tilt to pour  (LP: new picture)
//    WAVES   wave-machine tank; tilt sloshes, sound swells (LP: night glow)
//    BUBBLES blacklight aquarium, bubbles, a jellyfish   (LP: UV on/off)
// ============================================================
#include "app.h"
#include "audio.h"
#include "fx.h"
#include "wire.h"
#include <string.h>

enum Room : uint8_t { R_FLOW = 0, R_SPLASH, R_SAND, R_WAVES, R_BUBBLES, R_COUNT };
static uint8_t s_room = R_FLOW;
static uint8_t s_var[R_COUNT] = {0, 0, 0, 0, 1};
static float s_gx = 0, s_gy = 0.3f;                 // smoothed gravity
static uint32_t s_hapAt = 0;
static void softHap(uint8_t lv, uint16_t ms) {
  if (millis() - s_hapAt < 110) return;
  s_hapAt = millis(); hap(lv, ms);
}
static inline float frand() { return (esp_random() & 0xFFFF) / 65535.f; }
// Every room uses the same calibrated gravity (s_gx, s_gy). Rooms that need a floor (pool, tank)
// get a gentle downward floor ONLY while the device is close to its calibrated neutral.
static inline float flatFloor() { float m = sqrtf(s_gx * s_gx + s_gy * s_gy); return 0.35f * clampf(1.f - m * 2.5f, 0.f, 1.f); }

// ---------- palette gradient helper: stops (index, rgb565) ----------
static void grad(const int *at, const uint16_t *col, int n) {
  for (int s = 0; s < n - 1; s++) {
    int a = at[s], b = at[s + 1];
    uint8_t r0 = (col[s] >> 11) << 3, g0 = ((col[s] >> 5) & 63) << 2, b0 = (col[s] & 31) << 3;
    uint8_t r1 = (col[s + 1] >> 11) << 3, g1 = ((col[s + 1] >> 5) & 63) << 2, b1 = (col[s + 1] & 31) << 3;
    for (int i = a; i <= b && i < 256; i++) {
      float u = b > a ? (float)(i - a) / (b - a) : 0.f;
      fx::palSet(i, (uint8_t)(r0 + (r1 - r0) * u), (uint8_t)(g0 + (g1 - g0) * u), (uint8_t)(b0 + (b1 - b0) * u));
    }
  }
}

// ============================================================
//  particle fluid (position-based, shared by FLOW and SPLASH)
// ============================================================
static const int FN = 420;
static const float FX0 = 1.f, FX1 = 159.f, FY0 = 8.f, FY1 = 112.f;
static float px[FN], py[FN], pvx[FN], pvy[FN], opx[FN], opy[FN];
static int s_fn = 0;
static int16_t s_head[40 * 30], s_next[FN];
struct FluidP { float h, stiff, visc, cohes, grav; };

static void fluidSeed(int n, float yTop) {
  s_fn = n;
  float h = 3.6f;
  int cols = (int)((FX1 - FX0) / h);
  for (int i = 0; i < n; i++) {
    px[i] = FX0 + 1.f + (i % cols) * h + frand() * 0.5f;
    py[i] = FY1 - 1.f - (i / cols) * h;
    if (py[i] < yTop) py[i] = yTop + frand() * 10.f;
    pvx[i] = pvy[i] = 0;
  }
}
static inline int cellOf(float x, float y) {
  int cx = (int)(x * 0.25f), cy = (int)(y * 0.25f);
  cx = cx < 0 ? 0 : (cx > 39 ? 39 : cx); cy = cy < 0 ? 0 : (cy > 29 ? 29 : cy);
  return cy * 40 + cx;
}
static void fluidStep(const FluidP &P, float gx, float gy) {
  const float dt = 1.f / 60.f, h = P.h, h2 = h * h;
  for (int sub = 0; sub < 2; sub++) {
    for (int i = 0; i < s_fn; i++) {
      pvx[i] += gx * P.grav * dt; pvy[i] += gy * P.grav * dt;
      opx[i] = px[i]; opy[i] = py[i];
      px[i] += pvx[i] * dt; py[i] += pvy[i] * dt;
    }
    for (int c = 0; c < 1200; c++) s_head[c] = -1;
    for (int i = 0; i < s_fn; i++) { int c = cellOf(px[i], py[i]); s_next[i] = s_head[c]; s_head[c] = (int16_t)i; }
    // relax: push apart inside h, pull together near h (surface tension)
    for (int it = 0; it < 2; it++)
    for (int i = 0; i < s_fn; i++) {
      int c = cellOf(px[i], py[i]), cx = c % 40, cy = c / 40;
      for (int oy = -1; oy <= 1; oy++) {
        int yy = cy + oy; if (yy < 0 || yy > 29) continue;
        for (int ox = -1; ox <= 1; ox++) {
          int xx = cx + ox; if (xx < 0 || xx > 39) continue;
          for (int j = s_head[yy * 40 + xx]; j >= 0; j = s_next[j]) {
            if (j <= i) continue;
            float dx = px[j] - px[i], dy = py[j] - py[i], d2 = dx * dx + dy * dy;
            if (d2 >= h2 * 1.69f || d2 < 1e-6f) continue;
            float d = sqrtf(d2), nx = dx / d, ny = dy / d;
            float push = d < h ? (h - d) * P.stiff * 0.5f : -(d - h) * P.cohes * 0.5f;
            px[i] -= nx * push; py[i] -= ny * push; px[j] += nx * push; py[j] += ny * push;
          }
        }
      }
    }
    for (int i = 0; i < s_fn; i++) {
      if (px[i] < FX0) px[i] = FX0 + frand() * 0.2f; else if (px[i] > FX1) px[i] = FX1 - frand() * 0.2f;
      if (py[i] < FY0) py[i] = FY0 + frand() * 0.2f; else if (py[i] > FY1) py[i] = FY1 - frand() * 0.2f;
      pvx[i] = (px[i] - opx[i]) / dt * 0.995f; pvy[i] = (py[i] - opy[i]) / dt * 0.995f;
    }
    // XSPH viscosity: neighbours agree on velocity (thick = honey, thin = water)
    for (int i = 0; i < s_fn; i++) {
      int c = cellOf(px[i], py[i]), cx = c % 40, cy = c / 40;
      float ax = 0, ay = 0;
      for (int oy = -1; oy <= 1; oy++) {
        int yy = cy + oy; if (yy < 0 || yy > 29) continue;
        for (int ox = -1; ox <= 1; ox++) {
          int xx = cx + ox; if (xx < 0 || xx > 39) continue;
          for (int j = s_head[yy * 40 + xx]; j >= 0; j = s_next[j]) {
            float dx = px[j] - px[i], dy = py[j] - py[i], d2 = dx * dx + dy * dy;
            if (d2 >= h2 || j == i) continue;
            float w = 1.f - d2 / h2;
            ax += (pvx[j] - pvx[i]) * w; ay += (pvy[j] - pvy[i]) * w;
          }
        }
      }
      pvx[i] += ax * P.visc; pvy[i] += ay * P.visc;
    }
  }
}
// finger: stir (drag) and poke (outward shove)
static void fluidStir(float fx_, float fy_, float dx, float dy, float r) {
  for (int i = 0; i < s_fn; i++) {
    float ex = px[i] - fx_, ey = py[i] - fy_, d2 = ex * ex + ey * ey;
    if (d2 > r * r) continue;
    float w = 1.f - sqrtf(d2) / r;
    pvx[i] += dx * 40.f * w; pvy[i] += dy * 40.f * w;
  }
}
static void fluidShove(float fx_, float fy_, float r, float str) {
  for (int i = 0; i < s_fn; i++) {
    float ex = px[i] - fx_, ey = py[i] - fy_, d2 = ex * ex + ey * ey;
    if (d2 > r * r || d2 < 0.01f) continue;
    float d = sqrtf(d2), w = 1.f - d / r;
    pvx[i] += ex / d * str * w; pvy[i] += ey / d * str * w - str * 0.35f * w;
  }
}
static int8_t s_ker[11][11];
static void splat(uint8_t base) {
  for (int i = 0; i < s_fn; i++) {
    int cx = (int)px[i], cy = (int)py[i];
    float sp = fabsf(pvx[i]) + fabsf(pvy[i]);                   // moving liquid glows, still liquid rests
    int gain = base + (sp > 120.f ? 36 : (int)(sp * 0.3f));
    if (cx < 5 || cy < 5 || cx > fx::LW - 6 || cy > fx::LH - 6) {
      for (int ky = 0; ky < 11; ky++) for (int kx = 0; kx < 11; kx++) fx::addp(cx + kx - 5, cy + ky - 5, (s_ker[ky][kx] * gain) >> 6);
      continue;
    }
    for (int ky = 0; ky < 11; ky++) {
      uint8_t *row = fx::buf + (cy + ky - 5) * fx::LW + cx - 5;
      for (int kx = 0; kx < 11; kx++) { int v = row[kx] + ((s_ker[ky][kx] * gain) >> 6); row[kx] = (uint8_t)(v > 255 ? 255 : v); }
    }
  }
}
static void softBackground(int lo, int hi, float drift) {
  uint8_t t1 = (uint8_t)(g_t * 9.f * drift), t2 = (uint8_t)(g_t * -6.f * drift);
  auto rows_ = [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
      uint8_t *row = fx::buf + y * fx::LW;
      int base = lo + (hi - lo) * y / fx::LH;
      int ry = fx::sn[(uint8_t)(y * 2 + t2)];
      for (int x = 0; x < fx::LW; x++) {
        int v = base + ((fx::sn[(uint8_t)(x * 2 + t1)] + ry) >> 5);
        row[x] = (uint8_t)(v < 0 ? 0 : v);
      }
    }
  };
  fx::parallel(rows_);
}

// ---------- touch tracking (lores coords) ----------
static bool s_tDown = false;
static float s_tx = 0, s_ty = 0, s_tdx = 0, s_tdy = 0;
static void readTouch() {
  auto td = M5.Touch.getDetail();
  bool on = td.isPressed() && td.y >= 14 && td.y < H - 14;
  float x = td.x * 0.5f, y = td.y * 0.5f;
  if (on && s_tDown) { s_tdx = x - s_tx; s_tdy = y - s_ty; } else { s_tdx = s_tdy = 0; }
  if (on) { s_tx = x; s_ty = y; }
  s_tDown = on;
}

// ============================================================
//  FLOW — glowing fluid; sound thins it and tints it
// ============================================================
static void roomFlow() {
  uint8_t v = s_var[R_FLOW];
  float c = aud::calm;
  FluidP P;
  if (v == 0) P = {4.0f, 0.55f, 0.06f + 0.10f * (1.f - c), 0.04f, 190.f};          // neon: sound makes it runnier
  else if (v == 1) P = {4.0f, 0.45f, 0.20f + 0.10f * (1.f - c), 0.06f, 120.f};     // honey
  else P = {3.8f, 0.6f, 0.08f, 0.30f + 0.1f * c, 210.f};                            // mercury: strong surface tension
  if (s_tDown) fluidStir(s_tx, s_ty, s_tdx, s_tdy, 14.f);
  float gx = s_gx, gy = s_gy;
  if (fabsf(gx) + fabsf(gy) < 0.15f) { gx = sinf(g_t * 0.21f) * 0.12f; gy = cosf(g_t * 0.17f) * 0.12f; }  // zero-g drift
  fluidStep(P, gx, gy);
  softBackground(0, 22, 0.6f);
  splat(v == 2 ? 80 : 72);
  float hue = g_hue + aud::centroid * 90.f + c * 40.f;
  if (v == 0) {
    int at[] = {0, 34, 70, 86, 120, 200, 255};
    uint16_t col[] = {rgb565(4, 2, 14), hsv565(hue + 200.f, 0.8f, 0.2f), hsv565(hue + 200.f, 0.7f, 0.12f), hsv565(hue, 0.6f, 1.f),
                      hsv565(hue + 30.f, 0.9f, 0.55f + c * 0.25f), hsv565(hue + 60.f, 0.8f, 0.75f), hsv565(hue + 80.f, 0.3f, 1.f)};
    grad(at, col, 7);
  } else if (v == 1) {
    int at[] = {0, 34, 72, 90, 140, 255};
    uint16_t col[] = {rgb565(10, 4, 2), rgb565(30, 12, 4), rgb565(20, 8, 2), rgb565(255, 220, 120), rgb565(210, 120, 20), rgb565(255, 190, 60)};
    grad(at, col, 6);
  } else {
    int at[] = {0, 34, 70, 84, 96, 130, 190, 255};
    uint16_t col[] = {rgb565(2, 4, 10), hsv565(hue + 180.f, 0.5f, 0.14f), rgb565(6, 8, 16), rgb565(250, 250, 255), rgb565(90, 100, 120),
                      rgb565(170, 180, 200), rgb565(80, 90, 110), rgb565(230, 235, 255)};
    grad(at, col, 8);
  }
  fx::present(canvas);
  // gentle touch of the glass when it sloshes
  float e = 0;
  for (int i = 0; i < s_fn; i += 7) e += fabsf(pvx[i]) + fabsf(pvy[i]);
  if (e > 2600.f) hapRumble(clampf((e - 2600.f) / 5000.f, 0.08f, 0.32f), 8.f, 0.35f);   // the liquid's weight
}

// ============================================================
//  SPLASH — a pool you can drop water into
// ============================================================
struct Drop { float x, y, vx, vy; uint8_t live, kind; };   // kind 0 falling blob, 1 spray
static Drop s_drop[72];
struct Rip { float x, y, r, a; };
static Rip s_rip[8];
static float s_rainT = 0;
static void spawnDrop(float x, float y, float vx, float vy, uint8_t kind) {
  for (auto &d : s_drop) if (!d.live) { d = {x, y, vx, vy, 1, kind}; return; }
}
static void ripple(float x, float y) {
  for (auto &r : s_rip) if (r.a <= 0) { r = {x, y, 2.f, 1.f}; return; }
}
static void splashAt(float x, float y, float power) {
  fluidShove(x, y, 11.f, 60.f * power);
  for (int k = 0; k < (int)(6 + power * 8); k++)
    spawnDrop(x + (frand() - 0.5f) * 6.f, y - 2.f, (frand() - 0.5f) * 70.f * power, -40.f - frand() * 80.f * power, 1);
  ripple(x * 2.f, y * 2.f);
  softHap((uint8_t)(40 + power * 30.f), 14);
}
static void roomSplash() {
  FluidP P = {3.6f, 0.6f, 0.05f, 0.03f, 200.f};
  float gx = s_gx, gy = s_gy + flatFloor();          // honest gravity, soft floor when flat
  if (s_tDown && (fabsf(s_tdx) + fabsf(s_tdy)) > 0.5f) fluidStir(s_tx, s_ty, s_tdx, s_tdy, 10.f);
  fluidStep(P, gx, gy);
  // rain (long-press), and the room breathes a drop now and then with the sound
  s_rainT -= g_dt;
  if (s_var[R_SPLASH] && s_rainT < 0) { s_rainT = 0.12f + frand() * 0.25f; spawnDrop(4.f + frand() * 152.f, 9.f, 0, 30.f, 0); }
  if (aud::onset > 0.5f && frand() < 0.3f) spawnDrop(10.f + frand() * 140.f, 9.f, 0, 20.f, 0);

  softBackground(2, 24, 0.4f);
  splat(76);
  for (auto &d : s_drop) {
    if (!d.live) continue;
    d.vy += 260.f * g_dt; d.x += d.vx * g_dt; d.y += d.vy * g_dt;
    int ix = (int)d.x, iy = (int)d.y;
    if (ix < 0 || ix >= fx::LW || iy >= (int)FY1) { d.live = 0; continue; }
    bool wet = iy > 8 && fx::buf[iy * fx::LW + ix] > 110;
    if (wet && d.vy > 0) {
      if (d.kind == 0) splashAt(d.x, d.y, 0.6f + d.vy / 300.f);
      d.live = 0; continue;
    }
    if (d.kind == 0) fx::disc(ix, iy, 2, 230); else fx::plot(ix, iy, 255);
  }
  float hue = 190.f + sinf(g_t * 0.05f) * 20.f + aud::centroid * 30.f;
  int at[] = {0, 34, 80, 100, 150, 230, 255};
  uint16_t col[] = {rgb565(2, 4, 12), hsv565(hue + 40.f, 0.6f, 0.16f), hsv565(hue, 0.7f, 0.12f), hsv565(hue - 10.f, 0.35f, 1.f),
                    hsv565(hue, 0.85f, 0.55f + aud::calm * 0.2f), hsv565(hue - 20.f, 0.7f, 0.8f), rgb565(240, 250, 255)};
  grad(at, col, 7);
  fx::present(canvas);
  for (auto &r : s_rip) {
    if (r.a <= 0) continue;
    r.r += g_dt * 60.f; r.a -= g_dt * 1.4f;
    canvas.drawEllipse((int)r.x, (int)r.y, (int)r.r, (int)(r.r * 0.3f), hsv565(hue - 20.f, 0.25f, 0.4f + r.a * 0.6f));
  }
}

// ============================================================
//  SAND — sand art between glass panes
// ============================================================
static const int SW = 160, SH = 106, SY0 = 7;       // grid sits between the chrome bars
static uint8_t *s_sand = nullptr;                  // [mat:4][parity:1][shade:3]
static uint8_t s_par = 0;
static const uint8_t M_WATER = 0, M_BUBBLE = 14, M_WALL = 15;
static float s_sandHue = 0;
static inline uint8_t mat(uint8_t c) { return c >> 4; }
static void sandPicture() {
  static const int NCOL = 6;
  int order[NCOL] = {1, 2, 3, 4, 5, 6};
  for (int i = NCOL - 1; i > 0; i--) { int j = esp_random() % (i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
  s_sandHue = frand() * 360.f;
  float ph[3] = {frand() * 6.f, frand() * 6.f, frand() * 6.f};
  for (int y = 0; y < SH; y++)
    for (int x = 0; x < SW; x++) {
      uint8_t m = M_WATER;
      float top = SH * 0.42f + sinf(x * 0.05f + ph[0]) * 6.f;
      if (y > top) {
        float depth = (y - top) + sinf(x * 0.09f + ph[1]) * 3.f + sinf(x * 0.021f + ph[2]) * 5.f;
        m = (uint8_t)order[((int)(depth / 7.f)) % NCOL];
      } else if (y < 6 && (esp_random() % 9) == 0) m = M_BUBBLE;
      s_sand[y * SW + x] = (uint8_t)((m << 4) | (esp_random() & 7));
    }
}
static void sandStep(int dx, int dy, float p) {
  s_par ^= 8;
  uint32_t pr = (uint32_t)(p * 65535.f), ps = (uint32_t)(p * 0.55f * 65535.f);
  int ys = dy >= 0 ? SH - 1 : 0, ye = dy >= 0 ? -1 : SH, yi = dy >= 0 ? -1 : 1;
  int xs = dx >= 0 ? SW - 1 : 0, xe = dx >= 0 ? -1 : SW, xi = dx >= 0 ? -1 : 1;
  // slide directions: rotate the fall direction by +/-45 degrees
  int s1x, s1y, s2x, s2y;
  if (dx == 0 || dy == 0) { s1x = dx - dy; s1y = dy + dx; s2x = dx + dy; s2y = dy - dx; }
  else { s1x = dx; s1y = 0; s2x = 0; s2y = dy; }
  for (int y = ys; y != ye; y += yi)
    for (int x = xs; x != xe; x += xi) {
      uint8_t c = s_sand[y * SW + x];
      uint8_t m = mat(c);
      if (m == M_WATER || m == M_WALL || (c & 8) == s_par) continue;
      if (m == M_BUBBLE) {                           // bubbles rise; sand trickles through them
        int nx = x - dx + ((esp_random() & 3) == 0 ? (int)(esp_random() % 3) - 1 : 0), ny = y - dy;
        if (nx < 0 || ny < 0 || nx >= SW || ny >= SH) continue;
        uint8_t o = s_sand[ny * SW + nx];
        if (mat(o) == M_WATER || (mat(o) < M_BUBBLE && (esp_random() & 0xFFFF) < ps)) {
          s_sand[ny * SW + nx] = (uint8_t)((c & ~8) | s_par);
          s_sand[y * SW + x] = o;
        }
        continue;
      }
      if ((esp_random() & 0xFFFF) > pr) continue;
      int tx = x + dx, ty = y + dy;
      if (tx >= 0 && ty >= 0 && tx < SW && ty < SH && mat(s_sand[ty * SW + tx]) == M_WATER) {
        s_sand[ty * SW + tx] = (uint8_t)((c & ~8) | s_par); s_sand[y * SW + x] = (uint8_t)(M_WATER << 4 | (esp_random() & 7));
        continue;
      }
      if ((esp_random() & 0xFFFF) > ps + ps) continue;
      bool first = esp_random() & 1;
      for (int k = 0; k < 2; k++) {
        int sx = (k ^ first) ? s1x : s2x, sy = (k ^ first) ? s1y : s2y;
        tx = x + sx; ty = y + sy;
        if (tx < 0 || ty < 0 || tx >= SW || ty >= SH) continue;
        if (mat(s_sand[ty * SW + tx]) == M_WATER) {
          s_sand[ty * SW + tx] = (uint8_t)((c & ~8) | s_par); s_sand[y * SW + x] = (uint8_t)(M_WATER << 4 | (esp_random() & 7));
          break;
        }
      }
    }
}
static void roomSand() {
  float gm = sqrtf(s_gx * s_gx + s_gy * s_gy);
  if (gm > 0.22f) {
    float a = atan2f(s_gy, s_gx);
    static int oct = 2;                                   // keep the current direction until the tilt clearly changes
    float ca = oct * 0.785398f, da = a - ca;
    while (da > 3.14159f) da -= 6.28318f; while (da < -3.14159f) da += 6.28318f;
    if (fabsf(da) > 0.52f) oct = ((int)lroundf(a / 0.785398f) + 8) & 7;
    static const int8_t DX[8] = {1, 1, 0, -1, -1, -1, 0, 1}, DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    float p = clampf(0.45f + gm * 0.45f + aud::calm * 0.1f, 0.f, 0.97f);
    sandStep(DX[oct], DY[oct], p);
    sandStep(DX[oct], DY[oct], p);
  }
  // fingertip pushes grains aside
  if (s_tDown) {
    int fx_ = (int)s_tx, fy_ = (int)s_ty - SY0;
    for (int k = 0; k < 40; k++) {
      int ox = (int)(esp_random() % 11) - 5, oy = (int)(esp_random() % 11) - 5;
      int x = fx_ + ox, y = fy_ + oy;
      if (x < 0 || y < 0 || x >= SW || y >= SH || ox * ox + oy * oy > 25) continue;
      uint8_t c = s_sand[y * SW + x];
      if (mat(c) == M_WATER || mat(c) == M_WALL) continue;
      float d = sqrtf((float)(ox * ox + oy * oy)) + 0.5f;
      int tx = fx_ + (int)(ox / d * 7.f), ty = fy_ + (int)(oy / d * 7.f);
      if (tx < 0 || ty < 0 || tx >= SW || ty >= SH) continue;
      if (mat(s_sand[ty * SW + tx]) == M_WATER) { s_sand[ty * SW + tx] = c; s_sand[y * SW + x] = (uint8_t)(M_WATER << 4); }
    }
  }
  if (aud::onset > 0.6f) {                          // a sound lets a bubble go from the bottom
    int x = esp_random() % SW;
    for (int y = SH - 1; y > 0; y--) if (mat(s_sand[y * SW + x]) == M_WATER) { s_sand[y * SW + x] = (uint8_t)(M_BUBBLE << 4); break; }
  }
  // render: 16 materials x 16 shades; water shimmers
  uint8_t t1 = (uint8_t)(g_t * 30.f), t2 = (uint8_t)(g_t * -21.f);
  int shim = 1 + (int)(aud::calm * 3.f);
  auto rows_ = [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
      uint8_t *row = fx::buf + y * fx::LW;
      int sy = y - SY0;
      if (sy < 0 || sy >= SH) { memset(row, 250, fx::LW); continue; }
      const uint8_t *src = s_sand + sy * SW;
      for (int x = 0; x < fx::LW; x++) {
        uint8_t c = src[x], m = c >> 4;
        if (m == M_WATER) {
          int w = (fx::sn[(uint8_t)(x * 3 + sy * 2 + t1)] + fx::sn[(uint8_t)(x * 2 - sy * 3 + t2)]) >> 6;
          row[x] = (uint8_t)(4 + (w > 0 ? w * shim / 2 : 0) + (sy >> 4));
        } else row[x] = (uint8_t)(m * 16 + 4 + (c & 7));
      }
    }
  };
  fx::parallel(rows_);
  // palette: water, then six sand colours (blacklight pastel), bubbles
  float wh = 230.f + aud::centroid * 40.f + sinf(g_t * 0.04f) * 20.f;
  for (int i = 0; i < 16; i++) { uint16_t c = hsv565(wh, 0.7f, 0.07f + i * 0.02f); fx::palSet(i, (c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3); }
  for (int m = 1; m <= 6; m++) {
    float hh = s_sandHue + m * 57.f;
    float sat = (m == 3) ? 0.1f : 0.65f, val = (m == 5) ? 0.25f : 0.85f;
    for (int s = 0; s < 16; s++) {
      uint16_t c = hsv565(hh + s * 1.5f, sat, clampf(val * (0.7f + s * 0.025f), 0, 1));
      fx::palSet(m * 16 + s, (c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3);
    }
  }
  for (int s = 0; s < 16; s++) fx::palSet(M_BUBBLE * 16 + s, 170, 210, 240);
  fx::palSet(250, 18, 12, 10);
  fx::present(canvas);
  // the glass frame
  canvas.drawRect(0, 13, W, H - 26, rgb565(60, 44, 30));
  canvas.drawRect(1, 14, W - 2, H - 28, rgb565(110, 84, 58));
  canvas.drawLine(40, 16, 10, 60, rgb565(80, 90, 110));
  canvas.drawLine(48, 16, 14, 74, rgb565(50, 56, 70));
}

// ============================================================
//  WAVES — a wave-machine tank: tilt it, touch it, hum at it
// ============================================================
static float s_h[160], s_v[160], s_boatX = 80.f, s_boatV = 0;
static Drop s_spray[40];
// weather: the room's sound becomes the sky
static float s_cloud = 0, s_rainAmt = 0, s_wind = 0, s_cloudPos = 0, s_flash = 0, s_boltT = 0;
static uint32_t s_boltAt = 0;
static int s_boltX = 0;
struct Rain { float x, y; uint8_t live; };
static Rain s_rain[90];
static void roomWaves() {
  float dt = g_dt;
  bool night = s_var[R_WAVES];
  float gyE = fmaxf(0.3f, s_gy + flatFloor());       // the surface lies perpendicular to gravity
  float slope = clampf(s_gx / gyE, -0.7f, 0.7f);
  float c = aud::calm;
  // wave machine paddle on the left; its stroke follows the sound
  static float padPh = 0;
  padPh += dt * (0.9f + c * 0.6f);
  // touch: poke the surface, or drag through it
  if (s_tDown) {
    int x = (int)s_tx;
    float surf = 66.f - s_h[x < 0 ? 0 : (x > 159 ? 159 : x)];
    if (s_ty > surf - 6.f) {
      for (int k = -5; k <= 5; k++) {
        int xx = x + k; if (xx < 0 || xx > 159) continue;
        s_v[xx] += (-s_tdy * 12.f - fabsf(s_tdx) * 3.f - 4.f * dt * 60.f * (s_ty > surf ? 0.3f : 0.f)) * (1.f - fabsf(k) / 6.f);
      }
      if (fabsf(s_tdx) + fabsf(s_tdy) > 1.5f && frand() < 0.5f)
        for (auto &d : s_spray) if (!d.live) { d = {s_tx * 2.f, surf * 2.f, s_tdx * 30.f, -60.f - frand() * 60.f, 1, 1}; break; }
    }
  }
  for (int sub = 0; sub < 3; sub++) {
    float sdt = dt / 3.f, mean = 0;
    s_v[0] += sinf(padPh * 6.2831853f * 0.5f) * (0.6f + c * 3.f) * sdt * 60.f;
    for (int x = 0; x < 160; x++) {
      float l = s_h[x > 0 ? x - 1 : 0], r = s_h[x < 159 ? x + 1 : 159];
      float rest = slope * (x - 80) * 0.55f;
      s_v[x] += ((l + r - 2.f * s_h[x]) * 900.f + (rest - s_h[x]) * 1.2f - s_v[x] * 0.35f) * sdt;
    }
    for (int x = 0; x < 160; x++) { s_h[x] += s_v[x] * sdt; mean += s_h[x] - slope * (x - 80) * 0.55f; }
    mean /= 160.f;
    for (int x = 0; x < 160; x++) s_h[x] -= mean;
  }
  if (frand() < c * aud::treble * 0.8f) s_v[esp_random() % 160] += (frand() - 0.5f) * 20.f;   // breath on the water
  { float we = fabsf(s_v[2]) + fabsf(s_v[157]); if (we > 40.f) hapRumble(clampf((we - 40.f) / 160.f, 0.06f, 0.4f), 2.5f, 0.15f); }   // swells meeting the glass
  // weather follows the mic: quiet = clear, sustained sound gathers clouds, then rain; loud hits in a storm = lightning
  s_cloud += (clampf(c * 1.5f, 0.f, 1.f) - s_cloud) * clampf(dt * 0.5f, 0.f, 1.f);
  s_rainAmt += (clampf((c - 0.45f) * 2.5f, 0.f, 1.f) - s_rainAmt) * clampf(dt * 0.4f, 0.f, 1.f);
  s_wind += (clampf(aud::treble * 1.5f + c * 0.3f, 0.f, 1.f) - s_wind) * clampf(dt * 0.8f, 0.f, 1.f);
  s_cloudPos += dt * (4.f + s_wind * 30.f);
  s_boatV += s_wind * 3.f * dt;
  if (aud::onset > 0.7f && s_rainAmt > 0.35f && millis() - s_boltAt > 1400) {
    s_boltAt = millis(); s_boltT = 0.25f; s_flash = 1.f; s_boltX = 30 + (int)(esp_random() % 260);
    hapGesture(HG_THUNDER);
  }
  s_flash = fmaxf(0.f, s_flash - dt * 4.f); s_boltT -= dt;
  if (frand() < s_rainAmt * 0.9f)
    for (int k = 0; k < 1 + (int)(s_rainAmt * 3.f); k++)
      for (auto &r : s_rain) if (!r.live) { r = {frand() * 340.f - 20.f, 14.f, 1}; break; }

  // render
  uint8_t t1 = (uint8_t)(g_t * 26.f), t2 = (uint8_t)(g_t * -17.f), tm = (uint8_t)(g_t * 90.f);
  const int moonX = 118;
  auto rows_ = [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
      uint8_t *row = fx::buf + y * fx::LW;
      for (int x = 0; x < fx::LW; x++) {
        float sy = 66.f - s_h[x];
        int v;
        if (y < sy) {
          v = 2 + y / 4;                                               // sky 0..19
          if (s_cloud > 0.05f && y < 56) {                             // clouds 20..39, drifting with the wind
            int cp = (int)s_cloudPos;
            int cc = fx::sn[(uint8_t)(x * 2 + cp + y * 3)] + fx::sn[(uint8_t)(x * 5 - y * 7 + cp * 2)] + fx::sn[(uint8_t)(x + y * 11 - cp)];
            int thr = 200 - (int)(s_cloud * 300.f) + y * 2;
            if (cc > thr) { v = 20 + (cc - thr) / 5; if (v > 39) v = 39; }
          }
          if (night && ((x * 73 + y * 151) % 97) == 0 && y < 50) v = 40 + (fx::sn[(uint8_t)(x * 9 + tm)] > 60 ? 10 : 0);
          if (night) { int dx = x - moonX, dy = y - 24; if (dx * dx + dy * dy < 36) v = 60; }
        } else {
          int d = y - (int)sy;
          if (d < 1) v = 200 + (int)clampf(fabsf(s_v[x]) * 1.5f + fabsf(s_h[x < 159 ? x + 1 : 159] - s_h[x]) * 30.f, 0, 55);
          else {
            v = 72 + d * 2; if (v > 180) v = 180;
            int ca = fx::sn[(uint8_t)(x * 6 + d * 3 + t1)] + fx::sn[(uint8_t)(x * 4 - d * 7 + t2)] + (fx::sn[(uint8_t)(x * 9 + d * 11 - t2)] >> 1);
            int cw = 10 + (int)(c * 14.f) - d / 8;                    // caustic web: contours of two waves
            if (cw > 0 && abs(ca) < cw) v -= 18 - d / 6;
            if (v < 73) v = 73;
            if (night && abs(x - moonX) < 3 + d / 8 && ((x + y + (tm >> 3)) % 3) == 0 && fx::sn[(uint8_t)(y * 23 + tm + x * 7)] > 90) v = 205;
          }
        }
        row[x] = (uint8_t)v;
      }
    }
  };
  fx::parallel(rows_);
  float st = 1.f - s_rainAmt * 0.55f;                                   // storms dim the sky
  if (!night) {
    int at[] = {0, 19, 20, 39, 40, 60, 72, 180, 190, 200, 255};
    uint16_t col[] = {rgb565((uint8_t)(40 * st), (uint8_t)(70 * st), (uint8_t)(120 * st)), rgb565((uint8_t)(250 * st), (uint8_t)(170 * st), (uint8_t)(130 * st)),
                      rgb565((uint8_t)(150 * st), (uint8_t)(150 * st), (uint8_t)(170 * st)), rgb565((uint8_t)(255 * st), (uint8_t)(250 * st), (uint8_t)(245 * st)),
                      rgb565(250, 170, 130), rgb565(255, 240, 200), hsv565(185.f, 0.6f, 0.75f * (0.6f + 0.4f * st)), hsv565(215.f, 0.9f, 0.12f),
                      rgb565(200, 230, 255), rgb565(200, 240, 250), rgb565(255, 255, 255)};
    grad(at, col, 11);
  } else {
    float bh = 160.f + aud::centroid * 60.f;
    int at[] = {0, 19, 20, 39, 40, 50, 60, 72, 180, 190, 200, 255};
    uint16_t col[] = {rgb565(2, 2, 10), rgb565(14, 10, 40), rgb565(20, 20, 40), rgb565(90, 90, 130), rgb565(140, 140, 170), rgb565(255, 255, 230), rgb565(240, 240, 210),
                      hsv565(230.f, 0.8f, 0.2f), rgb565(0, 0, 4), rgb565(200, 210, 170), hsv565(bh, 0.9f, 0.35f), hsv565(bh, 0.4f, 1.f)};
    grad(at, col, 12);
  }
  fx::palFlash(s_flash * 0.45f);
  fx::present(canvas);
  // a little paper boat rides the swell
  int bx = (int)s_boatX;
  float sl = s_h[bx < 158 ? bx + 1 : 159] - s_h[bx > 0 ? bx - 1 : 0];
  s_boatV += (-sl * 40.f + slope * 12.f) * dt; s_boatV *= 0.985f;
  s_boatX = clampf(s_boatX + s_boatV * dt, 8.f, 152.f);
  if (s_boatX <= 8.f || s_boatX >= 152.f) s_boatV *= -0.5f;
  float by = (66.f - s_h[bx]) * 2.f, ang = atan2f(-sl, 2.f);
  float ca_ = cosf(ang), sa = sinf(ang), X = s_boatX * 2.f;
  auto P = [&](float lx, float ly, int &ox, int &oy) { ox = (int)(X + ca_ * lx - sa * ly); oy = (int)(by + sa * lx + ca_ * ly); };
  int x0, y0, x1, y1, x2, y2, x3, y3;
  P(-12, -3, x0, y0); P(12, -3, x1, y1); P(8, 3, x2, y2); P(-8, 3, x3, y3);
  uint16_t hull = night ? rgb565(170, 190, 200) : rgb565(245, 240, 225);
  canvas.fillTriangle(x0, y0, x1, y1, x2, y2, hull); canvas.fillTriangle(x0, y0, x2, y2, x3, y3, hull);
  P(0, -3, x0, y0); P(0, -18, x1, y1); P(9, -4, x2, y2);
  canvas.fillTriangle(x0, y0, x1, y1, x2, y2, night ? rgb565(120, 255, 220) : rgb565(255, 120, 110));
  // rain streaks: each drop that lands rings the surface
  for (auto &r : s_rain) {
    if (!r.live) continue;
    float wx = 40.f + s_wind * 160.f;
    r.x += wx * dt; r.y += 260.f * dt;
    int lx = (int)(r.x * 0.5f);
    if (lx >= 0 && lx < 160 && r.y * 0.5f > 66.f - s_h[lx]) { s_v[lx] -= 4.f; r.live = 0; continue; }
    if (r.y > H || r.x > W + 20) { r.live = 0; continue; }
    canvas.drawLine((int)r.x, (int)r.y, (int)(r.x - wx * 0.03f), (int)(r.y - 8.f), night ? rgb565(90, 110, 160) : rgb565(180, 200, 230));
  }
  if (s_boltT > 0) {                                        // lightning: jagged, forked, gone in a blink
    int x = s_boltX, y = 14;
    uint32_t sd = s_boltAt;
    int surf = (int)((66.f - s_h[(int)clampf((float)(s_boltX / 2), 0.f, 159.f)]) * 2.f);
    while (y < surf) {
      sd = sd * 1664525u + 1013904223u;
      int nx = x + (int)((sd >> 24) % 21) - 10, ny = y + 8 + (int)((sd >> 16) % 10);
      canvas.drawLine(x, y, nx, ny, rgb565(230, 220, 255)); canvas.drawLine(x + 1, y, nx + 1, ny, wire::LIME);
      if (((sd >> 8) & 7) == 0) canvas.drawLine(nx, ny, nx + 14, ny + 12, rgb565(160, 150, 220));
      x = nx; y = ny;
    }
  }
  for (auto &d : s_spray) {
    if (!d.live) continue;
    d.vy += 300.f * dt; d.x += d.vx * dt; d.y += d.vy * dt;
    int lx = (int)(d.x * 0.5f); lx = lx < 0 ? 0 : (lx > 159 ? 159 : lx);
    if (d.y * 0.5f > 66.f - s_h[lx] && d.vy > 0) { d.live = 0; continue; }
    canvas.fillCircle((int)d.x, (int)d.y, 1, night ? rgb565(120, 255, 210) : rgb565(230, 245, 255));
  }
}

// ============================================================
//  BUBBLES — blacklight aquarium
// ============================================================
struct Bub { float x, y, r, ph; uint8_t live; };
static Bub s_bub[56];
struct Pop { float x, y, r, a; };
static Pop s_pop[10];
struct Mote { float x, y, ph; };
static Mote s_pk[48];
static uint8_t *s_floor = nullptr;                // pebble layer, 160 x 22, palette index (0 = none)
static float s_emit = 0, s_holdEmit = 0;
static float s_jx = 90, s_jy = 150, s_jph = 0;
struct Fish { float x, y, vx, vy, head, size, hue, ph, dart; };
static Fish s_fish[6];
static float s_scareX = -1, s_scareY = -1; static uint32_t s_scareAt = 0;
static void fishInit() {
  static const float hues[6] = {178.f, 300.f, 95.f, 28.f, 178.f, 300.f};      // teal, plum, lime, orange...
  for (int i = 0; i < 6; i++) s_fish[i] = {40.f + frand() * 240.f, 50.f + frand() * 120.f, 0, 0, frand() * 6.28f, 9.f + frand() * 6.f, hues[i], frand() * 6.f, 0};
}
static void fishUpdate(float dt, bool uv) {
  float c = aud::calm;
  bool startle = aud::onset > 0.6f;
  for (auto &f : s_fish) {
    f.ph += dt * (6.f + f.dart * 20.f + c * 6.f);
    f.head += (sinf(f.ph * 0.13f + f.hue) * 0.9f) * dt;                        // idle wander
    float spd = 18.f + c * 22.f + f.dart * 110.f;
    // walls: turn back towards the middle of the tank
    float tx = 160.f - f.x, ty = 115.f - f.y;
    if (f.x < 40 || f.x > 280 || f.y < 44 || f.y > 180) { float ta = atan2f(ty, tx), d = ta - f.head; while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f; f.head += d * dt * 3.f; }
    // flee a tap / a loud sound; approach a held finger (curious), stop short of it
    if (s_scareX >= 0 && millis() - s_scareAt < 600) {
      float dx = f.x - s_scareX, dy = f.y - s_scareY, d2 = dx * dx + dy * dy;
      if (d2 < 90 * 90) { f.head = atan2f(dy, dx) + (frand() - 0.5f) * 0.6f; f.dart = 1.f; }
    }
    if (startle && frand() < 0.5f) { f.head += (frand() - 0.5f) * 3.f; f.dart = fmaxf(f.dart, 0.7f); }
    if (s_tDown) {
      float dx = s_tx * 2.f - f.x, dy = s_ty * 2.f - f.y, d = sqrtf(dx * dx + dy * dy);
      if (d > 26.f && d < 160.f && f.dart < 0.2f) { float ta = atan2f(dy, dx), dd = ta - f.head; while (dd > 3.14159f) dd -= 6.28318f; while (dd < -3.14159f) dd += 6.28318f; f.head += dd * dt * 1.5f; }
      if (d < 26.f) spd *= 0.2f;
    }
    f.dart = fmaxf(0.f, f.dart - dt * 1.4f);
    float wantX = cosf(f.head) * spd, wantY = sinf(f.head) * spd * 0.6f;
    f.vx += (wantX - f.vx) * clampf(dt * 3.f, 0.f, 1.f); f.vy += (wantY - f.vy) * clampf(dt * 3.f, 0.f, 1.f);
    f.x += (f.vx + s_gx * 8.f) * dt; f.y += (f.vy + s_gy * 5.f) * dt;               // a subtle current follows the tilt
    f.x = clampf(f.x, 14.f, 306.f); f.y = clampf(f.y, 30.f, 190.f);
    // draw: side-on, facing its motion; tail wags, UV makes them glow
    float dir = f.vx >= 0 ? 1.f : -1.f, sz = f.size;
    uint16_t body = uv ? hsv565(f.hue, 0.85f, 0.75f + aud::level * 0.25f) : hsv565(f.hue, 0.7f, 0.85f);
    uint16_t fin = uv ? hsv565(f.hue + 40.f, 0.9f, 1.f) : hsv565(f.hue + 20.f, 0.8f, 0.6f);
    float wag = sinf(f.ph) * sz * 0.45f;
    int tx0 = (int)(f.x - dir * sz * 0.9f), ty0 = (int)f.y;
    canvas.fillTriangle(tx0, ty0, (int)(tx0 - dir * sz * 0.9f), (int)(f.y - sz * 0.55f + wag), (int)(tx0 - dir * sz * 0.9f), (int)(f.y + sz * 0.55f + wag), fin);
    canvas.fillEllipse((int)f.x, (int)f.y, (int)sz, (int)(sz * 0.48f), body);
    canvas.drawFastHLine((int)(f.x - sz * 0.6f), (int)f.y, (int)(sz * 1.1f), fin);
    canvas.fillCircle((int)(f.x + dir * sz * 0.55f), (int)(f.y - sz * 0.12f), 1, rgb565(10, 10, 20));
  }
}
static void pebbles() {
  memset(s_floor, 0, 160 * 22);
  for (int k = 0; k < 70; k++) {
    int cx = esp_random() % 160, cy = 8 + esp_random() % 16, r = 2 + esp_random() % 4, col = esp_random() % 6;
    for (int y = -r; y <= r; y++)
      for (int x = -r - 1; x <= r + 1; x++) {
        int X = cx + x, Y = cy + y;
        if (X < 0 || X >= 160 || Y < 0 || Y >= 22) continue;
        float d = (x * x) / (float)((r + 1) * (r + 1)) + (y * y) / (float)(r * r);
        if (d > 1.f) continue;
        int shade = (int)((1.f - d) * 10.f) + (y < 0 ? 4 : 0) - (x > 0 ? 1 : 0);
        s_floor[Y * 160 + X] = (uint8_t)(144 + col * 16 + clampf((float)shade, 0, 15));
      }
  }
  for (auto &m : s_pk) m = {frand() * 160.f, 10.f + frand() * 90.f, frand() * 6.f};
}
static void bubble(float x, float y, float r) {
  for (auto &b : s_bub) if (!b.live) { b = {x, y, r, frand() * 6.f, 1}; return; }
}
static void popAt(float x, float y, float r) {
  for (auto &p : s_pop) if (p.a <= 0) { p = {x, y, r, 1.f}; break; }
  softHap(55, 10);
}
static void roomBubbles() {
  float dt = g_dt, c = aud::calm;
  bool uv = s_var[R_BUBBLES];
  float gm = sqrtf(s_gx * s_gx + s_gy * s_gy);        // bubbles rise against gravity, easing to 'up' when flat
  float wv = clampf(gm * 3.f, 0.f, 1.f);
  float ux = gm > 0.01f ? -s_gx / gm * wv : 0.f, uy = (gm > 0.01f ? -s_gy / gm * wv : 0.f) - (1.f - wv);
  float un = sqrtf(ux * ux + uy * uy) + 1e-4f; ux /= un; uy /= un;
  // bubbler + your finger
  s_emit -= dt;
  if (s_emit < 0) { s_emit = 0.35f / (1.f + c * 6.f); bubble(236.f + frand() * 6.f, 206.f, 1.5f + frand() * 2.f + c * 3.f); }
  if (s_tDown) { s_holdEmit -= dt; if (s_holdEmit < 0) { s_holdEmit = 0.07f; bubble(s_tx * 2.f + (frand() - 0.5f) * 6.f, s_ty * 2.f, 1.5f + frand() * 3.f); } }

  // background: water gradient + slow god-rays, pebble floor
  uint8_t t1 = (uint8_t)(g_t * 7.f + s_gx * 20.f), t2 = (uint8_t)(g_t * -5.f + s_gy * 12.f);   // rays lean with the tilt
  int rayK = 12 + (int)(aud::calm * 20.f + aud::onset * 12.f);                                  // and brighten with sound
  auto rows_ = [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
      uint8_t *row = fx::buf + y * fx::LW;
      if (y >= 98) {
        const uint8_t *f = s_floor + (y - 98) * 160;
        for (int x = 0; x < fx::LW; x++) row[x] = f[x] ? f[x] : (uint8_t)(20 + (y - 98) / 2);
        continue;
      }
      int base = 4 + y / 7;
      for (int x = 0; x < fx::LW; x++) {
        int r = fx::sn[(uint8_t)(x * 2 + y + t1)] + fx::sn[(uint8_t)(x * 3 - y + t2)];
        row[x] = (uint8_t)(base + (r > 150 ? ((r - 150) >> 3) * rayK >> 4 : 0) + (r > 200 ? 8 : 0));
      }
    }
  };
  fx::parallel(rows_);
  // plankton glow
  for (auto &m : s_pk) {
    m.ph += dt;
    m.x += (sinf(m.ph * 0.7f) * 3.f + s_gx * 6.f) * dt; m.y += (cosf(m.ph * 0.5f) * 2.f + s_gy * 3.f) * dt;
    if (s_tDown) { float dx = m.x - s_tx, dy = m.y - s_ty; if (dx * dx + dy * dy < 400.f) { m.x += s_tdx * 0.6f; m.y += s_tdy * 0.6f; } }
    if (m.x < 0) m.x += 160; if (m.x >= 160) m.x -= 160; m.y = clampf(m.y, 9.f, 96.f);
    uint8_t v = (uint8_t)(128 + (int)clampf(6.f + 6.f * sinf(m.ph * 3.f) + c * 4.f + aud::onset * 4.f, 0.f, 15.f));
    fx::plot((int)m.x, (int)m.y, v);
  }
  if (uv) {
    int at[] = {0, 30, 70, 127, 128, 140, 143};
    uint16_t col[] = {rgb565(3, 0, 10), rgb565(22, 4, 50), rgb565(60, 20, 120), rgb565(60, 20, 120), rgb565(40, 255, 200), rgb565(200, 255, 255), rgb565(200, 255, 255)};
    grad(at, col, 7);
    static const uint16_t neon[6] = {rgb565(255, 40, 200), rgb565(80, 255, 60), rgb565(255, 150, 20), rgb565(40, 200, 255), rgb565(255, 255, 60), rgb565(180, 80, 255)};
    for (int p = 0; p < 6; p++)
      for (int s = 0; s < 16; s++) {
        uint16_t cc = neon[p]; float k = (0.35f + s * 0.05f) * (0.75f + aud::bass * 0.6f);
        fx::palSet(144 + p * 16 + s, (uint8_t)clampf(((cc >> 11) << 3) * k, 0, 255), (uint8_t)clampf((((cc >> 5) & 63) << 2) * k, 0, 255), (uint8_t)clampf(((cc & 31) << 3) * k, 0, 255));
      }
  } else {
    int at[] = {0, 30, 70, 127, 128, 143};
    uint16_t col[] = {rgb565(4, 30, 50), rgb565(10, 80, 110), rgb565(120, 210, 220), rgb565(120, 210, 220), rgb565(200, 240, 230), rgb565(255, 255, 255)};
    grad(at, col, 6);
    static const uint16_t nat[6] = {rgb565(200, 180, 150), rgb565(120, 110, 100), rgb565(230, 220, 200), rgb565(150, 120, 90), rgb565(90, 90, 95), rgb565(210, 170, 140)};
    for (int p = 0; p < 6; p++)
      for (int s = 0; s < 16; s++) {
        uint16_t cc = nat[p]; float k = 0.35f + s * 0.05f;
        fx::palSet(144 + p * 16 + s, (uint8_t)clampf(((cc >> 11) << 3) * k, 0, 255), (uint8_t)clampf((((cc >> 5) & 63) << 2) * k, 0, 255), (uint8_t)clampf(((cc & 31) << 3) * k, 0, 255));
      }
  }
  fx::present(canvas);

  // jellyfish: pulses with the room, drifts up on each pulse, sinks between
  s_jph += dt * (0.8f + c * 0.8f);
  float pulse = powf(fmaxf(0.f, sinf(s_jph * 3.1416f)), 3.f);
  s_jy += (-pulse * 26.f + 7.f) * dt;
  s_jx += sinf(g_t * 0.13f) * 6.f * dt + s_gx * 10.f * dt;
  if (s_jy < 40) s_jy = 40; if (s_jy > 175) s_jy = 175;
  s_jx = clampf(s_jx, 30.f, 290.f);
  uint16_t jc = uv ? hsv565(300.f + sinf(g_t * 0.2f) * 40.f, 0.6f, 0.9f) : rgb565(230, 200, 240);
  uint16_t jc2 = uv ? hsv565(190.f, 0.7f, 0.9f) : rgb565(200, 170, 220);
  float bw = 17.f * (1.f - pulse * 0.25f), bh = 13.f * (1.f + pulse * 0.2f);
  for (int k = 0; k < 6; k++) {
    float tx = s_jx - bw * 0.7f + k * bw * 0.28f, ty = s_jy;
    for (int s = 0; s < 8; s++) {
      float nx = tx + sinf(g_t * 2.f + k + s * 0.6f) * (2.f + s * 0.5f), ny = ty + 5.f;
      canvas.drawLine((int)tx, (int)ty, (int)nx, (int)ny, jc2);
      tx = nx; ty = ny;
    }
  }
  for (int dy = 0; dy <= (int)bh; dy++) {
    float u = 1.f - (float)dy / bh;
    int hw = (int)(bw * sqrtf(fmaxf(0.f, 1.f - u * u)));
    canvas.drawFastHLine((int)s_jx - hw, (int)(s_jy - bh + dy), hw * 2 + 1, dy < 3 ? jc : (uv ? hsv565(290.f, 0.7f, 0.45f) : rgb565(170, 140, 190)));
  }
  canvas.drawFastHLine((int)(s_jx - bw), (int)s_jy, (int)(bw * 2), jc);

  fishUpdate(dt, uv);

  // bubbles: buoyancy, wobble, merge, pop at the surface
  for (int i = 0; i < 56; i++) {
    Bub &b = s_bub[i];
    if (!b.live) continue;
    b.ph += dt * (4.f + 6.f / b.r);
    float sp = 14.f + b.r * 7.f;
    float wob = sinf(b.ph) * b.r * 0.8f;
    b.x += (ux * sp - uy * wob) * dt; b.y += (uy * sp + ux * wob) * dt;
    for (int j = i + 1; j < 56; j++) {
      Bub &o = s_bub[j];
      if (!o.live) continue;
      float dx = o.x - b.x, dy = o.y - b.y;
      if (dx * dx + dy * dy < (b.r + o.r) * (b.r + o.r) * 0.55f) {
        b.r = cbrtf(b.r * b.r * b.r + o.r * o.r * o.r); o.live = 0;
        if (b.r > 11.f) b.r = 11.f;
      }
    }
    if (b.y < 16.f || b.y > 226.f || b.x < 2.f || b.x > 318.f) { if (b.y < 16.f) popAt(b.x, 18.f, b.r); b.live = 0; continue; }
    uint16_t rim = uv ? rgb565(150, 230, 255) : rgb565(220, 245, 255);
    canvas.drawCircle((int)b.x, (int)b.y, (int)b.r, rim);
    if (b.r > 3.f) canvas.drawCircle((int)b.x, (int)b.y, (int)b.r - 1, uv ? rgb565(60, 60, 140) : rgb565(90, 160, 190));
    canvas.fillCircle((int)(b.x - b.r * 0.4f), (int)(b.y - b.r * 0.4f), b.r > 4.f ? 1 : 0, rgb565(255, 255, 255));
  }
  for (auto &p : s_pop) {
    if (p.a <= 0) continue;
    p.r += dt * 40.f; p.a -= dt * 3.f;
    uint16_t pc = uv ? hsv565(180.f, 0.5f, p.a) : hsv565(190.f, 0.2f, p.a);
    for (int k = 0; k < 6; k++) { float a = k * 1.047f; canvas.drawPixel((int)(p.x + cosf(a) * p.r), (int)(p.y + sinf(a) * p.r), pc); }
  }
}

// ============================================================
//  public
// ============================================================
void calmBegin() {
  for (int y = 0; y < 11; y++)
    for (int x = 0; x < 11; x++) {
      float d2 = (x - 5) * (x - 5) + (y - 5) * (y - 5), u = 1.f - d2 / 30.f;
      s_ker[y][x] = (int8_t)clampf(u * 30.f, 0.f, 127.f);
    }
  s_sand = (uint8_t *)heap_caps_malloc(SW * SH, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_sand) s_sand = (uint8_t *)malloc(SW * SH);
  s_floor = (uint8_t *)malloc(160 * 22);
  sandPicture();
  pebbles();
  fishInit();
  fluidSeed(340, 40.f);
}

void calmNext() {
  s_room = (s_room + 1) % R_COUNT;
  if (s_room == R_FLOW) fluidSeed(340, 40.f);
  if (s_room == R_SPLASH) fluidSeed(FN, 30.f);
  fx::clear(0);
}

void calmLongPress() {
  if (s_room == R_FLOW) s_var[R_FLOW] = (uint8_t)((s_var[R_FLOW] + 1) % 3);
  else s_var[s_room] ^= 1;
  if (s_room == R_SAND) sandPicture();
}

void calmTouch(int x, int y, bool down) {
  if (!down) return;
  float lx = x * 0.5f, ly = y * 0.5f;
  switch (s_room) {
    case R_FLOW: fluidShove(lx, ly, 16.f, 50.f); break;
    case R_SPLASH: {
      int ix = (int)lx, iy = (int)ly;
      bool wet = fx::buf[iy * fx::LW + ix] > 110;
      if (wet) splashAt(lx, ly, 1.f);
      else for (int k = 0; k < 3; k++) spawnDrop(lx + (frand() - 0.5f) * 3.f, ly + k * 2.f, 0, 10.f, 0);
      break;
    }
    case R_BUBBLES: {
      for (auto &b : s_bub)
        if (b.live && (b.x - x) * (b.x - x) + (b.y - y) * (b.y - y) < (b.r + 8) * (b.r + 8)) { popAt(b.x, b.y, b.r); b.live = 0; return; }
      for (int k = 0; k < 5; k++) bubble(x + (frand() - 0.5f) * 14.f, y + (frand() - 0.5f) * 14.f, 1.5f + frand() * 4.f);
      s_scareX = x; s_scareY = y; s_scareAt = millis();
      break;
    }
    default: break;
  }
}

const char *calmName() {
  switch (s_room) {
    case R_FLOW: { static const char *v[] = {"flow: neon", "flow: honey", "flow: mercury"}; return v[s_var[R_FLOW] % 3]; }
    case R_SPLASH: return s_var[R_SPLASH] ? "splash: rain" : "splash";
    case R_SAND: return "sand";
    case R_WAVES: return s_var[R_WAVES] ? "waves: night" : "waves";
    default: return s_var[R_BUBBLES] ? "aquarium: uv" : "aquarium";
  }
}

void calmDraw() {
  float k = clampf(g_dt * 3.f, 0.f, 1.f);             // gravity eases in: nothing jerks
  s_gx += (g_gravX - s_gx) * k; s_gy += (g_gravY - s_gy) * k;
  readTouch();
  switch (s_room) {
    case R_FLOW: roomFlow(); break;
    case R_SPLASH: roomSplash(); break;
    case R_SAND: roomSand(); break;
    case R_WAVES: roomWaves(); break;
    default: roomBubbles(); break;
  }
}
