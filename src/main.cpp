// ============================================================
//  SYNAPSE — a small green god in a glass panel · M5Stack Core2
//  main.cpp: frame pipeline, input, visual modes.
//  audio.cpp: always-on listening. calm.cpp: the physics room.
//  fx.cpp: indexed-colour demo engine. mantis.cpp: the puppet.
// ============================================================
#include "app.h"
#include "audio.h"
#include "fx.h"
#include "mantis_splash.h"
#include <string.h>

// ---------------- shared state ----------------
M5Canvas *g_cv = nullptr;
float g_t = 0, g_dt = 0.033f, g_hue = 160;
float g_level = 0, g_peak = 0;
float g_lookX = 0, g_lookY = 0;
float g_gravX = 0, g_gravY = 0;        // downhill direction in screen space (1 = 1 g)
int16_t g_mic[MIC_N];

static float g_ax = 0, g_ay = 0, g_az = 1, g_gx = 0, g_gy = 0, g_gz = 0;
static float g_shake = 0;
static uint32_t g_shakeAt = 0;
bool g_shakeKick = false;                  // one-frame shake event

enum Mode : uint8_t { MODE_SWARM = 0, MODE_EYE, MODE_TUNNEL, MODE_PULSE, MODE_CALM, MODE_MANTIS, MODE_COUNT };
enum SwarmVar : uint8_t { SV_FLOCK = 0, SV_ORBIT, SV_CHAOS, SV_COUNT };
enum TunnelMode : uint8_t { TM_DIVE = 0, TM_RECEDE, TM_FRACTAL, TM_PORTAL, TM_COUNT };
static const int PP_COUNT = 5;
static Mode g_mode = MODE_SWARM;
static SwarmVar g_swarmVar = SV_FLOCK;
static TunnelMode g_tunnelMode = TM_DIVE;
static int g_pulsePat = 0;
static bool g_eyeTrack = true;
static bool g_mantisSing = false;

// ---------------- haptics (non-blocking, I2C writes only on change) ----------------
static uint32_t g_hapUntil = 0, g_kickStart = 0, g_kickEnd = 0;
static uint8_t g_hapLevel = 0, g_vibNow = 255;
static void vib(uint8_t v) { if (v != g_vibNow) { g_vibNow = v; M5.Power.setVibration(v); } }
void hap(uint8_t level, uint16_t ms) {
  if (level >= g_hapLevel || millis() >= g_hapUntil) { g_hapLevel = level; g_hapUntil = millis() + ms; }
}
void kickSubHaptic() { g_kickStart = millis(); g_kickEnd = g_kickStart + 150; }
static void hapService() {
  uint32_t now = millis();
  if (g_kickEnd && now < g_kickEnd) {                 // "subwoofer" throb, not a phone buzz
    float u = (float)(now - g_kickStart) / (float)(g_kickEnd - g_kickStart);
    float env = (1.f - u) * (1.f - u);
    vib((uint8_t)(env * (((now / 9) & 1) ? 230 : 40)));
    return;
  }
  g_kickEnd = 0;
  if (g_hapLevel && now < g_hapUntil) { vib(g_hapLevel); return; }
  g_hapLevel = 0;
  vib(0);
}

// ---------------- frame pipeline: render core 1, push core 0 ----------------
static M5Canvas s_fbA(&M5.Display), s_fbB(&M5.Display);
static M5Canvas *s_fb[2] = {&s_fbA, &s_fbB};
static int s_cur = 0;
static bool s_double = false;
static TaskHandle_t s_dispTask = nullptr;
static SemaphoreHandle_t s_dispIdle = nullptr;
static volatile int s_pushIdx = 0;

static void displayTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    s_fb[s_pushIdx]->pushSprite(&M5.Display, 0, 0);
    xSemaphoreGive(s_dispIdle);
  }
}

static void pollInput();
static void present() {
  if (!s_double) {
    s_fb[0]->pushSprite(&M5.Display, 0, 0);
    pollInput();
    return;
  }
  // wait for the LCD to finish the previous frame — and keep reading input meanwhile
  while (xSemaphoreTake(s_dispIdle, 0) != pdTRUE) { pollInput(); vTaskDelay(1); }
  s_pushIdx = s_cur;
  xTaskNotifyGive(s_dispTask);
  s_cur ^= 1;
  g_cv = s_fb[s_cur];
}

// ---------------- input ----------------
static bool g_tapLatch = false;
static int g_tapX = 0, g_tapY = 0;
static bool takeTap(int &x, int &y) {
  if (!g_tapLatch) return false;
  g_tapLatch = false; x = g_tapX; y = g_tapY;
  return true;
}
static void nextMode(int dir);
static void btnBShort();
static void eyePoke(int x, int y);
static uint32_t s_bDown = 0;
static uint32_t s_touchDown = 0;
static int s_touchX0 = 0, s_touchY0 = 0;
static bool s_longFired = false;

static void pollInput() {
  M5.update();
  hapService();
  uint32_t now = millis();
  if (M5.BtnA.wasPressed()) nextMode(-1);
  if (M5.BtnC.wasPressed()) nextMode(1);
  if (M5.BtnB.wasPressed()) s_bDown = now;
  if (M5.BtnB.wasReleased() && s_bDown) { btnBShort(); s_bDown = 0; }

  auto td = M5.Touch.getDetail();
  bool inStage = td.y >= 14 && td.y < H - 14;
  if (td.wasPressed()) {
    if (td.y >= H - 14 && td.y < H) { if (td.x < 90) nextMode(-1); else if (td.x > 230) nextMode(1); }
    else if (inStage) {
      g_tapLatch = true; g_tapX = td.x; g_tapY = td.y;
      s_touchDown = now; s_touchX0 = td.x; s_touchY0 = td.y; s_longFired = false;
      if (g_mode == MODE_EYE) eyePoke(td.x, td.y);
      else if (g_mode == MODE_MANTIS) mantisTap(td.x, td.y);
      else if (g_mode == MODE_CALM) calmTouch(td.x, td.y, true);
    }
  } else if (td.isPressed() && inStage && s_touchDown) {
    if (abs(td.x - s_touchX0) + abs(td.y - s_touchY0) > 14) s_touchDown = 0;   // it's a drag
    else if (!s_longFired && now - s_touchDown > 600) {
      s_longFired = true;
      if (g_mode == MODE_CALM) { calmLongPress(); hap(60, 60); }
    }
  }
  if (!td.isPressed()) s_touchDown = 0;
}

// ---------------- sensors ----------------
static void sampleImu() {
  if (!M5.Imu.update()) return;
  auto d = M5.Imu.getImuData();
  float ax = d.accel.x, ay = d.accel.y, az = d.accel.z;
  g_ax = g_ax * 0.7f + ax * 0.3f; g_ay = g_ay * 0.7f + ay * 0.3f; g_az = g_az * 0.7f + az * 0.3f;
  g_gx = d.gyro.x; g_gy = d.gyro.y; g_gz = d.gyro.z;
  g_gravX = -g_ay; g_gravY = g_ax;
  float tx = -g_ay, ty = g_ax;
  if (fabsf(tx) < 0.08f) tx = 0;
  if (fabsf(ty) < 0.08f) ty = 0;
  if (fabsf(g_gx) + fabsf(g_gy) > 8.f) { g_lookX += g_gy * 0.0025f; g_lookY += g_gx * 0.0025f; }
  float k = (fabsf(tx) + fabsf(ty) < 0.15f) ? 0.22f : 0.12f;
  g_lookX += (tx - g_lookX) * k; g_lookY += (ty - g_lookY) * k;
  g_lookX = clampf(g_lookX, -1.1f, 1.1f); g_lookY = clampf(g_lookY, -1.1f, 1.1f);
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  g_shake = g_shake * 0.8f + fabsf(mag - 1.f) * 0.2f;
  if (g_shake > 0.5f && millis() - g_shakeAt > 350) {
    g_shakeAt = millis(); g_shakeKick = true;
    g_hue = fmodf(g_hue + 40.f + (esp_random() % 120), 360.f);
    hap(150, 40);
  }
}

// ============================================================
//  SWARM
// ============================================================
static const int N_PART = 72, N_TRAIL = 96;
struct Particle { float x, y, vx, vy, hue; };
struct Trail { int16_t x, y; uint16_t c; uint8_t life; };
static Particle g_p[N_PART];
static Trail g_tr[N_TRAIL];
static int g_trI = 0;

static void seedParticles() {
  for (int i = 0; i < N_PART; i++) {
    g_p[i].x = (float)(esp_random() % W); g_p[i].y = (float)(14 + esp_random() % (H - 30));
    g_p[i].vx = ((int)(esp_random() % 100) - 50) * 0.03f; g_p[i].vy = ((int)(esp_random() % 100) - 50) * 0.03f;
    g_p[i].hue = g_hue + (esp_random() % 60) - 30;
  }
}
static void addTrail(int x, int y, uint16_t c) {
  g_tr[g_trI] = {(int16_t)x, (int16_t)y, c, 40};
  g_trI = (g_trI + 1) % N_TRAIL;
}
static void drawPsyBg() {
  float pulse = 0.08f + g_level * 0.18f + g_peak * 0.12f;
  for (int i = 0; i < 12; i++) {
    float yy = 16.f + fmodf(i * 19.f + g_t * (12.f + g_level * 40.f) + g_lookY * 30.f + 400.f, (float)(H - 30));
    canvas.drawFastHLine(0, (int)yy, W, hsv565(g_hue + i * 18.f + g_t * 15.f, 0.7f, pulse * (0.5f + 0.5f * sinf(i + g_t * 2.f))));
  }
  for (int i = 0; i < 10; i++) {
    float xx = fmodf(i * 37.f + g_t * (8.f + g_peak * 25.f) + g_lookX * 40.f + 400.f, (float)W);
    canvas.drawFastVLine((int)xx, 14, H - 28, hsv565(g_hue + 80.f + i * 12.f, 0.6f, 0.06f + g_peak * 0.15f * (0.5f + 0.5f * sinf(i * 1.7f + g_t))));
  }
}
static void drawTinyMantis(int cx, int cy) {
  const int sc = 5;
  int dw = MANTIS_W / sc, dh = MANTIS_H / sc, ox = cx - dw / 2, oy = cy - dh / 2;
  for (int y = 0; y < dh; y++)
    for (int x = 0; x < dw; x++) {
      uint16_t c = mantis_splash[(y * sc) * MANTIS_W + x * sc];
      if (c) canvas.drawPixel(ox + x, oy + y, c);
    }
}
static void modeSwarm() {
  canvas.fillSprite(rgb565(8, 4, 16));
  drawPsyBg();
  for (int i = 0; i < N_TRAIL; i++) {
    Trail &t = g_tr[i];
    if (t.life < 2) continue;
    int r = 1 + (t.life >> 4);
    canvas.fillCircle(t.x, t.y, r, t.c);
    t.life = (uint8_t)(t.life * 0.88f);
  }
  float gx = 0, gy = 0;
  if (g_swarmVar == SV_CHAOS) { gx = g_lookX * 0.85f; gy = g_lookY * 0.85f; }
  float pulse = 0.45f + g_level * 1.5f;
  float soundHue = g_hue + g_level * 100.f + g_peak * 40.f + aud::centroid * 90.f;
  float metaR = (g_swarmVar == SV_ORBIT) ? 220.f : 500.f;
  float metaPull = (g_swarmVar == SV_ORBIT) ? 0.0004f : 0.0015f;
  for (int i = 0; i < N_PART; i++)
    for (int j = i + 1; j < N_PART; j += 3) {
      float dx = g_p[i].x - g_p[j].x, dy = g_p[i].y - g_p[j].y, d2 = dx * dx + dy * dy;
      if (d2 < metaR && d2 > 1.f) {
        float tt = 1.f - d2 / metaR;
        int br = 2 + (int)(tt * (g_swarmVar == SV_ORBIT ? 4.f : 8.f) * (0.5f + g_level));
        canvas.fillCircle((int)((g_p[i].x + g_p[j].x) * 0.5f), (int)((g_p[i].y + g_p[j].y) * 0.5f), br,
                          hsv565(soundHue + i + j, 0.75f, 0.2f + tt * 0.4f));
        g_p[i].vx -= dx * metaPull * tt; g_p[i].vy -= dy * metaPull * tt;
        g_p[j].vx += dx * metaPull * tt; g_p[j].vy += dy * metaPull * tt;
      }
    }
  for (int i = 0; i < N_PART; i++) {
    Particle &p = g_p[i];
    float cx = W * 0.5f + sinf(g_t * 0.6f + i * 0.1f) * 22.f * g_peak;
    float cy = H * 0.5f + cosf(g_t * 0.5f) * 16.f * g_level;
    if (g_swarmVar == SV_FLOCK) {
      p.vx += (cx - p.x) * 0.0022f * pulse; p.vy += (cy - p.y) * 0.0022f * pulse;
      for (int j = 0; j < N_PART; j += 2) {
        if (j == i) continue;
        float sx = p.x - g_p[j].x, sy = p.y - g_p[j].y, d2 = sx * sx + sy * sy + 0.01f;
        if (d2 < 900.f) { p.vx += sx / d2 * 12.f; p.vy += sy / d2 * 12.f; }
      }
      p.vx *= 0.94f; p.vy *= 0.94f;
    } else if (g_swarmVar == SV_ORBIT) {
      float dx = p.x - cx, dy = p.y - cy;
      p.vx += -dy * 0.006f * pulse - dx * 0.001f; p.vy += dx * 0.006f * pulse - dy * 0.001f;
      p.vx *= 0.95f; p.vy *= 0.95f;
    } else {
      p.vx += gx * 1.2f + sinf(g_t * 2.f + p.y * 0.05f) * 0.15f * (0.3f + g_level);
      p.vy += gy * 1.2f + cosf(g_t * 1.7f + p.x * 0.05f) * 0.15f * (0.3f + g_level);
      p.vx *= 0.92f; p.vy *= 0.92f;
    }
    if (aud::onset > 0.4f) { p.vx *= 1.4f; p.vy *= 1.4f; }
    p.x += p.vx; p.y += p.vy;
    if (p.x < 4) { p.x = 4; p.vx *= -0.6f; }
    if (p.x > W - 5) { p.x = W - 5; p.vx *= -0.6f; }
    if (p.y < 16) { p.y = 16; p.vy *= -0.6f; }
    if (p.y > H - 18) { p.y = H - 18; p.vy *= -0.6f; }
    float h = soundHue + p.hue * 0.3f + i * 2.f, v = 0.4f + g_level * 0.5f + g_peak * 0.15f;
    int r = 3 + (int)(g_level * 6.f) + (i & 1);
    uint16_t col = hsv565(h, 0.8f, v);
    canvas.fillCircle((int)p.x, (int)p.y, r, col);
    if (r > 4) canvas.fillCircle((int)p.x - 1, (int)p.y - 1, r / 2, hsv565(h + 20.f, 0.5f, fminf(1.f, v + 0.2f)));
    if ((i & 1) == 0) addTrail((int)p.x, (int)p.y, col);
  }
  auto td = M5.Touch.getDetail();
  if (td.isPressed() && td.y > 16 && td.y < H - 18) {
    int tx = td.x, ty = td.y;
    drawTinyMantis(tx, ty);
    for (int i = 0; i < 8; i++) addTrail(tx + (int)(esp_random() % 11) - 5, ty + (int)(esp_random() % 11) - 5, hsv565(soundHue + 40.f, 0.9f, 0.7f));
    for (int i = 0; i < N_PART; i++) {
      float dx = g_p[i].x - tx, dy = g_p[i].y - ty, d2 = dx * dx + dy * dy + 0.01f;
      if (d2 < 900.f) { g_p[i].vx += dx / d2 * 40.f; g_p[i].vy += dy / d2 * 40.f; }
      else if (d2 < 10000.f) { g_p[i].vx -= dx * 0.012f; g_p[i].vy -= dy * 0.012f; }
    }
  }
}

// ============================================================
//  EYE — hypnotic moire, wet eyeball, real eyelids, squirting tears
// ============================================================
struct Tear { float x, y, vx, vy, life; uint8_t kind; };   // 0 squirt, 1 drip, 2 splash
static Tear s_tears[48];
static float s_pain = 0, s_flinch = 0, s_shakeEye = 0, s_dizzy = 0, s_blinkE = 0, s_blinkTE = 2.f;
static float s_irisHue = 110.f, s_gzX = 0, s_gzY = 0, s_startle = 0, s_angry = 0;
static int s_pokeX = 0, s_pokeY = 0;
static uint32_t s_pokeAt = 0;
static const int ERX = 62, ERY = 44;

static void spawnTear(float x, float y, float vx, float vy, uint8_t kind) {
  for (auto &t : s_tears) if (t.life <= 0) { t = {x, y, vx, vy, 1.f, kind}; return; }
}
static void eyePoke(int x, int y) {
  float cx = W / 2 + g_lookX * 12.f, cy = H / 2 + g_lookY * 10.f;
  float dx = (x - cx) / ERX, dy = (y - cy) / ERY;
  if (dx * dx + dy * dy > 1.25f) return;
  float ix = cx + s_gzX, iy = cy + s_gzY;
  bool bull = (x - ix) * (x - ix) + (y - iy) * (y - iy) < 16 * 16;     // right in the pupil!
  float hurt = bull ? 0.55f : 0.35f;
  s_pain = fminf(1.6f, s_pain + hurt);
  s_flinch = 1.f; s_shakeEye = 1.f; s_angry = fminf(1.f, s_angry + 0.4f);
  s_pokeX = x; s_pokeY = y; s_pokeAt = millis();
  hap(255, bull ? 140 : 90);
  aud::sfx(0.85f + s_pain * 0.35f + (bull ? 0.25f : 0.f));
  int n = 6 + (int)(s_pain * 8.f) + (bull ? 6 : 0);
  for (int i = 0; i < n; i++) {
    bool left = i & 1;
    float sx = cx + (left ? -ERX * 0.92f : ERX * 0.92f), sy = cy + 6.f;
    float sp = 90.f + (esp_random() % 120) + s_pain * 80.f;
    spawnTear(sx, sy, (left ? -1.f : 1.f) * sp * (0.6f + (esp_random() % 50) / 100.f), -120.f - (esp_random() % 140) - s_pain * 60.f, 0);
  }
}

static void moireBg() {
  // Set A: audio rings (centre breathes with bass, spacing with level, spin with mids)
  // Set B: IMU rings (centre slides with tilt, colour from tilt direction)
  static float phA = 0, phB = 0;
  phA += g_dt * (40.f + aud::bass * 260.f);
  phB += g_dt * (25.f + (fabsf(g_gx) + fabsf(g_gy)) * 0.6f);
  int fA = (int)(1500 + g_level * 900.f + aud::onset * 400.f);        // ring frequency, 1/64 steps
  int fB = (int)(1620 + (fabsf(g_lookX) + fabsf(g_lookY)) * 500.f);
  int twist = (int)(aud::mid * 4.f);
  int cax = 80 + (int)(sinf(g_t * 0.7f) * 6.f * (0.3f + aud::bass)), cay = 60 + (int)(cosf(g_t * 0.5f) * 4.f);
  int cbx = 80 - (int)(g_lookX * 55.f), cby = 60 + (int)(g_lookY * 42.f);
  int pA = (int)phA, pB = (int)phB;
  auto rows_ = [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
    uint8_t *row = fx::buf + y * fx::LW;
    for (int x = 0; x < fx::LW; x++) {
      int ra = fx::radAt(x - cax, y - cay);
      int aa = twist ? (fx::tunAt(x - cax, y - cay) >> 8) * twist : 0;
      int rb = fx::radAt(x - cbx, y - cby);
      int a = fx::sn[(uint8_t)(((ra * fA) >> 6) + aa - pA)] + 128;
      int b = fx::sn[(uint8_t)(((rb * fB) >> 6) + pB)] + 128;
      row[x] = (uint8_t)(((a >> 4) << 4) | (b >> 4));
    }
  }
  };
  fx::parallel(rows_);
  float hA = g_hue + g_level * 140.f + aud::centroid * 120.f;
  float hB = atan2f(g_lookY, g_lookX + 0.0001f) * 57.3f + 180.f + g_hue * 0.3f;
  uint16_t ca = hsv565(hA, 0.9f, 1.f), cb = hsv565(hB, 0.85f, 1.f);
  float ar = ((ca >> 11) << 3), ag = (((ca >> 5) & 63) << 2), ab = ((ca & 31) << 3);
  float br = ((cb >> 11) << 3), bg = (((cb >> 5) & 63) << 2), bb = ((cb & 31) << 3);
  float gA = 0.34f + g_level * 0.35f, gB = 0.3f + fminf(0.3f, fabsf(g_lookX) + fabsf(g_lookY)) * 0.5f;
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++) {
      float wa = i / 15.f, wb = j / 15.f;
      wa = wa * wa * gA; wb = wb * wb * gB;
      fx::palSet(i * 16 + j, (uint8_t)clampf(ar * wa + br * wb, 0, 255), (uint8_t)clampf(ag * wa + bg * wb, 0, 255),
                 (uint8_t)clampf(ab * wa + bb * wb, 0, 255));
    }
  fx::present(canvas);
}

static void modeEye() {
  float dt = g_dt;
  moireBg();

  if (g_shakeKick) { s_irisHue = (float)(esp_random() % 360); s_dizzy = 1.2f; }
  s_dizzy = fmaxf(0.f, s_dizzy - dt);
  s_pain = fmaxf(0.f, s_pain - dt * 0.22f);
  s_flinch = fmaxf(0.f, s_flinch - dt * 2.2f);
  s_shakeEye = fmaxf(0.f, s_shakeEye - dt * 3.f);
  s_angry = fmaxf(0.f, s_angry - dt * 0.12f);
  if (aud::onset > 0.6f && s_pain < 0.3f) s_startle = 1.f;
  s_startle = fmaxf(0.f, s_startle - dt * 3.f);

  float jit = s_shakeEye * (3.f + s_pain * 4.f);
  float cx = W / 2 + (g_eyeTrack ? g_lookX * 12.f : 0) + ((int)(esp_random() % 21) - 10) * jit * 0.1f;
  float cy = H / 2 + (g_eyeTrack ? g_lookY * 10.f : 0) + ((int)(esp_random() % 21) - 10) * jit * 0.1f;

  // gaze target: tilt, or glare at the finger that hurt it, or dizzy spin
  float tx = g_eyeTrack ? clampf(g_lookX * 30.f, -26.f, 26.f) : 0, ty = g_eyeTrack ? clampf(g_lookY * 22.f, -16.f, 16.f) : 0;
  if (millis() - s_pokeAt < 1500) { tx = clampf((s_pokeX - cx) * 0.5f, -26.f, 26.f); ty = clampf((s_pokeY - cy) * 0.5f, -16.f, 16.f); }
  if (s_dizzy > 0) { float r = 24.f * fminf(1.f, s_dizzy); tx = cosf(g_t * 30.f) * r; ty = sinf(g_t * 30.f) * r * 0.65f; }
  float gk = clampf(dt * (s_dizzy > 0 ? 25.f : 10.f), 0, 1);
  s_gzX += (tx - s_gzX) * gk; s_gzY += (ty - s_gzY) * gk;

  // lids: 0 = open. blink, flinch squeeze, pain squint, startle wide
  s_blinkTE -= dt;
  if (s_blinkTE < 0) { s_blinkE = 1.f; s_blinkTE = 2.5f + (esp_random() % 3000) / 1000.f; }
  s_blinkE = fmaxf(0.f, s_blinkE - dt * 6.5f);
  float blink = s_blinkE > 0.5f ? (1.f - s_blinkE) * 2.f : s_blinkE * 2.f;
  float squeeze = fmaxf(blink, s_flinch > 0.55f ? 1.f : s_flinch * 1.3f);
  float cU = clampf(0.08f + s_pain * 0.22f + squeeze * 0.5f - s_startle * 0.14f + sinf(g_t * 40.f) * 0.02f * s_pain, -0.1f, 0.52f);
  float cL = clampf(0.04f + s_pain * 0.16f + squeeze * 0.5f - s_startle * 0.06f, -0.05f, 0.5f);

  float skH = 285.f + s_pain * 45.f + sinf(g_t * 0.3f) * 12.f;
  uint16_t skin = hsv565(skH, 0.42f + s_pain * 0.25f, 0.62f);
  uint16_t skinS = hsv565(skH, 0.55f + s_pain * 0.2f, 0.36f);
  uint16_t skinD = hsv565(skH, 0.6f, 0.16f);
  uint16_t crease = hsv565(skH, 0.65f, 0.28f);
  uint16_t scl = rgb565(236, (uint8_t)(234 - s_pain * 70.f), (uint8_t)(228 - s_pain * 90.f));

  // socket + sclera
  canvas.fillEllipse((int)cx, (int)cy, ERX + 11, ERY + 11, skinD);
  canvas.fillEllipse((int)cx, (int)cy, ERX + 9, ERY + 9, skin);
  canvas.fillEllipse((int)cx, (int)cy, ERX, ERY, scl);
  // bloodshot veins
  int nv = (int)(s_pain * 10.f);
  for (int v = 0; v < nv; v++) {
    float a = v * 2.39996f, r = 1.f;
    float x0 = cx + cosf(a) * ERX * r, y0 = cy + sinf(a) * ERY * r;
    for (int s = 0; s < 4; s++) {
      float nr = r - 0.14f;
      float x1 = cx + cosf(a + sinf(v * 7.f + s * 3.f) * 0.18f) * ERX * nr, y1 = cy + sinf(a + cosf(v * 5.f + s) * 0.18f) * ERY * nr;
      canvas.drawLine((int)x0, (int)y0, (int)x1, (int)y1, rgb565(200, 30, 40));
      x0 = x1; y0 = y1; r = nr;
    }
  }
  // iris with striations, pupil dilates with sound, pinpoints with pain
  float ix = cx + s_gzX, iy = cy + s_gzY;
  int ir = (int)(25 + g_peak * 4.f - s_pain * 3.f);
  uint16_t irisC = hsv565(s_irisHue + g_level * 40.f, 0.85f, 0.65f + g_level * 0.25f);
  canvas.fillCircle((int)ix, (int)iy, ir + 1, hsv565(s_irisHue, 0.9f, 0.22f));
  canvas.fillCircle((int)ix, (int)iy, ir - 1, irisC);
  for (int k = 0; k < 20; k++) {
    float a = k * 0.314f + g_t * 0.2f;
    int r0 = 8, r1 = ir - 2;
    canvas.drawLine((int)(ix + cosf(a) * r0), (int)(iy + sinf(a) * r0), (int)(ix + cosf(a) * r1), (int)(iy + sinf(a) * r1),
                    hsv565(s_irisHue + (k & 1 ? 25.f : -20.f), 0.8f, (k & 1) ? 0.95f : 0.4f));
  }
  canvas.drawCircle((int)ix, (int)iy, ir / 2 + 2, hsv565(s_irisHue + 60.f, 0.5f, 0.9f));
  int pr = (int)(9 + g_level * 7.f + aud::bass * 3.f - s_pain * 5.f - s_startle * 3.f);
  if (pr < 2) pr = 2;
  canvas.fillCircle((int)(ix + s_gzX * 0.15f), (int)(iy + s_gzY * 0.15f), pr, rgb565(6, 4, 10));
  canvas.fillCircle((int)ix - 8, (int)iy - 9, 4, rgb565(255, 255, 255));
  canvas.fillCircle((int)ix + 7, (int)iy + 6, 2, rgb565(230, 240, 255));

  // eyelids: arcs that follow the eyeball's curvature; angry slant after pokes
  int lashC = rgb565(15, 8, 20);
  for (int x = -ERX - 9; x <= ERX + 9; x++) {
    float u = (float)x / ERX;
    float e = u * u < 1.f ? sqrtf(1.f - u * u) : 0.f;
    float us = (float)x / (ERX + 9);
    float soc = us * us < 1.f ? (ERY + 9) * sqrtf(1.f - us * us) : 0.f;
    float slant = s_angry * (x < 0 ? -x : x) * -0.12f;           // inner corners drop
    float yu = cy - ERY * (1.f - 2.f * cU) * e + slant * (1.f - e) + s_angry * 6.f * e;
    float yl = cy + ERY * (1.f - 2.f * cL) * e;
    if (yu > yl) { float m = (yu + yl) * 0.5f; yu = yl = m; }
    int X = (int)cx + x;
    int top = (int)(cy - soc), bot = (int)(cy + soc);
    if (yu > top) {
      int sh = (int)yu - top < 7 ? (int)yu - top : 7;           // lid rolls into shadow at the edge
      canvas.drawFastVLine(X, top, (int)yu - top - sh, skin);
      canvas.drawFastVLine(X, (int)yu - sh, sh, skinS);
      int cr = (int)(yu - 7.f - e * 5.f * (1.f - cU));
      if (e > 0 && cr > top + 1 && cU < 0.4f) canvas.drawPixel(X, cr, crease);
    }
    if (bot > yl) {
      canvas.drawFastVLine(X, (int)yl, bot - (int)yl + 1, skin);
      canvas.drawFastVLine(X, (int)yl, 3, skinS);
    }
    if (e > 0) {
      canvas.drawFastVLine(X, (int)yu - 1, 3, lashC);
      canvas.drawPixel(X, (int)yl, rgb565(200, 90, 110));
      if ((x & 7) == 0 && cU < 0.45f) {
        float lx = u * 5.f;
        canvas.drawLine(X, (int)yu - 1, X + (int)lx, (int)yu - 6 - (int)(e * 3.f), lashC);
      }
    }
  }

  // tears: squirt from the corners, arc, splat, dribble
  if (s_pain > 0.4f && (esp_random() % 100) < (int)(s_pain * 30.f)) {
    float u = ((int)(esp_random() % 160) - 80) / 100.f;
    spawnTear(cx + u * ERX, cy + ERY * 0.75f * sqrtf(fmaxf(0.f, 1.f - u * u)), 0, 10.f, 1);
  }
  for (auto &t : s_tears) {
    if (t.life <= 0) continue;
    float gdrop = t.kind == 1 ? 140.f : 520.f;
    t.vy += gdrop * dt; t.x += t.vx * dt; t.y += t.vy * dt;
    t.life -= dt * (t.kind == 2 ? 2.5f : 0.35f);
    if (t.y > H - 16 && t.kind != 2) {
      for (int k = 0; k < 3; k++) spawnTear(t.x, H - 17.f, ((int)(esp_random() % 120) - 60), -60.f - (esp_random() % 80), 2);
      t.life = 0; continue;
    }
    uint16_t tc = rgb565(120, 200, 255);
    int r = t.kind == 2 ? 1 : (t.kind == 0 ? 4 : 3);
    float tl = t.kind == 0 ? 0.045f : 0.08f;
    for (int k = -1; k <= 1; k++)
      canvas.drawLine((int)t.x + k, (int)t.y, (int)(t.x - t.vx * tl), (int)(t.y - t.vy * tl), rgb565(90, 160, 230));
    canvas.fillCircle((int)t.x, (int)t.y, r, tc);
    canvas.drawPixel((int)t.x - 1, (int)t.y - 1, rgb565(255, 255, 255));
  }
  if (millis() - s_pokeAt < 260) {
    for (int k = 0; k < 8; k++) {
      float a = k * 0.785f; int r0 = 8, r1 = 16 + (int)(s_pain * 8);
      canvas.drawLine(s_pokeX + (int)(cosf(a) * r0), s_pokeY + (int)(sinf(a) * r0), s_pokeX + (int)(cosf(a) * r1),
                      s_pokeY + (int)(sinf(a) * r1), rgb565(255, 230, 80));
    }
  }
}

// ============================================================
//  TUNNEL — dive / recede (audio-sliced, bendable), fractal, portal
// ============================================================
struct Slice { uint8_t b[16]; int8_t ox, oy; uint8_t en; };
static Slice s_sl[256];
static float s_tz = 0, s_camX = 0, s_camY = 0, s_bendX = 0, s_bendY = 0, s_scrape = 0;
static int s_zi = 0;
static int16_t s_offX[256], s_offY[256];
static uint8_t s_fog[256], s_twist[256];

static inline float pathX(float z) { return sinf(z * 0.021f) * 0.95f + sinf(z * 0.047f + 1.3f) * 0.5f; }
static inline float pathY(float z) { return cosf(z * 0.017f) * 0.55f + sinf(z * 0.039f) * 0.4f; }
static void fillSlice(Slice &s, float ox, float oy) {
  for (int i = 0; i < 16; i++) {
    float v = (aud::bands[i * 2] + aud::bands[i * 2 + 1]) * 0.5f;
    s.b[i] = (uint8_t)clampf(40.f + v * 200.f, 0, 255);
  }
  s.en = (uint8_t)clampf(aud::onset * 255.f + g_level * 60.f, 0, 255);   // beats become rings you fly through
  s.ox = (int8_t)clampf(ox * 50.f, -127, 127);
  s.oy = (int8_t)clampf(oy * 50.f, -127, 127);
}

static void tunnelRender(bool dive) {
  float dt = g_dt;
  float speed = 16.f + g_level * 45.f + aud::bass * 30.f;
  auto td = M5.Touch.getDetail();
  static int ptx = -1, pty = -1;
  float dragX = 0, dragY = 0;
  if (td.isPressed() && td.y > 14 && td.y < H - 14) {
    if (ptx >= 0) { dragX = (td.x - ptx) * 0.02f; dragY = (td.y - pty) * 0.02f; }
    ptx = td.x; pty = td.y;
  } else ptx = -1;

  if (dive) {
    s_tz += speed * dt;
    int zi = (int)s_tz;
    while (s_zi < zi) {
      s_zi++;
      float z = (float)(s_zi + 255);
      fillSlice(s_sl[(s_zi + 255) & 255], pathX(z) + aud::bass * 0.25f * sinf(z * 0.3f), pathY(z));
    }
    // steer: tilt (and drag) moves the camera inside the tube
    s_camX += (g_lookX * 1.5f + dragX * 8.f) * dt * (1.f + g_level);
    s_camY += (g_lookY * 1.2f + dragY * 8.f) * dt * (1.f + g_level);
    float nx = s_sl[(s_zi + 6) & 255].ox / 50.f, ny = s_sl[(s_zi + 6) & 255].oy / 50.f;
    float ex = s_camX - nx, ey = s_camY - ny, er = sqrtf(ex * ex + ey * ey);
    if (er > 0.62f) {
      s_camX = nx + ex / er * 0.6f; s_camY = ny + ey / er * 0.6f;
      if (s_scrape < 0.3f) hap(170, 45);
      s_scrape = 1.f;
    }
  } else {
    s_tz -= speed * dt;
    int zi = (int)floorf(s_tz);
    s_bendX += (g_lookX * 1.4f + dragX * 10.f + sinf(g_t * 3.f) * aud::bass * 0.8f) * dt;
    s_bendY += (g_lookY * 1.1f + dragY * 10.f + cosf(g_t * 2.3f) * aud::mid * 0.6f) * dt;
    s_bendX = clampf(s_bendX, -2.4f, 2.4f); s_bendY = clampf(s_bendY, -2.4f, 2.4f);
    while (s_zi > zi) { s_zi--; fillSlice(s_sl[(s_zi + 2) & 255], s_bendX, s_bendY); }
    s_camX += (s_bendX - s_camX) * clampf(dt * 8.f, 0, 1);
    s_camY += (s_bendY - s_camY) * clampf(dt * 8.f, 0, 1);
  }
  s_scrape = fmaxf(0.f, s_scrape - dt * 3.f);

  // per-depth tables
  float spin = g_t * (dive ? 20.f : -14.f) + g_gz * 0.05f;
  for (int d = 0; d < 256; d++) {
    const Slice &s = s_sl[(s_zi + d) & 255];
    float R = 900.f / (d + 0.6f);
    s_offX[d] = (int16_t)clampf((s.ox / 50.f - s_camX) * R, -150.f, 150.f);
    s_offY[d] = (int16_t)clampf((s.oy / 50.f - s_camY) * R, -110.f, 110.f);
    float fog = d < 5 ? d / 5.f : 1.f - (d - 5) / 250.f;
    s_fog[d] = (uint8_t)(clampf(fog, 0, 1) * 255.f);
    s_twist[d] = (uint8_t)(spin + d * (0.3f + aud::mid * 1.5f));
  }
  int cx = 80, cy = 60;
  uint8_t ph = (uint8_t)(g_t * 60.f);
  uint8_t ringPh = (uint8_t)(s_tz * 4.f);
  auto rows_ = [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
    uint8_t *row = fx::buf + y * fx::LW;
    for (int x = 0; x < fx::LW; x++) {
      int d1 = fx::tunAt(x - cx, y - cy) & 255;
      uint16_t t = fx::tunAt(x - cx - s_offX[d1], y - cy - s_offY[d1]);
      int d = t & 255;
      int a = (t >> 8) + s_twist[d];
      const Slice &s = s_sl[(s_zi + d) & 255];
      int sec = (a >> 3) & 31;
      int bv = s.b[sec < 16 ? sec : 31 - sec];
      int wz = s_zi + d;
      int stripe = fx::sn[(uint8_t)(a * 4 + d * 3 + ph)];
      int xr = ((a << 1) ^ (wz << 2)) & 127;                     // old-school XOR texture, flowing in world space
      int rim = ((wz & 7) == 0) ? 40 : 0;
      int v = (xr >> 1) + ((bv * (128 + stripe)) >> 8) + (s.en >> 1);
      v += rim + (fx::sn[(uint8_t)(d * 8 - ringPh)] > 100 ? 24 : 0);
      v = (v * s_fog[d]) >> 8;
      row[x] = (uint8_t)(v > 255 ? 255 : v);
    }
  }
  };
  fx::parallel(rows_);
  float h = g_hue / 360.f;
  if (dive) fx::palCosine(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.f, 1.f, 1.f, 0.0f + h, 0.33f + h + aud::centroid * 0.3f, 0.67f + h, g_t * 0.05f);
  else fx::palCosine(0.5f, 0.4f, 0.6f, 0.5f, 0.45f, 0.4f, 1.f, 0.8f, 0.6f, 0.8f + h, 0.2f + h, 0.5f + h, -g_t * 0.04f);
  fx::palFlash(s_scrape * 0.7f + aud::onset * 0.25f);
  fx::present(canvas);

  if (dive) {    // reticle shows where the tube heads next
    int rx = 160 + (int)(s_offX[40] * 2), ry = 120 + (int)(s_offY[40] * 2);
    canvas.drawCircle(160, 120, 9, rgb565(255, 255, 200));
    canvas.drawLine(rx - 4, ry, rx + 4, ry, hsv565(g_hue + 180.f, 0.6f, 1.f));
    canvas.drawLine(rx, ry - 4, rx, ry + 4, hsv565(g_hue + 180.f, 0.6f, 1.f));
  }
}

// ---- fractal: morphing Julia, adaptive quality, orbit-trap colouring (no flat fields) ----
static float s_fx = 0, s_fy = 0, s_fs = 3.2f / 160.f, s_frot = 0, s_fzoomDir = -1.f;
static int s_fIt = 22, s_preset = 0;
static float s_cTr = 1.f;
static float s_cr0 = -0.8f, s_ci0 = 0.156f, s_crP = -0.8f, s_ciP = 0.156f;
static const float PRESETS[][2] = {{-0.8f, 0.156f}, {0.285f, 0.01f}, {-0.4f, 0.6f}, {-0.70176f, -0.3842f},
                                   {0.355f, 0.355f}, {-0.835f, -0.2321f}, {-0.1f, 0.651f}, {0.37f, -0.1f}};
static void fractalRender() {
  float dt = g_dt;
  uint32_t t0 = micros();
  auto td = M5.Touch.getDetail();
  static int ptx = -1, pty = -1, dragAcc = 0;
  static uint32_t downAt = 0;
  if (td.isPressed() && td.y > 14 && td.y < H - 14) {
    if (ptx < 0) { downAt = millis(); dragAcc = 0; }
    else {
      int dx = td.x - ptx, dy = td.y - pty; dragAcc += abs(dx) + abs(dy);
      float c = cosf(s_frot), s = sinf(s_frot);
      s_fx -= (c * dx - s * dy) * s_fs * 0.5f; s_fy -= (s * dx + c * dy) * s_fs * 0.5f;
    }
    ptx = td.x; pty = td.y;
  } else {
    if (ptx >= 0 && dragAcc < 10 && millis() - downAt < 350) {       // tap = new world
      s_preset = (s_preset + 1) % 8; s_crP = s_cr0; s_ciP = s_ci0; s_cTr = 0; hap(120, 40);
    }
    ptx = -1;
  }
  s_cTr = fminf(1.f, s_cTr + dt * 0.8f);
  float tr = s_cTr * s_cTr * (3.f - 2.f * s_cTr);
  s_cr0 = s_crP + (PRESETS[s_preset][0] - s_crP) * tr;
  s_ci0 = s_ciP + (PRESETS[s_preset][1] - s_ciP) * tr;
  float cr = s_cr0 + 0.035f * cosf(g_t * 0.31f) + aud::bass * 0.03f * sinf(g_t * 2.f);
  float ci = s_ci0 + 0.035f * sinf(g_t * 0.23f) + aud::treble * 0.02f;

  // steer with tilt, rotate with twist, breathe zoom with bass
  float c = cosf(s_frot), s = sinf(s_frot);
  s_fx += (c * g_lookX - s * g_lookY) * s_fs * 70.f * dt;
  s_fy += (s * g_lookX + c * g_lookY) * s_fs * 70.f * dt;
  s_frot += (g_gz * 0.004f + 0.05f) * dt;
  s_fs *= expf(s_fzoomDir * dt * (0.22f + g_level * 0.5f));
  if (s_fs < 0.00002f) s_fzoomDir = 1.f;
  if (s_fs > 3.6f / 160.f) { s_fzoomDir = -1.f; s_fx *= 0.98f; s_fy *= 0.98f; }
  s_fx = clampf(s_fx, -1.8f, 1.8f); s_fy = clampf(s_fy, -1.4f, 1.4f);
  float sc = s_fs * (1.f - aud::bass * 0.12f);

  float ux = c * sc, uy = s * sc, vx = -s * sc, vy = c * sc;
  float ox = s_fx - ux * 80.f - vx * 60.f, oy = s_fy - uy * 80.f - vy * 60.f;
  const int maxIt = s_fIt;
  int ph = (int)(g_t * 40.f);
  float detXa[2] = {0, 0}, detYa[2] = {0, 0}; int detNa[2] = {0, 0}, inNa[2] = {0, 0}, loNa[2] = {0, 0};
  auto rows_ = [&](int y0, int y1) {
    const int hh = y0 != 0;
    for (int y = y0; y < y1; y++) {
    uint8_t *row = fx::buf + y * fx::LW;
    float zr0 = ox + vx * y, zi0 = oy + vy * y;
    for (int x = 0; x < fx::LW; x++) {
      float zr = zr0 + ux * x, zi = zi0 + uy * x;
      float trap = 10.f, trap2 = 100.f;            // cross trap (outside), point trap (inside)
      int k = 0;
      for (; k < maxIt; k++) {
        float r2 = zr * zr, i2 = zi * zi;
        if (r2 + i2 > 16.f) break;
        zi = 2.f * zr * zi + ci; zr = r2 - i2 + cr;
        float ax = fabsf(zr), ay = fabsf(zi);
        float tx = ax < ay ? ax : ay; if (tx < trap) trap = tx;
        float tp = (zr - 0.15f) * (zr - 0.15f) + zi * zi; if (tp < trap2) trap2 = tp;
      }
      int v;
      if (k < maxIt) {
        float m = zr * zr + zi * zi;
        int fine = (int)(clampf(1.f - (m - 16.f) / (m + 16.f), 0.f, 1.f) * 12.f);
        v = 24 + ((k * 9 + fine + (int)(sqrtf(trap) * 110.f) + ph) & 127);
        if (k > maxIt / 3) { detXa[hh] += x; detYa[hh] += y; detNa[hh]++; } else if (k < 3) loNa[hh]++;
      } else {
        inNa[hh]++;
        v = 152 + (((int)(sqrtf(trap2) * 240.f) + (int)(trap * 40.f) - ph * 2) & 103);
      }
      row[x] = (uint8_t)v;
    }
  }
  };
  fx::parallel(rows_);
  float detX = detXa[0] + detXa[1], detY = detYa[0] + detYa[1];
  int detN = detNa[0] + detNa[1], inN = inNa[0] + inNa[1], loN = loNa[0] + loNa[1];
  // demo trick: the camera is attracted to where the detail is, so exploring never sinks into a void
  if (detN > 30) {
    float mx = detX / detN - 80.f, my = detY / detN - 60.f;
    s_fx += (ux * mx + vx * my) * dt * 0.6f; s_fy += (uy * mx + vy * my) * dt * 0.6f;
  }
  if (inN > 17000 || loN > 17000 || detN < 60) s_fzoomDir = 1.f;
  else if (detN > 1500 && s_fzoomDir > 0 && s_fs < 2.5f / 160.f) s_fzoomDir = -1.f;
  float h = g_hue / 360.f + aud::centroid * 0.4f;
  fx::palCosine(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 2.f, 1.f, 1.f + g_level, 0.5f + h, 0.2f + h, 0.25f + h, g_t * 0.03f);
  fx::palFlash(aud::onset * 0.3f);
  fx::present(canvas);
  uint32_t took = micros() - t0;                       // adaptive quality, demo-style
  if (took > 21000 && s_fIt > 12) s_fIt--;
  else if (took < 14000 && s_fIt < 40) s_fIt++;
}

// ---- portal: warp-speed through dimensions ----
struct Star { float x, y, z, pz; };
static Star s_st[200];
static float s_wx = 0, s_wy = 0, s_wz = 0, s_pcx = 0, s_pcy = 0, s_transit = 0, s_emerge = 0;
static int s_combo = 0, s_dim = 0;
static float s_dimP[12];
static uint32_t s_wSpawn = 0;
static bool s_wAlive = false;
static void newDimension() {
  for (int i = 0; i < 12; i++) s_dimP[i] = (esp_random() % 1000) / 1000.f;
  s_dim++;
}
static void portalRender() {
  float dt = g_dt;
  static bool init = false;
  if (!init) {
    init = true; newDimension();
    for (auto &s : s_st) { s.x = ((int)(esp_random() % 2000) - 1000) / 1000.f; s.y = ((int)(esp_random() % 2000) - 1000) / 1000.f; s.z = s.pz = (esp_random() % 1000) / 1000.f + 0.05f; }
  }
  float speed = 0.45f + aud::bass * 1.3f + g_level * 0.6f + aud::onset * 0.8f;
  auto td = M5.Touch.getDetail();
  float steerX = g_lookX, steerY = g_lookY;
  if (td.isPressed() && td.y > 14 && td.y < H - 14) { steerX = (td.x - 160) / 90.f; steerY = (td.y - 120) / 70.f; }
  s_pcx += steerX * dt * 0.9f; s_pcy += steerY * dt * 0.9f;

  fx::swap();
  if (s_transit > 0) {
    // inside the wormhole: swirling LUT tunnel accelerating to a white-out
    s_transit -= dt;
    float u = 1.f - s_transit / 1.5f;
    uint8_t spin = (uint8_t)(g_t * 300.f), fly = (uint8_t)(g_t * (400.f + u * 900.f));
    auto rows_ = [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
      uint8_t *row = fx::buf + y * fx::LW;
      for (int x = 0; x < fx::LW; x++) {
        uint16_t t = fx::tunAt(x - 80 + (int)(sinf(g_t * 5.f) * 6.f), y - 60);
        int d = t & 255, a = t >> 8;
        int v = fx::sn[(uint8_t)(a * 3 + d * 2 + spin)] + fx::sn[(uint8_t)(d * 6 - fly)] + 128 + (int)(aud::bands[(a >> 3) & 31] * 60.f);
        v = (v * (d < 200 ? 255 : (255 - (d - 200) * 4))) >> 8;
        row[x] = (uint8_t)clampf((float)v, 1, 255);
      }
    }
  };
  fx::parallel(rows_);
    fx::palCosine(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.f, 1.f, 1.f, s_dimP[0], s_dimP[1], s_dimP[2], g_t * 0.2f);
    fx::palFlash(u > 0.7f ? (u - 0.7f) * 3.3f : 0.f);
    hap((uint8_t)(60 + u * 150.f), 40);
    if (s_transit <= 0) {
      newDimension(); s_combo++; s_emerge = 1.f; s_wAlive = false; s_wSpawn = millis() + 700;
      hap(220, 120); fx::clear(0);
    }
  } else {
    // feedback: dimension-specific swirl + zoom = streaks and nebula smear
    float swirl = (s_dimP[3] - 0.5f) * 0.05f + g_gz * 0.0004f;
    float zoom = 1.035f + aud::bass * 0.05f;
    float c = cosf(swirl), s = sinf(swirl);
    for (int j = 0; j < fx::GH; j++)
      for (int i = 0; i < fx::GW; i++) {
        float dx = i * 8.f - 80.f, dy = j * 8.f - 60.f;
        fx::gx[j][i] = 80.f + (c * dx + s * dy) / zoom; fx::gy[j][i] = 60.f + (-s * dx + c * dy) / zoom;
      }
    fx::warp((uint8_t)(12 + s_dimP[4] * 10.f), false);
    // this dimension's nebula: a slow plasma under the streaks (max-blend, dim)
    {
      uint8_t t1 = (uint8_t)(g_t * (8.f + s_dimP[5] * 20.f)), t2 = (uint8_t)(g_t * 13.f), t3 = (uint8_t)(-g_t * 11.f);
      int f1 = 1 + (int)(s_dimP[6] * 3.f), f2 = 1 + (int)(s_dimP[7] * 3.f);
      int ox = (int)(s_pcx * 40.f), oy = (int)(s_pcy * 40.f);
      int lift = 34 + (int)(aud::mid * 40.f);
      auto rows_ = [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
        uint8_t *row = fx::buf + y * fx::LW;
        int ry = fx::sn[(uint8_t)((y + oy) * f2 + t2)];
        for (int x = 0; x < fx::LW; x++) {
          int n = fx::sn[(uint8_t)((x + ox) * f1 + t1)] + ry + fx::sn[(uint8_t)((x + y + ox) * 2 + t3)];
          n = n > 0 ? (n * lift) >> 8 : 0;
          if (n > row[x]) row[x] = (uint8_t)n;
        }
      }
  };
  fx::parallel(rows_);
    }
    // nebula motes
    for (int k = 0; k < 3; k++) {
      float a = g_t * (0.2f + k * 0.13f) + s_dimP[5 + k] * 6.28f;
      fx::disc(80 + (int)(cosf(a) * (30 + k * 12)), 60 + (int)(sinf(a * 1.3f) * (20 + k * 8)), 2 + (int)(aud::mid * 4.f), (uint8_t)(40 + k * 12));
    }
    // stars
    float fov = 95.f + aud::bass * 30.f;
    for (auto &st : s_st) {
      st.pz = st.z;
      st.z -= speed * dt * (0.6f + s_dimP[6]);
      float sx0 = 80.f + (st.x - s_pcx * 0.3f) / st.pz * fov * 0.5f, sy0 = 60.f + (st.y - s_pcy * 0.3f) / st.pz * fov * 0.5f;
      if (st.z < 0.03f) { st.x = ((int)(esp_random() % 2000) - 1000) / 1000.f; st.y = ((int)(esp_random() % 2000) - 1000) / 1000.f; st.z = st.pz = 1.f; continue; }
      float sx = 80.f + (st.x - s_pcx * 0.3f) / st.z * fov * 0.5f, sy = 60.f + (st.y - s_pcy * 0.3f) / st.z * fov * 0.5f;
      uint8_t b = (uint8_t)clampf(90.f + 200.f * (1.f - st.z) + aud::treble * 60.f, 60, 255);
      fx::line((int)sx0, (int)sy0, (int)sx, (int)sy, b);
      if (st.z < 0.3f) fx::line((int)sx0 + 1, (int)sy0, (int)sx + 1, (int)sy, b);
    }
    // wormhole
    if (!s_wAlive && millis() > s_wSpawn) {
      s_wAlive = true; s_wz = 1.6f;
      s_wx = s_pcx + ((int)(esp_random() % 140) - 70) / 100.f; s_wy = s_pcy + ((int)(esp_random() % 100) - 50) / 100.f;
    }
    if (s_wAlive) {
      s_wz -= dt * (0.35f + speed * 0.25f);
      float ox = s_wx - s_pcx, oy = s_wy - s_pcy;
      float px = 80.f + ox / s_wz * 30.f, py = 60.f + oy / s_wz * 30.f;
      float r = 11.f / s_wz * (1.f + aud::bass * 0.3f);
      if (s_wz < 1.f) { s_pcx += ox * dt * 1.2f; s_pcy += oy * dt * 1.2f; }   // gentle aim assist
      for (int k = 0; k < 28; k++) {
        float a = k * 0.2244f + g_t * 4.f, rr = r * (1.f + 0.25f * sinf(k * 3.f + g_t * 9.f));
        float a2 = a + 0.9f;
        fx::line((int)(px + cosf(a) * rr), (int)(py + sinf(a) * rr), (int)(px + cosf(a2) * rr * 0.55f), (int)(py + sinf(a2) * rr * 0.55f), 255);
      }
      fx::disc((int)px, (int)py, (int)(r * 0.45f), 0);
      fx::ring((int)px, (int)py, (int)r + 2, 200);
      if (s_wz < 0.22f) {
        if (ox * ox + oy * oy < 0.16f) { s_transit = 1.5f; hap(200, 80); }
        else { s_combo = 0; hap(60, 60); }
        s_wAlive = false; s_wSpawn = millis() + 900;
      }
    }
    // palette: deep-space tint -> dimension hues -> white-hot star cores
    {
      float hA = s_dimP[0] * 360.f + g_t * 6.f + aud::centroid * 60.f, hB = hA + 60.f + s_dimP[1] * 180.f;
      for (int i = 0; i < 256; i++) {
        float u = i / 255.f;
        uint16_t c = u < 0.25f ? hsv565(hA, 0.8f, 0.06f + u * 1.4f)
                     : (u < 0.7f ? hsv565(hA + (hB - hA) * (u - 0.25f) / 0.45f, 0.85f - (u - 0.25f) * 0.6f, 0.4f + u * 0.6f)
                                 : hsv565(hB, (1.f - u) * 1.6f, 1.f));
        fx::palSet(i, (uint8_t)(((c >> 11) & 31) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3));
      }
    }
    fx::palFlash(s_emerge + aud::onset * 0.2f);
    s_emerge = fmaxf(0.f, s_emerge - dt * 1.5f);
  }
  fx::present(canvas);
  if (s_transit <= 0) {
    canvas.drawCircle(160, 120, 6, rgb565(255, 255, 220));
    if (s_wAlive) {   // arrow toward the wormhole
      float ox = s_wx - s_pcx, oy = s_wy - s_pcy, d = sqrtf(ox * ox + oy * oy);
      if (d > 0.2f) canvas.fillCircle(160 + (int)(ox / d * 18.f), 120 + (int)(oy / d * 18.f), 2, rgb565(255, 120, 255));
    }
  }
  if (s_combo > 0) {
    canvas.setTextSize(2); canvas.setTextColor(hsv565(g_hue + 180.f, 0.6f, 1.f));
    canvas.setCursor(W / 2 - 10, 20); canvas.printf("x%d", s_combo);
    canvas.setTextSize(1);
  }
}

static void modeTunnel() {
  switch (g_tunnelMode) {
    case TM_DIVE: tunnelRender(true); break;
    case TM_RECEDE: tunnelRender(false); break;
    case TM_FRACTAL: fractalRender(); break;
    default: portalRender(); break;
  }
}

// ============================================================
//  PULSE — audio feedback visualizers, increasingly cross-mapped
// ============================================================
struct Mote { float x, y, life; };
static Mote s_mote[64];
static float s_shock = -1;
static float s_palSeed[9] = {0.f, 0.33f, 0.67f, 1.f, 1.f, 1.f, 0.5f, 0.5f, 0.5f};

static void gridWarp(float cx, float cy, float zoom, float rot, float wobA, float wobF, float wobT) {
  float c = cosf(rot), s = sinf(rot);
  for (int j = 0; j < fx::GH; j++)
    for (int i = 0; i < fx::GW; i++) {
      float dx = i * 8.f - cx, dy = j * 8.f - cy;
      float sx = cx + (c * dx + s * dy) / zoom, sy = cy + (-s * dx + c * dy) / zoom;
      if (wobA != 0.f) { sx += sinf(j * wobF + wobT) * wobA; sy += cosf(i * wobF * 1.3f - wobT) * wobA; }
      fx::gx[j][i] = sx; fx::gy[j][i] = sy;
    }
}

static void modePulse() {
  float dt = g_dt;
  const int16_t *sc = aud::scope;
  int cx = 80 - (int)(g_lookX * 20.f), cy = 60 + (int)(g_lookY * 15.f);
  if (g_shakeKick) for (int i = 0; i < 9; i++) s_palSeed[i] = (esp_random() % 1000) / 1000.f * (i < 3 ? 1.f : 1.5f) + (i >= 3 && i < 6 ? 0.5f : 0.f);
  if (aud::onset > 0.45f) { s_shock = 4.f; if (!aud::speakerLive()) hap(40, 12); }
  if (s_shock >= 0) s_shock += dt * 140.f;
  if (s_shock > 110) s_shock = -1;
  fx::swap();
  float t = g_t;
  switch (g_pulsePat) {
    case 0: {  // BLOOM: scope ring blossoms outward
      gridWarp(cx, cy, 1.02f + aud::bass * 0.035f, 0.012f + aud::mid * 0.04f, 0, 0, 0);
      fx::warp(5, true);
      int px = 0, py = 0;
      for (int i = 0; i <= 128; i++) {
        int k = i & 127;
        float a = k / 128.f * 6.2831853f + t * 0.4f;
        float v = sc[(k < 64 ? k : 127 - k) * 2] / 9000.f;
        float r = 18.f + g_level * 18.f + v * 26.f;
        int x = cx + (int)(cosf(a) * r), y = cy + (int)(sinf(a) * r * 0.9f);
        if (i) fx::line(px, py, x, y, 255);
        px = x; py = y;
      }
      fx::palCosine(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.f, 1.f, 1.f, 0.f + aud::centroid, 0.33f, 0.67f, t * 0.05f);
      break;
    }
    case 1: {  // KALEIDO: mirrored waveform wedges, implosion swirl steered by tilt
      gridWarp(cx, cy, 0.975f + aud::bass * 0.08f, (g_lookX >= 0 ? 1.f : -1.f) * (0.03f + aud::mid * 0.06f), 0, 0, 0);
      fx::warp(4, true);
      for (int w = 0; w < 8; w++) {
        float base = w * 0.785398f + t * 0.3f;
        float mir = (w & 1) ? -1.f : 1.f;
        int px = cx, py = cy;
        for (int i = 0; i < 48; i++) {
          float rr = 4.f + i * 1.3f;
          float off = sc[i * 5] / 12000.f * mir;
          float a = base + off;
          int x = cx + (int)(cosf(a) * rr), y = cy + (int)(sinf(a) * rr);
          fx::line(px, py, x, y, (uint8_t)(140 + i * 2));
          px = x; py = y;
        }
      }
      fx::palCosine(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.f, 1.f, 0.5f, 0.8f + g_lookX * 0.2f, 0.9f, 0.3f + g_lookY * 0.2f, t * 0.07f);
      break;
    }
    case 2: {  // SPECTRUM STAR: 64 band spokes rocketing through a hyperspace zoom
      gridWarp(cx, cy, 1.05f + aud::bass * 0.05f, g_lookX * 0.06f + 0.01f, 0, 0, 0);
      fx::warp(7, true);
      for (int b = 0; b < 64; b++) {
        int band = b < 32 ? b : 63 - b;
        float a = b / 64.f * 6.2831853f + t * 0.25f;
        float v = aud::bands[band];
        float r0 = 8.f + aud::bass * 8.f, r1 = r0 + v * 55.f;
        fx::line(cx + (int)(cosf(a) * r0), cy + (int)(sinf(a) * r0), cx + (int)(cosf(a) * r1), cy + (int)(sinf(a) * r1),
                 (uint8_t)(120 + v * 110.f));
      }
      if (s_shock >= 0) fx::ring(cx, cy, (int)s_shock, 255);
      fx::disc(cx, cy, 3 + (int)(aud::bass * 8.f), 255);
      fx::palCosine(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.f, 0.7f, 0.4f, 0.f, 0.15f + aud::bass * 0.2f, 0.2f, t * 0.03f);
      break;
    }
    case 3: {  // PHASE SPACE: lissajous of the sound against itself, liquid warp
      gridWarp(80, 60, 1.0f + aud::bass * 0.03f, 0.004f, 1.5f + aud::mid * 5.f, 0.25f + aud::treble * 0.6f, t * 3.f);
      fx::warp(3, true);
      int lag = 3 + (int)(aud::centroid * 40.f);
      float rot = t * 0.4f + g_lookX;
      float cr = cosf(rot), sr = sinf(rot);
      int px = 0, py = 0;
      for (int i = 0; i < 200; i++) {
        float xa = sc[i] / 11000.f * 44.f, ya = sc[(i + lag) & 255] / 11000.f * 44.f;
        for (int m = 0; m < 4; m++) {
          float mx = (m & 1) ? -xa : xa, my = (m & 2) ? -ya : ya;
          int x = cx + (int)(cr * mx - sr * my), y = cy + (int)(sr * mx + cr * my);
          if (m == 0 && i) fx::line(px, py, x, y, 230);
          else fx::plot(x, y, 200);
          if (m == 0) { px = x; py = y; }
        }
      }
      float tl = atan2f(g_lookY, g_lookX + 0.001f) / 6.2831853f;
      fx::palCosine(0.6f, 0.5f, 0.5f, 0.4f, 0.5f, 0.5f, 1.f, 1.f, 1.f, tl, tl + 0.2f, tl + 0.5f, t * 0.02f);
      break;
    }
    default: {  // SYNESTHESIA: spectrum sculpts the flow field; motes paint it; everything cross-maps
      float big = aud::bass, fine = aud::treble, loud = g_level;
      float cxs = cx, cys = cy;
      auto td = M5.Touch.getDetail();
      if (td.isPressed() && td.y > 14 && td.y < H - 14) { cxs = td.x / 2.f; cys = td.y / 2.f; }
      for (int j = 0; j < fx::GH; j++)
        for (int i = 0; i < fx::GW; i++) {
          float x = i * 8.f, y = j * 8.f, dx = x - cxs, dy = y - cys;
          float r = sqrtf(dx * dx + dy * dy) + 1.f;
          float sw = (0.4f + big * 2.5f) / (1.f + r * 0.03f);              // inverse: near centre spins hardest
          float rip = sinf(r * (0.08f + fine * 0.5f) - t * 4.f) * (1.f + loud * 4.f);
          fx::gx[j][i] = x + (-dy / r) * sw * 6.f + dx / r * rip * 0.8f + sinf(y * 0.05f + t) * 0.6f;
          fx::gy[j][i] = y + (dx / r) * sw * 6.f + dy / r * rip * 0.8f + cosf(x * 0.05f - t) * 0.6f;
        }
      fx::warp(loud > 0.5f ? 7 : 3, true);
      if (aud::onset > 0.3f)
        for (int k = 0; k < 10; k++) {
          for (auto &m : s_mote) if (m.life <= 0) { float a = (esp_random() % 628) / 100.f; m = {cxs + cosf(a) * 6.f, cys + sinf(a) * 6.f, 1.f}; break; }
        }
      for (auto &m : s_mote) {
        if (m.life <= 0) continue;
        int gi = (int)(m.x / 8.f), gj = (int)(m.y / 8.f);
        if (gi < 0 || gj < 0 || gi >= fx::GW || gj >= fx::GH) { m.life = 0; continue; }
        m.x += (m.x - fx::gx[gj][gi]) * 1.6f; m.y += (m.y - fx::gy[gj][gi]) * 1.6f;
        m.life -= dt * 0.4f;
        fx::disc((int)m.x, (int)m.y, 1, (uint8_t)(120 + m.life * 135.f));
      }
      if (td.isPressed() && td.y > 14 && td.y < H - 14) fx::disc(td.x / 2, td.y / 2, 3 + (int)(loud * 4), 255);
      int px = 0, py = 0;                                  // scope ring gets twisted into spirals by the field
      for (int i = 0; i <= 96; i++) {
        float a = (i % 96) / 96.f * 6.2831853f;
        float rr = 14.f + big * 10.f + sc[(i % 96) * 2] / 9000.f * 10.f;
        int x = (int)cxs + (int)(cosf(a) * rr), y = (int)cys + (int)(sinf(a) * rr);
        if (i) fx::line(px, py, x, y, (uint8_t)(150 + fine * 100.f));
        px = x; py = y;
      }
      // quiet = deep saturated slow colour; loud = fast pastel strobe (inverse, nonlinear)
      float spd = 0.02f + loud * loud * 0.6f;
      static float ph = 0; ph += dt * spd * 10.f;
      float amp = 0.5f - loud * 0.2f;
      fx::palCosine(0.5f + loud * 0.2f, 0.5f + loud * 0.2f, 0.5f + loud * 0.2f, amp, amp, amp,
                    s_palSeed[3], s_palSeed[4], s_palSeed[5], s_palSeed[0], s_palSeed[1], s_palSeed[2], ph);
      break;
    }
  }
  fx::palFlash(aud::onset * 0.15f);
  fx::present(canvas);
}

// ============================================================
//  chrome, modes, buttons
// ============================================================
static void drawChrome() {
  static const char *names[] = {"swarm", "eye", "tunnel", "pulse", "calm", "mantis"};
  canvas.fillRect(0, 0, W, 13, rgb565(6, 3, 12));
  canvas.fillRect(0, H - 13, W, 13, rgb565(6, 3, 12));
  canvas.setTextSize(1);
  canvas.setTextColor(hsv565(g_hue, 0.55f, 0.75f)); canvas.setCursor(8, 3); canvas.print("synapse");
  canvas.setTextColor(hsv565(g_hue + 40.f, 0.4f, 0.6f)); canvas.setCursor(64, 3); canvas.print(names[g_mode]);
  int orb = 2 + (int)(g_level * 5.f + g_peak * 3.f);
  if (orb > 6) orb = 6;
  canvas.fillCircle(W - 12, 6, orb, hsv565(g_hue + g_level * 60.f, 0.7f, 0.45f + g_level * 0.4f));

  const char *bl = "";
  switch (g_mode) {
    case MODE_SWARM: { static const char *v[] = {"flock", "orbit", "chaos"}; bl = v[g_swarmVar]; break; }
    case MODE_EYE: bl = g_eyeTrack ? "gaze" : "stare"; break;
    case MODE_TUNNEL: { static const char *v[] = {"dive", "recede", "fractal", "portal"}; bl = v[g_tunnelMode]; break; }
    case MODE_PULSE: { static const char *v[] = {"bloom", "kaleido", "star", "phase", "synesthesia"}; bl = v[g_pulsePat]; break; }
    case MODE_MANTIS: bl = g_mantisSing ? "sing" : "dance"; break;
    case MODE_CALM: bl = calmName(); break;
    default: break;
  }
  canvas.setTextColor(hsv565(g_hue, 0.35f, 0.55f));
  canvas.setCursor(10, H - 10); canvas.print("<");
  canvas.setTextColor(hsv565(g_hue + 20.f, 0.7f, 0.95f));
  canvas.setCursor(W / 2 - (int)strlen(bl) * 3, H - 10); canvas.print(bl);
  canvas.setTextColor(hsv565(g_hue, 0.35f, 0.55f));
  canvas.setCursor(W - 16, H - 10); canvas.print(">");
}

static void nextMode(int dir) {
  int m = ((int)g_mode + dir + MODE_COUNT) % MODE_COUNT;
  g_mode = (Mode)m;
  fx::clear(0);
  hap(100, 25);
}

static void btnBShort() {
  switch (g_mode) {
    case MODE_SWARM: g_swarmVar = (SwarmVar)((g_swarmVar + 1) % SV_COUNT); break;
    case MODE_EYE: g_eyeTrack = !g_eyeTrack; break;
    case MODE_TUNNEL: g_tunnelMode = (TunnelMode)((g_tunnelMode + 1) % TM_COUNT); fx::clear(0); break;
    case MODE_PULSE: g_pulsePat = (g_pulsePat + 1) % PP_COUNT; break;
    case MODE_MANTIS: g_mantisSing = !g_mantisSing; break;
    case MODE_CALM: calmNext(); break;
    default: break;
  }
  hap(90, 20);
}
// ============================================================
//  boot
// ============================================================
static void splash() {
  M5Canvas &c = *s_fb[0];
  c.fillSprite(rgb565(6, 2, 14));
  int ox = (W - MANTIS_W) / 2, oy = 18;
  for (int y = 0; y < MANTIS_H; y++)
    for (int x = 0; x < MANTIS_W; x++) {
      uint16_t col = mantis_splash[y * MANTIS_W + x];
      if (col) c.drawPixel(ox + x, oy + y, col);
    }
  c.setTextSize(2); c.setTextColor(hsv565(160, 0.85f, 0.95f));
  c.setCursor((W - 7 * 12) / 2, oy + MANTIS_H + 8); c.print("SYNAPSE");
  c.setTextSize(1); c.setTextColor(rgb565(120, 180, 160));
  c.setCursor((W - 17 * 6) / 2, oy + MANTIS_H + 30); c.print("a small green god");
  c.pushSprite(&M5.Display, 0, 0);
  M5.Speaker.begin();
  M5.Speaker.setVolume(170);
  static const int seq[][2] = {{523, 55}, {659, 55}, {784, 55}, {1047, 110}, {0, 35}, {784, 45}, {1047, 150}};
  for (auto &s : seq) {
    if (s[0]) M5.Speaker.tone((float)s[0], (uint32_t)s[1]);
    delay(s[1] + 12);
  }
  M5.Speaker.stop();
  M5.Speaker.end();
}

#ifdef HOST
M5Canvas *hostLastFrame() { return s_fb[s_pushIdx]; }
#endif

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true; cfg.internal_mic = true; cfg.internal_spk = true;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  if (M5.Imu.isEnabled()) { M5.Imu.loadOffsetFromNVS(); for (int i = 0; i < 15; i++) { M5.Imu.update(); delay(4); } }

  for (int i = 0; i < 2; i++) { s_fb[i]->setColorDepth(16); s_fb[i]->setPsram(true); }
  s_fb[0]->createSprite(W, H);
  s_double = s_fb[1]->createSprite(W, H) != nullptr;
  g_cv = s_fb[0];
  splash();
  fx::begin();
  mantisBegin();
  calmBegin();
  seedParticles();
  aud::begin();
  if (s_double) {
    s_dispIdle = xSemaphoreCreateBinary();
    xSemaphoreGive(s_dispIdle);
    xTaskCreatePinnedToCore(displayTask, "lcd", 4096, nullptr, 2, &s_dispTask, 0);
  }
  g_cv = s_fb[s_cur];
}

void loop() {
  static uint32_t last = micros();
  uint32_t now = micros();
  g_dt = clampf((now - last) / 1e6f, 0.004f, 0.06f);
  last = now;

  pollInput();
  aud::service(g_dt);
  sampleImu();
  g_level = aud::level; g_peak = aud::peak;
  for (int i = 0; i < MIC_N; i++) g_mic[i] = aud::scope[i * 2];

  g_t += g_dt * (0.9f + g_level * 0.6f);
  g_hue = fmodf(g_hue + g_dt * (3.6f + g_level * 2.4f), 360.f);

  switch (g_mode) {
    case MODE_SWARM: modeSwarm(); break;
    case MODE_EYE: modeEye(); break;
    case MODE_TUNNEL: modeTunnel(); break;
    case MODE_PULSE: modePulse(); break;
    case MODE_CALM: calmDraw(); break;
    case MODE_MANTIS: mantisDraw(g_mantisSing); break;
    default: break;
  }
  g_shakeKick = false;
  drawChrome();
  present();
}
