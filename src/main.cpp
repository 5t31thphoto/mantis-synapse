// ============================================================
//  SYNAPSE v3 — mantis psychedelic fidget for M5Stack Core2
//  Mic + IMU + touch + haptic via M5Unified. Double-buffered.
// ============================================================
#include <M5Unified.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <SD.h>
#include <SPI.h>
#include "mantis_splash.h"

static const int W = 320, H = 240;
static const int N_PART = 72;
static const int N_TRAIL = 96;
static const int MIC_N = 128;
static const int LOOP_MAX = 64;

enum Mode : uint8_t {
  MODE_SWARM = 0, MODE_EYE, MODE_TUNNEL, MODE_PULSE, MODE_DRUM, MODE_MANTIS, MODE_COUNT
};
enum SwarmVar : uint8_t { SV_FLOCK = 0, SV_ORBIT, SV_CHAOS, SV_COUNT };
enum PulsePat : uint8_t {
  PP_WAVE = 0, PP_MIRROR, PP_STAR, PP_RIBBON, PP_CHAOS, PP_COUNT
};
enum TunnelMode : uint8_t {
  TM_DIVE = 0, TM_RECEDE, TM_FRACTAL, TM_PORTAL, TM_COUNT
};

struct Particle {
  float x, y, vx, vy;
  float hue;
};
struct Trail {
  int16_t x, y;
  uint16_t c;
  uint8_t life;
};

static M5Canvas canvas(&M5.Display);
static Particle g_p[N_PART];
static Trail g_tr[N_TRAIL];
static int g_trI = 0;
static Mode g_mode = MODE_SWARM;
static SwarmVar g_swarmVar = SV_FLOCK;
static PulsePat g_pulsePat = PP_WAVE;
static TunnelMode g_tunnelMode = TM_DIVE;
static int g_portalCombo = 0;
static float g_portalX = 0, g_portalY = 0;
static float g_portalZ = 3.f;
static uint32_t g_portalSpawn = 0;
static float g_t = 0;
static float g_level = 0, g_peak = 0;
static float g_ax = 0, g_ay = 0, g_az = 1;
static float g_gx = 0, g_gy = 0, g_gz = 0;
static float g_lookX = 0, g_lookY = 0;  // integrated view (accel+gyro)
static bool g_imuOk = false;
static float g_shake = 0;
static float g_hue = 160;
static uint32_t g_lastBtn = 0;
static int16_t g_mic[MIC_N];
static bool g_eyeTrack = true;
static bool g_mantisSing = false;
static uint32_t g_micRestoreAt = 0;

// Eye poke reaction
static float g_poke = 0;       // 1 = just poked, decays
static float g_pokeSquint = 0;
static uint32_t g_pokeUntil = 0;

// --- haptic non-blocking ---
static uint32_t g_hapUntil = 0;
static uint8_t g_hapLevel = 0;
static uint32_t g_kickHapEnd = 0;
static uint32_t g_kickHapStart = 0;
static void hap(uint8_t level, uint16_t ms) {
  g_hapLevel = level;
  g_hapUntil = millis() + ms;
  M5.Power.setVibration(level);
}
static void hapService() {
  uint32_t now = millis();
  // Kick "subwoofer" envelope: deep throb, not a text-message buzz
  if (g_kickHapEnd && now < g_kickHapEnd) {
    float u = (float)(now - g_kickHapStart) / (float)(g_kickHapEnd - g_kickHapStart);
    // 55Hz-ish pulse decaying
    float env = (1.f - u) * (1.f - u);
    int phase = ((now / 9) & 1);
    int lvl = (int)(env * (phase ? 220 : 30));
    M5.Power.setVibration(lvl);
    return;
  }
  if (g_kickHapEnd && now >= g_kickHapEnd) {
    g_kickHapEnd = 0;
    M5.Power.setVibration(0);
  }
  if (g_hapLevel && now >= g_hapUntil) {
    M5.Power.setVibration(0);
    g_hapLevel = 0;
  }
}
static void kickSubHaptic() {
  g_kickHapStart = millis();
  g_kickHapEnd = g_kickHapStart + 140;
}

// --- drum ---
enum Pad : uint8_t { PAD_HAT_C = 0, PAD_HAT_O, PAD_KICK, PAD_SNARE };
static const char *PAD_NAME[] = {"CH", "OH", "KICK", "SNR"};
static int16_t *g_padSample[4] = {nullptr, nullptr, nullptr, nullptr};
static int g_padSampleLen[4] = {0, 0, 0, 0};
static bool g_padArmed[4] = {false, false, false, false};
static uint32_t g_padHoldStart = 0;
static int g_padHoldId = -1;
static bool g_loopOn = false;
static uint8_t g_loopEv[LOOP_MAX];
static uint16_t g_loopAt[LOOP_MAX];
static int g_loopN = 0;
static uint32_t g_loopStart = 0;
static uint32_t g_loopLenMs = 8000; // 4 bars @ 120bpm default
static int g_loopPlayI = 0;
static float g_loopPulse = 0;
static float g_bpm = 120.f;
static bool g_tempoMode = false;
static uint32_t g_tempoTaps[8];
static int g_tempoTapN = 0;
static uint32_t g_btnBDown = 0;
static bool g_btnBLong = false;

static const char *DRUM_DIR = "/drums";
static const char *DRUM_SPEC = "/drums/SAMPLES.txt";
static const char *PAD_FILE[] = {
  "/drums/hat_closed.raw", "/drums/hat_open.raw",
  "/drums/kick.raw", "/drums/snare.raw"
};

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static uint16_t hsv565(float h, float s, float v) {
  while (h < 0) h += 360.f;
  while (h >= 360.f) h -= 360.f;
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

static void recomputeLoopLen() {
  // 4 bars of 4/4: 16 beats
  float ms = (60000.f / g_bpm) * 16.f;
  if (ms < 2000.f) ms = 2000.f;
  if (ms > 32000.f) ms = 32000.f;
  g_loopLenMs = (uint32_t)ms;
}

static void seedParticles() {
  for (int i = 0; i < N_PART; i++) {
    g_p[i].x = (float)(rand() % W);
    g_p[i].y = (float)(14 + rand() % (H - 30));
    g_p[i].vx = ((rand() % 100) - 50) * 0.03f;
    g_p[i].vy = ((rand() % 100) - 50) * 0.03f;
    g_p[i].hue = g_hue + (rand() % 60) - 30;
  }
  memset(g_tr, 0, sizeof(g_tr));
}

static void addTrail(int x, int y, uint16_t c) {
  g_tr[g_trI].x = (int16_t)x;
  g_tr[g_trI].y = (int16_t)y;
  g_tr[g_trI].c = c;
  g_tr[g_trI].life = 40;
  g_trI = (g_trI + 1) % N_TRAIL;
}

static void drawTrails() {
  for (int i = 0; i < N_TRAIL; i++) {
    if (g_tr[i].life < 2) continue;
    uint8_t L = g_tr[i].life;
    int r = 1 + (L >> 4);
    // afterglow: dim by redrawing smaller
    canvas.fillCircle(g_tr[i].x, g_tr[i].y, r, g_tr[i].c);
    if (L > 20)
      canvas.fillCircle(g_tr[i].x, g_tr[i].y, r + 1,
                        hsv565(g_hue + L, 0.4f, 0.08f + L * 0.004f));
    g_tr[i].life = (uint8_t)(L * 0.88f);
  }
}

// Soft reactive backdrop — plasma / moire wash (not a full-screen storm)
static void drawPsyBg() {
  float base = 0.06f + g_level * 0.08f + g_peak * 0.04f;
  // horizontal reactive bands
  for (int y = 14; y < H - 14; y += 6) {
    float wiggle = sinf(y * 0.04f + g_t * 1.2f + g_ax) * 0.5f
                 + cosf(y * 0.07f - g_t * 0.8f + g_ay * 2.f) * 0.5f;
    float v = base + wiggle * 0.05f + g_level * 0.04f * sinf(y * 0.1f + g_t * 3.f);
    if (v < 0.02f) v = 0.02f;
    if (v > 0.22f) v = 0.22f;
    uint16_t c = hsv565(g_hue + y * 0.4f + g_t * 12.f, 0.55f, v);
    canvas.drawFastHLine(0, y, W, c);
    if ((y & 15) == 0)
      canvas.drawFastHLine(0, y + 1, W, hsv565(g_hue + 40.f + y * 0.2f, 0.4f, v * 0.7f));
  }
  // sparse vertical shimmer from mid energy
  for (int x = 0; x < W; x += 20) {
    float v = 0.04f + g_peak * 0.1f * (0.5f + 0.5f * sinf(x * 0.05f + g_t * 2.f));
    canvas.drawFastVLine(x + (int)(sinf(g_t + x) * 3), 14, H - 28,
                         hsv565(g_hue + 90.f, 0.5f, v));
  }
}


static void drawTinyMantis(M5Canvas &c, int cx, int cy) {
  // ~1/5 scale splash asset centered on (cx,cy)
  const int sc = 5;
  int dw = MANTIS_W / sc, dh = MANTIS_H / sc;
  int ox = cx - dw / 2, oy = cy - dh / 2;
  for (int y = 0; y < dh; y++) {
    for (int x = 0; x < dw; x++) {
      uint16_t col = mantis_splash[(y * sc) * MANTIS_W + (x * sc)];
      if (col) c.drawPixel(ox + x, oy + y, col);
    }
  }
}

static void sampleAudio() {
  if (!M5.Mic.isEnabled()) {
    M5.Speaker.end();
    M5.Mic.begin();
  }
  if (!M5.Mic.record(g_mic, MIC_N, 16000)) return;
  float sum = 0, peak = 0;
  for (int i = 0; i < MIC_N; i++) {
    float a = fabsf((float)g_mic[i]);
    sum += a;
    if (a > peak) peak = a;
  }
  float lvl = (sum / MIC_N) / 6000.f;
  if (lvl > 1.5f) lvl = 1.5f;
  g_level = g_level * 0.75f + lvl * 0.25f;
  g_peak = g_peak * 0.8f + (peak / 22000.f) * 0.2f;
  if (g_level > 0.7f) hap(55, 20);
}

// Board-agnostic Core2 IMU via M5Unified (no chip names)
static void sampleImu() {
  // Official M5Unified pattern (Imu.ino): update() then getImuData().
  // Works across Core2 revisions — library picks the sensor.
  auto imu_update = M5.Imu.update();
  if (!imu_update) return;

  auto data = M5.Imu.getImuData();
  g_imuOk = true;

  // Accel (g). Light smooth so tilt is still snappy.
  g_ax = g_ax * 0.5f + data.accel.x * 0.5f;
  g_ay = g_ay * 0.5f + data.accel.y * 0.5f;
  g_az = g_az * 0.5f + data.accel.z * 0.5f;

  g_gx = data.gyro.x; // deg/s
  g_gy = data.gyro.y;
  g_gz = data.gyro.z;

  // Integrate gyro for responsive look; spring toward accel tilt.
  // Display is landscape (rotation 1): map board tilt → screen X/Y.
  const float dt = 0.033f;
  g_lookX += g_gy * dt * 0.08f;
  g_lookY += g_gx * dt * 0.08f;
  // Accel targets (board frame → screen after rot 1)
  float targetX = -g_ay;  // left/right tilt
  float targetY =  g_ax;  // forward/back
  g_lookX = g_lookX * 0.92f + targetX * 0.08f;
  g_lookY = g_lookY * 0.92f + targetY * 0.08f;
  // clamp
  if (g_lookX > 1.2f) g_lookX = 1.2f;
  if (g_lookX < -1.2f) g_lookX = -1.2f;
  if (g_lookY > 1.2f) g_lookY = 1.2f;
  if (g_lookY < -1.2f) g_lookY = -1.2f;

  float mag = sqrtf(data.accel.x * data.accel.x + data.accel.y * data.accel.y + data.accel.z * data.accel.z);
  float sh = fabsf(mag - 1.0f);
  g_shake = g_shake * 0.8f + sh * 0.2f;
  if (g_shake > 0.5f) {
    g_hue += 18.f;
    if (g_hue >= 360.f) g_hue -= 360.f;
    hap(140, 30);
    for (int i = 0; i < N_PART; i++) {
      g_p[i].vx += ((rand() % 100) - 50) * 0.07f;
      g_p[i].vy += ((rand() % 100) - 50) * 0.07f;
    }
  }
}


static void playStartup() {
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(180);
  const int seq[][2] = {
    {523, 55}, {659, 55}, {784, 55}, {1047, 110}, {0, 35},
    {784, 45}, {1047, 150}
  };
  for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
    if (seq[i][0] == 0) delay(seq[i][1]);
    else {
      M5.Speaker.tone(seq[i][0], seq[i][1]);
      delay(seq[i][1] + 12);
    }
  }
  M5.Speaker.stop();
  M5.Speaker.end();
  M5.Mic.begin();
}

static void splash() {
  canvas.fillSprite(rgb565(6, 2, 14));
  int ox = (W - MANTIS_W) / 2;
  int oy = 18;
  for (int y = 0; y < MANTIS_H; y++) {
    for (int x = 0; x < MANTIS_W; x++) {
      uint16_t col = mantis_splash[y * MANTIS_W + x];
      if (col) canvas.drawPixel(ox + x, oy + y, col);
    }
  }
  canvas.setTextSize(2);
  canvas.setTextColor(hsv565(160, 0.85f, 0.95f));
  canvas.setCursor((W - 8 * 12) / 2, oy + MANTIS_H + 8);
  canvas.print("SYNAPSE");
  canvas.setTextSize(1);
  canvas.setTextColor(rgb565(140, 160, 180));
  canvas.setCursor(70, oy + MANTIS_H + 32);
  canvas.print("tilt · touch · make noise");
  canvas.pushSprite(0, 0);
  playStartup();
  delay(450);
}

// ========== SWARM ==========
static void modeSwarm() {
  drawPsyBg();
  drawTrails();

  float gx = g_lookX * 0.85f;
  float gy = g_lookY * 0.85f;
  float pulse = 0.45f + g_level * 1.5f;
  float soundHue = g_hue + g_level * 100.f + g_peak * 40.f;

  // metaball-ish pairs: when close, draw a bridging blob
  for (int i = 0; i < N_PART; i++) {
    for (int j = i + 1; j < N_PART; j += 3) {
      float dx = g_p[i].x - g_p[j].x;
      float dy = g_p[i].y - g_p[j].y;
      float d2 = dx * dx + dy * dy;
      if (d2 < 500.f && d2 > 1.f) {
        float mx = (g_p[i].x + g_p[j].x) * 0.5f;
        float my = (g_p[i].y + g_p[j].y) * 0.5f;
        float t = 1.f - d2 / 500.f;
        int br = 2 + (int)(t * 8.f * (0.5f + g_level));
        uint16_t bc = hsv565(soundHue + i + j, 0.75f, 0.25f + t * 0.45f);
        canvas.fillCircle((int)mx, (int)my, br, bc);
        // mild cohesion pull
        g_p[i].vx -= dx * 0.0015f * t;
        g_p[i].vy -= dy * 0.0015f * t;
        g_p[j].vx += dx * 0.0015f * t;
        g_p[j].vy += dy * 0.0015f * t;
      }
    }
  }

  for (int i = 0; i < N_PART; i++) {
    Particle &p = g_p[i];
    float cx = W * 0.5f + sinf(g_t * 0.6f + i * 0.1f) * 22.f * g_peak;
    float cy = H * 0.5f + cosf(g_t * 0.5f) * 16.f * g_level;

    if (g_swarmVar == SV_FLOCK) {
      float dx = cx - p.x, dy = cy - p.y;
      p.vx += dx * 0.0022f * pulse + gx;
      p.vy += dy * 0.0022f * pulse + gy;
      for (int j = 0; j < N_PART; j += 2) {
        if (j == i) continue;
        float sx = p.x - g_p[j].x, sy = p.y - g_p[j].y;
        float d2 = sx * sx + sy * sy + 0.01f;
        if (d2 < 900.f) {
          p.vx += sx / d2 * 12.f;
          p.vy += sy / d2 * 12.f;
        }
      }
      p.vx *= 0.94f;
      p.vy *= 0.94f;
    } else if (g_swarmVar == SV_ORBIT) {
      float dx = p.x - cx, dy = p.y - cy;
      p.vx += -dy * 0.006f * pulse + gx * 0.8f;
      p.vy += dx * 0.006f * pulse + gy * 0.8f;
      p.vx += -dx * 0.001f;
      p.vy += -dy * 0.001f;
      p.vx *= 0.95f;
      p.vy *= 0.95f;
    } else {
      p.vx += gx * 1.2f + sinf(g_t * 2.f + p.y * 0.05f) * 0.15f * (0.3f + g_level);
      p.vy += gy * 1.2f + cosf(g_t * 1.7f + p.x * 0.05f) * 0.15f * (0.3f + g_level);
      p.vx *= 0.92f;
      p.vy *= 0.92f;
    }

    p.x += p.vx;
    p.y += p.vy;
    if (p.x < 4) { p.x = 4; p.vx *= -0.6f; }
    if (p.x > W - 5) { p.x = W - 5; p.vx *= -0.6f; }
    if (p.y < 16) { p.y = 16; p.vy *= -0.6f; }
    if (p.y > H - 18) { p.y = H - 18; p.vy *= -0.6f; }

    float h = soundHue + p.hue * 0.3f + i * 2.f;
    float v = 0.4f + g_level * 0.5f + g_peak * 0.15f;
    int r = 3 + (int)(g_level * 6.f) + (i & 1);
    uint16_t col = hsv565(h, 0.8f, v);
    canvas.fillCircle((int)p.x, (int)p.y, r, col);
    if (r > 4)
      canvas.fillCircle((int)p.x - 1, (int)p.y - 1, r / 2,
                        hsv565(h + 20.f, 0.5f, fminf(1.f, v + 0.2f)));
    if ((i & 1) == 0) addTrail((int)p.x, (int)p.y, col);
  }

  auto td = M5.Touch.getDetail();
  if (td.isPressed() && td.y > 16 && td.y < H - 18) {
    int tx = td.x, ty = td.y;
    drawTinyMantis(canvas, tx, ty);
    for (int i = 0; i < 8; i++)
      addTrail(tx + (rand() % 11) - 5, ty + (rand() % 11) - 5,
               hsv565(soundHue + 40.f, 0.9f, 0.7f));
    // Strong push: break globs + drag field
    for (int i = 0; i < N_PART; i++) {
      float dx = g_p[i].x - tx, dy = g_p[i].y - ty;
      float d2 = dx * dx + dy * dy + 0.01f;
      if (d2 < 10000.f) {
        // repulsive core (breaks metaballs) + soft attract outer
        float inv = 1.f / d2;
        if (d2 < 900.f) {
          g_p[i].vx += dx * inv * 40.f;
          g_p[i].vy += dy * inv * 40.f;
        } else {
          g_p[i].vx -= dx * 0.012f;
          g_p[i].vy -= dy * 0.012f;
        }
      }
    }
  }
}

// ========== EYE ==========
static void modeEye() {
  // Subtle bg only — eye is the star; less whole-scene parallax
  float base = 0.04f + g_level * 0.05f;
  for (int y = 14; y < H - 14; y += 8) {
    uint16_t c = hsv565(g_hue + y * 0.3f + g_t * 8.f, 0.4f, base);
    canvas.drawFastHLine(0, y, W, c);
  }

  // Gaze: mostly pupil/iris, small sclera shift
  float gazeX = g_eyeTrack ? constrain(g_lookX * 28.f, -22.f, 22.f) : 0;
  float gazeY = g_eyeTrack ? constrain(g_lookY * 22.f, -16.f, 16.f) : 0;
  float bodyX = g_eyeTrack ? constrain(g_lookX * 12.f, -10.f, 10.f) : 0;
  float bodyY = g_eyeTrack ? constrain(g_lookY * 10.f, -8.f, 8.f) : 0;

  float squint = 0.f;
  if (g_poke > 0.05f) {
    squint = g_poke;
    g_poke *= 0.93f;
  }

  int cx = W / 2 + (int)bodyX;
  int cy = H / 2 + (int)bodyY;

  // True moire: two ring sets, different speed/spacing/center
  float mo1 = g_t * 0.55f + g_level * 2.f;
  float mo2 = -g_t * 0.9f + g_peak * 3.f;
  for (int ring = 12; ring >= 0; ring--) {
    float d1 = 14.f + ring * 11.f + sinf(mo1 + ring * 0.4f) * 3.f;
    float d2 = 16.f + ring * 10.5f + cosf(mo2 + ring * 0.5f) * 4.f + g_level * 6.f;
    uint16_t c1 = hsv565(g_hue + ring * 8.f, 0.5f, 0.08f + ring * 0.015f);
    uint16_t c2 = hsv565(g_hue + 60.f + ring * 6.f, 0.55f, 0.06f + g_peak * 0.08f);
    canvas.drawCircle(cx, cy, (int)d1, c1);
    canvas.drawCircle(cx + (int)(sinf(mo2) * 3), cy + (int)(cosf(mo1) * 2), (int)d2, c2);
  }

  // Sclera (squint vertically when poked)
  int erX = 44;
  int erY = (int)(44 * (1.f - squint * 0.72f));
  if (erY < 10) erY = 10;
  // approximate squint with stacked ellipses via scaled circles
  for (int k = 0; k < erY; k++) {
    float t = 1.f - (float)k / erY;
    int w = (int)(erX * sqrtf(t));
    canvas.drawFastHLine(cx - w, cy - erY / 2 + k, w * 2,
                         hsv565(g_hue + 180, 0.12f, 0.82f - squint * 0.25f));
  }

  // Iris + pupil follow gaze harder
  int ix = cx + (int)gazeX;
  int iy = cy + (int)gazeY;
  int ir = (int)(20 + g_peak * 6 - squint * 10);
  if (ir < 5) ir = 5;
  canvas.fillCircle(ix, iy, ir, hsv565(g_hue + 40, 0.9f, 0.5f + g_level * 0.3f));
  int pr = (int)(9 + g_level * 3 - squint * 6);
  if (pr < 2) pr = 2;
  canvas.fillCircle(ix + (int)(gazeX * 0.2f), iy + (int)(gazeY * 0.2f), pr, rgb565(6, 4, 10));
  if (squint < 0.35f)
    canvas.fillCircle(ix - 3, iy - 3, 2, rgb565(230, 235, 255));

  // Pain: red flash rings + tears
  if (squint > 0.4f) {
    canvas.drawCircle(cx, cy, erX + 4, rgb565(180, 40, 40));
    for (int i = 0; i < 5; i++) {
      canvas.fillCircle(cx - 20 + i * 3, cy + erY / 2 + 4 + i * 2, 2,
                        hsv565(200, 0.3f, 0.7f));
    }
  }

  auto td = M5.Touch.getDetail();
  if (td.wasPressed()) {
    float dx = td.x - cx, dy = td.y - cy;
    if (dx * dx + dy * dy < 55.f * 55.f) {
      g_poke = 1.f;
      hap(200, 60);
    }
  }
}

static void modeTunnel() {
  static float z = 0;
  // Direction: DIVE increases depth into hole; RECEDE reverses
  float dir = (g_tunnelMode == TM_RECEDE) ? -1.f : 1.f;
  float speed = (0.07f + g_level * 0.2f + g_peak * 0.08f) * dir;
  if (g_tunnelMode == TM_FRACTAL) speed *= 0.7f;
  z += speed;

  // Background parallax layer — uncoupled from tunnel, own tilt/sound map
  for (int i = 0; i < 30; i++) {
    float px = fmodf(i * 47.f + g_lookX * 40.f + g_t * 15.f * (1 + (i & 3)), (float)W);
    float py = fmodf(i * 31.f + g_lookY * 35.f - g_t * 10.f, (float)(H - 28)) + 14.f;
    float a = 0.05f + g_peak * 0.12f * (0.5f + 0.5f * sinf(i + g_t));
    canvas.fillCircle((int)px, (int)py, 1 + (i & 1),
                      hsv565(g_hue + 90.f + i * 9.f, 0.7f, a));
  }
  for (int y = 14; y < H - 14; y += 10) {
    float v = 0.03f + 0.04f * sinf(y * 0.08f + g_t * 0.5f + g_lookY * 2.f)
            + g_level * 0.03f * sinf(y * 0.2f);
    canvas.drawFastHLine(0, y, W, hsv565(g_hue + 120.f + y * 0.2f, 0.45f, v));
  }

  float spin = g_t * 0.2f + g_lookX * 2.0f;
  int cx = W / 2 + (int)(g_lookX * -55.f);
  int cy = H / 2 + (int)(g_lookY * 45.f);

  if (g_tunnelMode == TM_FRACTAL) {
    // Cheap mandelbrot-ish escape bands for demo-scene candy
    for (int iy = 14; iy < H - 14; iy += 3) {
      for (int ix = 0; ix < W; ix += 3) {
        float u = ((ix - cx) / 90.f) * (1.2f + g_lookX * 0.3f);
        float v = ((iy - cy) / 70.f) * (1.2f + g_lookY * 0.3f);
        // zoom with z
        float zoom = 0.6f + fmodf(fabsf(z) * 0.15f, 2.5f);
        u /= zoom; v /= zoom;
        float zr = u, zi = v;
        int k;
        for (k = 0; k < 12; k++) {
          float zr2 = zr * zr - zi * zi + u;
          zi = 2.f * zr * zi + v;
          zr = zr2;
          if (zr * zr + zi * zi > 4.f) break;
        }
        if (k > 2) {
          canvas.fillRect(ix, iy, 3, 3,
            hsv565(g_hue + k * 28.f + g_t * 20.f + g_level * 50.f, 0.9f, 0.15f + k * 0.06f));
        }
      }
    }
  } else {
    // Rings — dive means outer rings faster toward center (increasing z depth)
    for (int ring = 16; ring >= 0; ring--) {
      float zz = fmodf(fabsf(z) + ring * 0.38f, 8.f);
      // invert depth perception for dive: small z = far
      float depth = (g_tunnelMode == TM_RECEDE) ? (8.f - zz) : zz;
      float sc = 12.f / (depth + 0.35f);
      int rad = (int)(sc * (18.f + g_level * 6.f));
      float lobe = 1.f + g_peak * 0.3f * sinf(ring * 0.7f + g_t * 2.f);
      uint16_t c = hsv565(g_hue + ring * 12.f + g_t * 25.f, 0.85f,
                          0.12f + (16 - ring) * 0.03f + g_level * 0.3f);
      int sides = 5 + (ring % 3);
      for (int k = 0; k < sides; k++) {
        float a0 = spin + k * (2.f * (float)M_PI / sides);
        float a1 = spin + (k + 1) * (2.f * (float)M_PI / sides);
        float r0 = rad * lobe, r1 = rad * lobe;
        canvas.drawLine(
          cx + (int)(cosf(a0) * r0), cy + (int)(sinf(a0) * r0 * 0.85f),
          cx + (int)(cosf(a1) * r1), cy + (int)(sinf(a1) * r1 * 0.85f), c);
      }
    }
  }

  // Portal wormhole mode: aim with IMU, keep combo
  if (g_tunnelMode == TM_PORTAL) {
    if (millis() - g_portalSpawn > 2200) {
      g_portalSpawn = millis();
      g_portalX = (float)((rand() % 160) - 80);
      g_portalY = (float)((rand() % 100) - 50);
      g_portalZ = 5.f;
    }
    g_portalZ -= 0.08f + g_level * 0.05f;
    float sc = 80.f / (g_portalZ + 0.5f);
    int px = W / 2 + (int)(g_portalX + g_lookX * -40.f);
    int py = H / 2 + (int)(g_portalY + g_lookY * 30.f);
    int rad = (int)sc;
    for (int r = 0; r < 4; r++)
      canvas.drawCircle(px, py, rad - r * 3,
                        hsv565(280 + r * 20.f + g_t * 30.f, 1.f, 0.5f + g_peak * 0.4f));
    // crosshair at view center
    canvas.drawLine(cx - 8, cy, cx + 8, cy, rgb565(255, 255, 100));
    canvas.drawLine(cx, cy - 8, cx, cy + 8, rgb565(255, 255, 100));
    // hit test
    float dx = (float)(px - cx), dy = (float)(py - cy);
    if (g_portalZ < 1.2f) {
      if (dx * dx + dy * dy < 28.f * 28.f) {
        g_portalCombo++;
        g_portalSpawn = 0;
        hap(120, 30);
      } else if (g_portalZ < 0.4f) {
        g_portalCombo = 0;
        g_portalSpawn = 0;
      }
    }
    canvas.setTextColor(rgb565(255, 220, 80));
    canvas.setCursor(8, 20);
    canvas.printf("COMBO %d", g_portalCombo);
  }

  int core = 3 + (int)(g_level * 12);
  canvas.fillCircle(cx, cy, core, hsv565(g_hue + g_t * 50.f, 0.9f, 0.55f + g_peak * 0.4f));
}

static void modePulse() {
  // Psychedelic oil / ink plasma — own mapping vs foreground wave
  for (int y = 14; y < H - 14; y += 2) {
    for (int x = 0; x < W; x += 4) {
      float u = x * 0.02f + g_lookX * 1.5f;
      float v = y * 0.025f + g_lookY * 1.2f;
      float n = sinf(u + g_t * 0.7f) * cosf(v - g_t * 0.5f)
              + sinf((u + v) * 0.8f + g_level * 3.f)
              + cosf(u * 1.3f - v + g_peak * 4.f + g_t);
      float bri = 0.06f + 0.1f * (0.5f + 0.5f * n) + g_level * 0.06f;
      if (bri > 0.28f) bri = 0.28f;
      canvas.fillRect(x, y, 4, 2, hsv565(g_hue + n * 40.f + y * 0.3f, 0.75f, bri));
    }
  }

  int cx = W / 2 + (int)(g_lookX * -20.f);
  int cy = H / 2 + (int)(g_lookY * 16.f);

  auto sampleR = [&](int i) -> float {
    float s = fabsf((float)g_mic[i % MIC_N]) / 16000.f;
    return 26.f + s * 75.f + g_level * 28.f;
  };

  if (g_pulsePat == PP_WAVE || g_pulsePat == PP_MIRROR) {
    int prevx = cx, prevy = cy;
    for (int i = 0; i < MIC_N; i++) {
      float ang = (float)i / MIC_N * 2.f * (float)M_PI + g_t * 0.5f;
      float r = sampleR(i);
      if (g_pulsePat == PP_MIRROR) r += sinf(ang * 3.f + g_t) * 10.f * g_peak;
      int x = cx + (int)(cosf(ang) * r);
      int y = cy + (int)(sinf(ang) * r * 0.9f);
      uint16_t c = hsv565(g_hue + i * 2.f + g_t * 30.f, 0.9f, 0.4f + r / 150.f);
      if (i > 0) canvas.drawLine(prevx, prevy, x, y, c);
      prevx = x; prevy = y;
    }
  } else if (g_pulsePat == PP_STAR) {
    for (int arm = 0; arm < 8; arm++) {
      float base = arm * (float)M_PI / 4.f + g_t * 0.4f + g_lookX;
      int prevx = cx, prevy = cy;
      for (int i = 0; i < 32; i++) {
        float tt = i / 32.f;
        float r = sampleR(i * 4) * tt;
        float ang = base + tt * 0.4f * g_level;
        int x = cx + (int)(cosf(ang) * r);
        int y = cy + (int)(sinf(ang) * r * 0.9f);
        canvas.drawLine(prevx, prevy, x, y, hsv565(g_hue + arm * 40.f + tt * 80.f, 0.95f, 0.4f + tt * 0.5f));
        prevx = x; prevy = y;
      }
    }
  } else if (g_pulsePat == PP_RIBBON) {
    for (int layer = 0; layer < 3; layer++) {
      int prevx = 0, prevy = H / 2;
      for (int x = 0; x < W; x += 2) {
        int mi = (x + layer * 40) % MIC_N;
        float s = fabsf((float)g_mic[mi]) / 14000.f;
        float y = H * 0.5f
          + sinf(x * 0.03f + g_t * (1.2f + layer * 0.4f) + g_lookY)
            * (30.f + g_level * 40.f + s * 50.f)
          + g_lookX * 18.f * (layer - 1);
        uint16_t c = hsv565(g_hue + layer * 50.f + x * 0.5f, 0.85f, 0.35f + s * 0.5f);
        if (x > 0) canvas.drawLine(prevx, prevy, x, (int)y, c);
        prevx = x; prevy = (int)y;
      }
    }
  } else {
    for (int i = 0; i < MIC_N; i += 2) {
      float ang = (float)i / MIC_N * 2.f * (float)M_PI * (2.f + g_peak)
                + g_t * (1.f + g_level) + g_lookX * 2.f;
      float r = sampleR(i) * (0.6f + 0.4f * sinf(g_t * 3.f + i * 0.2f + g_lookY * 4.f));
      int x = cx + (int)(cosf(ang) * r);
      int y = cy + (int)(sinf(ang * 1.3f) * r * 0.85f);
      canvas.fillCircle(x, y, 1 + (int)(g_level * 3),
                        hsv565(g_hue + i * 3.f + g_shake * 80.f, 1.f, 0.45f + g_peak * 0.5f));
    }
  }
  int core = 8 + (int)(g_level * 22);
  canvas.fillCircle(cx, cy, core, hsv565(g_hue + g_t * 40.f, 0.8f, 0.5f + g_level * 0.4f));
}

// ========== DRUM FS + audio ==========
static void ensureDrumFs() {
  if (!SD.begin(4, SPI, 25000000)) return;
  if (!SD.exists(DRUM_DIR)) SD.mkdir(DRUM_DIR);
  if (!SD.exists(DRUM_SPEC)) {
    File f = SD.open(DRUM_SPEC, FILE_WRITE);
    if (f) {
      f.println("SYNAPSE drum pads — one raw sample per pad");
      f.println("Format: signed 16-bit mono PCM, 16000 Hz, little-endian");
      f.println("No WAV header — pure .raw");
      f.println("");
      f.println("hat_closed.raw  = closed hi-hat (top-left)");
      f.println("hat_open.raw    = open hi-hat  (top-right)");
      f.println("kick.raw        = kick drum    (bottom-left)");
      f.println("snare.raw       = snare        (bottom-right)");
      f.println("");
      f.println("Record in-app or copy files here. Max ~0.5s.");
      f.close();
    }
  }
}

static void loadPadFromSd(int pad) {
  if (pad < 0 || pad > 3 || !SD.exists(PAD_FILE[pad])) return;
  File f = SD.open(PAD_FILE[pad], FILE_READ);
  if (!f) return;
  size_t bytes = f.size();
  if (bytes < 4) { f.close(); return; }
  if (bytes > 16000) bytes = 16000;
  int n = (int)(bytes / 2);
  int16_t *buf = (int16_t *)heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_8BIT);
  if (!buf) buf = (int16_t *)malloc(n * sizeof(int16_t));
  if (!buf) { f.close(); return; }
  f.read((uint8_t *)buf, n * 2);
  f.close();
  if (g_padSample[pad]) free(g_padSample[pad]);
  g_padSample[pad] = buf;
  g_padSampleLen[pad] = n;
}

static void savePadToSd(int pad) {
  if (pad < 0 || pad > 3 || !g_padSample[pad] || g_padSampleLen[pad] <= 0) return;
  ensureDrumFs();
  if (SD.exists(PAD_FILE[pad])) SD.remove(PAD_FILE[pad]);
  File f = SD.open(PAD_FILE[pad], FILE_WRITE);
  if (!f) return;
  f.write((uint8_t *)g_padSample[pad], g_padSampleLen[pad] * 2);
  f.close();
}

static void clearPadSample(int pad) {
  if (pad < 0 || pad > 3) return;
  if (g_padSample[pad]) { free(g_padSample[pad]); g_padSample[pad] = nullptr; }
  g_padSampleLen[pad] = 0;
  ensureDrumFs();
  if (SD.exists(PAD_FILE[pad])) SD.remove(PAD_FILE[pad]);
  hap(80, 40);
}

static void loadAllPads() {
  ensureDrumFs();
  for (int i = 0; i < 4; i++) loadPadFromSd(i);
}

static int autoTrim(int16_t *buf, int n) {
  if (n < 16) return n;
  int peak = 0;
  for (int i = 0; i < n; i++) {
    int a = buf[i] < 0 ? -buf[i] : buf[i];
    if (a > peak) peak = a;
  }
  int thr = peak / 20;
  if (thr < 400) thr = 400;
  int start = 0;
  while (start < n) {
    int a = buf[start] < 0 ? -buf[start] : buf[start];
    if (a >= thr) break;
    start++;
  }
  start -= 32;
  if (start < 0) start = 0;
  if (start > 0) {
    int keep = n - start;
    memmove(buf, buf + start, keep * sizeof(int16_t));
    n = keep;
  }
  int end = n - 1;
  while (end > 0) {
    int a = buf[end] < 0 ? -buf[end] : buf[end];
    if (a >= thr / 2) break;
    end--;
  }
  end += 48;
  if (end > n) end = n;
  return end;
}

// Lightweight one-shot: no multi-second delay chains
static void playPad(Pad p, bool recordIntoLoop) {
  // Free I2S from mic so speaker can run
  if (M5.Mic.isEnabled()) M5.Mic.end();
  if (!M5.Speaker.isEnabled()) M5.Speaker.begin();
  M5.Speaker.setVolume(220);

  if (g_padSample[p] && g_padSampleLen[p] > 40) {
    // Mono raw @ 16k — playRaw is DMA; do not delay the whole buffer
    M5.Speaker.playRaw(g_padSample[p], (size_t)g_padSampleLen[p], 16000, false);
    if (p == PAD_KICK) kickSubHaptic();
    else if (p == PAD_SNARE) hap(90, 22);
  } else {
    // Synth fallback — short single tones (Speaker queues)
    switch (p) {
      case PAD_KICK:
        M5.Speaker.tone(55, 100);
        kickSubHaptic();
        break;
      case PAD_SNARE:
        M5.Speaker.tone(200, 25);
        M5.Speaker.tone(3200, 40);
        hap(80, 18);
        break;
      case PAD_HAT_C:
        M5.Speaker.tone(9000, 12);
        break;
      case PAD_HAT_O:
        M5.Speaker.tone(7500, 45);
        break;
    }
  }

  if (recordIntoLoop && g_loopOn && g_loopN < LOOP_MAX) {
    uint32_t at = (millis() - g_loopStart) % g_loopLenMs;
    g_loopEv[g_loopN] = (uint8_t)p;
    g_loopAt[g_loopN] = (uint16_t)at;
    g_loopN++;
  }
  g_micRestoreAt = millis() + 180; // let sample start, then free speaker for mic
}

static void serviceMicSpeaker() {
  if (g_micRestoreAt && millis() >= g_micRestoreAt) {
    g_micRestoreAt = 0;
    if (g_mode != MODE_DRUM || g_tempoMode) {
      // non-drum always wants mic
    }
    if (g_mode != MODE_DRUM) {
      M5.Speaker.end();
      if (!M5.Mic.isEnabled()) M5.Mic.begin();
    }
  }
}


static void recordPadSample(int pad) {
  const int nMax = 7200;
  int16_t *buf = (int16_t *)heap_caps_malloc(nMax * sizeof(int16_t), MALLOC_CAP_8BIT);
  if (!buf) buf = (int16_t *)malloc(nMax * sizeof(int16_t));
  if (!buf) return;

  M5.Speaker.end();
  delay(5);
  M5.Mic.begin();
  hap(60, 20);
  canvas.fillSprite(rgb565(20, 10, 30));
  canvas.setTextColor(rgb565(255, 220, 80));
  canvas.setTextSize(2);
  canvas.setCursor(90, 100);
  canvas.print("REC in 3...");
  canvas.pushSprite(0, 0);
  delay(350);
  canvas.setCursor(90, 100);
  canvas.print("REC in 2...");
  canvas.pushSprite(0, 0);
  delay(350);
  canvas.setCursor(90, 100);
  canvas.print("REC in 1...");
  canvas.pushSprite(0, 0);
  delay(300);
  canvas.fillSprite(rgb565(80, 10, 10));
  canvas.setCursor(110, 100);
  canvas.print("RECORD!");
  canvas.pushSprite(0, 0);
  hap(120, 30);

  int got = 0;
  uint32_t t0 = millis();
  while (got < nMax && millis() - t0 < 500) {
    int chunk = nMax - got;
    if (chunk > 256) chunk = 256;
    if (M5.Mic.record(buf + got, chunk, 16000)) got += chunk;
    else delay(1);
    hapService();
  }

  int trimmed = autoTrim(buf, got);
  if (trimmed < 64) {
    free(buf);
    canvas.fillSprite(rgb565(40, 10, 10));
    canvas.setTextSize(1);
    canvas.setCursor(80, 110);
    canvas.print("too quiet — try again");
    canvas.pushSprite(0, 0);
    delay(600);
    return;
  }

  if (g_padSample[pad]) free(g_padSample[pad]);
  g_padSample[pad] = buf;
  g_padSampleLen[pad] = trimmed;
  savePadToSd(pad);

  hap(160, 50);
  M5.Mic.end();
  delay(5);
  M5.Speaker.begin();
  M5.Speaker.setVolume(200);
  M5.Speaker.playRaw(g_padSample[pad], g_padSampleLen[pad], 16000, false);
  delay(80);
  M5.Speaker.end();
}

static void serviceLoop() {
  if (!g_loopOn || g_loopN == 0) return;
  uint32_t elapsed = (millis() - g_loopStart) % g_loopLenMs;
  g_loopPulse = (float)elapsed / (float)g_loopLenMs;
  static uint32_t lastE = 0;
  if (elapsed < lastE) g_loopPlayI = 0;
  lastE = elapsed;
  while (g_loopPlayI < g_loopN && g_loopAt[g_loopPlayI] <= elapsed + 15) {
    if (g_loopAt[g_loopPlayI] + 30 >= elapsed) {
      Pad p = (Pad)g_loopEv[g_loopPlayI];
      playPad(p, false);
    }
    g_loopPlayI++;
  }
  if (g_loopPlayI >= g_loopN && elapsed < 50) g_loopPlayI = 0;
}

static void modeDrum() {
  canvas.fillSprite(rgb565(10, 8, 18));

  float phase = -1.f;
  if (g_tempoMode) {
    // blink to current BPM
    float beatMs = 60000.f / g_bpm;
    phase = fmodf((float)millis(), beatMs) / beatMs;
  } else if (g_loopOn) {
    phase = g_loopPulse;
  }

  const uint16_t baseCols[4] = {
    rgb565(0, 160, 150), rgb565(180, 30, 160),
    rgb565(70, 200, 35), rgb565(35, 70, 150)
  };
  const int pw = W / 2, ph = (H - 20) / 2;

  for (int i = 0; i < 4; i++) {
    int px = (i % 2) * pw;
    int py = 12 + (i / 2) * ph;
    float boost = 0.f;
    if (phase >= 0.f) {
      if (g_tempoMode) {
        // all pads pulse together in tempo edit
        float d = phase;
        if (d > 0.5f) d = 1.f - d;
        boost = (1.f - d * 2.f) * 0.5f;
        if (boost < 0) boost = 0;
      } else {
        float beat = fmodf(phase * 4.f, 4.f);
        float d = fabsf(beat - (float)i);
        if (d > 2.f) d = 4.f - d;
        float hit = 1.f - d;
        if (hit < 0) hit = 0;
        boost = (i == 0) ? hit * 0.55f : hit * 0.28f;
        float down = 1.f - fabsf(phase) * 8.f;
        if (down < 0) down = 0;
        boost = fmaxf(boost, down * 0.2f);
      }
    }
    uint8_t r = ((baseCols[i] >> 11) & 0x1F) << 3;
    uint8_t g = ((baseCols[i] >> 5) & 0x3F) << 2;
    uint8_t b = (baseCols[i] & 0x1F) << 3;
    r = (uint8_t)fminf(255.f, r + boost * 120.f);
    g = (uint8_t)fminf(255.f, g + boost * 120.f);
    b = (uint8_t)fminf(255.f, b + boost * 80.f);
    uint16_t c = rgb565(r, g, b);
    if (g_padArmed[i] && ((millis() / 200) & 1))
      c = rgb565(255, 255, 120);
    canvas.fillRoundRect(px + 4, py + 4, pw - 8, ph - 8, 10, c);
    if (boost > 0.15f)
      canvas.drawRoundRect(px + 8, py + 8, pw - 16, ph - 16, 8,
                           rgb565(255, 255, (uint8_t)(180 + boost * 75)));
    canvas.setTextColor(rgb565(15, 12, 25));
    canvas.setTextSize(2);
    canvas.setCursor(px + pw / 2 - 24, py + ph / 2 - 8);
    canvas.print(PAD_NAME[i]);
    canvas.setTextSize(1);
    if (g_padSample[i]) {
      canvas.setCursor(px + 12, py + ph - 22);
      canvas.print("SMP");
    }
  }

  if (g_tempoMode) {
    canvas.fillRoundRect(60, 100, 200, 36, 6, rgb565(30, 20, 50));
    canvas.setTextColor(rgb565(255, 220, 100));
    canvas.setTextSize(1);
    canvas.setCursor(75, 112);
    canvas.printf("TEMPO  %.0f BPM  tap pads  B=ok", g_bpm);
  }
}


static void modeMantis() {
  // Dancing splash mantis — pose from audio + IMU
  float bounce = g_level * 18.f + g_peak * 10.f;
  float sway = g_lookX * 25.f;
  float bob = sinf(g_t * (3.f + g_level * 8.f)) * (6.f + g_level * 14.f);
  int cx = W / 2 + (int)sway;
  int cy = H / 2 + (int)(bob - bounce * 0.3f) - 10;

  // reactive floor glow
  for (int i = 0; i < 5; i++) {
    int gy = H - 20 - i * 4;
    canvas.drawFastHLine(40, gy, W - 80,
      hsv565(g_hue + i * 20.f, 0.7f, 0.05f + g_level * 0.12f));
  }

  // scale draw with dance squash
  float squash = 1.f - g_peak * 0.15f;
  int ox = cx - MANTIS_W / 2;
  int oy = cy - (int)(MANTIS_H * squash / 2);
  for (int y = 0; y < MANTIS_H; y++) {
    int dy = oy + (int)(y * squash);
    if (dy < 14 || dy >= H - 14) continue;
    // limb flail via horizontal shear from audio
    int shear = (int)(sinf(g_t * 5.f + y * 0.08f) * g_level * 8.f);
    for (int x = 0; x < MANTIS_W; x++) {
      uint16_t col = mantis_splash[y * MANTIS_W + x];
      if (!col) continue;
      int dx = ox + x + shear;
      if (dx >= 0 && dx < W) canvas.drawPixel(dx, dy, col);
    }
  }

  if (g_mantisSing) {
    // Mic band avatar (lightweight FFT-ish energy bins)
    const int bins = 16;
    float band[16];
    for (int b = 0; b < bins; b++) band[b] = 0;
    for (int i = 0; i < MIC_N; i++) {
      int b = i * bins / MIC_N;
      band[b] += fabsf((float)g_mic[i]);
    }
    for (int b = 0; b < bins; b++) {
      float e = band[b] / (MIC_N / bins) / 8000.f;
      if (e > 1.2f) e = 1.2f;
      int h = (int)(e * 50.f);
      int bx = 20 + b * 18;
      canvas.fillRect(bx, H - 18 - h, 14, h,
                      hsv565(g_hue + b * 15.f, 0.9f, 0.4f + e * 0.5f));
    }
    // mouth glow
    canvas.fillCircle(cx, cy + (int)(30 * squash), 4 + (int)(g_level * 8),
                      hsv565(0, 0.8f, 0.5f + g_peak * 0.5f));
  }
}

static void drawChrome() {
  static const char *names[] = {"SWARM", "EYE", "TUNNEL", "PULSE", "DRUM", "MANTIS"};
  canvas.setTextSize(1);
  canvas.setTextColor(hsv565(g_hue, 0.7f, 0.9f));
  canvas.setCursor(4, 2);
  canvas.printf("SYNAPSE  %s", names[g_mode]);

  canvas.setCursor(100, 2);
  canvas.setTextColor(rgb565(160, 150, 180));
  switch (g_mode) {
    case MODE_SWARM: {
      const char *sv[] = {"flock", "orbit", "chaos"};
      canvas.printf("[B] %s", sv[g_swarmVar]);
      break;
    }
    case MODE_EYE:
      canvas.printf("[B] track:%s", g_eyeTrack ? "ON" : "off");
      break;
    case MODE_TUNNEL: {
      const char *tm[] = {"dive", "recede", "fractal", "portal"};
      canvas.printf("[B] %s", tm[g_tunnelMode]);
      break;
    }
    case MODE_PULSE: {
      const char *pp[] = {"wave", "mirror", "star", "ribbon", "chaos"};
      canvas.printf("[B] %s", pp[g_pulsePat]);
      break;
    }
    case MODE_MANTIS:
      canvas.printf("[B] %s", g_mantisSing ? "sing" : "dance");
      break;
    case MODE_DRUM: {
      if (g_tempoMode) canvas.print("[B] set tempo");
      else {
        bool anyArm = g_padArmed[0] || g_padArmed[1] || g_padArmed[2] || g_padArmed[3];
        canvas.printf("[B] %s", anyArm ? "REC" : (g_loopOn ? "STOP" : "LOOP"));
      }
      break;
    }
    default: break;
  }

  int lw = (int)(g_level * 70.f);
  if (lw > 70) lw = 70;
  canvas.drawRect(W - 78, 2, 74, 7, rgb565(40, 40, 55));
  canvas.fillRect(W - 77, 3, lw, 5, hsv565(g_hue + g_level * 40.f, 0.9f, 0.85f));

  canvas.setTextColor(rgb565(90, 100, 120));
  canvas.setCursor(8, H - 11);
  canvas.print("< mode");
  canvas.setCursor(90, H - 11);
  if (g_imuOk)
    canvas.printf("imu %.2f %.2f", g_lookX, g_lookY);
  else
    canvas.print("imu --");
  canvas.setCursor(250, H - 11);
  canvas.print("mode >");
}

static void nextMode(int dir) {
  int m = (int)g_mode + dir;
  if (m < 0) m = MODE_COUNT - 1;
  if (m >= MODE_COUNT) m = 0;
  g_mode = (Mode)m;
  g_tempoMode = false;
  hap(100, 25);
  if (g_mode != MODE_DRUM) {
    M5.Speaker.end();
    if (!M5.Mic.isEnabled()) M5.Mic.begin();
  }
}

static void btnBShort() {
  switch (g_mode) {
    case MODE_SWARM:
      g_swarmVar = (SwarmVar)((g_swarmVar + 1) % SV_COUNT);
      hap(90, 20);
      break;
    case MODE_EYE:
      g_eyeTrack = !g_eyeTrack;
      hap(90, 20);
      break;
    case MODE_TUNNEL:
      g_tunnelMode = (TunnelMode)((g_tunnelMode + 1) % TM_COUNT);
      g_portalCombo = 0;
      hap(100, 25);
      break;
    case MODE_PULSE:
      g_pulsePat = (PulsePat)((g_pulsePat + 1) % PP_COUNT);
      hap(90, 20);
      break;
    case MODE_MANTIS:
      g_mantisSing = !g_mantisSing;
      hap(90, 20);
      break;
    case MODE_DRUM: {
      if (g_tempoMode) {
        g_tempoMode = false;
        recomputeLoopLen();
        hap(120, 40);
        break;
      }
      int armed = -1;
      for (int i = 0; i < 4; i++) if (g_padArmed[i]) { armed = i; break; }
      if (armed >= 0) {
        recordPadSample(armed);
        g_padArmed[armed] = false;
      } else if (g_loopOn) {
        g_loopOn = false;
        g_loopN = 0;
        g_loopPlayI = 0;
        hap(120, 40);
      } else {
        g_loopOn = true;
        g_loopN = 0;
        g_loopStart = millis();
        g_loopPlayI = 0;
        recomputeLoopLen();
        hap(80, 25);
      }
      break;
    }
    default: break;
  }
}

static void btnBLong() {
  if (g_mode != MODE_DRUM) return;
  g_tempoMode = true;
  g_tempoTapN = 0;
  g_bpm = 120.f;
  recomputeLoopLen();
  hap(150, 50);
}

static void handleInput() {
  M5.update();
  hapService();

  // B long-press detection
  if (M5.BtnB.isPressed()) {
    if (g_btnBDown == 0) g_btnBDown = millis();
    else if (!g_btnBLong && millis() - g_btnBDown > 650) {
      g_btnBLong = true;
      btnBLong();
    }
  }
  if (M5.BtnB.wasReleased()) {
    if (!g_btnBLong && g_btnBDown && millis() - g_lastBtn > 160)
      btnBShort();
    g_btnBDown = 0;
    g_btnBLong = false;
    g_lastBtn = millis();
  }

  if (millis() - g_lastBtn > 180) {
    if (M5.BtnA.wasPressed()) { nextMode(-1); g_lastBtn = millis(); }
    if (M5.BtnC.wasPressed()) { nextMode(1); g_lastBtn = millis(); }
  }

  auto td = M5.Touch.getDetail();
  if (g_mode == MODE_DRUM) {
    if (td.wasPressed()) {
      int col = td.x < W / 2 ? 0 : 1;
      int row = td.y < (12 + (H - 20) / 2) ? 0 : 1;
      int id = row * 2 + col;
      if (id < 0 || id > 3) return;

      if (g_tempoMode) {
        // tap tempo
        uint32_t now = millis();
        if (g_tempoTapN > 0 && now - g_tempoTaps[g_tempoTapN - 1] > 2000)
          g_tempoTapN = 0;
        if (g_tempoTapN < 8) g_tempoTaps[g_tempoTapN++] = now;
        if (g_tempoTapN >= 2) {
          float sum = 0;
          int cnt = 0;
          for (int i = 1; i < g_tempoTapN; i++) {
            float dt = (float)(g_tempoTaps[i] - g_tempoTaps[i - 1]);
            if (dt > 200.f && dt < 2000.f) { sum += dt; cnt++; }
          }
          if (cnt > 0) {
            float avg = sum / cnt;
            g_bpm = 60000.f / avg;
            if (g_bpm < 40.f) g_bpm = 40.f;
            if (g_bpm > 240.f) g_bpm = 240.f;
            recomputeLoopLen();
          }
        }
        hap(60, 15);
        return;
      }

      // armed pad tapped again → clear sample + cancel arm
      if (g_padArmed[id]) {
        clearPadSample(id);
        g_padArmed[id] = false;
        g_padHoldId = -1;
        return;
      }

      g_padHoldId = id;
      g_padHoldStart = millis();
      playPad((Pad)id, true);
    }
    if (td.isPressed() && g_padHoldId >= 0 && !g_tempoMode) {
      if (millis() - g_padHoldStart > 3000) {
        for (int i = 0; i < 4; i++) g_padArmed[i] = (i == g_padHoldId);
        g_padHoldId = -1;
        hap(140, 60);
      }
    }
    if (td.wasReleased()) g_padHoldId = -1;
  } else if (td.isPressed() && td.y > H - 18) {
    if (td.x < 100 && millis() - g_lastBtn > 200) { nextMode(-1); g_lastBtn = millis(); }
    if (td.x > 220 && millis() - g_lastBtn > 200) { nextMode(1); g_lastBtn = millis(); }
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;
  cfg.internal_mic = true;
  cfg.internal_spk = true;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  // IMU is initialized by M5.begin when present (any Core2 revision).
  // Optional: load factory/user calibration from NVS if available.
  if (M5.Imu.isEnabled()) {
    M5.Imu.loadOffsetFromNVS();
    for (int i = 0; i < 15; i++) { M5.Imu.update(); delay(4); }
  }
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  srand((unsigned)esp_random());
  seedParticles();
  recomputeLoopLen();
  splash();
  loadAllPads();
  M5.Mic.begin();
}

void loop() {
  handleInput();
  serviceMicSpeaker();

  if (g_mode != MODE_DRUM) {
    sampleAudio();
    sampleImu();
  } else {
    sampleImu();
    if (!g_tempoMode) serviceLoop();
  }

  g_t += 0.03f + g_level * 0.02f;
  g_hue += 0.12f + g_level * 0.08f;
  if (g_hue >= 360.f) g_hue -= 360.f;

  canvas.fillSprite(rgb565(8, 4, 16));

  switch (g_mode) {
    case MODE_SWARM:  modeSwarm(); break;
    case MODE_EYE:    modeEye(); break;
    case MODE_TUNNEL: modeTunnel(); break;
    case MODE_PULSE:  modePulse(); break;
    case MODE_DRUM:   modeDrum(); break;
    case MODE_MANTIS: modeMantis(); break;
    default: break;
  }
  drawChrome();
  canvas.pushSprite(0, 0);
}
