// ============================================================
//  SYNAPSE — the mantis puppet
//  Skeletal rig built from the separated sprite sheet: every part is a
//  sprite rotated about its real joint (forward kinematics), legs are
//  2-bone IK with planted feet, and a beat-locked choreographer blends
//  dance moves. Sing mode zooms in and lip-syncs an anchored mouth.
// ============================================================
#include "app.h"
#include "audio.h"
#include "fx.h"
#include "mantis_rig.h"
#include <string.h>

static const float DEG = 0.01745329f;

// ---------- sprites ----------
struct Bone {
  M5Canvas spr;
  float pvx, pvy;
  int w, h;
  bool mirror;
};
enum { B_TORSO, B_HEAD, B_ABD, B_ARM_L, B_ARM_R, B_SCY_L, B_SCY_R, B_THI_L, B_THI_R, B_SHI_L, B_SHI_R, B_COUNT };
static Bone s_b[B_COUNT];
static bool s_ready = false;
static uint16_t s_skin = 0;

static void build(Bone &b, const RigPart &p, bool mirror) {
  b.w = p.w; b.h = p.h; b.mirror = mirror;
  b.pvx = mirror ? p.w - p.pvx : p.pvx;
  b.pvy = p.pvy;
  b.spr.setColorDepth(16);
  b.spr.setPsram(true);
  if (!b.spr.createSprite(p.w, p.h)) return;
  for (int y = 0; y < p.h; y++)
    for (int x = 0; x < p.w; x++) {
      int sx = mirror ? (p.w - 1 - x) : x;
      b.spr.drawPixel(x, y, p.px[y * p.w + sx]);
    }
  b.spr.setPivot(b.pvx, b.pvy);
}

void mantisBegin() {
  build(s_b[B_TORSO], RIG_TORSO, false);
  build(s_b[B_HEAD], RIG_HEAD, false);
  build(s_b[B_ABD], RIG_ABDOMEN, false);
  build(s_b[B_ARM_L], RIG_ARM, false);
  build(s_b[B_ARM_R], RIG_ARM, true);
  build(s_b[B_SCY_L], RIG_SCYTHE, false);
  build(s_b[B_SCY_R], RIG_SCYTHE, true);
  build(s_b[B_THI_L], RIG_THIGH, false);
  build(s_b[B_THI_R], RIG_THIGH, true);
  build(s_b[B_SHI_L], RIG_SHIN, false);
  build(s_b[B_SHI_R], RIG_SHIN, true);
  s_skin = RIG_HEAD.px[(int)(RIG_HEAD_MOUTH_Y - 16) * RIG_HEAD.w + (int)RIG_HEAD_MOUTH_X];
  if (s_skin == RIG_KEY) s_skin = rgb565(150, 205, 120);
  s_ready = true;
}

// ---------- kinematics ----------
static float s_Z = 0.72f;          // rig units -> screen px
static float s_rx = 160, s_ry = 163;

struct Xf { float x, y, a, zx, zy; };
// point (lx,ly) in bone's sprite space -> rig space
static inline void fk(const Xf &f, const Bone &b, float lx, float ly, float &ox, float &oy, bool mir = false) {
  if (mir) lx = b.w - lx;
  float dx = (lx - b.pvx) * f.zx, dy = (ly - b.pvy) * f.zy;
  float c = cosf(f.a * DEG), s = sinf(f.a * DEG);
  ox = f.x + c * dx - s * dy;
  oy = f.y + s * dx + c * dy;
}
static inline void toScr(float x, float y, float &sx, float &sy) { sx = s_rx + x * s_Z; sy = s_ry + y * s_Z; }
static void blit(Bone &b, const Xf &f) {
  if (!b.spr.width()) return;
  float sx, sy; toScr(f.x, f.y, sx, sy);
  b.spr.pushRotateZoomWithAA(&canvas, sx, sy, f.a, f.zx * s_Z, f.zy * s_Z, (uint16_t)RIG_KEY);
}
static float boneAngle(const Bone &b, float ex, float ey, bool mir) {
  float px = b.pvx, ex2 = mir ? b.w - ex : ex;
  return atan2f(ey - b.pvy, ex2 - px) / DEG;
}

// ---------- pose ----------
struct Pose {
  float x, y, torso, head, hx, hy, abd;
  float au[2], as[2];
  float fx[4], fy[4];
  float sq, mouth;
};
static const int NPF = sizeof(Pose) / sizeof(float);
static void lerpPose(Pose &o, const Pose &a, const Pose &b, float t) {
  const float *pa = (const float *)&a, *pb = (const float *)&b;
  float *po = (float *)&o;
  for (int i = 0; i < NPF; i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
}

enum Move { M_IDLE, M_BOB, M_SWAY, M_WAVE_L, M_WAVE_R, M_CHEER, M_BANG, M_SPREAD, M_SHUFFLE, M_SING, M_COUNT };

static inline float dip(float f) { return 0.5f + 0.5f * cosf(6.2831853f * f); }
static inline float hit(float f) { float u = 1.f - f; return u * u * u; }

static float s_t = 0;
static float s_look = 0, s_lookY = 0;      // touch/tilt gaze
static float s_sustain = 0;               // sing: held-note energy

static void evalMove(int m, float b, float e, Pose &p) {
  memset(&p, 0, sizeof(p));
  float f = b - floorf(b);
  int bi = ((int)floorf(b)) & 3;
  float sgn = (bi & 1) ? 1.f : -1.f;
  float t = s_t;
  switch (m) {
    case M_IDLE:
      p.y = sinf(t * 1.6f) * 1.4f;
      p.sq = sinf(t * 1.6f) * 0.25f;
      p.head = s_look * 16.f + sinf(t * 0.7f) * 4.f;
      p.hx = s_look * 3.f;
      p.torso = s_look * 4.f + sinf(t * 0.5f) * 1.5f;
      p.abd = sinf(t * 1.1f) * 5.f;
      p.as[0] = p.as[1] = sinf(t * 2.7f) * 5.f;
      p.au[0] = p.au[1] = 3.f + 2.f * sinf(t * 1.3f);
      break;
    case M_BOB:
      p.y = 8.f * e * dip(f) + 1.f;
      p.sq = 0.9f * hit(f) * e;
      p.head = 7.f * sgn * (1.f - f) + s_look * 8.f;
      p.hy = 2.f * dip(f);
      p.au[0] = p.au[1] = 16.f + 24.f * dip(f);
      p.as[0] = p.as[1] = -8.f + 16.f * dip(f);
      p.abd = 8.f * sinf(3.14159f * b);
      break;
    case M_SWAY: {
      float s = sinf(3.14159f * b);
      p.x = 13.f * s; p.torso = -9.f * s; p.head = 13.f * s; p.abd = -16.f * s;
      p.au[0] = 12.f + 60.f * fmaxf(0.f, s);
      p.au[1] = 12.f + 60.f * fmaxf(0.f, -s);
      p.as[0] = 25.f * fmaxf(0.f, s); p.as[1] = 25.f * fmaxf(0.f, -s);
      p.y = 5.f * dip(f) * e;
      p.fy[0] = p.fy[1] = -7.f * fmaxf(0.f, s);
      p.fy[2] = p.fy[3] = -7.f * fmaxf(0.f, -s);
      p.fx[0] = p.fx[1] = p.fx[2] = p.fx[3] = 4.f * s;
      break;
    }
    case M_WAVE_L: case M_WAVE_R: {
      int a = (m == M_WAVE_L) ? 0 : 1; float d = a ? 1.f : -1.f;
      p.au[a] = 150.f + 6.f * sinf(6.2831853f * b);
      p.as[a] = -15.f + 40.f * sinf(6.2831853f * b);
      p.au[1 - a] = 12.f; p.as[1 - a] = 6.f * dip(f);
      p.torso = 6.f * d; p.head = 10.f * d + 5.f * sinf(3.14159f * b); p.x = 5.f * d;
      p.y = 3.5f * dip(f); p.mouth = 0.25f;
      p.abd = -10.f * d;
      break;
    }
    case M_CHEER: {
      float air = sinf(3.14159f * f);
      p.au[0] = p.au[1] = 150.f + 12.f * cosf(6.2831853f * f);
      p.as[0] = p.as[1] = -30.f + 35.f * cosf(6.2831853f * f);
      p.y = -13.f * e * air;
      for (int i = 0; i < 4; i++) p.fy[i] = p.y * 0.9f;
      p.sq = 1.2f * hit(f) - 0.5f * air;
      p.head = 7.f * sinf(3.14159f * b); p.mouth = 0.55f + 0.3f * air;
      p.abd = 12.f * sinf(6.2831853f * b);
      break;
    }
    case M_BANG: {
      float h = hit(f);
      p.hy = 6.f * h; p.head = 18.f * sgn * h; p.torso = 6.f * sgn * h;
      p.au[0] = p.au[1] = 32.f;
      p.as[0] = (bi & 1) ? 55.f * h : 0.f; p.as[1] = (bi & 1) ? 0.f : 55.f * h;
      p.y = 6.f * h; p.sq = 1.0f * h; p.abd = -14.f * sgn * h;
      break;
    }
    case M_SPREAD:
      p.au[0] = p.au[1] = 95.f;
      p.as[0] = p.as[1] = 55.f + 18.f * sinf(6.2831853f * b);
      p.abd = 22.f * sinf(12.566f * b);
      p.y = -2.f - 3.f * dip(f); p.sq = -0.5f; p.head = 8.f * sinf(3.14159f * b); p.mouth = 0.4f;
      break;
    case M_SHUFFLE: {
      float s = sinf(3.14159f * b), up = sinf(3.14159f * f);
      if (bi & 1) { p.fy[0] = p.fy[2] = -11.f * up; } else { p.fy[1] = p.fy[3] = -11.f * up; }
      p.x = 7.f * s; p.y = 4.f * dip(f);
      p.au[0] = 25.f + 35.f * fmaxf(0.f, s); p.au[1] = 25.f + 35.f * fmaxf(0.f, -s);
      p.as[0] = 30.f * fmaxf(0.f, -s); p.as[1] = 30.f * fmaxf(0.f, s);
      p.head = -9.f * s; p.torso = 4.f * s; p.abd = 12.f * s;
      break;
    }
    case M_SING: {
      float lv = clampf(aud::level * 1.6f, 0.f, 1.f);
      p.torso = sinf(t * 1.1f) * 4.f - lv * 3.f;
      p.head = sinf(t * 0.8f) * 6.f + (aud::zcr - 0.4f) * 14.f + s_look * 10.f;
      p.hy = -lv * 3.f;
      p.x = sinf(t * 0.55f) * 4.f;
      // left scythe cradles the microphone near the mouth
      p.au[0] = 118.f + lv * 6.f; p.as[0] = 118.f + sinf(t * 1.3f) * 3.f;
      // right arm: diva reach grows with held notes, flutters with vibrato
      p.au[1] = 14.f + 115.f * s_sustain + sinf(t * 7.f) * 5.f * s_sustain;
      p.as[1] = 10.f + 55.f * s_sustain + sinf(t * 9.f) * 8.f * lv;
      p.abd = sinf(t * 0.9f) * 8.f;
      p.y = sinf(t * 1.1f) * 1.2f + lv * 2.f;
      break;
    }
  }
}

// ---------- behaviour state ----------
static int s_move = M_IDLE, s_prevMove = M_IDLE;
static float s_blend = 1.f;
static int s_lastBar = -1;
static float s_acc = 0;
static float s_blinkT = 2.f, s_blink = 0;
static float s_happy = 0;                 // after petting: ^^ eyes + blush
static float s_mouthOpen = 0, s_mouthWide = 0;
static float s_idleBeat = 0;
static float s_idleWaveT = 6.f;
static float s_dizzy = 0;
static uint32_t s_touchUntil = 0;
static bool s_lastSing = false;

struct Fx { float x, y, vx, vy, life; uint8_t kind; float hue; };
static Fx s_fx[24];
static void spawnFx(float x, float y, uint8_t kind, float hue) {
  for (auto &p : s_fx) if (p.life <= 0) {
    p.x = x; p.y = y; p.kind = kind; p.hue = hue; p.life = 1.f;
    p.vx = (float)((int)(esp_random() % 61) - 30) * 0.9f;
    p.vy = -30.f - (float)(esp_random() % 30);
    return;
  }
}

static int pickMove(float e) {
  static const uint8_t low[] = {M_BOB, M_SWAY, M_BOB, M_SHUFFLE};
  static const uint8_t mid[] = {M_SWAY, M_WAVE_L, M_SHUFFLE, M_WAVE_R, M_BANG, M_BOB};
  static const uint8_t high[] = {M_CHEER, M_BANG, M_SPREAD, M_SHUFFLE, M_CHEER, M_SWAY};
  for (int tries = 0; tries < 6; tries++) {
    int m;
    if (e < 0.35f) m = low[esp_random() % sizeof(low)];
    else if (e < 0.7f) m = mid[esp_random() % sizeof(mid)];
    else m = high[esp_random() % sizeof(high)];
    if (m != s_move) return m;
  }
  return M_BOB;
}
static void setMove(int m) {
  if (m == s_move) return;
  s_prevMove = s_move; s_move = m; s_blend = 0.f;
}

void mantisTap(int x, int y) {
  // head region in screen space (approx; uses last frame's layout)
  float hx = s_rx, hy = s_ry - 150.f * s_Z;
  float dx = x - hx, dy = y - hy;
  if (dx * dx + dy * dy < (60.f * s_Z) * (60.f * s_Z) + 400.f) {
    s_happy = 1.6f; s_acc = 1.f;
    hap(70, 60);
    for (int i = 0; i < 4; i++) spawnFx((float)x, (float)y, 1, 330.f);
    if ((esp_random() & 3) == 0) aud::sfx(1.7f);
  } else {
    s_acc = 0.7f;
    for (int i = 0; i < 2; i++) spawnFx((float)x, (float)y, 2, g_hue);
    hap(40, 20);
  }
}

// ---------- stage (fx buffer) ----------
static void drawStage(bool sing, float beatF, float e) {
  static float rot = 0;
  rot += g_dt * (0.25f + e * 1.2f) * 40.f;
  int cx = 80 + (int)(g_lookX * -10.f), cy = sing ? 34 : 52;
  int floorY = sing ? 200 : (int)((s_ry + 85.f * s_Z) * 0.5f);
  uint8_t r8 = (uint8_t)rot;
  int pulseR = (int)(beatF * 90.f);
  int glow = 40 + (int)(aud::bass * 60.f + s_acc * 30.f);
  auto rows_ = [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
    uint8_t *row = fx::buf + y * fx::LW;
    if (y >= floorY) {
      int d = y - floorY + 1;
      int z = 900 / (d + 2);
      for (int x = 0; x < fx::LW; x++) {
        int u = ((x - 80) * z) >> 6;
        int chk = ((u >> 3) ^ ((z + (int)(s_t * 30.f)) >> 3)) & 1;
        int v = (chk ? 70 : 28) + (glow >> 2) - (d >> 1);
        row[x] = (uint8_t)(v < 8 ? 8 : (v > 200 ? 200 : v));
      }
      continue;
    }
    for (int x = 0; x < fx::LW; x++) {
      uint16_t t = fx::tunAt(x - cx, y - cy);
      int a = t >> 8;
      int r = fx::radAt(x - cx, y - cy);
      int ray = fx::sn[(uint8_t)(a * 6 + r8)] > 40 ? 26 : 12;
      int ringd = abs(r - pulseR);
      int ring = ringd < 4 ? (4 - ringd) * 14 : 0;
      int g = glow - r / 2; if (g < 0) g = 0;
      int v = ray + ring + g;
      row[x] = (uint8_t)(v > 180 ? 180 : v);
    }
  }
  };
  fx::parallel(rows_);
  float hsh = g_hue / 360.f;
  fx::palCosine(0.35f, 0.25f, 0.45f, 0.35f, 0.3f, 0.35f, 1.f, 1.f, 1.f, 0.f + hsh, 0.25f + hsh, 0.6f + hsh, 0.f);
  fx::present(canvas);
}

// ---------- face details ----------
static void drawHeart(int x, int y, int r, uint16_t c) {
  canvas.fillCircle(x - r / 2, y, r / 2 + 1, c);
  canvas.fillCircle(x + r / 2, y, r / 2 + 1, c);
  canvas.fillTriangle(x - r - 1, y + 1, x + r + 1, y + 1, x, y + r + 2, c);
}
static void drawNote(int x, int y, uint16_t c) {
  canvas.fillEllipse(x, y, 3, 2, c);
  canvas.drawFastVLine(x + 2, y - 9, 9, c);
  canvas.drawLine(x + 2, y - 9, x + 6, y - 6, c);
}

static void drawFace(const Xf &hf, bool sing, float lid) {
  Bone &hb = s_b[B_HEAD];
  float zs = hf.zx * s_Z;
  // eyes / blink / happy
  float ex[2], ey[2];
  fk(hf, hb, RIG_HEAD_EYEL_X, RIG_HEAD_EYEL_Y, ex[0], ey[0]);
  fk(hf, hb, RIG_HEAD_EYER_X, RIG_HEAD_EYER_Y, ex[1], ey[1]);
  bool happy = s_happy > 0.2f;
  for (int i = 0; i < 2; i++) {
    float sx, sy; toScr(ex[i], ey[i], sx, sy);
    int rx = (int)(13.5f * zs), ry = (int)(15.5f * zs);
    if (happy) {
      canvas.fillEllipse((int)sx, (int)sy, rx + 1, ry + 1, s_skin);
      int w = rx - 2;
      for (int k = -1; k <= 1; k++) {
        canvas.drawLine((int)sx - w, (int)sy + 3 + k, (int)sx, (int)sy - ry / 3 + k, rgb565(20, 50, 25));
        canvas.drawLine((int)sx, (int)sy - ry / 3 + k, (int)sx + w, (int)sy + 3 + k, rgb565(20, 50, 25));
      }
    } else if (s_dizzy > 0.05f) {
      canvas.fillEllipse((int)sx, (int)sy, rx, ry, rgb565(245, 245, 250));
      float a0 = s_t * 14.f * (i ? -1.f : 1.f);
      int px = (int)sx, py = (int)sy;
      for (int k = 1; k < 18; k++) {
        float a = a0 + k * 0.7f, rr = k * rx / 18.f;
        int nx = (int)(sx + cosf(a) * rr), ny = (int)(sy + sinf(a) * rr);
        canvas.drawLine(px, py, nx, ny, rgb565(30, 20, 60));
        px = nx; py = ny;
      }
    } else if (lid > 0.02f) {
      // lid slides down the round eye: per-column, follows the eye's curve
      int cut = (int)(sy - ry - 1 + (2 * ry + 2) * lid);
      for (int x = -rx - 1; x <= rx + 1; x++) {
        float u = (float)x / (rx + 1);
        int e = (int)((ry + 1) * sqrtf(fmaxf(0.f, 1.f - u * u)));
        int top = (int)sy - e, bot = (int)sy + e;
        int c = cut + (int)((ry * 0.25f) * (1.f - u * u) * (1.f - lid));
        if (c > bot) c = bot;
        if (c > top) canvas.drawFastVLine((int)sx + x, top, c - top, s_skin);
        if (c > top && c < bot) canvas.drawPixel((int)sx + x, c, rgb565(20, 45, 25));
      }
    }
  }
  // blush
  if (happy || sing || s_acc > 0.6f) {
    float a = happy ? 1.f : (sing ? clampf(aud::level * 2.f, 0.f, 0.8f) : 0.5f);
    uint16_t bc = rgb565(255, (uint8_t)(170 - a * 40), (uint8_t)(190 - a * 30));
    for (int i = 0; i < 2; i++) {
      float cx, cy; fk(hf, hb, i ? 84.f : 38.f, 79.f, cx, cy);
      float sx, sy; toScr(cx, cy, sx, sy);
      if (a > 0.2f) canvas.fillEllipse((int)sx, (int)sy, (int)(5.f * zs), (int)(2.5f * zs), bc);
    }
  }
  // mouth — anchored to the sprite's own mouth point, scaled with the head
  float mo = s_mouthOpen, wi = s_mouthWide;
  if (mo > 0.06f) {
    float mx, my; fk(hf, hb, RIG_HEAD_MOUTH_X, RIG_HEAD_MOUTH_Y - 1.f, mx, my);
    float sx, sy; toScr(mx, my, sx, sy);
    int mw = (int)((7.f + 6.f * wi - 2.f * mo * (1.f - wi)) * zs);
    int mh = (int)((1.5f + 12.f * mo * (1.f - 0.45f * wi)) * zs);
    if (mw < 2) mw = 2;
    if (mh < 1) mh = 1;
    int X = (int)sx, Y = (int)sy + mh / 3;
    canvas.fillEllipse(X, Y, mw + 2, mh + 2, rgb565(30, 110, 50));
    canvas.fillEllipse(X, Y, mw, mh, rgb565(40, 8, 22));
    if (mh > 4) canvas.fillEllipse(X, Y + mh / 2, mw * 2 / 3, mh / 3 + 1, rgb565(210, 70, 100));
    if (mo > 0.3f && mh > 5) {
      canvas.fillRect(X - mw * 2 / 3, Y - mh + 1, mw * 4 / 3, 1 + mh / 5, rgb565(245, 245, 235));
      for (int k = -1; k <= 1; k++) canvas.drawFastVLine(X + k * mw / 3, Y - mh + 1, 1 + mh / 5, rgb565(180, 180, 170));
    }
  }
}

// ---------- the frame ----------
void mantisDraw(bool sing) {
  if (!s_ready) return;
  float dt = g_dt;
  s_t += dt;

  // layout: dance shows the whole bug; sing moves in close on the face
  float zT = sing ? 1.12f : 0.72f;
  float ryT = sing ? 236.f : 163.f;
  if (sing != s_lastSing) { s_lastSing = sing; s_blend = 0.f; s_prevMove = s_move; }
  s_Z += (zT - s_Z) * clampf(dt * 5.f, 0.f, 1.f);
  s_ry += (ryT - s_ry) * clampf(dt * 5.f, 0.f, 1.f);
  s_rx = 160.f;

  // gaze: tilt, or finger while touching
  auto td = M5.Touch.getDetail();
  float lookT = clampf(g_lookX, -1.f, 1.f);
  if (td.isPressed() && td.y > 14 && td.y < H - 14) { lookT = clampf((td.x - 160) / 120.f, -1.f, 1.f); s_touchUntil = millis() + 1200; }
  else if (millis() < s_touchUntil) lookT = s_look;
  s_look += (lookT - s_look) * clampf(dt * 6.f, 0.f, 1.f);

  // energy & beat
  float e = clampf(aud::level * 1.5f + aud::beatConf * 0.25f, 0.f, 1.f);
  if (aud::onset > 0.f) s_acc = fmaxf(s_acc, aud::onset);
  s_acc *= expf(-dt * 7.f);
  bool grooving = aud::beatConf > 0.3f || aud::level > 0.22f;
  float b = grooving ? aud::beatPos : (s_idleBeat += dt * 0.8f);

  if (sing) {
    float lv = clampf(aud::level * 1.8f, 0.f, 1.f);
    s_sustain += ((lv > 0.35f ? 1.f : 0.f) - s_sustain) * clampf(dt * (lv > 0.35f ? 0.9f : 2.5f), 0.f, 1.f);
    if (s_move != M_SING) setMove(M_SING);
  } else if (grooving) {
    int bar = (int)floorf(b / 4.f);
    if (s_move == M_IDLE || s_move == M_SING || bar != s_lastBar) { s_lastBar = bar; setMove(pickMove(e)); }
  } else {
    s_idleWaveT -= dt;
    if (s_idleWaveT < 0 && s_move == M_IDLE) { setMove((esp_random() & 1) ? M_WAVE_L : M_WAVE_R); s_idleWaveT = 2.5f; }
    else if (s_idleWaveT < 0) { setMove(M_IDLE); s_idleWaveT = 7.f + (esp_random() % 6); }
    else if (s_move != M_IDLE && s_move != M_WAVE_L && s_move != M_WAVE_R) setMove(M_IDLE);
  }

  Pose pa, pb, p;
  evalMove(s_prevMove, b, e, pa);
  evalMove(s_move, b, e, pb);
  s_blend = fminf(1.f, s_blend + dt * 3.2f);
  float k = s_blend * s_blend * (3.f - 2.f * s_blend);
  lerpPose(p, pa, pb, k);
  p.y += 3.f * s_acc; p.sq += 0.7f * s_acc; p.hy += 1.5f * s_acc;
  if (s_happy > 0) { s_happy -= dt; p.y -= 4.f * sinf(s_t * 18.f) * fminf(1.f, s_happy); p.mouth = fmaxf(p.mouth, 0.35f); }
  if (g_shakeKick) s_dizzy = 1.4f;
  if (s_dizzy > 0) { s_dizzy -= dt; p.head += sinf(s_t * 11.f) * 14.f * s_dizzy; p.torso += sinf(s_t * 7.f) * 5.f * s_dizzy; }

  // blink
  s_blinkT -= dt;
  if (s_blinkT < 0) { s_blink = 1.f; s_blinkT = 2.f + (esp_random() % 3000) / 1000.f; }
  s_blink = s_blink > 0 ? s_blink - dt * 7.f : 0.f;
  float lid = s_blink > 0.5f ? (1.f - s_blink) * 2.f : s_blink * 2.f;

  // mouth
  float mTarget = p.mouth;
  if (sing) {
    float lv = aud::speakerLive() ? aud::level : clampf((aud::level - 0.03f) * 2.4f, 0.f, 1.f);
    mTarget = fmaxf(mTarget, clampf(lv + aud::onset * 0.3f, 0.f, 1.f));
    s_mouthWide += (clampf(aud::zcr * 1.6f, 0.f, 1.f) - s_mouthWide) * clampf(dt * 10.f, 0.f, 1.f);
  } else s_mouthWide *= 0.9f;
  s_mouthOpen += (mTarget - s_mouthOpen) * clampf(dt * (mTarget > s_mouthOpen ? 22.f : 9.f), 0.f, 1.f);

  // ---------- stage ----------
  float beatF = b - floorf(b);
  drawStage(sing, beatF, e);

  // ---------- skeleton ----------
  Bone &T = s_b[B_TORSO];
  float sq = clampf(p.sq, -0.8f, 1.5f);
  Xf tf{p.x, p.y, p.torso, 1.f + sq * 0.05f, 1.f - sq * 0.075f};

  // hips / shoulders / neck / abdomen anchors
  float hipX[4], hipY[4];
  const float hipLx[4] = {RIG_TORSO_HIPBL_X, RIG_TORSO_HIPFL_X, RIG_TORSO_HIPFR_X, RIG_TORSO_HIPBR_X};
  const float hipLy[4] = {RIG_TORSO_HIPBL_Y, RIG_TORSO_HIPFL_Y, RIG_TORSO_HIPFR_Y, RIG_TORSO_HIPBR_Y};
  for (int i = 0; i < 4; i++) fk(tf, T, hipLx[i], hipLy[i], hipX[i], hipY[i]);
  float shX[2], shY[2];
  fk(tf, T, RIG_TORSO_SHL_X, RIG_TORSO_SHL_Y, shX[0], shY[0]);
  fk(tf, T, RIG_TORSO_SHR_X, RIG_TORSO_SHR_Y, shX[1], shY[1]);
  float nkX, nkY; fk(tf, T, RIG_TORSO_NECK_X, RIG_TORSO_NECK_Y, nkX, nkY);
  float abX, abY; fk(tf, T, RIG_TORSO_ABD_X, RIG_TORSO_ABD_Y, abX, abY);

  // abdomen (behind everything)
  Xf af{abX, abY, -48.f + p.abd + p.torso * 0.5f, 1.f, 1.f};
  blit(s_b[B_ABD], af);

  // legs: IK with planted feet
  const float footBx[4] = {-62.f, -34.f, 34.f, 62.f};
  const float floorY = 85.f;
  float L1 = hypotf(RIG_THIGH_END_X - RIG_THIGH.pvx, RIG_THIGH_END_Y - RIG_THIGH.pvy);
  float L2 = hypotf(RIG_SHIN_END_X - RIG_SHIN.pvx, RIG_SHIN_END_Y - RIG_SHIN.pvy);
  const int legOrder[4] = {0, 3, 1, 2};   // back pair first
  for (int oi = 0; oi < 4; oi++) {
    int i = legOrder[oi];
    bool right = i >= 2;
    float fX = footBx[i] + p.fx[i], fY = floorY + p.fy[i];
    float dx = fX - hipX[i], dy = fY - hipY[i];
    float d = hypotf(dx, dy);
    float reach = (L1 + L2) * 0.995f;
    if (d > reach) { fX = hipX[i] + dx / d * reach; fY = hipY[i] + dy / d * reach; d = reach; }
    if (d < fabsf(L1 - L2) + 1.f) d = fabsf(L1 - L2) + 1.f;
    float th = atan2f(dy, dx);
    float ca = clampf((L1 * L1 + d * d - L2 * L2) / (2.f * L1 * d), -1.f, 1.f);
    float al = acosf(ca);
    float ka = right ? th - al : th + al;
    float kX = hipX[i] + cosf(ka) * L1, kY = hipY[i] + sinf(ka) * L1;
    Bone &TH = s_b[right ? B_THI_R : B_THI_L];
    Bone &SH = s_b[right ? B_SHI_R : B_SHI_L];
    float thRest = boneAngle(TH, RIG_THIGH_END_X, RIG_THIGH_END_Y, right);
    float shRest = boneAngle(SH, RIG_SHIN_END_X, RIG_SHIN_END_Y, right);
    float shA = atan2f(fY - kY, fX - kX) / DEG;
    Xf sf{kX, kY, shA - shRest, 1.f, 1.f};
    Xf thf{hipX[i], hipY[i], ka / DEG - thRest, 1.f, 1.f};
    blit(TH, thf);
    blit(SH, sf);
  }

  blit(T, tf);

  // head
  Xf hf{nkX + p.hx, nkY + p.hy, p.torso + p.head, 1.f - sq * 0.03f, 1.f + sq * 0.02f};

  // arms (upper then scythe). index 0 = viewer-left, positive = outward/up
  float tipX[2], tipY[2], tipA[2];
  float micX = 0, micY = 0, mouthX = 0, mouthY = 0;
  fk(hf, s_b[B_HEAD], RIG_HEAD_MOUTH_X, RIG_HEAD_MOUTH_Y, mouthX, mouthY);
  micX = mouthX - 12.f; micY = mouthY + 14.f;
  for (int a = 0; a < 2; a++) {
    bool r = a == 1;
    float sgn = r ? -1.f : 1.f;
    Bone &AR = s_b[r ? B_ARM_R : B_ARM_L];
    Bone &SC = s_b[r ? B_SCY_R : B_SCY_L];
    Xf uf{shX[a], shY[a], p.torso + sgn * p.au[a], 1.f, 1.f};
    float eX, eY; fk(uf, AR, RIG_ARM_END_X, RIG_ARM_END_Y, eX, eY, r);
    Xf scf{eX, eY, uf.a + sgn * p.as[a], 1.f, 1.f};
    if (sing && a == 0) {
      // 2-bone IK: the hook grips the mic handle just under the mouth
      float L1 = hypotf(RIG_ARM_END_X - AR.pvx, RIG_ARM_END_Y - AR.pvy);
      float L2 = hypotf(RIG_SCYTHE_TIP_X - SC.pvx, RIG_SCYTHE_TIP_Y - SC.pvy);
      float gX = micX - 16.f, gY = micY + 30.f;
      float dx = gX - shX[0], dy = gY - shY[0], d = clampf(hypotf(dx, dy), fabsf(L1 - L2) + 1.f, (L1 + L2) * 0.99f);
      float th = atan2f(dy, dx), al = acosf(clampf((L1 * L1 + d * d - L2 * L2) / (2.f * L1 * d), -1.f, 1.f));
      float ea = th + al;                                   // elbow swings out to the left
      if (cosf(th - al) * L1 < cosf(ea) * L1) ea = th - al;
      eX = shX[0] + cosf(ea) * L1; eY = shY[0] + sinf(ea) * L1;
      uf.a = ea / DEG - boneAngle(AR, RIG_ARM_END_X, RIG_ARM_END_Y, false);
      scf.x = eX; scf.y = eY;
      scf.a = atan2f(gY - eY, gX - eX) / DEG - boneAngle(SC, RIG_SCYTHE_TIP_X, RIG_SCYTHE_TIP_Y, false);
    }
    blit(AR, uf);
    blit(SC, scf);
    fk(scf, SC, RIG_SCYTHE_TIP_X, RIG_SCYTHE_TIP_Y, tipX[a], tipY[a], r);
    tipA[a] = scf.a;
  }
  blit(s_b[B_HEAD], hf);
  drawFace(hf, sing, lid);

  // sing prop: a little mic, gripped by the left hook, head just under the mouth
  if (sing) {
    float hx, hy, gx, gy, ax, ay;
    toScr(micX, micY, hx, hy);
    toScr(micX - 16.f, micY + 30.f, gx, gy);
    toScr(mouthX, mouthY, ax, ay);
    for (int k = -2; k <= 2; k++)
      canvas.drawLine((int)gx + k, (int)gy + 6, (int)hx + k, (int)hy, k == -2 ? rgb565(30, 30, 38) : (k == 1 ? rgb565(150, 150, 165) : rgb565(80, 80, 92)));
    int mr = (int)(7.f * s_Z);
    canvas.fillCircle((int)hx, (int)hy, mr + 1, rgb565(30, 30, 40));
    canvas.fillCircle((int)hx, (int)hy, mr, rgb565(120, 120, 135));
    for (int k = -mr + 2; k < mr - 1; k += 3) canvas.drawFastHLine((int)hx - mr + 2, (int)hy + k, 2 * mr - 3, rgb565(70, 70, 85));
    canvas.fillCircle((int)hx - mr / 3, (int)hy - mr / 3, 2, rgb565(230, 230, 245));
    (void)tipA;
    static float noteT = 0;
    noteT -= dt;
    if (aud::level > 0.2f && noteT < 0) {
      noteT = 0.35f - aud::level * 0.15f;
      spawnFx(ax + 10.f, ay - 6.f, 3, g_hue + (esp_random() % 120));
    }
  }

  // floating hearts / sparks / notes
  for (auto &q : s_fx) {
    if (q.life <= 0) continue;
    q.life -= dt * (q.kind == 3 ? 0.5f : 0.8f);
    q.x += q.vx * dt * (q.kind == 3 ? 0.5f : 1.f) + (q.kind == 3 ? sinf(s_t * 5.f + q.hue) * 0.6f : 0.f);
    q.y += q.vy * dt;
    q.vy += (q.kind == 2 ? 60.f : -6.f) * dt;
    uint16_t c = hsv565(q.hue, q.kind == 1 ? 0.55f : 0.8f, 0.6f + 0.4f * q.life);
    if (q.kind == 1) drawHeart((int)q.x, (int)q.y, 4 + (int)(q.life * 3), c);
    else if (q.kind == 3) drawNote((int)q.x, (int)q.y, c);
    else canvas.fillCircle((int)q.x, (int)q.y, 1 + (int)(q.life * 2.f), c);
  }
}
