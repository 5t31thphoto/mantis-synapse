// ============================================================
//  SYNAPSE — ROOMS: small physical puzzles. Nothing explains them.
//  Solve a room and the "next" label over B lights up. After the last
//  room it loops back to Mantis NRG (and every visit starts there).
//    nrg     shake the soda until it blows           breeze  blow on the pinwheel
//    arcade  mash the red button                     wrap    pop every bubble
//    clap    clap along with the mantis (3 in time)  seed    tip the can to water the seed
//    shade   drag the cloud off the sun              align   tip the rows into line, hold level to lock
//    knock   knock on the Core2's case               globe   shake the snow globe, then hold it still
//    hum     hum a steady note at the crystal        hush    be silent until the firefly lands
// ============================================================
#include "app.h"
#include "audio.h"
#include "wire.h"
#include <string.h>
#ifdef HOST
extern float g_dbgMarbleX, g_dbgMarbleY;
#endif

namespace {

bool s_solved = false;
float s_t = 0;                                             // time in this room
inline float fr() { return (esp_random() & 0xFFFF) / 65535.f; }
inline uint16_t mix565(uint16_t a, uint16_t b, float u) {
  int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31, rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((int)(ra + (rb - ra) * u) << 11) | ((int)(ga + (gb - ga) * u) << 5) | (int)(ba + (bb - ba) * u));
}
void bg(uint16_t top, uint16_t bot) {
  for (int y = 14; y < H - 14; y += 2) canvas.fillRect(0, y, W, 2, mix565(top, bot, (float)(y - 14) / (H - 28)));
}
void textC(const char *s, int y, uint16_t col, int size) {
  canvas.setTextSize(size); canvas.setTextColor(col);
  canvas.setCursor(W / 2 - (int)strlen(s) * 3 * size, y); canvas.print(s);
  canvas.setTextSize(1);
}
// motion + sound helpers
inline float agitation() { return clampf(g_jolt * 1.6f + (fabsf(g_gyroX) + fabsf(g_gyroY) + fabsf(g_gyroZ)) / 420.f, 0.f, 2.f); }
bool touchNow(int &x, int &y) {
  auto td = M5.Touch.getDetail();
  if (!td.isPressed() || td.y < 14 || td.y >= H - 14) return false;
  x = td.x; y = td.y; return true;
}

// ---- claps: a sharp spike in peak loudness over its own recent average (doesn't adapt away a steady rhythm) ----
float s_clapSlow = 0.05f;
bool clapSpike(float dt) {
  float x = aud::peak;
  bool hit = x > 0.28f && x > s_clapSlow * 2.2f + 0.08f;
  s_clapSlow += (x - s_clapSlow) * clampf(dt * (x > s_clapSlow ? 1.5f : 6.f), 0.f, 1.f);
  return hit || (aud::onset > 0.4f && (aud::treble > 0.3f || aud::zcr > 0.3f));
}

// ---- sparkles / confetti ----
struct Spark { float x, y, vx, vy, life; uint16_t c; bool live; };
Spark s_sp[120];
void spark(float x, float y, float vx, float vy, uint16_t c, float life) {
  for (auto &p : s_sp) if (!p.live) { p = {x, y, vx, vy, life, c, true}; return; }
}
void sparks(float dt, float grav) {
  for (auto &p : s_sp) {
    if (!p.live) continue;
    p.vy += grav * dt; p.x += p.vx * dt; p.y += p.vy * dt; p.life -= dt;
    if (p.life <= 0 || p.y > H) { p.live = false; continue; }
    canvas.fillCircle((int)p.x, (int)p.y, p.life > 0.4f ? 2 : 1, p.c);
  }
}
void burst(float x, float y, int n, float sp) {
  static const uint16_t C[] = {wire::LIME, wire::TEAL, 0xF81F, 0xFFE0, 0xFFFF};
  for (int i = 0; i < n; i++) { float a = fr() * 6.2831853f, s = sp * (0.3f + fr()); spark(x, y, cosf(a) * s, sinf(a) * s - sp * 0.3f, C[i % 5], 0.6f + fr()); }
}

// ---- a vector mantis: arms 0 = folded (praying) .. 1 = raised wide ----
void mantisV(float x, float y, float s, float armL, float armR, float eyeGlow, uint16_t body = 0) {
  if (!body) body = rgb565(120, 220, 90);
  uint16_t dark = rgb565(60, 130, 50);
  canvas.fillEllipse((int)x, (int)(y + 26 * s), (int)(9 * s), (int)(22 * s), dark);          // abdomen
  canvas.fillEllipse((int)x, (int)(y + 24 * s), (int)(7 * s), (int)(19 * s), body);
  canvas.fillRect((int)(x - 3 * s), (int)(y - 6 * s), (int)(6 * s), (int)(12 * s), body);     // thorax
  for (int side = -1; side <= 1; side += 2) {                                                   // legs
    canvas.drawLine((int)x, (int)(y + 10 * s), (int)(x + side * 18 * s), (int)(y + 30 * s), dark);
    canvas.drawLine((int)(x + side * 18 * s), (int)(y + 30 * s), (int)(x + side * 22 * s), (int)(y + 48 * s), dark);
  }
  for (int side = -1; side <= 1; side += 2) {                                                   // raptorial arms
    float a = side < 0 ? armL : armR;
    float ex = x + side * (6 + 12 * a) * s, ey = y - (2 + 10 * a) * s;
    float hx = x + side * (2 + 16 * a) * s, hy = y - (14 + 14 * a) * s;
    canvas.drawLine((int)x, (int)y, (int)ex, (int)ey, body); canvas.drawLine((int)x + side, (int)y, (int)ex + side, (int)ey, body);
    canvas.drawLine((int)ex, (int)ey, (int)hx, (int)hy, body); canvas.drawLine((int)ex + side, (int)ey, (int)hx + side, (int)hy, body);
    canvas.fillCircle((int)hx, (int)hy, (int)(2 * s), dark);
  }
  canvas.fillTriangle((int)(x - 11 * s), (int)(y - 14 * s), (int)(x + 11 * s), (int)(y - 14 * s), (int)x, (int)(y - 1 * s), body);   // head
  uint16_t ec = mix565(rgb565(20, 20, 30), wire::LIME, eyeGlow);
  canvas.fillCircle((int)(x - 8 * s), (int)(y - 15 * s), (int)(4 * s), ec);
  canvas.fillCircle((int)(x + 8 * s), (int)(y - 15 * s), (int)(4 * s), ec);
  canvas.fillCircle((int)(x - 9 * s), (int)(y - 16 * s), (int)fmaxf(1, s), rgb565(255, 255, 255));
  canvas.fillCircle((int)(x + 7 * s), (int)(y - 16 * s), (int)fmaxf(1, s), rgb565(255, 255, 255));
  canvas.drawLine((int)(x - 4 * s), (int)(y - 17 * s), (int)(x - 14 * s), (int)(y - 34 * s), body);   // antennae
  canvas.drawLine((int)(x + 4 * s), (int)(y - 17 * s), (int)(x + 14 * s), (int)(y - 34 * s), body);
}

// ============================================================
//  1  MANTIS NRG — shake it up (the pressure is hidden)
// ============================================================
namespace nrg {
float p = 0, level = 1, capY = 0, capVY = 0, capVX = 0, capX = 0, capRot = 0, fount = 0, lidBack = 0, wob = 0;
int phase = 0;                                   // 0 capped, 1 fountain, 2 aftermath, 3 lid returning
struct Drop { float x, y, vx, vy; bool live; };
Drop dr[140];
struct Bub { float x, y, vx, vy, r, life, ph; bool live; };
Bub bb[120];
struct Drip { float x, y, vy, life; float tx[10], ty[10]; int n; bool live; };
Drip dp[26];
struct In { float x, y, sp; };
In ins[40];
const int BX = 160, BBOT = 214, BTOP = 112, BW = 36, NECK = 11, NTOP = 62;
void enter() { p = 0; level = 1; phase = 0; lidBack = 1; fount = 0; for (auto &d : dr) d.live = false; for (auto &b : bb) b.live = false; for (auto &d : dp) d.live = false;
  for (auto &i : ins) i = {BX - BW + 6 + fr() * (BW * 2 - 12), BBOT - fr() * 80.f, 10.f + fr() * 30.f}; }
void bubble(float x, float y, float vx, float vy) {
  for (auto &b : bb) if (!b.live) { b = {x, y, vx, vy, 2.f + fr() * 5.f, 3.5f + fr() * 5.f, fr() * 6.f, true}; return; }
}
void drip(float x, float y) {
  for (auto &d : dp) if (!d.live) { d.x = x; d.y = y; d.vy = 6.f + fr() * 10.f; d.life = 5.f + fr() * 3.f; d.n = 0; d.live = true; return; }
}
void drawBottle(float ox, float oy) {
  int x = BX + (int)ox, y = (int)oy;
  uint16_t glass = rgb565(10, 70, 60), glassHi = rgb565(90, 190, 170);
  // liquid (behind the glass tint)
  int lvlTop = BBOT - (int)((BBOT - BTOP + 20) * level);
  canvas.fillRoundRect(x - BW, y + BTOP, BW * 2, BBOT - BTOP, 14, glass);
  canvas.fillTriangle(x - BW, y + BTOP + 2, x + BW, y + BTOP + 2, x, y + BTOP - 30, glass);
  canvas.fillRect(x - NECK, y + NTOP, NECK * 2, BTOP - NTOP - 10, glass);
  if (level > 0.02f) {
    int top = lvlTop < BTOP ? BTOP : lvlTop;
    canvas.fillRoundRect(x - BW + 3, y + top, BW * 2 - 6, BBOT - top - 3, 10, rgb565(120, 230, 40));
    canvas.drawFastHLine(x - BW + 5, y + top, BW * 2 - 10, rgb565(220, 255, 150));
  }
  // bubbles inside (more as it gets agitated)
  int nIn = (int)(4 + (p + wob) * 30.f); if (nIn > 40) nIn = 40;
  for (int i = 0; i < nIn; i++) {
    In &b = ins[i];
    b.y -= b.sp * (0.3f + wob * 2.f + p) * g_dt;
    if (b.y < y + lvlTop + 2 || b.y < y + BTOP) { b.y = BBOT - 4; b.x = BX - BW + 6 + fr() * (BW * 2 - 12); }
    canvas.drawCircle(x + (int)(b.x - BX), (int)b.y, 1 + (i % 3 == 0), rgb565(230, 255, 200));
  }
  // label: plum band, MANTIS / NRG, a little mantis head
  canvas.fillRect(x - BW, y + 142, BW * 2, 44, rgb565(93, 0, 93));
  canvas.drawFastHLine(x - BW, y + 142, BW * 2, wire::LIME); canvas.drawFastHLine(x - BW, y + 185, BW * 2, wire::LIME);
  canvas.fillTriangle(x - 7, y + 147, x + 7, y + 147, x, y + 157, wire::LIME);
  canvas.fillCircle(x - 5, y + 147, 3, rgb565(20, 20, 30)); canvas.fillCircle(x + 5, y + 147, 3, rgb565(20, 20, 30));
  canvas.setTextSize(1); canvas.setTextColor(rgb565(255, 255, 255)); canvas.setCursor(x - 18, y + 161); canvas.print("MANTIS");
  canvas.setTextSize(2); canvas.setTextColor(wire::LIME); canvas.setCursor(x - 17, y + 170); canvas.print("NRG"); canvas.setTextSize(1);
  // glass highlights + outline
  canvas.drawFastVLine(x - BW + 6, y + BTOP + 8, BBOT - BTOP - 20, glassHi);
  canvas.drawFastVLine(x - BW + 8, y + BTOP + 14, 40, glassHi);
  canvas.drawRoundRect(x - BW, y + BTOP, BW * 2, BBOT - BTOP, 14, rgb565(30, 120, 110));
  canvas.drawLine(x - BW, y + BTOP + 2, x - NECK, y + BTOP - 28, rgb565(30, 120, 110));
  canvas.drawLine(x + BW, y + BTOP + 2, x + NECK, y + BTOP - 28, rgb565(30, 120, 110));
  canvas.drawFastVLine(x - NECK, y + NTOP, BTOP - NTOP - 28, rgb565(30, 120, 110));
  canvas.drawFastVLine(x + NECK - 1, y + NTOP, BTOP - NTOP - 28, rgb565(30, 120, 110));
}
void drawCap(float x, float y, float rot) {
  uint16_t gold = rgb565(230, 190, 60), dk = rgb565(140, 100, 20);
  float c = cosf(rot), s = sinf(rot);
  auto P = [&](float lx, float ly, int &ox, int &oy) { ox = (int)(x + c * lx - s * ly); oy = (int)(y + s * lx + c * ly); };
  int a0, b0, a1, b1, a2, b2, a3, b3;
  P(-14, -4, a0, b0); P(14, -4, a1, b1); P(14, 5, a2, b2); P(-14, 5, a3, b3);
  canvas.fillTriangle(a0, b0, a1, b1, a2, b2, gold); canvas.fillTriangle(a0, b0, a2, b2, a3, b3, gold);
  for (int k = -3; k <= 3; k++) { int tx, ty, ux, uy; P(k * 4.f, 5, tx, ty); P(k * 4.f + 2, 9, ux, uy); canvas.drawLine(tx, ty, ux, uy, dk); }
  P(0, 0, a0, b0); canvas.fillCircle(a0, b0, 3, dk);
}
void draw(float dt) {
  bg(rgb565(40, 8, 44), rgb565(8, 16, 22));
  for (int y = 20; y < H - 14; y += 24) canvas.drawFastHLine(0, y, W, rgb565(50, 14, 52));     // a tiled wall
  for (int x = 0; x < W; x += 24) canvas.drawFastVLine(x, 14, H - 28, rgb565(50, 14, 52));
  // drips run down the wall behind everything
  for (auto &d : dp) {
    if (!d.live) continue;
    d.life -= dt; d.vy = fminf(d.vy + dt * 6.f, 40.f); d.y += d.vy * dt * (0.6f + 0.4f * sinf(s_t * 3.f + d.x));
    if (d.n < 10 && (d.n == 0 || d.y - d.ty[d.n - 1] > 10.f)) { d.tx[d.n] = d.x; d.ty[d.n] = d.y; d.n++; }
    float f = clampf(d.life / 3.f, 0.f, 1.f);
    uint16_t c = mix565(rgb565(30, 12, 34), rgb565(140, 230, 60), f);
    for (int i = 1; i < d.n; i++) canvas.drawLine((int)d.tx[i - 1], (int)d.ty[i - 1], (int)d.tx[i], (int)d.ty[i], c);
    if (d.n) canvas.drawLine((int)d.tx[d.n - 1], (int)d.ty[d.n - 1], (int)d.x, (int)d.y, c);
    canvas.fillCircle((int)d.x, (int)d.y, 2, c);
    if (d.life <= 0 || d.y > H - 14) d.live = false;
  }
  float ag = agitation();
  wob += (fminf(ag, 1.5f) - wob) * clampf(dt * 6.f, 0.f, 1.f);
  if (phase == 0) {
    p += (ag > 0.25f ? ag * 0.085f : -0.06f) * dt;                  // shaking builds it; resting lets it calm down
    p = clampf(p, 0.f, 1.f);
    if (p > 0.05f || wob > 0.1f) hapRumble(clampf(p * 0.55f + wob * 0.15f, 0.f, 0.8f), 5.f + p * 16.f, 0.3f + p * 0.6f);
    if (p > 0.7f && fr() < dt * 3.f) hap(90, 10);                    // the cap creaks
    float tr = wob * 5.f + p * p * 6.f;
    float ox = sinf(s_t * 47.f) * tr, oy = cosf(s_t * 39.f) * tr * 0.4f;
    if (s_t > 2.f && fmodf(s_t, 5.f) < 0.25f && p < 0.05f) ox += sinf(s_t * 60.f) * 2.f;   // an idle wobble: "shake me?"
    drawBottle(ox, oy);
    float capTw = p > 0.6f ? (fr() - 0.5f) * (p - 0.6f) * 12.f : 0.f;
    if (lidBack > 0) lidBack = fmaxf(0.f, lidBack - dt);
    drawCap(BX + ox, NTOP - 2 + oy + capTw - lidBack * 60.f * 0, capTw * 0.05f);
    if (p > 0.85f) for (int k = 0; k < 3; k++) canvas.fillCircle(BX + (int)ox + (int)((fr() - 0.5f) * 20.f), NTOP + 4 + (int)oy, 2, rgb565(240, 255, 220));
    if (p >= 1.f) {                                                   // POP
      phase = 1; fount = 2.4f; capX = BX; capY = NTOP; capVY = -520.f; capVX = (fr() - 0.5f) * 160.f; capRot = 0;
      hapGesture(HG_CRACK);
    }
  } else {
    // bottle, uncapped
    drawBottle(0, 0);
    if (phase == 1) {
      fount -= dt;
      level = fmaxf(0.f, level - dt * 0.38f);
      float k = clampf(fount / 2.4f, 0.f, 1.f);
      hapRumble(0.35f + k * 0.6f, 8.f + k * 10.f, 0.7f);
      for (int n = 0; n < 5; n++)
        for (auto &d : dr) if (!d.live) { d = {BX + (fr() - 0.5f) * 8.f, (float)NTOP, (fr() - 0.5f) * 110.f + sinf(s_t * 5.f) * 40.f, -(300.f + fr() * 240.f) * (0.35f + k * 0.65f), true}; break; }
      if (fount <= 0) phase = 2;
    }
    if (capY > -40) { capVY += 900.f * dt; capY += capVY * dt; capX += capVX * dt; capRot += dt * 14.f; drawCap(capX, capY, capRot); }
    // droplets: fly up, fall; most turn into foam bubbles, some stick to the wall and run down
    for (auto &d : dr) {
      if (!d.live) continue;
      d.vy += 700.f * dt; d.x += d.vx * dt; d.y += d.vy * dt;
      if (d.vy > -40.f && fr() < dt * 3.f) { if (fr() < 0.72f) bubble(d.x, d.y, d.vx * 0.2f, -20.f); else drip(d.x, d.y); d.live = false; continue; }
      if (d.y > H - 16) { bubble(d.x, H - 20.f, d.vx * 0.3f, -40.f); d.live = false; continue; }
      canvas.fillCircle((int)d.x, (int)d.y, 2, rgb565(160, 255, 70));
    }
    int alive = 0;
    for (auto &b : bb) {
      if (!b.live) continue;
      alive++;
      b.ph += dt * 3.f; b.life -= dt;
      b.vx += sinf(b.ph) * 20.f * dt; b.vy += (-26.f - b.vy) * dt;           // buoyant, lazy
      b.x += b.vx * dt; b.y += b.vy * dt;
      if (b.x < b.r || b.x > W - b.r) b.vx = -b.vx;
      if (b.y < 16 + b.r) { b.y = 16 + b.r; b.vy = 4.f; }
      if (b.life <= 0) { b.live = false; spark(b.x, b.y, 0, -10, rgb565(230, 255, 210), 0.3f); continue; }
      canvas.drawCircle((int)b.x, (int)b.y, (int)b.r, rgb565(200, 255, 170));
      canvas.drawPixel((int)(b.x - b.r * 0.4f), (int)(b.y - b.r * 0.4f), rgb565(255, 255, 255));
      if (fr() < 0.03f) canvas.fillCircle((int)b.x + (int)b.r / 2, (int)b.y - (int)b.r / 2, 1, rgb565(255, 255, 255));   // sparkle
    }
    int drops = 0; for (auto &d : dr) drops += d.live;
    int drips = 0; for (auto &d : dp) drips += d.live;
    if (phase == 2 && alive == 0 && drops == 0 && drips == 0) { phase = 3; lidBack = 1.f; }
    if (phase == 3) {
      lidBack = fmaxf(0.f, lidBack - dt * 0.9f);
      level = fminf(1.f, level + dt * 0.9f);
      drawCap(BX, NTOP - 2 - lidBack * 70.f, 0);
      if (lidBack <= 0 && level >= 1.f) { phase = 0; p = 0; s_solved = true; hapGesture(HG_SETTLE); }
    }
  }
}
void tap(int x, int y) {
  for (auto &b : bb)
    if (b.live && (b.x - x) * (b.x - x) + (b.y - y) * (b.y - y) < (b.r + 7) * (b.r + 7)) {
      b.live = false; hap(130, 10);
      for (int k = 0; k < 5; k++) spark(b.x, b.y, (fr() - 0.5f) * 80.f, (fr() - 0.5f) * 80.f, rgb565(230, 255, 210), 0.35f);
    }
}
}  // namespace nrg

// ============================================================
//  2  BREEZE — blow on the pinwheel
// ============================================================
namespace breeze {
float ang = 0, w = 0, spinT = 0;
void enter() { ang = 0; w = 0; spinT = 0; }
void draw(float dt) {
  bg(rgb565(90, 170, 230), rgb565(210, 240, 250));
  canvas.fillEllipse(160, 250, 260, 60, rgb565(80, 170, 70));
  // blowing = loud AND noisy (breath is hiss, not tone); any sound nudges it a little
  float blow = clampf((aud::level - 0.3f) * 2.f, 0.f, 1.f) * (aud::zcr > 0.3f ? 1.f : 0.25f);
  w += (blow * 16.f + aud::level * 1.2f) * dt - w * 0.45f * dt;
  ang += w * dt;
  spinT = w > 8.f ? spinT + dt : fmaxf(0.f, spinT - dt * 0.5f);
  if (spinT > 2.f && !s_solved) { s_solved = true; burst(160, 104, 40, 160); hapGesture(HG_CHAIN); }
  if (w > 1.5f) hapRumble(clampf(w / 20.f * 0.4f, 0.f, 0.45f), 3.f + w, 0.25f);
  if (blow > 0.15f) for (int k = 0; k < 6; k++) { int y = 40 + (int)(fr() * 140); int x = (int)(fr() * W); canvas.drawFastHLine(x, y, 18 + (int)(blow * 30), rgb565(240, 250, 255)); }
  canvas.fillRect(157, 104, 6, 118, rgb565(150, 110, 70));                      // stick
  static const uint16_t C[4] = {wire::TEAL, 0x780F, wire::LIME, rgb565(255, 140, 40)};
  for (int k = 0; k < 4; k++) {
    float a = ang + k * 1.5708f, b = a + 0.9f;
    int x1 = 160 + (int)(cosf(a) * 52), y1 = 104 + (int)(sinf(a) * 52);
    int x2 = 160 + (int)(cosf(b) * 30), y2 = 104 + (int)(sinf(b) * 30);
    canvas.fillTriangle(160, 104, x1, y1, x2, y2, C[k]);
    canvas.fillTriangle(160, 104, x1, y1, 160 + (int)(cosf(a + 0.25f) * 24), 104 + (int)(sinf(a + 0.25f) * 24), mix565(C[k], 0xFFFF, 0.35f));
  }
  canvas.fillCircle(160, 104, 5, rgb565(255, 230, 120));
  if (s_solved) canvas.drawCircle(160, 104, 60 + (int)(sinf(s_t * 4.f) * 3), wire::LIME);
}
void tap(int, int) {}
}  // namespace breeze

// ============================================================
//  3  ARCADE — mash the red button until the mantis on the TV strikes
// ============================================================
namespace arcade {
float charge = 0, press = 0, bx = 120, by = 70, bvx = 60, bvy = 40, strike = 0, jolt = 0;
bool caught = false;
void enter() { charge = 0; caught = false; strike = 0; bx = 120; by = 70; }
void draw(float dt) {
  bg(rgb565(30, 20, 40), rgb565(12, 8, 18));
  // TV (it jolts with every press)
  jolt = fmaxf(0.f, jolt - dt * 8.f);
  int jx = (int)(sinf(s_t * 90.f) * jolt * 3.f);
  canvas.fillRoundRect(62 + jx, 20, 196, 118, 14, rgb565(70, 40, 30));
  canvas.fillRoundRect(72, 28, 176, 100, 10, rgb565(8, 30, 14));
  for (int y = 30; y < 126; y += 3) canvas.drawFastHLine(74, y, 172, rgb565(4, 22, 10));
  if (!caught) {                                                              // the bug buzzes about
    bvx += (fr() - 0.5f) * 600.f * dt; bvy += (fr() - 0.5f) * 600.f * dt;
    bvx = clampf(bvx, -90, 90); bvy = clampf(bvy, -70, 70);
    bx += bvx * dt; by += bvy * dt;
    if (bx < 82 || bx > 200) bvx = -bvx; if (by < 36 || by > 90) bvy = -bvy;
    bx = clampf(bx, 82, 200); by = clampf(by, 36, 90);
  }
  float tense = clampf(charge, 0.f, 1.f);
  float arm = caught ? 0.2f : (strike > 0 ? 1.f : 0.1f + tense * 0.5f);
  mantisV(222 + jx, 96 + tense * 6.f, 0.9f, arm * 0.3f, arm, tense, mix565(rgb565(90, 230, 110), rgb565(255, 230, 90), tense));
  if (caught) { bx = 222 - 18; by = 64; }
  canvas.fillCircle((int)bx, (int)by, 3, rgb565(20, 20, 20));
  canvas.drawLine((int)bx - 4, (int)by - 3 + (int)(sinf(s_t * 60.f) * 2), (int)bx, (int)by, rgb565(200, 230, 255));
  canvas.drawLine((int)bx + 4, (int)by - 3 - (int)(sinf(s_t * 60.f) * 2), (int)bx, (int)by, rgb565(200, 230, 255));
  if (caught) textC("GOT IT", 108, wire::LIME, 2);
  // controller + big red button
  canvas.fillRoundRect(40, 150, 240, 70, 16, rgb565(40, 40, 50));
  canvas.fillCircle(80, 185, 10, rgb565(20, 20, 24)); canvas.fillRect(77, 168, 6, 18, rgb565(60, 60, 70)); canvas.fillCircle(80, 166, 7, rgb565(200, 30, 40));
  canvas.fillCircle(236, 176, 6, rgb565(60, 180, 220)); canvas.fillCircle(252, 192, 6, rgb565(240, 200, 40));
  press = fmaxf(0.f, press - dt * 8.f);
  int d = (int)(press * 5.f);
  float glow = 0.5f + 0.5f * sinf(s_t * 4.f);
  canvas.fillCircle(160, 188 + d, 28, rgb565(90, 10, 14));
  canvas.fillCircle(160, 184 + d, 26 - d / 2, mix565(rgb565(200, 20, 30), rgb565(255, 70, 70), glow * 0.4f));
  canvas.fillCircle(152, 176 + d, 7, rgb565(255, 150, 150));
  charge = fmaxf(0.f, charge - dt * 0.22f);
  for (int k = 0; k < 10; k++) {                                                  // power meter on the controller
    bool on = charge * 10.f > k;
    uint16_t c = k < 6 ? wire::LIME : (k < 8 ? rgb565(255, 200, 40) : rgb565(255, 60, 40));
    canvas.fillRoundRect(112 + k * 10, 156, 8, 8, 2, on ? c : rgb565(50, 50, 60));
  }
  if (!caught && charge >= 1.f) { strike = 0.35f; caught = true; s_solved = true; hapGesture(HG_CRACK); burst(222, 64, 30, 120); }
  strike = fmaxf(0.f, strike - dt);
  if (charge > 0.2f && !caught) hapRumble(charge * 0.35f, 6.f + charge * 12.f, 0.3f);
}
void tap(int x, int y) {
  if ((x - 160) * (x - 160) + (y - 186) * (y - 186) < 36 * 36) {
    press = 1.f; charge += 0.085f; jolt = 1.f;
    hap((uint8_t)(90 + fminf(charge, 1.f) * 160.f), 16);                         // it gets stronger as it builds
    for (int k = 0; k < 6; k++) spark(160, 176, (fr() - 0.5f) * 160.f, -fr() * 140.f, k & 1 ? rgb565(255, 220, 80) : rgb565(255, 90, 60), 0.35f);
  }
}
}  // namespace arcade

// ============================================================
//  4  WRAP — pop every bubble
// ============================================================
namespace wrap {
const int C = 8, R = 5;
bool popped[C * R];
int left = C * R;
void enter() { memset(popped, 0, sizeof(popped)); left = C * R; }
bool pos(int i, int &x, int &y) { int r = i / C, c = i % C; x = 34 + c * 36 + (r & 1) * 18; y = 44 + r * 36; return x < W - 20; }
void pop(int i) {
  if (popped[i]) return;
  int x, y; pos(i, x, y);
  popped[i] = true; left--; hapGesture(HG_POP);
  for (int k = 0; k < 6; k++) spark(x, y, (fr() - 0.5f) * 90.f, (fr() - 0.5f) * 90.f, rgb565(230, 250, 255), 0.3f);
  if (left <= 0 && !s_solved) { s_solved = true; hapGesture(HG_SETTLE); }
}
void draw(float dt) {
  bg(rgb565(60, 50, 70), rgb565(30, 25, 40));
  canvas.fillRoundRect(12, 22, 296, 196, 10, rgb565(170, 200, 210));
  canvas.fillRoundRect(14, 24, 292, 192, 9, rgb565(200, 225, 232));
  int tx, ty;
  static bool was = false;
  bool now = touchNow(tx, ty);
  if (now) {                                                                      // first contact pops; rolling pops too
    int rr = was ? 15 : 19;
    for (int i = 0; i < C * R; i++) { int x, y; if (pos(i, x, y) && (x - tx) * (x - tx) + (y - ty) * (y - ty) < rr * rr) { pop(i); if (!was) break; } }
  }
  was = now;
  for (int i = 0; i < C * R; i++) {
    int x, y; if (!pos(i, x, y)) { if (!popped[i]) { popped[i] = true; left--; } continue; }
    if (!popped[i]) {
      canvas.fillCircle(x, y, 14, rgb565(160, 195, 205));
      canvas.fillCircle(x, y, 12, rgb565(215, 238, 245));
      canvas.fillCircle(x - 4, y - 5, 4, rgb565(255, 255, 255));
      canvas.drawCircle(x, y, 14, rgb565(140, 175, 190));
    } else {
      canvas.drawLine(x - 8, y - 2, x - 2, y + 3, rgb565(140, 170, 180)); canvas.drawLine(x - 2, y + 3, x + 4, y - 3, rgb565(140, 170, 180));
      canvas.drawLine(x + 4, y - 3, x + 9, y + 2, rgb565(140, 170, 180));
    }
  }
}
void tap(int x, int y) { for (int i = 0; i < C * R; i++) { int bx, by; if (pos(i, bx, by) && (bx - x) * (bx - x) + (by - y) * (by - y) < 19 * 19) { pop(i); break; } } }
}  // namespace wrap

// ============================================================
//  5  CLAP — clap along with the mantis, three in time
// ============================================================
namespace clap {
const float BEAT = 0.625f;                          // 96 bpm
int streak = 0; float lastGood = -9, flash = 0, lastBeatIdx = -1; uint8_t hist = 0;
bool wantBeat = false;
void enter() { streak = 0; lastGood = -9; lastBeatIdx = -1; hist = 0; }
void draw(float dt) {
  bg(rgb565(20, 6, 30), rgb565(6, 4, 10));
  canvas.fillTriangle(160, 14, 60, 226, 260, 226, rgb565(40, 22, 56));        // spotlight
  float ph = fmodf(s_t, BEAT) / BEAT;
  float arm = ph < 0.12f ? 0.f : 0.25f + 0.75f * sinf((ph - 0.12f) / 0.88f * 3.1416f);   // arms snap together on the beat
  int bi = (int)(s_t / BEAT);
  if (bi != lastBeatIdx) { lastBeatIdx = (float)bi; flash = 1.f; if (streak > 0 && s_t - lastGood > BEAT * 4.5f && !s_solved) { streak = 0; hist = 0; } }
  flash = fmaxf(0.f, flash - dt * 5.f);
  if (s_solved) arm = 1.f;
  mantisV(160, 132, 1.6f, arm, arm, s_solved ? 1.f : flash * 0.6f);
  if (flash > 0.3f && !s_solved) for (int k = 0; k < 6; k++) { float a = k * 1.047f; canvas.drawLine(160 + (int)(cosf(a) * 14), 96 + (int)(sinf(a) * 14), 160 + (int)(cosf(a) * 24), 96 + (int)(sinf(a) * 24), rgb565(255, 240, 150)); }
  // your clap: a sharp, bright transient near the beat
  if (!s_solved && clapSpike(dt)) {
    static float lastClap = -9;
    if (s_t - lastClap > 0.2f) {                                                  // one clap = one event
      lastClap = s_t;
      float heard = s_t - 0.13f;                                                  // the mic + analysis hear it ~130 ms late
      float err = fmodf(heard + BEAT * 0.5f, BEAT) - BEAT * 0.5f;                 // distance to the nearest beat
      bool good = fabsf(err) < 0.31f;                                             // generous: 'on it' feels on it
#ifdef CLAP_LOG
      printf("clap heard t=%.3f err=%+.3f good=%d\n", s_t, err, good);
#endif
      hist = (uint8_t)(((hist << 1) | (good ? 1 : 0)) & 0x0F);
      int n = 0; for (int k = 0; k < 4; k++) n += (hist >> k) & 1;
      streak = n;
      if (good) { lastGood = s_t; hap(110, 18); burst(160, 90, 8, 60); }
      if (n >= 3) { s_solved = true; hapGesture(HG_CHAIN); burst(160, 90, 50, 180); }
    }
  }
  for (int k = 0; k < 3; k++) {
    int x = 136 + k * 24;
    if (k < streak || s_solved) canvas.fillCircle(x, 30, 7, wire::LIME); else canvas.drawCircle(x, 30, 7, rgb565(80, 80, 100));
  }
}
void tap(int, int) {}
}  // namespace clap

// ============================================================
//  6  SEED — tip the watering can to water the seed
// ============================================================
namespace seed {
float moist = 0, canA = 0;
struct D { float x, y, vy; bool live; };
D d[60];
void enter() { moist = 0; for (auto &q : d) q.live = false; }
void plant(float g, int x, int y) {                     // 0 seed .. 1 bloom
  uint16_t stem = rgb565(80, 190, 70);
  if (g < 0.08f) { canvas.fillEllipse(x, y - 3, 4, 3, rgb565(120, 80, 40)); return; }
  int h = (int)(10 + g * 80);
  canvas.fillRect(x - 1, y - h, 3, h, stem);
  int leaves = (int)(g * 5);
  for (int k = 0; k < leaves; k++) {
    int ly = y - 10 - k * 15, s = (k & 1) ? 1 : -1;
    canvas.fillEllipse(x + s * 9, ly, 9, 4, rgb565(100, 210, 80));
  }
  if (g > 0.85f) {
    float o = clampf((g - 0.85f) / 0.15f, 0.f, 1.f);
    for (int k = 0; k < 6; k++) { float a = k * 1.047f + s_t * 0.3f; canvas.fillCircle(x + (int)(cosf(a) * 9 * o), y - h + (int)(sinf(a) * 9 * o), (int)(6 * o) + 1, k & 1 ? wire::LIME : rgb565(200, 60, 200)); }
    canvas.fillCircle(x, y - h, 5, rgb565(255, 220, 80));
  }
}
void draw(float dt) {
  bg(rgb565(150, 210, 240), rgb565(240, 230, 200));
  canvas.fillRect(0, 196, W, 30, rgb565(120, 90, 60));
  // pot
  canvas.fillTriangle(130, 170, 190, 170, 182, 214, rgb565(190, 90, 50)); canvas.fillTriangle(130, 170, 182, 214, 138, 214, rgb565(190, 90, 50));
  canvas.fillRect(126, 164, 68, 10, rgb565(210, 110, 60));
  canvas.fillEllipse(160, 170, 30, 5, rgb565(80, 50, 30));
  plant(moist, 160, 168);
  // watering can: tip the Core2 right to pour
  float target = clampf(g_gravX * 80.f, -10.f, 80.f) * 0.01745f;
  canA += (target - canA) * clampf(dt * 8.f, 0.f, 1.f);
  if (s_t > 1.5f && fmodf(s_t, 4.f) < 0.3f && canA < 0.2f) canA += sinf(s_t * 30.f) * 0.03f;   // a little wiggle: "tip me"
  float cx = 112, cy = 62, c = cosf(canA), s = sinf(canA);
  auto P = [&](float lx, float ly, int &ox, int &oy) { ox = (int)(cx + c * lx - s * ly); oy = (int)(cy + s * lx + c * ly); };
  int a0, b0, a1, b1, a2, b2, a3, b3;
  P(-24, -16, a0, b0); P(20, -16, a1, b1); P(20, 18, a2, b2); P(-24, 18, a3, b3);
  canvas.fillTriangle(a0, b0, a1, b1, a2, b2, rgb565(70, 150, 170)); canvas.fillTriangle(a0, b0, a2, b2, a3, b3, rgb565(70, 150, 170));
  P(20, 4, a0, b0); P(52, -10, a1, b1); P(52, -4, a2, b2); P(20, 12, a3, b3);
  canvas.fillTriangle(a0, b0, a1, b1, a2, b2, rgb565(60, 130, 150)); canvas.fillTriangle(a0, b0, a2, b2, a3, b3, rgb565(60, 130, 150));
  P(-24, -8, a0, b0); P(-36, 0, a1, b1); P(-24, 10, a2, b2);
  canvas.drawLine(a0, b0, a1, b1, rgb565(50, 110, 130)); canvas.drawLine(a1, b1, a2, b2, rgb565(50, 110, 130));
  int spx, spy; P(54, -8, spx, spy);
  if (canA > 0.45f && moist < 1.f)
    for (int n = 0; n < 2; n++) for (auto &q : d) if (!q.live) { q = {spx + (fr() - 0.5f) * 4.f, (float)spy, 30.f, true}; break; }
  for (auto &q : d) {
    if (!q.live) continue;
    q.vy += 500.f * dt; q.y += q.vy * dt; q.x += g_gravX * 30.f * dt;
    if (q.y > 166 && q.x > 132 && q.x < 188) { moist = fminf(1.f, moist + 0.006f); q.live = false; if (fr() < 0.2f) hap(40, 6); continue; }
    if (q.y > 196) { q.live = false; continue; }
    canvas.fillCircle((int)q.x, (int)q.y, 2, rgb565(90, 170, 255));
  }
  if (moist >= 1.f && !s_solved) { s_solved = true; burst(160, 80, 40, 140); hapGesture(HG_SETTLE); }
}
void tap(int, int) {}
}  // namespace seed

// ============================================================
//  7  SHADE — drag the cloud off the sun so the sprout can grow
// ============================================================
namespace shade {
float cx = 200, cy = 70, grow = 0, dragDX = 0, dragDY = 0;
bool dragging = false;
void enter() { cx = 214; cy = 62; grow = 0; dragging = false; }
void draw(float dt) {
  const float SX = 270, SY = 40, PX = 120, PY = 196;
  // is the line sun -> plant blocked by the cloud?
  float vx = PX - SX, vy = PY - SY, L = sqrtf(vx * vx + vy * vy), u = ((cx - SX) * vx + (cy - SY) * vy) / (L * L);
  u = clampf(u, 0.f, 1.f);
  float qx = SX + vx * u - cx, qy = SY + vy * u - cy, dist = sqrtf(qx * qx + qy * qy);
  bool lit = dist > 46.f;
  bg(lit ? rgb565(120, 200, 245) : rgb565(80, 110, 140), lit ? rgb565(250, 240, 200) : rgb565(150, 150, 150));
  canvas.fillRect(0, 200, W, 26, rgb565(90, 150, 60));
  canvas.fillCircle((int)SX, (int)SY, 20, rgb565(255, 220, 80));
  for (int k = 0; k < 10; k++) { float a = k * 0.628f + s_t * 0.2f; canvas.drawLine((int)SX + (int)(cosf(a) * 24), (int)SY + (int)(sinf(a) * 24), (int)SX + (int)(cosf(a) * 32), (int)SY + (int)(sinf(a) * 32), rgb565(255, 230, 120)); }
  if (lit) for (int k = -2; k <= 2; k++) canvas.drawLine((int)SX, (int)SY, (int)PX + k * 6, (int)PY - 20, rgb565(255, 240, 170));
  // the sprout: droops in shade, straightens and grows in sun
  grow = lit ? fminf(1.f, grow + dt * 0.22f) : fmaxf(0.f, grow - dt * 0.02f);
  float droop = lit ? 0.f : 0.6f;
  int h = (int)(18 + grow * 70);
  int tx = (int)(PX + droop * 20.f), ty = (int)PY - h + (int)(droop * 10.f);
  canvas.drawLine((int)PX, (int)PY, tx, ty, rgb565(70, 170, 60)); canvas.drawLine((int)PX + 1, (int)PY, tx + 1, ty, rgb565(70, 170, 60));
  for (int k = 0; k < (int)(1 + grow * 4); k++) { int ly = (int)PY - 12 - k * 14; canvas.fillEllipse((int)PX + ((k & 1) ? 8 : -8) + (int)(droop * 6), ly, 8, 3, rgb565(100, 200, 80)); }
  if (grow > 0.9f) { for (int k = 0; k < 5; k++) { float a = k * 1.2566f; canvas.fillCircle(tx + (int)(cosf(a) * 7), ty + (int)(sinf(a) * 7), 5, rgb565(255, 120, 60)); } canvas.fillCircle(tx, ty, 4, rgb565(255, 230, 80)); }
  // the cloud drifts back toward the sun unless you keep it away
  int x, y;
  if (touchNow(x, y)) {
    if (!dragging && (x - cx) * (x - cx) + (y - cy) * (y - cy) < 50 * 50) { dragging = true; dragDX = cx - x; dragDY = cy - y; }
    if (dragging) { cx = x + dragDX; cy = y + dragDY; }
  } else {
    dragging = false;
    cx += (SX - 50 - cx) * dt * 0.06f; cy += (SY + 24 - cy) * dt * 0.06f;
  }
  cx = clampf(cx, -40.f, W + 40.f); cy = clampf(cy, 20.f, 170.f);
  uint16_t cc = rgb565(110, 110, 125), cd = rgb565(80, 80, 95);
  canvas.fillCircle((int)cx - 26, (int)cy + 6, 20, cd); canvas.fillCircle((int)cx + 24, (int)cy + 8, 18, cd);
  canvas.fillCircle((int)cx, (int)cy, 28, cc); canvas.fillCircle((int)cx - 24, (int)cy + 4, 18, cc); canvas.fillCircle((int)cx + 22, (int)cy + 6, 16, cc);
  if (grow >= 1.f && !s_solved) { s_solved = true; burst(tx, ty, 40, 140); hapGesture(HG_SETTLE); }
}
void tap(int, int) {}
}  // namespace shade

// ============================================================
//  8  ALIGN — tip the rows into line, hold level to lock each one
// ============================================================
namespace align {
float pos[3], still[3]; bool lock[3];
const float RATE[3] = {1.f, -0.62f, 1.55f};
void enter() { pos[0] = 90; pos[1] = -120; pos[2] = 200; for (int i = 0; i < 3; i++) { still[i] = 0; lock[i] = false; } }
void emblemRow(int row, int cx, int y0) {
  uint16_t L = wire::LIME, T = rgb565(40, 200, 190), P = rgb565(180, 60, 190);
  if (row == 0) {
    canvas.drawLine(cx - 8, y0 + 50, cx - 30, y0 + 8, L); canvas.drawLine(cx + 8, y0 + 50, cx + 30, y0 + 8, L);
    canvas.fillCircle(cx - 30, y0 + 8, 4, P); canvas.fillCircle(cx + 30, y0 + 8, 4, P);
    canvas.fillTriangle(cx - 40, y0 + 44, cx + 40, y0 + 44, cx, y0 + 30, T);
  } else if (row == 1) {
    canvas.fillTriangle(cx - 44, y0, cx + 44, y0, cx, y0 + 54, T);
    canvas.fillCircle(cx - 26, y0 + 12, 13, L); canvas.fillCircle(cx + 26, y0 + 12, 13, L);
    canvas.fillCircle(cx - 26, y0 + 12, 6, rgb565(20, 10, 30)); canvas.fillCircle(cx + 26, y0 + 12, 6, rgb565(20, 10, 30));
  } else {
    canvas.fillTriangle(cx - 10, y0, cx + 10, y0, cx, y0 + 18, T);
    canvas.drawLine(cx - 20, y0 + 6, cx - 54, y0 + 34, L); canvas.drawLine(cx - 54, y0 + 34, cx - 36, y0 + 50, L);
    canvas.drawLine(cx + 20, y0 + 6, cx + 54, y0 + 34, L); canvas.drawLine(cx + 54, y0 + 34, cx + 36, y0 + 50, L);
    canvas.fillCircle(cx - 36, y0 + 50, 4, P); canvas.fillCircle(cx + 36, y0 + 50, 4, P);
  }
}
void draw(float dt) {
  bg(rgb565(14, 10, 26), rgb565(6, 20, 24));
  int nLock = 0;
  for (int i = 0; i < 3; i++) {
    int y0 = 30 + i * 60;
    if (!lock[i]) {
      float gt = fabsf(g_gravX) < 0.08f ? 0.f : g_gravX - copysignf(0.08f, g_gravX);   // level = the rows rest
      pos[i] += RATE[i] * gt * 200.f * dt;
      if (pos[i] > 160) pos[i] -= 320; if (pos[i] < -160) pos[i] += 320;
      bool near = fabsf(pos[i]) < 7.f, level = fabsf(g_gravX) < 0.1f;
      still[i] = (near && level) ? still[i] + dt : 0.f;
      if (still[i] > 0.4f) { lock[i] = true; pos[i] = 0; hap(160, 20); burst(160, y0 + 28, 10, 70); }
    }
    nLock += lock[i];
    canvas.fillRect(0, y0, W, 58, lock[i] ? rgb565(20, 40, 36) : rgb565(18, 14, 30));
    canvas.setClipRect(0, y0, W, 58);
    emblemRow(i, 160 + (int)pos[i], y0);
    emblemRow(i, 160 + (int)pos[i] + 320, y0); emblemRow(i, 160 + (int)pos[i] - 320, y0);
    canvas.clearClipRect();
    canvas.fillTriangle(156, y0, 164, y0, 160, y0 + 5, rgb565(90, 90, 110));        // quiet notches mark the centre
    canvas.fillTriangle(156, y0 + 57, 164, y0 + 57, 160, y0 + 52, rgb565(90, 90, 110));
  }
  if (nLock == 3 && !s_solved) { s_solved = true; hapGesture(HG_CHAIN); burst(160, 110, 60, 180); }
  if (s_solved) canvas.drawRoundRect(98, 28, 124, 184, 12, wire::LIME);
}
void tap(int, int) {}
}  // namespace align

// ============================================================
//  9  KNOCK — knock on the door (the case itself counts)
// ============================================================
namespace knock {
int n = 0; float lastK = -9, shake = 0, open = 0, cool = 0;
void enter() { n = 0; lastK = -9; open = 0; }
void doKnock() {
  if (s_t - lastK > 1.6f) n = 0;
  n++; lastK = s_t; shake = 1.f; hap(210, 16);
  if (n >= 3 && open <= 0) { open = 0.001f; }
}
void draw(float dt) {
  bg(rgb565(30, 20, 50), rgb565(10, 30, 20));
  canvas.fillEllipse(160, 240, 250, 110, rgb565(40, 90, 40));
  // knocks on the case: a sharp jolt while nobody touches the screen and the device isn't being swung
  int tx, ty;
  cool -= dt;
  if (!touchNow(tx, ty) && g_jolt > 0.22f && fabsf(g_gyroX) + fabsf(g_gyroY) + fabsf(g_gyroZ) < 90.f && cool <= 0) { doKnock(); cool = 0.14f; }
  shake = fmaxf(0.f, shake - dt * 6.f);
  int sx = (int)(sinf(s_t * 70.f) * shake * 3.f);
  canvas.fillCircle(160 + sx, 150, 64, rgb565(90, 60, 30));
  if (open > 0) {
    open = fminf(1.f, open + dt * 0.8f);
    canvas.fillCircle(160, 150, 56, rgb565(255, 200, 110));                        // warm light
    mantisV(160, 150 - open * 20.f, 1.2f, 0.2f, 0.4f + 0.6f * fabsf(sinf(s_t * 6.f)) * open, open);   // peeks out, waves
    int w = (int)(56 * (1.f - open));
    if (w > 2) canvas.fillRect(104, 94, w, 112, rgb565(140, 90, 40));
    if (open >= 1.f && !s_solved) { s_solved = true; hapGesture(HG_SETTLE); burst(160, 130, 30, 120); }
  } else {
    canvas.fillCircle(160 + sx, 150, 56, rgb565(140, 90, 40));
    for (int k = -3; k <= 3; k++) canvas.drawFastVLine(160 + sx + k * 14, 96, 108, rgb565(110, 70, 30));
    canvas.fillCircle(160 + sx, 124, 12, rgb565(255, 200, 110)); canvas.drawCircle(160 + sx, 124, 12, rgb565(80, 50, 20));
    canvas.fillCircle(196 + sx, 156, 4, rgb565(230, 190, 80));
  }
  canvas.fillRect(0, 206, W, 20, rgb565(40, 90, 40));
  if (shake > 0.5f) textC("knock", 60, rgb565(255, 230, 180), 1);
}
void tap(int x, int y) { if ((x - 160) * (x - 160) + (y - 150) * (y - 150) < 56 * 56 && open <= 0) doKnock(); }
}  // namespace knock

// ============================================================
//  10  GLOBE — shake the snow globe, then hold it perfectly still
// ============================================================
namespace globe {
struct F { float x, y, vx, vy; bool up; };
F f[150];
float stillT = 0, reveal = 0; bool shaken = false;
const float GX = 160, GY = 112, GR = 80;
void enter() { stillT = 0; reveal = 0; shaken = false; for (auto &q : f) { q.x = GX + (fr() - 0.5f) * 120.f; q.y = GY + GR - 6 - fr() * 26.f; q.vx = q.vy = 0; q.up = false; } }
void draw(float dt) {
  bg(rgb565(12, 16, 40), rgb565(30, 20, 50));
  float ag = agitation();
  canvas.fillCircle((int)GX, (int)GY, (int)GR + 2, rgb565(120, 150, 190));
  canvas.fillCircle((int)GX, (int)GY, (int)GR, rgb565(20, 40, 80));
  float cover = 0; int up = 0;
  // the crystal (glows when uncovered) and a tiny mantis beside it
  mantisV(GX - 30, GY + 26, 0.6f, 0.3f, 0.3f, reveal);
  for (auto &q : f) {
    if (ag > 0.35f && fr() < ag * 0.25f) { q.up = true; q.vx += (fr() - 0.5f) * 300.f * ag; q.vy -= fr() * 260.f * ag; shaken = true; }
    if (q.up) {
      q.vx += sinf(s_t * 1.3f + q.y * 0.05f) * 20.f * dt; q.vy += (14.f - q.vy) * dt * 1.5f;   // drifting down slowly
      q.vx *= 1.f - dt * 1.2f;
      q.x += q.vx * dt; q.y += q.vy * dt;
      float dx = q.x - GX, dy = q.y - GY, d = sqrtf(dx * dx + dy * dy);
      if (d > GR - 3) {                                                      // slide along the glass, don't bounce off it
        float nx = dx / d, ny = dy / d, vr = q.vx * nx + q.vy * ny;
        q.x = GX + nx * (GR - 3); q.y = GY + ny * (GR - 3);
        if (vr > 0) { q.vx -= vr * nx * 1.3f; q.vy -= vr * ny * 1.3f; }
      }
      if (q.y > GY + GR - 8 - fr() * 18.f && q.vy >= 0) {
        q.up = false;
        if (stillT > 1.f && fabsf(q.x - (GX + 22)) < 16) q.x += (q.x < GX + 22 ? -18.f : 18.f);   // settling calmly, it slides off the crystal
      }
      up++;
    } else if (fabsf(q.x - (GX + 22)) < 14 && q.y > GY + GR - 32) {
      cover += 1;
      if (stillT > 0.8f) q.x += (q.x < GX + 22 ? -1.f : 1.f) * dt * 24.f;          // the warm crystal sheds snow while you're still
    }
    canvas.fillCircle((int)q.x, (int)q.y, 1, rgb565(240, 245, 255));
  }
  stillT = ag < 0.08f ? stillT + dt : 0.f;
  float glow = clampf(1.f - cover / 12.f, 0.f, 1.f);
  if (shaken && up == 0 && glow > 0.9f) reveal = fminf(1.f, reveal + dt * 0.7f);
#ifdef GLOBE_DEBUG
  { static int fc = 0; if (++fc % 60 == 0) printf("globe up=%d cover=%.0f glow=%.2f still=%.1f shaken=%d reveal=%.2f ag=%.2f\n", up, cover, glow, stillT, shaken, reveal, ag); }
#endif
  uint16_t cc = mix565(rgb565(80, 90, 120), wire::LIME, glow * (0.4f + 0.6f * reveal));
  canvas.fillTriangle((int)GX + 22, (int)(GY + GR - 40), (int)GX + 14, (int)(GY + GR - 14), (int)GX + 30, (int)(GY + GR - 14), cc);
  if (reveal > 0) for (int k = 0; k < 8; k++) { float a = k * 0.785f + s_t; canvas.drawLine((int)GX + 22, (int)(GY + GR - 28), (int)GX + 22 + (int)(cosf(a) * 20 * reveal), (int)(GY + GR - 28) + (int)(sinf(a) * 20 * reveal), wire::LIME); }
  canvas.drawCircle((int)GX, (int)GY, (int)GR, rgb565(200, 220, 255));
  canvas.drawCircle((int)GX - 30, (int)GY - 34, 18, rgb565(90, 110, 150));
  canvas.fillRoundRect((int)GX - 70, (int)(GY + GR - 6), 140, 28, 8, rgb565(110, 70, 40));
  if (reveal >= 1.f && !s_solved) { s_solved = true; hapGesture(HG_SETTLE); }
}
void tap(int, int) {}
}  // namespace globe

// ============================================================
//  11  HUM — hum a steady note until the crystal glass sings and shatters
// ============================================================
namespace hum {
float res = 0, shatter = 0; int lastPk = -1; float stableT = 0;
struct S { float x, y, vx, vy, a, va; bool live; };
S sh[30];
void enter() { res = 0; shatter = 0; lastPk = -1; stableT = 0; for (auto &s : sh) s.live = false; }
void draw(float dt) {
  bg(rgb565(16, 8, 30), rgb565(6, 14, 30));
  int pk = 1; float pv = 0;
  for (int b = 1; b < 20; b++) if (aud::bands[b] > pv) { pv = aud::bands[b]; pk = b; }
  bool tonal = aud::level > 0.18f && aud::zcr < 0.3f && pv > 0.35f;
  bool steady = tonal && lastPk >= 0 && abs(pk - lastPk) <= 1;
  lastPk = tonal ? pk : -1;
  stableT = steady ? stableT + dt : fmaxf(0.f, stableT - dt * 2.f);
  if (shatter <= 0) res = steady ? fminf(1.f, res + dt * 0.3f) : fmaxf(0.f, res - dt * 0.45f);
  float hue = pk * 18.f;
  if (res > 0.05f && shatter <= 0) hapRumble(res * 0.5f, 10.f + pk * 2.f, 0.1f);
  // sound rings
  if (res > 0.05f) for (int k = 0; k < 3; k++) { int r = (int)(fmodf(s_t * 60.f + k * 30.f, 90.f)); canvas.drawCircle(160, 90, 30 + r, hsv565(hue, 0.6f, res * (1.f - r / 90.f))); }
  if (shatter <= 0) {
    float wob = sinf(s_t * 70.f) * res * 3.f;
    uint16_t gc = mix565(rgb565(170, 210, 240), hsv565(hue, 0.7f, 1.f), res);
    canvas.drawLine(130 + (int)wob, 50, 142, 120, gc); canvas.drawLine(190 - (int)wob, 50, 178, 120, gc);
    canvas.drawEllipse(160, 50, 30 + (int)wob, 7, gc);
    canvas.drawLine(142, 120, 178, 120, gc);
    canvas.drawFastVLine(160, 120, 60, gc); canvas.drawEllipse(160, 182, 26, 5, gc);
    canvas.drawLine(136, 60, 146, 110, rgb565(255, 255, 255));
    if (res >= 1.f) {
      shatter = 2.2f; hapGesture(HG_CRACK);
      for (auto &s : sh) s = {160 + (fr() - 0.5f) * 50.f, 50 + fr() * 70.f, (fr() - 0.5f) * 300.f, -fr() * 250.f, fr() * 6.f, (fr() - 0.5f) * 20.f, true};
      burst(160, 90, 40, 200);
    }
  } else {
    shatter -= dt;
    for (auto &s : sh) {
      if (!s.live) continue;
      if (shatter > 1.f) { s.vy += 500.f * dt; s.x += s.vx * dt; s.y += s.vy * dt; s.a += s.va * dt; }
      else { s.x += (160 - s.x) * dt * 3.f; s.y += (90 - s.y) * dt * 3.f; }                  // ...and it re-forms
      int ex = (int)(cosf(s.a) * 6), ey = (int)(sinf(s.a) * 6);
      canvas.drawLine((int)s.x - ex, (int)s.y - ey, (int)s.x + ex, (int)s.y + ey, rgb565(200, 230, 255));
    }
    if (shatter <= 0) { res = 0; if (!s_solved) { s_solved = true; hapGesture(HG_SETTLE); } }
  }
}
void tap(int, int) {}
}  // namespace hum

// ============================================================
//  12  HUSH — be quiet, and the firefly will land
// ============================================================
namespace hush {
float qT = 0, fx = 60, fy = 60, fvx = 0, fvy = 0, glow = 0;
void enter() { qT = 0; fx = 60; fy = 60; glow = 0; }
void draw(float dt) {
  bg(rgb565(4, 6, 24), rgb565(10, 24, 30));
  for (int k = 0; k < 40; k++) { int x = (k * 73) % W, y = 18 + (k * 41) % 110; if (((int)(s_t * 3) + k) % 7) canvas.drawPixel(x, y, rgb565(200, 200, 230)); }
  canvas.fillCircle(270, 44, 16, rgb565(230, 230, 200));
  for (int x = 0; x < W; x += 6) canvas.drawLine(x, H - 14, x + (int)(sinf(x * 0.3f + s_t) * 4), H - 40 - (x * 7) % 20, rgb565(30, 70, 40));
  bool quiet = aud::level < 0.06f && !aud::calibrating();
  qT = quiet ? qT + dt : fmaxf(0.f, qT - dt * 4.f);
  float clawX = 190, clawY = 118;
  float want = clampf(qT / 5.f, 0.f, 1.f);
  // loud: the firefly darts about; quiet: it drifts closer and closer to the claw
  float tx = clawX + (1.f - want) * (sinf(s_t * 0.9f) * 110.f - 40.f), ty = clawY + (1.f - want) * (cosf(s_t * 1.3f) * 50.f - 30.f);
  float jit = aud::level * 400.f;
  fvx += ((tx - fx) * 3.f + (fr() - 0.5f) * jit) * dt; fvy += ((ty - fy) * 3.f + (fr() - 0.5f) * jit) * dt;
  fvx *= 1.f - dt * 2.f; fvy *= 1.f - dt * 2.f;
  fx += fvx * dt; fy += fvy * dt;
  mantisV(170, 150, 1.3f, 0.2f, 0.9f, glow);
  glow += ((want > 0.99f ? 1.f : 0.f) - glow) * dt * 1.5f;
  float bl = 0.5f + 0.5f * sinf(s_t * 5.f);
  canvas.fillCircle((int)fx, (int)fy, 5 + (int)(glow * 5), mix565(rgb565(40, 60, 10), rgb565(220, 255, 90), bl * 0.5f + glow * 0.5f));
  canvas.fillCircle((int)fx, (int)fy, 2, rgb565(255, 255, 200));
  if (want > 0.99f && !s_solved) { s_solved = true; hapGesture(HG_SETTLE); }
}
void tap(int, int) {}
}  // namespace hush

// ============================================================
//  24 MORE ROOMS — two more for every formula
// ============================================================
// ---- shared sensing helpers ----
bool clapEvent() { static float last = -9; if (s_t < last) last = -9; if (clapSpike(g_dt) && s_t - last > 0.2f) { last = s_t; return true; } return false; }
bool knockEvent() { static float last = -9; if (s_t < last) last = -9; int tx, ty; if (!touchNow(tx, ty) && g_jolt > 0.22f && fabsf(g_gyroX) + fabsf(g_gyroY) + fabsf(g_gyroZ) < 90.f && s_t - last > 0.14f) { last = s_t; return true; } return false; }
float blowAmt() { return clampf((aud::level - 0.3f) * 2.f, 0.f, 1.f) * (aud::zcr > 0.3f ? 1.f : 0.25f); }
int humBand(bool &steady) {
  static int lastPk = -1; int pk = 1; float pv = 0;
  for (int b = 1; b < 20; b++) if (aud::bands[b] > pv) { pv = aud::bands[b]; pk = b; }
  bool tonal = aud::level > 0.18f && aud::zcr < 0.3f && pv > 0.35f;
  steady = tonal && lastPk >= 0 && abs(pk - lastPk) <= 1; lastPk = tonal ? pk : -1; return tonal ? pk : -1;
}
bool quietNow() { return aud::level < 0.06f && !aud::calibrating(); }
void win(float x, float y) { s_solved = true; hapGesture(HG_CHAIN); burst(x, y, 50, 170); }

// ---------------- SHAKE ----------------
namespace popcorn {   // shake the pot: kernels pop; enough pops and the lid blows
int pops = 0; float lid = 0, fly = 0; struct K { float x, y, vx, vy; bool live; }; K k[40];
void enter() { pops = 0; lid = 0; fly = 0; for (auto &q : k) q.live = false; }
void draw(float dt) {
  bg(rgb565(60, 30, 20), rgb565(20, 10, 10));
  canvas.fillRect(90, 200, 140, 12, rgb565(40, 40, 44)); for (int i = 0; i < 5; i++) canvas.fillTriangle(110 + i * 25, 200, 118 + i * 25, 200, 114 + i * 25, 186 - (int)(fr() * 8), rgb565(255, 120 + i * 20, 30));
  float ag = agitation();
  if (!s_solved && ag > 0.3f && fr() < ag * 0.35f) { pops++; hap(120, 8); for (auto &q : k) if (!q.live) { q = {130 + fr() * 60, 170, (fr() - 0.5f) * 80, -60 - fr() * 80, true}; break; } }
  int ox = (int)(sinf(s_t * 50.f) * ag * 3.f);
  canvas.fillRoundRect(100 + ox, 130, 120, 70, 12, rgb565(150, 150, 160)); canvas.fillRect(84 + ox, 150, 16, 6, rgb565(60, 40, 30)); canvas.fillRect(220 + ox, 150, 16, 6, rgb565(60, 40, 30));
  if (pops >= 45 && !s_solved) { win(160, 110); fly = 1.f; }
  if (fly > 0) { fly += dt; lid = fminf(200.f, lid + dt * 400.f); if (fr() < 0.8f) for (auto &q : k) if (!q.live) { q = {140 + fr() * 40, 128, (fr() - 0.5f) * 220, -200 - fr() * 150, true}; break; } }
  else lid = (pops % 3 == 0 && ag > 0.3f) ? 3.f : 0.f;
  canvas.fillRoundRect(96 + ox, 122 - (int)lid, 128, 12, 6, rgb565(190, 190, 200)); canvas.fillCircle(160 + ox, 118 - (int)lid, 5, rgb565(60, 40, 30));
  for (auto &q : k) { if (!q.live) continue; q.vy += 400 * dt; q.x += q.vx * dt; q.y += q.vy * dt; if (q.y > 215) q.live = false;
    canvas.fillCircle((int)q.x, (int)q.y, 4, rgb565(255, 245, 210)); canvas.fillCircle((int)q.x + 3, (int)q.y - 2, 3, rgb565(255, 250, 225)); }
}
void tap(int, int) {}
}
namespace ketchup {   // upside down AND shake: the glop finally lets go
float glop = 0; bool out = false; float splatR = 0;
void enter() { glop = 0; out = false; splatR = 0; }
void draw(float dt) {
  bg(rgb565(230, 230, 220), rgb565(200, 190, 170));
  canvas.fillEllipse(160, 210, 90, 12, rgb565(250, 250, 250)); canvas.fillEllipse(160, 206, 40, 6, rgb565(230, 190, 120));   // a plate of fries
  for (int i = 0; i < 6; i++) canvas.fillRect(130 + i * 10, 196 - (i % 3) * 3, 5, 14, rgb565(240, 190, 70));
  bool upside = g_gravY < -0.3f; float ag = agitation();
  if (!out && upside && ag > 0.3f) glop += ag * dt * 0.35f; else glop = fmaxf(0.f, glop - dt * 0.05f);
  float ang = upside ? 3.14159f : 0.f; int by = upside ? 70 : 90;
  canvas.fillRoundRect(140, by - 40, 40, 80, 10, rgb565(200, 30, 30)); canvas.fillRect(150, upside ? by + 40 : by - 52, 20, 12, rgb565(240, 240, 240));
  canvas.fillRect(144, by - 10, 32, 18, rgb565(250, 250, 250)); canvas.setTextColor(rgb565(200, 30, 30)); canvas.setCursor(146, by - 4); canvas.print("MNTS");
  if (upside && glop > 0.1f && !out) canvas.fillCircle(160, by + 54, (int)(2 + glop * 6), rgb565(190, 20, 20));
  if (glop >= 1.f && !out) { out = true; hapGesture(HG_CRACK); }
  if (out) { splatR = fminf(30.f, splatR + dt * 120.f); canvas.fillEllipse(160, 200, (int)splatR, (int)(splatR * 0.4f), rgb565(190, 20, 20)); if (splatR >= 30.f && !s_solved) win(160, 190); }
  (void)ang;
}
void tap(int, int) {}
}
// ---------------- BLOW ----------------
namespace candles {   // blow out every candle on the cake
float fl[5]; int left = 5;
void enter() { for (auto &f : fl) f = 1.f; left = 5; }
void draw(float dt) {
  bg(rgb565(30, 16, 40), rgb565(60, 30, 50));
  canvas.fillRoundRect(70, 140, 180, 70, 10, rgb565(240, 200, 220)); canvas.fillRect(70, 150, 180, 8, rgb565(200, 80, 150));
  float b = blowAmt(); left = 0;
  for (int i = 0; i < 5; i++) {
    int x = 96 + i * 32; canvas.fillRect(x - 3, 110, 6, 30, i & 1 ? wire::LIME : rgb565(0, 150, 150));
    if (fl[i] > 0) { left++; float resist = 0.35f + i * 0.08f; if (b > resist) fl[i] -= dt * (b - resist) * 2.5f;
      int fh = (int)(10 * fl[i] * (1.f - b * 0.6f)); int sway = (int)(b * 6.f + sinf(s_t * 12 + i) * 1.5f);
      canvas.fillTriangle(x - 4 + sway, 108, x + 4 + sway, 108, x + sway * 2, 108 - fh - 4, rgb565(255, 200, 60)); canvas.fillCircle(x + sway, 106, 3, rgb565(255, 240, 180)); }
    else canvas.drawLine(x, 108, x + (int)(sinf(s_t * 2 + i) * 4), 90, rgb565(120, 120, 130));
  }
  if (left == 0 && !s_solved) win(160, 100);
}
void tap(int, int) {}
}
namespace dandelion {   // blow the seeds off
struct S { float x, y, vx, vy, a; bool on; }; S sd[40];
void enter() { for (int i = 0; i < 40; i++) { float a = i * 0.157f; sd[i] = {160 + cosf(a) * 26, 90 + sinf(a) * 26, 0, 0, a, true}; } }
void draw(float dt) {
  bg(rgb565(120, 190, 240), rgb565(200, 240, 200)); canvas.fillRect(0, 200, W, 26, rgb565(90, 170, 70));
  canvas.drawLine(160, 96, 160, 210, rgb565(70, 150, 60)); canvas.fillCircle(160, 92, 5, rgb565(170, 160, 110));
  float b = blowAmt(); int left = 0;
  for (auto &s : sd) {
    if (s.on) { left++; if (b > 0.2f && fr() < b * dt * 3.f) { s.on = false; s.vx = 60 + fr() * 80; s.vy = -20 - fr() * 40; }
      canvas.drawLine(160, 92, (int)s.x, (int)s.y, rgb565(240, 240, 240)); canvas.fillCircle((int)s.x, (int)s.y, 2, rgb565(255, 255, 255)); }
    else { s.x += s.vx * dt; s.y += (s.vy + sinf(s_t * 3 + s.a) * 10) * dt; s.vy += 5 * dt; if (s.x < W + 10) { canvas.drawLine((int)s.x, (int)s.y, (int)s.x - 4, (int)s.y + 4, rgb565(250, 250, 250)); canvas.fillCircle((int)s.x, (int)s.y, 2, rgb565(255, 255, 255)); } }
  }
  if (left == 0 && !s_solved) win(160, 90);
}
void tap(int, int) {}
}
// ---------------- MASH ----------------
namespace pump {   // pump the balloon up until it floats away
float size = 0.1f, press = 0; bool gone = false; float gy = 0;
void enter() { size = 0.1f; gone = false; gy = 0; }
void draw(float dt) {
  bg(rgb565(250, 220, 200), rgb565(220, 190, 170));
  size = fmaxf(0.1f, size - dt * 0.03f); press = fmaxf(0.f, press - dt * 6.f);
  int ph = (int)(press * 20);
  canvas.fillRect(60, 120 + ph, 8, 60, rgb565(80, 80, 90)); canvas.fillRect(44, 118 + ph, 40, 8, rgb565(40, 40, 50));
  canvas.fillRect(52, 170, 24, 50, rgb565(200, 40, 40)); canvas.drawLine(76, 200, 150, 190, rgb565(40, 40, 40));
  if (!gone) { int r = (int)(12 + size * 60); canvas.fillEllipse(170, 170 - r, r, (int)(r * 1.15f), rgb565(93, 0, 93)); canvas.fillCircle(170 - r / 3, 160 - r, r / 5, rgb565(180, 90, 180)); if (size >= 1.f) { gone = true; win(170, 100); } }
  else { gy += dt * 60; canvas.fillEllipse(170, (int)(98 - gy), 72, 83, rgb565(93, 0, 93)); canvas.drawLine(170, (int)(180 - gy), 170, (int)(240 - gy), rgb565(40, 40, 40)); }
  for (int k = 0; k < 10; k++) canvas.fillRoundRect(40 + k * 12, 30, 9, 6, 2, k < size * 10 ? wire::LIME : rgb565(90, 90, 100));
}
void tap(int x, int y) { if (x < 110 && !gone) { press = 1.f; size += 0.06f; hap((uint8_t)(100 + size * 120), 14); } }
}
namespace drumroll {   // a fast roll on the drum fills the crowd; keep it going for the crash
float roll = 0, hitT = 0, crash = 0;
void enter() { roll = 0; crash = 0; }
void draw(float dt) {
  bg(rgb565(20, 10, 30), rgb565(40, 20, 20));
  roll = fmaxf(0.f, roll - dt * 0.16f); hitT = fmaxf(0.f, hitT - dt * 10.f);
  for (int i = 0; i < 12; i++) { int x = 20 + i * 26; int jump = (int)(roll * 10 * fabsf(sinf(s_t * 9 + i))); canvas.fillCircle(x, 60 - jump, 8, rgb565(60, 50, 70)); }
  canvas.fillEllipse(160, 170, 70, 22, rgb565(220, 220, 230)); canvas.fillRect(90, 170, 140, 40, rgb565(200, 40, 60)); canvas.fillEllipse(160, 210, 70, 16, rgb565(150, 30, 40));
  canvas.fillEllipse(160, 170, 66 - (int)(hitT * 4), 19, rgb565(245, 245, 250));
  mantisV(270, 150, 0.9f, 0.3f + hitT * 0.5f, 0.3f + (1 - hitT) * 0.5f, roll);
  canvas.fillRect(20, 100, (int)(roll * 280), 6, wire::LIME);
  if (roll >= 0.95f && !s_solved) { win(160, 120); crash = 1.f; }
  if (crash > 0) { crash = fmaxf(0.f, crash - dt); canvas.fillEllipse(60, 120, 30, 6, rgb565(255, 220, 80)); }
}
void tap(int x, int y) { if (y > 140) { hitT = 1.f; roll = fminf(1.f, roll + 0.07f); hap((uint8_t)(90 + roll * 150), 10); } }
}
// ---------------- POKE-ALL ----------------
namespace soap {   // pop every drifting soap bubble
struct B { float x, y, r, ph; bool on; }; B bb[14];
void enter() { for (auto &b : bb) b = {30 + fr() * 260, 40 + fr() * 160, 10 + fr() * 12, fr() * 6, true}; }
void draw(float dt) {
  bg(rgb565(120, 180, 230), rgb565(220, 230, 250)); int left = 0;
  for (auto &b : bb) { if (!b.on) continue; left++; b.ph += dt; b.x += sinf(b.ph * 0.9f) * 12 * dt; b.y += cosf(b.ph * 0.7f) * 8 * dt - 4 * dt; if (b.y < 30) b.y = 200;
    canvas.drawCircle((int)b.x, (int)b.y, (int)b.r, hsv565(fmodf(b.ph * 60, 360), 0.4f, 1.f)); canvas.fillCircle((int)(b.x - b.r * 0.4f), (int)(b.y - b.r * 0.4f), 2, 0xFFFF); }
  if (left == 0 && !s_solved) win(160, 120);
}
void tap(int x, int y) { for (auto &b : bb) if (b.on && (b.x - x) * (b.x - x) + (b.y - y) * (b.y - y) < (b.r + 8) * (b.r + 8)) { b.on = false; hapGesture(HG_POP); burst(b.x, b.y, 8, 60); break; } }
}
namespace stars {   // light every star and a mantis constellation appears
const int N = 9; const int SX[N] = {130, 190, 115, 205, 160, 160, 125, 195, 160}, SY[N] = {50, 50, 85, 85, 100, 140, 180, 180, 205};
bool lit[N];
void enter() { memset(lit, 0, sizeof(lit)); }
void draw(float dt) {
  bg(rgb565(4, 6, 24), rgb565(14, 10, 40)); int n = 0;
  for (int i = 0; i < N; i++) n += lit[i];
  if (n == N) { static const int E[][2] = {{0, 2}, {1, 3}, {2, 4}, {3, 4}, {4, 5}, {5, 6}, {5, 7}, {5, 8}}; for (auto &e : E) canvas.drawLine(SX[e[0]], SY[e[0]], SX[e[1]], SY[e[1]], wire::TEAL); if (!s_solved) win(160, 120); }
  for (int i = 0; i < N; i++) { float tw = 0.5f + 0.5f * sinf(s_t * 3 + i); canvas.fillCircle(SX[i], SY[i], lit[i] ? 4 : 2, lit[i] ? rgb565(255, 250, 200) : mix565(rgb565(40, 40, 70), rgb565(90, 90, 120), tw)); }
}
void tap(int x, int y) { for (int i = 0; i < N; i++) if (!lit[i] && (SX[i] - x) * (SX[i] - x) + (SY[i] - y) * (SY[i] - y) < 20 * 20) { lit[i] = true; hap(90, 12); burst(SX[i], SY[i], 6, 40); break; } }
}
// ---------------- CLAP ----------------
namespace echoclap {   // the mantis claps a pattern; clap it back
const float PAT[3] = {0.f, 0.4f, 0.8f}; float t0 = 0; int got = 0; float gotT[3]; bool listen = false;
void enter() { t0 = 0; got = 0; listen = false; }
void draw(float dt) {
  bg(rgb565(30, 10, 40), rgb565(10, 10, 20));
  float c = fmodf(s_t, 5.f); float arm = 1.f;
  for (float p : PAT) if (c > p && c < p + 0.12f) arm = 0.f;
  bool mine = c > 1.6f && c < 4.2f;
  if (mine && !listen) { listen = true; got = 0; }
  if (!mine && listen) { listen = false; if (got == 3) { float a = gotT[1] - gotT[0], b = gotT[2] - gotT[1]; if (fabsf(a - 0.4f) < 0.18f && fabsf(b - 0.4f) < 0.18f && !s_solved) win(160, 100); } }
  if (listen && clapEvent() && got < 3) { gotT[got++] = s_t; hap(110, 14); }
  mantisV(160, 130, 1.5f, s_solved ? 1.f : arm * 0.8f, s_solved ? 1.f : arm * 0.8f, mine ? 0.2f : 0.8f);
  for (int k = 0; k < 3; k++) { if (listen && k < got) canvas.fillCircle(136 + k * 24, 30, 7, wire::LIME); else canvas.drawCircle(136 + k * 24, 30, 7, mine ? wire::LIME : rgb565(80, 80, 100)); }
}
void tap(int, int) {}
}
namespace clapper {   // clap-clap turns the lamp on
bool on = false; float last = -9;
void enter() { on = false; last = -9; }
void draw(float dt) {
  bg(on ? rgb565(90, 80, 60) : rgb565(10, 10, 16), on ? rgb565(60, 50, 40) : rgb565(6, 6, 10));
  if (clapEvent()) { if (s_t - last < 0.6f && s_t - last > 0.15f) { on = !on; hap(140, 20); if (on && !s_solved) win(160, 70); } last = s_t; }
  canvas.fillRect(155, 90, 10, 110, rgb565(90, 80, 70)); canvas.fillEllipse(160, 205, 40, 8, rgb565(70, 60, 50));
  canvas.fillTriangle(120, 90, 200, 90, 160, 50, on ? rgb565(255, 230, 150) : rgb565(60, 50, 40));
  if (on) for (int k = 0; k < 5; k++) canvas.drawLine(160, 90, 90 + k * 35, 200, rgb565(120, 110, 80));
}
void tap(int, int) {}
}
// ---------------- TIP / POUR ----------------
namespace fishbowl {   // tip the jug to fill the fishbowl
float fill = 0, jug = 0; struct D { float x, y, vy; bool on; }; D d[40];
void enter() { fill = 0; for (auto &q : d) q.on = false; }
void draw(float dt) {
  bg(rgb565(200, 230, 240), rgb565(240, 240, 230)); canvas.fillRect(0, 214, W, 12, rgb565(150, 110, 80));
  float target = clampf(g_gravX * 80.f, -10.f, 80.f) * 0.01745f; jug += (target - jug) * clampf(dt * 8, 0, 1);
  float cx = 132, cy = 66, c = cosf(jug), s = sinf(jug);
  auto P = [&](float lx, float ly, int &ox, int &oy) { ox = (int)(cx + c * lx - s * ly); oy = (int)(cy + s * lx + c * ly); };
  int a0, b0, a1, b1, a2, b2, a3, b3; P(-20, -20, a0, b0); P(20, -20, a1, b1); P(20, 22, a2, b2); P(-20, 22, a3, b3);
  canvas.fillTriangle(a0, b0, a1, b1, a2, b2, rgb565(200, 220, 240)); canvas.fillTriangle(a0, b0, a2, b2, a3, b3, rgb565(200, 220, 240));
  int sx, sy; P(26, -18, sx, sy);
  if (jug > 0.5f && fill < 1.f) for (auto &q : d) if (!q.on) { q = {(float)sx, (float)sy, 20, true}; break; }
  for (auto &q : d) { if (!q.on) continue; q.vy += 500 * dt; q.y += q.vy * dt; q.x += 20 * dt; if (q.y > 140 && q.x > 150 && q.x < 250) { fill = fminf(1.f, fill + 0.008f); q.on = false; continue; } if (q.y > 214) { q.on = false; continue; } canvas.fillCircle((int)q.x, (int)q.y, 2, rgb565(90, 160, 255)); }
  canvas.fillEllipse(200, 175, 50, 40, rgb565(210, 235, 245)); int wy = (int)(210 - fill * 70);
  canvas.fillRect(152, wy, 96, 212 - wy, rgb565(110, 180, 240)); canvas.drawEllipse(200, 175, 50, 40, rgb565(150, 190, 210));
  if (fill > 0.3f) { int fx = 200 + (int)(sinf(s_t) * 20), fy = (int)fmaxf((float)wy + 12, 180.f); canvas.fillEllipse(fx, fy, 9, 5, rgb565(255, 130, 40)); canvas.fillTriangle(fx - 9, fy, fx - 16, fy - 5, fx - 16, fy + 5, rgb565(255, 130, 40)); }
  if (fill >= 1.f && !s_solved) win(200, 140);
}
void tap(int, int) {}
}
namespace marble {   // roll the marble through the maze to the hole
float mx = 40, my = 40, vx = 0, vy = 0;
const int WL[][4] = {{20, 20, 300, 26}, {20, 20, 26, 220}, {294, 20, 300, 220}, {20, 214, 300, 220}, {20, 80, 220, 86}, {100, 140, 300, 146}};
void enter() { mx = 40; my = 45; vx = vy = 0; }
void draw(float dt) {
  bg(rgb565(120, 90, 60), rgb565(90, 70, 50));
  for (auto &w : WL) canvas.fillRect(w[0], w[1], w[2] - w[0], w[3] - w[1], rgb565(60, 40, 30));
  canvas.fillCircle(270, 190, 10, rgb565(10, 10, 10));
  vx += g_gravX * 400 * dt; vy += g_gravY * 400 * dt; vx *= 0.99f; vy *= 0.99f;
  float nx = mx + vx * dt, ny = my + vy * dt;
  for (auto &w : WL) { if (nx + 6 > w[0] && nx - 6 < w[2] && my + 6 > w[1] && my - 6 < w[3]) { nx = mx; vx = -vx * 0.4f; }
                       if (nx + 6 > w[0] && nx - 6 < w[2] && ny + 6 > w[1] && ny - 6 < w[3]) { ny = my; vy = -vy * 0.4f; } }
  mx = nx; my = ny;
#ifdef HOST
  ::g_dbgMarbleX = mx; ::g_dbgMarbleY = my;
#endif
  canvas.fillCircle((int)mx, (int)my, 6, rgb565(90, 200, 230)); canvas.fillCircle((int)mx - 2, (int)my - 2, 2, 0xFFFF);
  if ((mx - 270) * (mx - 270) + (my - 190) * (my - 190) < 64 && !s_solved) { win(270, 190); vx = vy = 0; }
}
void tap(int, int) {}
}
// ---------------- DRAG ----------------
namespace burrow {   // drag the rock off the burrow; a critter pops out
float rx = 160, ry = 170, pop = 0; bool drag = false; float dX = 0, dY = 0;
void enter() { rx = 160; ry = 170; pop = 0; drag = false; }
void draw(float dt) {
  bg(rgb565(150, 200, 240), rgb565(210, 200, 150)); canvas.fillRect(0, 180, W, 46, rgb565(140, 110, 70)); canvas.fillEllipse(160, 184, 22, 8, rgb565(40, 25, 15));
  int x, y;
  if (touchNow(x, y)) { if (!drag && (x - rx) * (x - rx) + (y - ry) * (y - ry) < 40 * 40) { drag = true; dX = rx - x; dY = ry - y; } if (drag) { rx = x + dX; ry = y + dY; } } else drag = false;
  bool clear = fabsf(rx - 160) > 45;
  if (clear) { pop = fminf(1.f, pop + dt); int py = 184 - (int)(pop * 26); mantisV(160, (float)py, 0.6f, 0.2f, 0.9f * pop, pop); if (pop >= 1 && !s_solved) win(160, 150); }
  canvas.fillEllipse((int)rx, (int)ry, 34, 24, rgb565(120, 120, 125)); canvas.fillEllipse((int)rx - 8, (int)ry - 8, 14, 8, rgb565(160, 160, 165));
}
void tap(int, int) {}
}
namespace window {   // wipe the fog off the window: someone's waving outside
uint8_t fog[40 * 30]; int left = 0;
void enter() { memset(fog, 1, sizeof(fog)); left = 40 * 30; }
void draw(float dt) {
  bg(rgb565(120, 180, 230), rgb565(180, 220, 160));
  mantisV(160, 140, 1.4f, 0.2f, 0.6f + 0.4f * fabsf(sinf(s_t * 5)), 0.5f);
  int x, y;
  if (touchNow(x, y)) for (int oy = -2; oy <= 2; oy++) for (int ox = -2; ox <= 2; ox++) { int gx = x / 8 + ox, gy = (y - 14) / 7 + oy; if (gx >= 0 && gx < 40 && gy >= 0 && gy < 30 && fog[gy * 40 + gx]) { fog[gy * 40 + gx] = 0; left--; if ((left & 15) == 0) hapRumble(0.15f, 30, 0.8f); } }
  for (int gy = 0; gy < 30; gy++) for (int gx = 0; gx < 40; gx++) if (fog[gy * 40 + gx]) canvas.fillRect(gx * 8, 14 + gy * 7, 8, 7, rgb565(200, 210, 215));
  canvas.drawRect(0, 14, W, H - 28, rgb565(120, 90, 60)); canvas.drawFastVLine(160, 14, H - 28, rgb565(120, 90, 60));
  if (left < 60 && !s_solved) win(160, 100);
}
void tap(int, int) {}
}
// ---------------- TILT / ALIGN ----------------
namespace level {   // centre the bubble and hold it: the wobbly shelf settles
float bx = 0, holdT = 0;
void enter() { bx = 0; holdT = 0; }
void draw(float dt) {
  bg(rgb565(230, 220, 200), rgb565(200, 190, 170));
  float target = clampf(-g_gravX * 400.f, -110.f, 110.f); bx += (target - bx) * clampf(dt * 5, 0, 1);
  float wob = s_solved ? 0.f : sinf(s_t * 3.f) * 6.f * (1.f - holdT / 2.f);
  canvas.fillRect(60, 80 + (int)wob, 200, 10, rgb565(140, 100, 60)); canvas.fillRect(90, 60 + (int)wob, 20, 20, rgb565(200, 80, 80)); canvas.fillRect(200, 55 + (int)wob, 16, 25, rgb565(80, 120, 200));
  canvas.fillRoundRect(40, 150, 240, 30, 15, rgb565(240, 220, 90)); canvas.fillRoundRect(46, 156, 228, 18, 9, rgb565(220, 250, 180));
  canvas.drawFastVLine(150, 154, 22, rgb565(60, 60, 60)); canvas.drawFastVLine(170, 154, 22, rgb565(60, 60, 60));
  canvas.fillEllipse(160 + (int)bx, 165, 14, 7, rgb565(250, 255, 250));
  holdT = fabsf(bx) < 9.f ? holdT + dt : 0.f;
  if (holdT > 2.f && !s_solved) win(160, 80);
}
void tap(int, int) {}
}
namespace mirror {   // aim the mirror so the beam hits the crystal
float ang = 0, onT = 0;
void enter() { ang = 0; onT = 0; }
void draw(float dt) {
  bg(rgb565(10, 10, 24), rgb565(20, 16, 40));
  float target = clampf(g_gravX, -0.8f, 0.8f); ang += (target - ang) * clampf(dt * 4, 0, 1);
  float mx = 160, my = 180, a = -1.5708f + ang;                       // mirror normal
  canvas.drawLine(20, 60, (int)mx, (int)my, rgb565(255, 240, 150));  // incoming beam
  float ix = (mx - 20), iy = (my - 60), il = sqrtf(ix * ix + iy * iy); ix /= il; iy /= il;
  float nx = cosf(a), ny = sinf(a), d = ix * nx + iy * ny, rx = ix - 2 * d * nx, ry = iy - 2 * d * ny;
  int ex = (int)(mx + rx * 300), ey = (int)(my + ry * 300);
  canvas.drawLine((int)mx, (int)my, ex, ey, rgb565(255, 240, 150));
  float cx = 260, cy = 50, t = (cx - mx) * rx + (cy - my) * ry, px = mx + rx * t - cx, py = my + ry * t - cy;
  bool hitC = t > 0 && px * px + py * py < 14 * 14;
  onT = hitC ? onT + dt : 0.f;
  canvas.fillTriangle((int)cx - 10, (int)cy + 10, (int)cx + 10, (int)cy + 10, (int)cx, (int)cy - 16, hitC ? wire::LIME : rgb565(80, 90, 110));
  canvas.drawLine((int)(mx - cosf(a + 1.5708f) * 26), (int)(my - sinf(a + 1.5708f) * 26), (int)(mx + cosf(a + 1.5708f) * 26), (int)(my + sinf(a + 1.5708f) * 26), rgb565(200, 220, 240));
  if (onT > 1.f && !s_solved) win(cx, cy);
}
void tap(int, int) {}
}
// ---------------- KNOCK ----------------
namespace coconut {   // knock until it cracks open
int n = 0; float shake = 0;
void enter() { n = 0; }
void hitIt() { n++; shake = 1.f; hap(200, 16); if (n >= 7 && !s_solved) win(160, 120); }
void draw(float dt) {
  bg(rgb565(250, 220, 150), rgb565(80, 190, 200)); canvas.fillEllipse(160, 240, 260, 40, rgb565(240, 220, 170));
  if (knockEvent()) hitIt();
  shake = fmaxf(0.f, shake - dt * 6); int sx = (int)(sinf(s_t * 70) * shake * 4);
  if (!s_solved) { canvas.fillCircle(160 + sx, 130, 46, rgb565(110, 70, 40)); for (int k = 0; k < n; k++) canvas.drawLine(150 + sx + k * 3, 100 + k * 6, 170 + sx - k * 2, 110 + k * 7, rgb565(40, 25, 10)); }
  else { canvas.fillEllipse(130, 140, 34, 30, rgb565(110, 70, 40)); canvas.fillEllipse(130, 138, 26, 22, rgb565(250, 250, 245)); canvas.fillEllipse(196, 140, 34, 30, rgb565(110, 70, 40)); canvas.fillEllipse(196, 138, 26, 22, rgb565(250, 250, 245)); }
}
void tap(int x, int y) { if ((x - 160) * (x - 160) + (y - 130) * (y - 130) < 50 * 50 && !s_solved) hitIt(); }
}
namespace egg {   // tap-tap on the egg... something's inside
int n = 0; float shake = 0, hatch = 0;
void enter() { n = 0; hatch = 0; }
void hitIt() { n++; shake = 1.f; hap(120, 12); if (n >= 5 && hatch == 0) hatch = 0.001f; }
void draw(float dt) {
  bg(rgb565(250, 240, 210), rgb565(220, 200, 160)); canvas.fillEllipse(160, 200, 80, 16, rgb565(200, 170, 110));
  if (knockEvent()) hitIt();
  shake = fmaxf(0.f, shake - dt * 6); int sx = (int)(sinf(s_t * 60) * shake * 3);
  if (hatch > 0) { hatch = fminf(1.f, hatch + dt * 0.7f); mantisV(160, 170 - hatch * 30, 0.7f, 0.9f * hatch, 0.9f * hatch, hatch); if (hatch >= 1 && !s_solved) win(160, 130); }
  if (hatch < 0.5f) { canvas.fillEllipse(160 + sx, 150, 36, 46, rgb565(250, 250, 240)); for (int k = 0; k < n; k++) canvas.drawLine(140 + sx + k * 8, 140 + (k & 1) * 6, 148 + sx + k * 8, 146 - (k & 1) * 6, rgb565(120, 110, 100)); }
  else { canvas.fillTriangle(124, 170, 196, 170, 160, 196, rgb565(250, 250, 240)); canvas.fillTriangle(130, 110, 170, 110, 150, 96, rgb565(250, 250, 240)); }
}
void tap(int x, int y) { if ((x - 160) * (x - 160) + (y - 150) * (y - 150) < 50 * 50 && hatch == 0) hitIt(); }
}
// ---------------- SHAKE, THEN STILL ----------------
namespace eightball {   // shake the magic 8-ball, then hold still for the answer
float shook = 0, still = 0; int ans = 0;
const char *A[] = {"YES", "SURELY", "ASK AGAIN", "DEFINITELY", "OF COURSE"};
void enter() { shook = 0; still = 0; }
void draw(float dt) {
  bg(rgb565(20, 20, 40), rgb565(40, 10, 50));
  float ag = agitation(); if (ag > 0.5f) { shook = fminf(1.f, shook + dt); still = 0; ans = esp_random() % 5; } else if (shook > 0.6f) still += dt;
  int sx = (int)(sinf(s_t * 50) * fminf(ag, 1.f) * 4);
  canvas.fillCircle(160 + sx, 120, 80, rgb565(20, 20, 24)); canvas.fillCircle(140 + sx, 96, 22, rgb565(60, 60, 70));
  canvas.fillCircle(160 + sx, 124, 34, rgb565(10, 20, 70));
  float up = clampf(still / 2.f, 0.f, 1.f);
  if (up > 0.05f) { canvas.fillTriangle(160 + sx - (int)(26 * up), 110, 160 + sx + (int)(26 * up), 110, 160 + sx, 110 + (int)(34 * up), rgb565(40, 60, 200));
    if (up > 0.7f) { canvas.setTextColor(0xFFFF); canvas.setCursor(160 + sx - (int)strlen(A[ans]) * 3, 116); canvas.print(A[ans]); } }
  else { canvas.setTextColor(0xFFFF); canvas.setTextSize(3); canvas.setCursor(151 + sx, 70); canvas.print("8"); canvas.setTextSize(1); }
  if (up >= 1.f && !s_solved) win(160, 120);
}
void tap(int, int) {}
}
namespace kitten {   // rock the kitten gently, then keep still until it sleeps
float rocked = 0, sleepT = 0;
void enter() { rocked = 0; sleepT = 0; }
void draw(float dt) {
  bg(rgb565(40, 30, 60), rgb565(20, 20, 40));
  float ag = agitation();
  if (ag > 0.15f && ag < 0.9f) rocked = fminf(1.f, rocked + dt * 0.4f); else if (ag >= 0.9f) { rocked = fmaxf(0.f, rocked - dt); sleepT = 0; }
  if (rocked > 0.6f && ag < 0.08f) sleepT += dt; else if (ag > 0.15f) sleepT = 0;
  float sway = sinf(s_t * 2.f) * ag * 10.f;
  canvas.fillEllipse(160 + (int)sway, 180, 90, 22, rgb565(120, 80, 60));
  int cx = 160 + (int)sway; canvas.fillEllipse(cx, 160, 40, 24, rgb565(230, 150, 70)); canvas.fillCircle(cx - 30, 146, 18, rgb565(230, 150, 70));
  canvas.fillTriangle(cx - 44, 136, cx - 38, 118, cx - 30, 132, rgb565(230, 150, 70)); canvas.fillTriangle(cx - 24, 132, cx - 18, 118, cx - 14, 136, rgb565(230, 150, 70));
  bool asleep = sleepT > 3.f;
  if (asleep) { canvas.drawLine(cx - 38, 146, cx - 32, 146, 0); canvas.drawLine(cx - 28, 146, cx - 22, 146, 0); canvas.setTextColor(rgb565(200, 200, 255)); canvas.setCursor(cx, 110 - (int)fmodf(s_t * 10, 20)); canvas.print("z z"); }
  else { canvas.fillCircle(cx - 36, 145, 3, 0); canvas.fillCircle(cx - 24, 145, 3, 0); }
  if (asleep && !s_solved) win(cx, 120);
}
void tap(int, int) {}
}
// ---------------- HUM ----------------
namespace pitch {   // the mantis sings a note; hum along until you match it
int target = 6; float match = 0;
void enter() { target = 4 + esp_random() % 6; match = 0; }
void draw(float dt) {
  bg(rgb565(16, 10, 30), rgb565(10, 20, 30));
  bool st; int pk = humBand(st);
  bool ok = pk >= 0 && abs(pk - target) <= 1;
  match = ok ? fminf(1.f, match + dt * 0.5f) : fmaxf(0.f, match - dt * 0.3f);
  mantisV(90, 150, 1.1f, 0.3f, 0.5f, 0.5f);
  int ty = 200 - target * 8; canvas.drawFastHLine(40, ty, 240, wire::TEAL);
  if (pk >= 0) canvas.drawFastHLine(160, 200 - pk * 8, 120, ok ? wire::LIME : rgb565(200, 80, 80));
  canvas.fillRect(20, 220, (int)(280 * match), 4, wire::LIME);
  if (match >= 1.f && !s_solved) win(200, ty);
}
void tap(int, int) {}
}
namespace levitate {   // a steady hum lifts the stone
float h = 0;
void enter() { h = 0; }
void draw(float dt) {
  bg(rgb565(20, 30, 40), rgb565(30, 20, 40)); canvas.fillRect(0, 200, W, 26, rgb565(50, 50, 60));
  bool st; int pk = humBand(st);
  h = st ? fminf(1.f, h + dt * 0.25f) : fmaxf(0.f, h - dt * 0.35f);
  int y = 190 - (int)(h * 140) + (int)(sinf(s_t * 3) * 3 * h);
  if (h > 0.05f) for (int k = 0; k < 3; k++) canvas.drawEllipse(160, y + 22 + k * 6, 30 + k * 8, 4, mix565(wire::TEAL, 0, k * 0.3f));
  canvas.fillEllipse(160, y, 30, 18, rgb565(110, 110, 120)); canvas.drawLine(150, y - 6, 164, y + 4, wire::LIME);
  if (h >= 1.f && !s_solved) win(160, 50); (void)pk;
}
void tap(int, int) {}
}
// ---------------- SILENCE ----------------
namespace sneak {   // an ant carries the cookie past the sleeping guard: stay quiet
float ax = 20; bool woke = false; float wokeT = 0;
void enter() { ax = 20; woke = false; }
void draw(float dt) {
  bg(rgb565(30, 20, 40), rgb565(20, 20, 30)); canvas.fillRect(0, 196, W, 30, rgb565(60, 40, 30));
  if (!quietNow() && !s_solved) { woke = true; wokeT = 1.2f; ax = fmaxf(20.f, ax - 60.f * dt); }
  wokeT -= dt; if (wokeT <= 0) woke = false;
  if (!woke && !s_solved) ax += dt * 14.f;
  mantisV(160, 150, 1.1f, woke ? 0.9f : 0.1f, woke ? 0.9f : 0.1f, woke ? 1.f : 0.f);
  if (!woke) { canvas.setTextColor(rgb565(160, 160, 220)); canvas.setCursor(180, 100 - (int)fmodf(s_t * 8, 16)); canvas.print("z"); }
  canvas.fillCircle((int)ax, 190, 6, rgb565(200, 150, 80)); canvas.fillCircle((int)ax - 8, 193, 3, rgb565(20, 20, 20)); canvas.fillCircle((int)ax - 13, 193, 3, rgb565(20, 20, 20));
  if (ax > 300 && !s_solved) win(300, 180);
}
void tap(int, int) {}
}
namespace snowfall {   // silence lets the snow fall; it builds a snowman
float snow = 0; struct F { float x, y; }; F f[50];
void enter() { snow = 0; for (auto &q : f) q = {fr() * W, fr() * 200}; }
void draw(float dt) {
  bg(rgb565(20, 30, 60), rgb565(60, 70, 100)); canvas.fillRect(0, 206, W, 20, rgb565(230, 235, 245));
  bool q = quietNow();
  if (q) snow = fminf(1.f, snow + dt * 0.12f);
  for (auto &p : f) { if (q) { p.y += 20 * dt; p.x += sinf(s_t + p.y * 0.05f) * 8 * dt; } if (p.y > 206) p = {fr() * W, 14}; if (q) canvas.fillCircle((int)p.x, (int)p.y, 1, 0xFFFF); }
  if (snow > 0.2f) canvas.fillCircle(160, 190, (int)(10 + 16 * fminf(1.f, snow * 2)), 0xFFFF);
  if (snow > 0.5f) canvas.fillCircle(160, 158, (int)(8 + 10 * fminf(1.f, (snow - 0.5f) * 3)), 0xFFFF);
  if (snow > 0.8f) { canvas.fillCircle(160, 136, 10, 0xFFFF); canvas.fillCircle(156, 134, 1, 0); canvas.fillCircle(164, 134, 1, 0); canvas.fillTriangle(160, 137, 160, 139, 168, 138, rgb565(255, 140, 40)); }
  if (snow >= 1.f && !s_solved) win(160, 120);
}
void tap(int, int) {}
}

struct Room { const char *name; void (*enter)(); void (*draw)(float); void (*tap)(int, int); };
const Room ROOMS[] = {
#define RM(n, ns) {n, ns::enter, ns::draw, ns::tap}
  RM("nrg", nrg), RM("popcorn", popcorn), RM("ketchup", ketchup),
  RM("breeze", breeze), RM("candles", candles), RM("dandelion", dandelion),
  RM("arcade", arcade), RM("pump", pump), RM("drumroll", drumroll),
  RM("wrap", wrap), RM("soap", soap), RM("stars", stars),
  RM("clap", clap), RM("echo", echoclap), RM("clapper", clapper),
  RM("seed", seed), RM("fishbowl", fishbowl), RM("marble", marble),
  RM("shade", shade), RM("burrow", burrow), RM("window", window),
  RM("align", align), RM("level", level), RM("mirror", mirror),
  RM("knock", knock), RM("coconut", coconut), RM("egg", egg),
  RM("globe", globe), RM("8-ball", eightball), RM("kitten", kitten),
  RM("hum", hum), RM("pitch", pitch), RM("levitate", levitate),
  RM("hush", hush), RM("sneak", sneak), RM("snowfall", snowfall),
#undef RM
};
const int NROOM = sizeof(ROOMS) / sizeof(ROOMS[0]);
int s_room = 0;
int s_solvedCount = 0;
void start(int i) { s_room = i; s_solved = false; s_t = 0; for (auto &p : s_sp) p.live = false; ROOMS[i].enter(); }

}  // namespace

void roomsBegin() { start(0); }
void roomsEnter() { start(0); }                          // every visit starts at Mantis NRG
bool roomsSolved() { return s_solved; }
void roomsNext() { s_solvedCount++; start((s_room + 1) % NROOM); }
const char *roomsName() { return ROOMS[s_room].name; }
void roomsTouch(int x, int y) { ROOMS[s_room].tap(x, y); }
void roomsDraw() {
  float dt = fminf(g_dt, 0.05f);
  s_t += dt;
  ROOMS[s_room].draw(dt);
  sparks(dt, 200.f);
  if (s_solved) {                                        // the only feedback: a soft glow toward the B label
    float a = 0.5f + 0.5f * sinf(s_t * 3.f);
    canvas.fillTriangle(W / 2 - 6, H - 18, W / 2 + 6, H - 18, W / 2, H - 14, mix565(rgb565(20, 40, 20), wire::LIME, a));
  }
}
#ifdef HOST
float roomsTime() { return s_t; }
__attribute__((weak)) float g_dbgMarbleX = 0, g_dbgMarbleY = 0;
float roomsAlignPos(int i) { return align::lock[i] ? 999.f : align::pos[i]; }
void roomsDebugSolve() { s_solved = true; }
int roomsIndex() { return s_room; }
#endif
