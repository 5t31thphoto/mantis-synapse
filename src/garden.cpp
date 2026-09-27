// ============================================================
//  SYNAPSE — GARDEN: things that grow from how you spend your time here.
//  B switches gardens. No numbers anywhere: the growth is the feedback.
//   CRYSTALS  quartz (quiet) · amethyst (sound) · bismuth (play) ·
//             fluorite (motion) · opal (care). Dust settles over real time;
//             swipe to brush it off. Hold the mist bottle to tend them.
//             Tap a crystal and it rings.
//   SUCCULENTS five plants, a zen sand bed you rake with a finger and a little
//             waterfall. Soil dries over a few real days: tap the soil to water
//             (not too much). Drag a fallen leaf away. Hold the pool to refill.
// ============================================================
#include "app.h"
#include "audio.h"
#include "stats.h"
#include "wire.h"
#include <string.h>

namespace {
uint8_t s_which = 0;                        // 0 crystals, 1 succulents
float s_t = 0;
inline float fr() { return (esp_random() & 0xFFFF) / 65535.f; }
inline uint16_t mixc(uint16_t a, uint16_t b, float u) {
  u = clampf(u, 0.f, 1.f);
  int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31, rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((int)(ra + (rb - ra) * u) << 11) | ((int)(ga + (gb - ga) * u) << 5) | (int)(ba + (bb - ba) * u));
}
inline uint32_t hsh(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
struct Sp { float x, y, vx, vy, life; uint16_t c; bool live; };
Sp s_sp[80];
void sp(float x, float y, float vx, float vy, uint16_t c, float l) { for (auto &p : s_sp) if (!p.live) { p = {x, y, vx, vy, l, c, true}; return; } }
void sparks(float dt, float g) {
  for (auto &p : s_sp) {
    if (!p.live) continue;
    p.vy += g * dt; p.x += p.vx * dt; p.y += p.vy * dt; p.life -= dt;
    if (p.life <= 0) { p.live = false; continue; }
    canvas.drawPixel((int)p.x, (int)p.y, p.c); if (p.life > 0.3f) canvas.drawPixel((int)p.x + 1, (int)p.y, p.c);
  }
}
bool touchNow(int &x, int &y) { auto td = M5.Touch.getDetail(); if (!td.isPressed() || td.y < 14 || td.y >= H - 14) return false; x = td.x; y = td.y; return true; }

// ============================================================
//  CRYSTALS
// ============================================================
const int CX[5] = {48, 106, 162, 218, 276};
const int GROUND = 196;
int lvl(int i) { float x = stats::s.cxp[i]; int L = (int)(log2f(fmaxf(1.f, x)) * 1.6f); return L > 12 ? 12 : L; }
int s_seenL[5] = {-1, -1, -1, -1, -1};
float s_ring[5];
// a crystal point: two lit faces + a tip, light follows your tilt
void prism(float x, float y, float ang, float len, float w, float hue, float sat, float val) {
  float ca = cosf(ang), sa = sinf(ang), px = -sa, py = ca;                 // along / across
  float tipX = x + ca * len, tipY = y + sa * len, shX = x + ca * len * 0.78f, shY = y + sa * len * 0.78f;
  float lx = -g_lookX, ly = -1.f;                                          // light from above, leaning with tilt
  float l1 = clampf(0.55f + 0.45f * (px * lx + py * ly) * 0.7f, 0.2f, 1.f), l2 = clampf(1.1f - l1, 0.2f, 1.f);
  uint16_t cA = hsv565(hue, sat, val * l1), cB = hsv565(hue + 8.f, sat, val * l2), cT = hsv565(hue - 6.f, sat * 0.6f, fminf(1.f, val * 1.2f));
  int x0 = (int)(x - px * w), y0 = (int)(y - py * w), x1 = (int)(x + px * w), y1 = (int)(y + py * w);
  int s0x = (int)(shX - px * w), s0y = (int)(shY - py * w), s1x = (int)(shX + px * w), s1y = (int)(shY + py * w);
  canvas.fillTriangle((int)x, (int)y, x0, y0, s0x, s0y, cA); canvas.fillTriangle((int)x, (int)y, s0x, s0y, (int)shX, (int)shY, cA);
  canvas.fillTriangle((int)x, (int)y, x1, y1, s1x, s1y, cB); canvas.fillTriangle((int)x, (int)y, s1x, s1y, (int)shX, (int)shY, cB);
  canvas.fillTriangle(s0x, s0y, (int)shX, (int)shY, (int)tipX, (int)tipY, cT); canvas.fillTriangle(s1x, s1y, (int)shX, (int)shY, (int)tipX, (int)tipY, cA);
  canvas.drawLine((int)x, (int)y, (int)shX, (int)shY, hsv565(hue, sat * 0.4f, 1.f));
}
void drawCluster(int i, float dt) {
  int L = lvl(i);
  float x = CX[i], y = GROUND, ring = s_ring[i];
  float glow = 0.35f + L * 0.05f + ring * 0.4f;
  canvas.fillEllipse((int)x, (int)y + 2, 14 + L * 2, 5, hsv565(i * 60.f, 0.5f, 0.12f + glow * 0.1f));
  switch (i) {
    case 0: {                                                   // QUARTZ: clear hexagonal points
      int n = 1 + L / 2;
      for (int k = 0; k < n; k++) {
        uint32_t h = hsh(k * 7919 + 11);
        float a = -1.5708f + ((int)(h % 100) - 50) / 100.f * (0.25f + k * 0.08f);
        float len = 14.f + L * 5.f * (0.6f + (h >> 8) % 40 / 100.f) - k * 3.f;
        prism(x + ((int)(h >> 12) % 16 - 8), y, a, len, 3.f + L * 0.35f, 190.f, 0.08f, 0.75f + glow * 0.25f);
      }
      break;
    }
    case 1: {                                                   // AMETHYST: a druzy of purple points
      int n = 3 + L * 2;
      for (int k = 0; k < n; k++) {
        uint32_t h = hsh(k * 104729 + 3);
        float a = -1.5708f + ((int)(h % 140) - 70) / 100.f;
        float len = 6.f + L * 2.2f * ((h >> 8) % 60 / 100.f + 0.4f);
        prism(x + ((int)(h >> 16) % 30 - 15), y - ((h >> 20) % 5), a, len, 1.5f + L * 0.15f, 285.f, 0.7f, 0.55f + glow * 0.4f);
      }
      break;
    }
    case 2: {                                                   // BISMUTH: stair-stepped iridescent hoppers
      int n = 1 + L / 3;
      for (int k = 0; k < n; k++) {
        uint32_t h = hsh(k * 31337 + 5);
        int s = 6 + L * 2 - k * 3, bx = (int)x + ((int)(h % 24) - 12), by = (int)y - s / 2 - k * 4;
        for (int st = s; st > 2; st -= 3) {
          float hue = fmodf(st * 30.f + g_lookX * 120.f + g_lookY * 80.f + s_t * 20.f, 360.f);
          canvas.drawRect(bx - st / 2, by - st / 2, st, st, hsv565(hue, 0.8f, 0.7f + glow * 0.3f));
          canvas.drawRect(bx - st / 2 + 1, by - st / 2 + 1, st - 2, st - 2, hsv565(hue + 40.f, 0.7f, 0.45f + glow * 0.2f));
        }
      }
      break;
    }
    case 3: {                                                   // FLUORITE: stacked green-teal cubes
      int n = 1 + L / 2;
      for (int k = 0; k < n; k++) {
        uint32_t h = hsh(k * 2654435 + 9);
        int s = 5 + L + (int)(h % 5), bx = (int)x + ((int)(h >> 8) % 26 - 13), by = (int)y - s - (k / 3) * s;
        uint16_t top = hsv565(160.f + (k % 3) * 25.f, 0.6f, 0.75f + glow * 0.25f), side = hsv565(170.f + (k % 3) * 25.f, 0.7f, 0.45f + glow * 0.2f);
        canvas.fillRect(bx, by, s, s, side);
        canvas.fillTriangle(bx, by, bx + s, by, bx + s / 2, by - s / 2, top);
        canvas.fillTriangle(bx + s, by, bx + s / 2, by - s / 2, bx + s + s / 2, by - s / 2, top);
        canvas.drawRect(bx, by, s, s, hsv565(170.f, 0.4f, 0.9f));
      }
      break;
    }
    default: {                                                  // OPAL: a rounded nodule with a play of colour
      int r = 6 + L * 2;
      canvas.fillEllipse((int)x, (int)y - r / 2, r, (int)(r * 0.75f), rgb565(210, 210, 225));
      for (int k = 0; k < 6 + L; k++) {
        uint32_t h = hsh(k * 97 + 1);
        float px = x + ((int)(h % 100) - 50) / 100.f * r * 1.4f, py = y - r / 2 + ((int)((h >> 8) % 100) - 50) / 100.f * r;
        float hue = fmodf(k * 55.f + g_lookX * 200.f + g_lookY * 140.f, 360.f);
        canvas.fillCircle((int)px, (int)py, 1 + L / 4, hsv565(hue, 0.8f, 0.9f));
      }
      break;
    }
  }
  // dust dulls them; a level-up rings out once
  if (stats::s.dust > 0.05f) {
    int n = (int)(stats::s.dust * (10 + L * 3));
    for (int k = 0; k < n; k++) { uint32_t h = hsh(k * 131 + i * 977); canvas.drawPixel((int)x + (int)(h % 36) - 18, (int)y - (int)((h >> 8) % (20 + L * 6)), rgb565(120, 110, 100)); }
  }
  if (s_seenL[i] >= 0 && L > s_seenL[i]) { s_ring[i] = 1.f; aud::bell(i % 3, 120); for (int k = 0; k < 20; k++) sp(x, y - 20, (fr() - 0.5f) * 120.f, -fr() * 120.f, hsv565(i * 60.f, 0.6f, 1.f), 1.f); }
  s_seenL[i] = L;
  s_ring[i] = fmaxf(0.f, s_ring[i] - dt * 0.8f);
}
float s_mist = 0; float s_lastWipe = 0; int s_px = -1, s_py = -1;
void crystals(float dt) {
  // geode cave
  for (int y = 14; y < H - 14; y += 2) canvas.fillRect(0, y, W, 2, mixc(rgb565(18, 4, 30), rgb565(4, 20, 26), (float)(y - 14) / (H - 28)));
  for (int k = 0; k < 40; k++) { uint32_t h = hsh(k + 5); int x = h % W, y = 16 + (h >> 10) % 90; canvas.fillCircle(x, y, 1 + (h >> 20) % 2, hsv565(280.f + (k % 5) * 18.f, 0.5f, 0.25f + 0.1f * sinf(s_t + k))); }
  canvas.fillRect(0, GROUND, W, H - 14 - GROUND, rgb565(30, 20, 34));
  for (int x = 0; x < W; x += 8) canvas.fillTriangle(x, GROUND, x + 8, GROUND, x + 4, GROUND - 3 - (int)(hsh(x) % 4), rgb565(40, 28, 44));
  for (int i = 0; i < 5; i++) drawCluster(i, dt);
  // mist bottle (hold to tend)
  int bx = 292, by = 34;
  canvas.fillRoundRect(bx - 7, by - 4, 14, 22, 4, rgb565(90, 170, 190)); canvas.fillRect(bx - 3, by - 10, 6, 7, rgb565(200, 200, 210)); canvas.fillRect(bx - 8, by - 12, 5, 3, rgb565(200, 200, 210));
  int tx, ty;
  if (touchNow(tx, ty)) {
    if ((tx - bx) * (tx - bx) + (ty - by) * (ty - by) < 22 * 22) {
      s_mist += dt;
      for (int k = 0; k < 3; k++) sp(bx - 10, by - 10, -60.f - fr() * 90.f, fr() * 40.f, rgb565(200, 240, 255), 1.5f);
      if (s_mist > 1.5f) { s_mist = -60.f; stats::event(stats::EV_CARE, 1.f); hapGesture(HG_SETTLE); }   // one good misting per minute counts
      hapRumble(0.12f, 18.f, 0.8f);
    } else if (s_px >= 0 && ty > 90) {                               // a swipe brushes dust away
      float d = sqrtf((float)((tx - s_px) * (tx - s_px) + (ty - s_py) * (ty - s_py)));
      if (stats::s.dust > 0) { stats::s.dust = fmaxf(0.f, stats::s.dust - d * 0.0015f); s_lastWipe += d; hapRumble(clampf(d / 40.f, 0.05f, 0.25f), 30.f, 0.9f); }
      if (s_lastWipe > 400.f) { s_lastWipe = 0; stats::event(stats::EV_CARE, 0.3f); }
      for (int k = 0; k < 2; k++) sp(tx, ty, (fr() - 0.5f) * 40.f, -fr() * 30.f, rgb565(150, 140, 130), 0.6f);
    }
    s_px = tx; s_py = ty;
  } else { s_px = -1; if (s_mist > 0) s_mist = 0; if (s_mist < 0) s_mist = fminf(0.f, s_mist + dt); }
}
void crystalTap(int x, int y) {
  for (int i = 0; i < 5; i++)
    if (abs(x - CX[i]) < 24 && y > GROUND - 70 && y < GROUND + 10) {
      s_ring[i] = 1.f; aud::bell(i % 3, 90);
      for (int k = 0; k < 10; k++) sp(CX[i], GROUND - 20, (fr() - 0.5f) * 90.f, -fr() * 90.f, hsv565(i * 60.f, 0.5f, 1.f), 0.8f);
      hap(60, 12);
    }
}

// ============================================================
//  SUCCULENTS
// ============================================================
const int PX[5] = {150, 188, 226, 262, 296}, SOILY = 196;
struct Drop { float x, y, vy; bool live; };
Drop s_drop[40];
struct Fall { float x, y; bool live; bool drag; };
Fall s_leaf = {0, 0, false, false};
float s_leafT = 0;
uint8_t *s_sand = nullptr;                    // raked grooves (160 x 30, lores)
struct WF { float x, y, vy; bool live; };
WF s_wf[50];
float s_refill = 0;
void rosette(int x, int y, float g, float hp, float hue) {        // echeveria seen from the side-top
  float r = 6 + g * 16;
  uint16_t lo = hsv565(hue, 0.35f + hp * 0.3f, 0.35f + hp * 0.35f), hi = hsv565(hue - 10.f, 0.25f + hp * 0.25f, 0.55f + hp * 0.35f);
  for (int ring = 3; ring >= 0; ring--) {
    float rr = r * (0.35f + ring * 0.22f);
    int n = 5 + ring * 2;
    for (int k = 0; k < n; k++) {
      float a = k * 6.2831853f / n + ring * 0.4f;
      canvas.fillEllipse(x + (int)(cosf(a) * rr), y - (int)(fabsf(sinf(a)) * rr * 0.35f) - ring * 2, (int)(rr * 0.45f) + 1, (int)(rr * 0.22f) + 1, ring & 1 ? lo : hi);
    }
  }
  canvas.fillCircle(x, y - 8, 2, hi);
}
void plant(int i, float dt) {
  float g = stats::s.grow[i], hp = stats::s.health[i];
  int x = PX[i], y = SOILY;
  float wilt = 1.f - hp;
  switch (i) {
    case 0: rosette(x, y, g, hp, 150.f); break;
    case 1: {                                                  // haworthia: striped spikes
      int n = 5 + (int)(g * 6);
      for (int k = 0; k < n; k++) {
        float a = -1.5708f + (k - n / 2.f) * 0.22f + wilt * 0.3f * (k < n / 2 ? -1 : 1);
        float L = 10 + g * 22;
        int ex = x + (int)(cosf(a) * L), ey = y + (int)(sinf(a) * L);
        canvas.fillTriangle(x - 2, y, x + 2, y, ex, ey, hsv565(130.f, 0.5f + hp * 0.2f, 0.35f + hp * 0.3f));
        canvas.drawLine((x + ex) / 2 - 1, (y + ey) / 2, (x + ex) / 2 + 1, (y + ey) / 2, rgb565(230, 240, 220));
      }
      break;
    }
    case 2: {                                                  // jade: a little tree of round leaves
      int h = 8 + (int)(g * 30);
      canvas.fillRect(x - 1, y - h, 3, h, rgb565(110, 80, 50));
      for (int k = 0; k < 2 + (int)(g * 7); k++) {
        uint32_t hh = hsh(k * 17 + 2);
        int lx = x + ((int)(hh % 28) - 14), ly = y - h + (int)((hh >> 8) % (h + 1)) - 2;
        canvas.fillEllipse(lx, ly, 4, 3, hsv565(120.f, 0.6f, 0.3f + hp * 0.35f));
        canvas.drawPixel(lx - 1, ly - 1, rgb565(200, 230, 160));
      }
      break;
    }
    case 3: {                                                  // barrel cactus, blooms when thriving
      int r = 6 + (int)(g * 12);
      canvas.fillEllipse(x, y - r, r, r, hsv565(115.f, 0.55f, 0.3f + hp * 0.3f));
      for (int k = -2; k <= 2; k++) canvas.drawLine(x + k * r / 3, y - 2 * r + 2, x + k * r / 3, y - 1, hsv565(115.f, 0.6f, 0.2f));
      for (int k = 0; k < 10; k++) canvas.drawPixel(x + (int)(hsh(k) % (2 * r)) - r, y - (int)((hsh(k) >> 8) % (2 * r)), rgb565(240, 230, 200));
      if (g > 0.8f && hp > 0.75f) for (int k = 0; k < 5; k++) { float a = k * 1.2566f; canvas.fillCircle(x + (int)(cosf(a) * 4), y - 2 * r + (int)(sinf(a) * 3), 3, rgb565(255, 90, 150)); }
      break;
    }
    default: {                                                 // string of pearls spilling over the edge
      int n = 2 + (int)(g * 4);
      for (int c = 0; c < n; c++) {
        int len = 4 + (int)(g * 10) + c;
        for (int k = 0; k < len; k++) {
          int bx = x - 10 + c * 5 + (int)(sinf(k * 0.8f + c + s_t * 0.5f) * 2), by = y - 4 + k * 3;
          canvas.fillCircle(bx, by, 2, hsv565(110.f, 0.5f, 0.35f + hp * 0.35f));
        }
      }
      break;
    }
  }
  for (int k = 0; k < stats::s.pups[i]; k++) rosette(x - 14 + k * 9, y + 2, 0.1f, hp, 150.f);   // pups
}
void succulents(float dt) {
  // light follows the real time of day
  int hr = stats::hourOfDay();
  float day = clampf(1.f - fabsf(hr - 13.f) / 8.f, 0.f, 1.f), eve = clampf(1.f - fabsf(hr - 18.5f) / 2.5f, 0.f, 1.f);
  uint16_t wallT = mixc(rgb565(30, 34, 60), rgb565(230, 214, 190), day), wallB = mixc(rgb565(20, 20, 40), rgb565(200, 180, 150), day);
  wallT = mixc(wallT, rgb565(240, 150, 90), eve * 0.5f);
  for (int y = 14; y < H - 14; y += 2) canvas.fillRect(0, y, W, 2, mixc(wallT, wallB, (float)(y - 14) / (H - 28)));
  canvas.fillRect(200, 22, 100, 70, mixc(rgb565(20, 30, 70), rgb565(170, 220, 250), day));          // window
  canvas.drawRect(200, 22, 100, 70, rgb565(120, 90, 60)); canvas.drawFastVLine(250, 22, 70, rgb565(120, 90, 60)); canvas.drawFastHLine(200, 57, 100, rgb565(120, 90, 60));
  if (day < 0.3f) canvas.fillCircle(280, 40, 6, rgb565(230, 230, 200));
  // tray
  canvas.fillRect(0, 186, W, 40, rgb565(120, 84, 50));
  // zen sand (left): raked grooves persist in RAM
  if (!s_sand) { s_sand = (uint8_t *)calloc(60 * 18, 1); }
  canvas.fillRect(4, 188, 116, 34, rgb565(214, 200, 170));
  for (int y = 0; y < 18; y++) for (int x = 0; x < 60; x++) if (s_sand[y * 60 + x]) canvas.fillRect(4 + x * 2, 188 + y * 2, 2, 2, rgb565(180, 164, 134));
  // soil bed (right)
  uint16_t soilC = mixc(rgb565(150, 110, 70), rgb565(70, 45, 30), stats::s.soil);
  canvas.fillRect(126, 190, 192, 32, soilC);
  // waterfall (back left): rocks, a stream whose strength follows the reservoir
  canvas.fillRoundRect(16, 118, 46, 20, 6, rgb565(90, 90, 96)); canvas.fillRoundRect(40, 140, 50, 22, 6, rgb565(80, 80, 88));
  canvas.fillRoundRect(10, 160, 96, 26, 8, rgb565(70, 70, 78));
  canvas.fillEllipse(60, 176, 34, 7, mixc(rgb565(60, 80, 90), rgb565(90, 170, 210), stats::s.pump));
  float flow = stats::s.pump;
  if (fr() < flow * 0.9f) for (auto &w : s_wf) if (!w.live) { w = {40.f + fr() * 6.f, 120.f, 20.f, true}; break; }
  for (auto &w : s_wf) {
    if (!w.live) continue;
    w.vy += 220.f * dt; w.y += w.vy * dt;
    if (w.y > 140 && w.y < 146 && w.x < 60) { w.x += 18.f; w.vy *= 0.3f; }
    if (w.y > 172) { w.live = false; continue; }
    canvas.fillCircle((int)w.x, (int)w.y, 1, rgb565(170, 220, 250));
  }
  stats::s.pump = fmaxf(0.f, stats::s.pump - dt * 0.004f / 3600.f);
  // plants: health drifts toward how well the soil suits them; growth when thriving
  for (int i = 0; i < 5; i++) {
    float want = (stats::s.soil < 0.08f) ? -0.02f : (stats::s.soil > 0.9f ? -0.03f : 0.01f);
    stats::s.health[i] = clampf(stats::s.health[i] + want * dt / 60.f, 0.1f, 1.f);
    if (stats::s.health[i] > 0.6f) stats::s.grow[i] = fminf(1.f, stats::s.grow[i] + dt * 0.00012f * (1.f + stats::s.care * 0.05f));
    if (stats::s.grow[i] > 0.55f + stats::s.pups[i] * 0.15f && stats::s.pups[i] < 3) { stats::s.pups[i]++; hapGesture(HG_SETTLE); }
    plant(i, dt);
  }
  // water drops from a tap on the soil
  for (auto &d : s_drop) {
    if (!d.live) continue;
    d.vy += 400.f * dt; d.y += d.vy * dt;
    if (d.y > SOILY) { d.live = false; continue; }
    canvas.fillCircle((int)d.x, (int)d.y, 2, rgb565(110, 170, 255));
  }
  // now and then a leaf drops onto the sand; drag it away
  s_leafT += dt;
  if (!s_leaf.live && s_leafT > 90.f && fr() < dt * 0.02f) { s_leaf = {20.f + fr() * 90.f, 196.f, true, false}; s_leafT = 0; }
  int tx, ty;
  bool t = touchNow(tx, ty);
  if (s_leaf.live) {
    if (t && (s_leaf.drag || (abs(tx - (int)s_leaf.x) < 14 && abs(ty - (int)s_leaf.y) < 12))) { s_leaf.drag = true; s_leaf.x = tx; s_leaf.y = ty; }
    else s_leaf.drag = false;
    if (s_leaf.x < -6 || s_leaf.x > W + 6 || s_leaf.y < 20) { s_leaf.live = false; stats::event(stats::EV_CARE, 0.5f); hapGesture(HG_THREAD); }
    canvas.fillEllipse((int)s_leaf.x, (int)s_leaf.y, 5, 3, rgb565(170, 150, 80));
  }
  // raking the sand with a finger
  static int lx = -1, ly = -1;
  if (t && tx < 120 && ty > 186 && !s_leaf.drag) {
    if (lx >= 0) {
      int n = abs(tx - lx) + abs(ty - ly) + 1;
      for (int k = 0; k <= n; k++) {
        int x = (lx + (tx - lx) * k / n - 4) / 2, y = (ly + (ty - ly) * k / n - 188) / 2;
        for (int o = -1; o <= 1; o += 2) { int yy = y + o * 2; if (x >= 0 && x < 60 && yy >= 0 && yy < 18) s_sand[yy * 60 + x] = 1; }
        if (x >= 0 && x < 60 && y >= 0 && y < 18) s_sand[y * 60 + x] = 0;
      }
      hapRumble(clampf(n / 30.f, 0.05f, 0.2f), 40.f, 1.f);                                     // a gritty little rake
    }
    lx = tx; ly = ty;
  } else lx = -1;
  // hold the pool to refill the waterfall
  if (t && abs(tx - 60) < 36 && abs(ty - 176) < 12) {
    s_refill += dt; stats::s.pump = fminf(1.f, stats::s.pump + dt * 0.5f);
    if (s_refill > 1.2f) { s_refill = -30.f; stats::event(stats::EV_CARE, 0.5f); }
  } else if (s_refill > 0) s_refill = 0; else if (s_refill < 0) s_refill = fminf(0.f, s_refill + dt);
}
void succTap(int x, int y) {
  if (x > 126 && y > 150) {                                          // water: a small, soft pour
    for (int k = 0; k < 10; k++) for (auto &d : s_drop) if (!d.live) { d = {x + (fr() - 0.5f) * 20.f, 150.f - fr() * 20.f, 0, true}; break; }
    bool over = stats::s.soil > 0.85f;
    stats::s.soil = fminf(1.f, stats::s.soil + 0.22f);
    if (!over) stats::event(stats::EV_CARE, 0.4f);
    hapRumble(0.18f, 6.f, 0.2f);
  }
}
}  // namespace

void gardenBegin() {}
void gardenDraw() {
  float dt = fminf(g_dt, 0.05f);
  s_t += dt;
  if (s_which == 0) crystals(dt); else succulents(dt);
  sparks(dt, s_which ? 150.f : -30.f);
}
void gardenNext() { s_which ^= 1; stats::save(); }
void gardenTouch(int x, int y) { if (s_which == 0) crystalTap(x, y); else succTap(x, y); }
const char *gardenName() { return s_which ? "succulents" : "crystals"; }
