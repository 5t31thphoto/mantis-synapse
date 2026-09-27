// ============================================================
//  SYNAPSE — mantis sigil · Core2
// ============================================================
#include <M5Unified.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <SD.h>
#include <SPI.h>
#include "mantis_splash.h"
#include "mantis_parts.h"

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
static float g_fracPanX = 0, g_fracPanY = 0;
static float g_fracZoom = 1.f;
static int16_t g_touchPrevX = -1, g_touchPrevY = -1;
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
  // Cheap reactive wash — a few bands + sparks, not a full-screen pixel field
  float pulse = 0.08f + g_level * 0.18f + g_peak * 0.12f;
  for (int i = 0; i < 12; i++) {
    float yy = 16.f + fmodf(i * 19.f + g_t * (12.f + g_level * 40.f) + g_lookY * 30.f, (float)(H - 30));
    float bri = pulse * (0.5f + 0.5f * sinf(i + g_t * 2.f + g_level * 3.f));
    canvas.drawFastHLine(0, (int)yy, W, hsv565(g_hue + i * 18.f + g_t * 15.f, 0.7f, bri));
  }
  for (int i = 0; i < 10; i++) {
    float xx = fmodf(i * 37.f + g_t * (8.f + g_peak * 25.f) + g_lookX * 40.f, (float)W);
    float bri = 0.06f + g_peak * 0.15f * (0.5f + 0.5f * sinf(i * 1.7f + g_t));
    canvas.drawFastVLine((int)xx, 14, H - 28, hsv565(g_hue + 80.f + i * 12.f, 0.6f, bri));
  }
  // audio sparks
  int n = 4 + (int)(g_level * 12.f);
  for (int i = 0; i < n; i++) {
    int sx = (int)fmodf(i * 97.f + g_t * 50.f * (1.f + g_peak), (float)W);
    int sy = 16 + (int)fmodf(i * 53.f + g_t * 30.f, (float)(H - 32));
    canvas.fillCircle(sx, sy, 1 + (i & 1), hsv565(g_hue + i * 30.f, 0.9f, 0.35f + g_peak * 0.5f));
  }
}




static void drawTinyMantis(M5Canvas &c, int cx, int cy) {
  const int sc = 5;
  int dw = MANTIS_W / sc, dh = MANTIS_H / sc;
  int ox = cx - dw / 2, oy = cy - dh / 2;
  for (int y = 0; y < dh; y++) {
    for (int x = 0; x < dw; x++) {
      uint16_t col = mantis_splash[(y * sc) * MANTIS_W + (x * sc)];
      if (!col) continue;
      c.drawPixel(ox + x, oy + y, col);
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
  auto imu_update = M5.Imu.update();
  if (!imu_update) return;
  auto data = M5.Imu.getImuData();
  g_imuOk = true;

  float ax = data.accel.x, ay = data.accel.y, az = data.accel.z;
  g_ax = g_ax * 0.7f + ax * 0.3f;
  g_ay = g_ay * 0.7f + ay * 0.3f;
  g_az = g_az * 0.7f + az * 0.3f;
  g_gx = data.gyro.x;
  g_gy = data.gyro.y;
  g_gz = data.gyro.z;

  // Target from tilt (board frame)
  float targetX = -g_ay;
  float targetY =  g_ax;
  // Deadzone so resting doesn't crawl
  if (fabsf(targetX) < 0.08f) targetX = 0;
  if (fabsf(targetY) < 0.08f) targetY = 0;

  // Light gyro assist only when moving
  float gmag = fabsf(g_gx) + fabsf(g_gy);
  if (gmag > 8.f) {
    g_lookX += g_gy * 0.0025f;
    g_lookY += g_gx * 0.0025f;
  }

  // Spring toward tilt target — strong settle when near level
  float still = (fabsf(targetX) + fabsf(targetY) < 0.15f) ? 0.22f : 0.12f;
  g_lookX = g_lookX * (1.f - still) + targetX * still;
  g_lookY = g_lookY * (1.f - still) + targetY * still;
  // Extra recenter when board is flat
  if (fabsf(g_ax) < 0.12f && fabsf(g_ay) < 0.12f && fabsf(g_az - 1.f) < 0.2f) {
    g_lookX *= 0.9f;
    g_lookY *= 0.9f;
  }
  if (g_lookX > 1.1f) g_lookX = 1.1f;
  if (g_lookX < -1.1f) g_lookX = -1.1f;
  if (g_lookY > 1.1f) g_lookY = 1.1f;
  if (g_lookY < -1.1f) g_lookY = -1.1f;

  float mag = sqrtf(ax * ax + ay * ay + az * az);
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
      if (!col) continue;
      canvas.drawPixel(ox + x, oy + y, col);
    }
  }
  canvas.setTextSize(2);
  canvas.setTextColor(hsv565(160, 0.85f, 0.95f));
  canvas.setCursor((W - 8 * 12) / 2, oy + MANTIS_H + 8);
  canvas.print("SYNAPSE");
  canvas.setTextSize(1);
  canvas.setTextColor(rgb565(120, 180, 160));
  canvas.setCursor(88, oy + MANTIS_H + 30);
  canvas.print("a small green god");
  canvas.pushSprite(0, 0);
  playStartup();
  delay(450);
}

// ========== SWARM ==========
static void modeSwarm() {
  drawPsyBg();
  drawTrails();

  // Gravity ONLY in chaos mode (X sign fixed for gravity alone)
  float gx = 0.f, gy = 0.f;
  if (g_swarmVar == SV_CHAOS) {
    gx = g_lookX * 0.85f;
    gy = g_lookY * 0.85f;
  }
  float pulse = 0.45f + g_level * 1.5f;
  float soundHue = g_hue + g_level * 100.f + g_peak * 40.f;

  // Metaballs: strong in flock/chaos, weak in orbit
  float metaR = (g_swarmVar == SV_ORBIT) ? 220.f : 500.f;
  float metaPull = (g_swarmVar == SV_ORBIT) ? 0.0004f : 0.0015f;
  for (int i = 0; i < N_PART; i++) {
    for (int j = i + 1; j < N_PART; j += 3) {
      float dx = g_p[i].x - g_p[j].x;
      float dy = g_p[i].y - g_p[j].y;
      float d2 = dx * dx + dy * dy;
      if (d2 < metaR && d2 > 1.f) {
        float mx = (g_p[i].x + g_p[j].x) * 0.5f;
        float my = (g_p[i].y + g_p[j].y) * 0.5f;
        float tt = 1.f - d2 / metaR;
        int br = 2 + (int)(tt * (g_swarmVar == SV_ORBIT ? 4.f : 8.f) * (0.5f + g_level));
        uint16_t bc = hsv565(soundHue + i + j, 0.75f, 0.2f + tt * 0.4f);
        canvas.fillCircle((int)mx, (int)my, br, bc);
        g_p[i].vx -= dx * metaPull * tt;
        g_p[i].vy -= dy * metaPull * tt;
        g_p[j].vx += dx * metaPull * tt;
        g_p[j].vy += dy * metaPull * tt;
      }
    }
  }

  for (int i = 0; i < N_PART; i++) {
    Particle &p = g_p[i];
    float cx = W * 0.5f + sinf(g_t * 0.6f + i * 0.1f) * 22.f * g_peak;
    float cy = H * 0.5f + cosf(g_t * 0.5f) * 16.f * g_level;

    if (g_swarmVar == SV_FLOCK) {
      float dx = cx - p.x, dy = cy - p.y;
      p.vx += dx * 0.0022f * pulse;  // no IMU gravity
      p.vy += dy * 0.0022f * pulse;
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
      p.vx += -dy * 0.006f * pulse;
      p.vy += dx * 0.006f * pulse;
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
  static float fcx = -0.743643887037151f;
  static float fcy = 0.131825904205312f;

  float speed = 0.12f + g_level * 0.25f + g_peak * 0.1f;

  // DIVE into hole vs RECEDE out
  if (g_tunnelMode == TM_DIVE || g_tunnelMode == TM_PORTAL || g_tunnelMode == TM_FRACTAL)
    z -= speed;
  else
    z += speed;

  float spin = g_t * 0.3f + g_lookX * 1.4f;
  int cx = W / 2 + (int)(g_lookX * -50.f);
  int cy = H / 2 + (int)(g_lookY * 42.f);

  auto td = M5.Touch.getDetail();
  if (td.isPressed() && td.y > 16 && td.y < H - 16) {
    if (g_touchPrevX >= 0) {
      float dx = (float)(td.x - g_touchPrevX);
      float dy = (float)(td.y - g_touchPrevY);
      if (g_tunnelMode == TM_FRACTAL) {
        g_fracPanX -= dx * 0.0035f / g_fracZoom;
        g_fracPanY -= dy * 0.0035f / g_fracZoom;
      } else {
        g_lookX += dx * 0.004f;
        g_lookY += dy * 0.004f;
      }
    }
    g_touchPrevX = td.x;
    g_touchPrevY = td.y;
  } else {
    g_touchPrevX = -1;
    g_touchPrevY = -1;
  }

  if (g_tunnelMode == TM_FRACTAL) {
    fcx += g_lookX * 0.00008f / g_fracZoom + g_fracPanX * 0.02f;
    fcy += g_lookY * 0.00008f / g_fracZoom + g_fracPanY * 0.02f;
    g_fracPanX *= 0.85f;
    g_fracPanY *= 0.85f;
    g_fracZoom *= 1.f + 0.012f + g_level * 0.01f;
    if (g_fracZoom > 1e5f) g_fracZoom *= 0.1f;

    const int step = 2;
    const int maxIt = 18;
    for (int iy = 14; iy < H - 14; iy += step) {
      for (int ix = 0; ix < W; ix += step) {
        float u = (ix - W * 0.5f) / (0.28f * W * g_fracZoom) + fcx;
        float v = (iy - H * 0.5f) / (0.28f * H * g_fracZoom) + fcy;
        float zr = 0.f, zi = 0.f;
        int k;
        for (k = 0; k < maxIt; k++) {
          float zr2 = zr * zr - zi * zi + u;
          zi = 2.f * zr * zi + v;
          zr = zr2;
          if (zr * zr + zi * zi > 4.f) break;
        }
        uint16_t col;
        if (k >= maxIt) col = rgb565(0, 0, 0);
        else {
          float mu = (float)k + g_t * 0.5f + g_level * 4.f;
          col = hsv565(g_hue + mu * 14.f + logf(g_fracZoom) * 8.f, 0.9f,
                       0.25f + 0.5f * (k / (float)maxIt) + g_peak * 0.15f);
        }
        canvas.fillRect(ix, iy, step, step, col);
      }
    }
    canvas.drawCircle(cx, cy, 10, hsv565(g_hue + 40.f, 0.5f, 0.7f));
    canvas.drawLine(cx - 5, cy, cx + 5, cy, rgb565(255, 255, 200));
    canvas.drawLine(cx, cy - 5, cx, cy + 5, rgb565(255, 255, 200));
  } else {
    drawPsyBg();
    const float period = 14.f;
    const int rings = 16;
    for (int ring = 0; ring < rings; ring++) {
      float depth = fmodf(fabsf(z) + ring * (period / rings), period);
      if (depth < 0.15f) depth = 0.15f;
      float rad = (150.f + g_level * 50.f + g_peak * 20.f * sinf(ring + g_t * 3.f)) / depth;
      float spinR = spin + g_level * 0.4f * sinf(ring * 0.5f + g_t);
      int sides = 5 + ((int)(g_peak * 4.f + ring * 0.3f) % 4);
      uint16_t c = hsv565(g_hue + ring * 13.f + g_t * 20.f + depth * 6.f,
                          0.85f, 0.2f + (1.f - depth / period) * 0.55f);
      for (int k = 0; k < sides; k++) {
        float a0 = spinR + k * (2.f * (float)M_PI / sides);
        float a1 = spinR + (k + 1) * (2.f * (float)M_PI / sides);
        float w0 = 1.f + g_level * 0.25f * sinf(a0 * 3.f + g_t * 2.f);
        float w1 = 1.f + g_level * 0.25f * sinf(a1 * 3.f + g_t * 2.f);
        canvas.drawLine(
          cx + (int)(cosf(a0) * rad * w0), cy + (int)(sinf(a0) * rad * w0 * 0.8f),
          cx + (int)(cosf(a1) * rad * w1), cy + (int)(sinf(a1) * rad * w1 * 0.8f), c);
      }
    }

    if (g_tunnelMode == TM_PORTAL) {
      if (millis() - g_portalSpawn > 1800) {
        g_portalSpawn = millis();
        g_portalX = (float)((rand() % 140) - 70);
        g_portalY = (float)((rand() % 90) - 45);
        g_portalZ = 5.5f;
      }
      g_portalZ -= 0.11f + g_level * 0.06f;
      float sc = 90.f / (g_portalZ + 0.4f);
      int px = cx + (int)(g_portalX * 0.5f);
      int py = cy + (int)(g_portalY * 0.5f);
      for (int r = 0; r < 4; r++)
        canvas.drawCircle(px, py, (int)sc - r * 3,
                          hsv565(300 + r * 15.f + g_t * 40.f, 1.f, 0.55f + g_peak * 0.35f));
      canvas.drawLine(cx - 6, cy, cx + 6, cy, rgb565(255, 255, 140));
      canvas.drawLine(cx, cy - 6, cx, cy + 6, rgb565(255, 255, 140));
      float dx = (float)(px - cx), dy = (float)(py - cy);
      if (g_portalZ < 1.3f) {
        if (dx * dx + dy * dy < 28.f * 28.f) {
          g_portalCombo++;
          g_portalSpawn = 0;
          hap(130, 35);
        } else if (g_portalZ < 0.35f) {
          g_portalCombo = 0;
          g_portalSpawn = 0;
        }
      }
      if (g_portalCombo > 0) {
        canvas.setTextColor(hsv565(280, 0.7f, 0.85f));
        canvas.setCursor(W / 2 - 10, 16);
        canvas.printf("%d", g_portalCombo);
      }
    }

    canvas.fillCircle(cx, cy, 2 + (int)(g_level * 8),
                      hsv565(g_hue + g_t * 50.f, 1.f, 0.5f + g_peak * 0.4f));
  }
}

static void modePulse() {
  drawPsyBg();

  int cx = W / 2 + (int)(g_lookX * -18.f);
  int cy = H / 2 + (int)(g_lookY * 14.f);

  auto samp = [&](int i) -> float {
    return fabsf((float)g_mic[i % MIC_N]) / 15000.f;
  };

  if (g_pulsePat == PP_WAVE || g_pulsePat == PP_MIRROR) {
    for (int layer = 0; layer < 3; layer++) {
      int prevx = cx, prevy = cy;
      for (int i = 0; i <= MIC_N; i += 2) {
        float ang = (float)i / MIC_N * 2.f * (float)M_PI + g_t * (0.4f + layer * 0.15f);
        float s = samp(i + layer * 17);
        float r = 20.f + layer * 12.f + s * (55.f + g_level * 40.f) + g_peak * 15.f;
        if (g_pulsePat == PP_MIRROR) r += sinf(ang * (3 + layer) + g_t) * 12.f * s;
        int x = cx + (int)(cosf(ang) * r);
        int y = cy + (int)(sinf(ang) * r * 0.88f);
        if (i > 0)
          canvas.drawLine(prevx, prevy, x, y,
            hsv565(g_hue + layer * 40.f + i * 2.f, 0.9f, 0.35f + s * 0.55f));
        prevx = x; prevy = y;
      }
    }
  } else if (g_pulsePat == PP_STAR) {
    for (int arm = 0; arm < 10; arm++) {
      float base = arm * (2.f * (float)M_PI / 10.f) + g_t * 0.5f + g_lookX * 0.5f;
      int prevx = cx, prevy = cy;
      for (int i = 1; i < 40; i++) {
        float tt = i / 40.f;
        float s = samp(i * 3 + arm);
        float r = tt * (40.f + s * 70.f + g_level * 35.f);
        float ang = base + tt * 0.6f * sinf(g_t + arm);
        int x = cx + (int)(cosf(ang) * r);
        int y = cy + (int)(sinf(ang) * r * 0.9f);
        canvas.drawLine(prevx, prevy, x, y, hsv565(g_hue + arm * 25.f + tt * 60.f, 1.f, 0.35f + s));
        prevx = x; prevy = y;
      }
    }
  } else if (g_pulsePat == PP_RIBBON) {
    for (int layer = 0; layer < 4; layer++) {
      int prevx = 0, prevy = H / 2;
      for (int x = 0; x < W; x += 3) {
        float s = samp((x + layer * 30) % MIC_N);
        float y = H * 0.5f
          + sinf(x * 0.04f + g_t * (1.5f + layer * 0.35f) + g_lookY * 2.f)
            * (25.f + g_level * 45.f + s * 55.f)
          + cosf(x * 0.02f - g_t + layer) * 12.f
          + g_lookX * 15.f * (layer - 1.5f);
        canvas.drawLine(prevx, prevy, x, (int)y,
          hsv565(g_hue + layer * 35.f + x * 0.4f, 0.85f, 0.3f + s * 0.6f));
        prevx = x; prevy = (int)y;
      }
    }
  } else {
    for (int i = 0; i < MIC_N; i += 2) {
      float ang = (float)i / MIC_N * 4.f * (float)M_PI + g_t * (1.5f + g_level) + g_lookX;
      float s = samp(i);
      float r = 15.f + s * 90.f + sinf(g_t * 4.f + i * 0.3f) * 20.f * g_peak;
      int x = cx + (int)(cosf(ang) * r);
      int y = cy + (int)(sinf(ang * 1.2f + g_lookY) * r * 0.85f);
      canvas.fillCircle(x, y, 1 + (int)(s * 4), hsv565(g_hue + i * 4.f, 1.f, 0.4f + s));
    }
  }
  canvas.fillCircle(cx, cy, 6 + (int)(g_level * 16),
                    hsv565(g_hue + g_t * 50.f, 0.85f, 0.45f + g_peak * 0.4f));
}

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
  if (n < 32) return n;
  int peak = 1;
  for (int i = 0; i < n; i++) {
    int a = abs((int)buf[i]);
    if (a > peak) peak = a;
  }
  // 8% of peak — cut real lead-in silence
  int thr = peak / 12;
  if (thr < 300) thr = 300;

  int start = 0;
  while (start < n - 16) {
    int a = abs((int)buf[start]);
    if (a >= thr) break;
    start++;
  }
  // keep a few ms of pre-roll
  start -= 48;
  if (start < 0) start = 0;

  int end = n - 1;
  while (end > start + 16) {
    int a = abs((int)buf[end]);
    if (a >= thr / 3) break;
    end--;
  }
  end += 64;
  if (end > n) end = n;

  int keep = end - start;
  if (start > 0 && keep > 0)
    memmove(buf, buf + start, keep * sizeof(int16_t));
  return keep > 0 ? keep : n;
}

static void playPad(Pad p, bool recordIntoLoop) {
  // Non-blocking as possible: don't thrash Mic/Speaker every hit
  if (M5.Mic.isEnabled()) M5.Mic.end();
  if (!M5.Speaker.isEnabled()) {
    M5.Speaker.begin();
    M5.Speaker.setVolume(220);
  }

  if (g_padSample[p] && g_padSampleLen[p] > 40) {
    M5.Speaker.playRaw(g_padSample[p], (size_t)g_padSampleLen[p], 16000, false);
    if (p == PAD_KICK) kickSubHaptic();
    else if (p == PAD_SNARE) hap(90, 18);
  } else {
    switch (p) {
      case PAD_KICK:
        M5.Speaker.tone(55, 90);
        kickSubHaptic();
        break;
      case PAD_SNARE:
        M5.Speaker.tone(200, 20);
        M5.Speaker.tone(3200, 35);
        hap(70, 15);
        break;
      case PAD_HAT_C:
        M5.Speaker.tone(9000, 10);
        break;
      case PAD_HAT_O:
        M5.Speaker.tone(7500, 35);
        break;
    }
  }

  if (recordIntoLoop && g_loopOn && g_loopN < LOOP_MAX) {
    uint32_t at = (millis() - g_loopStart) % g_loopLenMs;
    // insert sorted by time so playback stays ordered
    int pos = g_loopN;
    while (pos > 0 && g_loopAt[pos - 1] > (uint16_t)at) {
      g_loopEv[pos] = g_loopEv[pos - 1];
      g_loopAt[pos] = g_loopAt[pos - 1];
      pos--;
    }
    g_loopEv[pos] = (uint8_t)p;
    g_loopAt[pos] = (uint16_t)at;
    g_loopN++;
  }
  g_micRestoreAt = millis() + 150;
}

static void serviceLoop() {
  if (!g_loopOn || g_loopN <= 0) return;

  uint32_t now = millis();
  uint32_t elapsed = (now - g_loopStart) % g_loopLenMs;
  g_loopPulse = (float)elapsed / (float)g_loopLenMs;

  // Edge-detect each event once per cycle — NEVER clear the loop here
  static uint32_t prevElapsed = 0;
  static uint32_t armedMask = 0; // bits: which events still need to fire this cycle

  bool wrapped = elapsed < prevElapsed;
  if (wrapped || armedMask == 0) {
    // new cycle — re-arm all events (loop continues until user stops)
    if (g_loopN >= 32) armedMask = 0xFFFFFFFFu;
    else armedMask = (1u << g_loopN) - 1u;
  }

  for (int i = 0; i < g_loopN; i++) {
    if (!(armedMask & (1u << i))) continue;
    uint16_t at = g_loopAt[i];
    bool cross = false;
    if (wrapped) {
      // fired if in the tail after prevElapsed OR in the head up to elapsed
      if (at >= prevElapsed || at <= elapsed + 12) cross = true;
    } else {
      if (prevElapsed < at && elapsed + 12 >= at) cross = true;
      // also catch if we jumped past it
      if (prevElapsed < at && elapsed >= at) cross = true;
    }
    if (cross) {
      playPad((Pad)g_loopEv[i], false);
      armedMask &= ~(1u << i);
    }
  }
  prevElapsed = elapsed;
}

static void modeDrum() {
  // FULL SCREEN beat pulse when loop is active — the whole stage breathes
  float phase = g_loopOn ? g_loopPulse : (g_tempoMode
      ? fmodf((float)millis(), 60000.f / g_bpm) / (60000.f / g_bpm)
      : -1.f);

  float beatFlash = 0.f;
  if (phase >= 0.f) {
    // 4 beats per loop: sharp attack, soft decay
    float b = fmodf(phase * 4.f, 1.f);
    beatFlash = (1.f - b) * (1.f - b); // bright on each beat
    // stronger on downbeat
    int which = (int)(phase * 4.f) % 4;
    if (which == 0) beatFlash *= 1.f;
    else beatFlash *= 0.55f;
  }

  // Base + full-screen pulse wash
  uint8_t br = (uint8_t)(12 + beatFlash * 50.f);
  uint8_t bg = (uint8_t)(8 + beatFlash * 30.f);
  uint8_t bb = (uint8_t)(20 + beatFlash * 70.f);
  canvas.fillSprite(rgb565(br, bg, bb));
  if (beatFlash > 0.15f) {
    // expanding ring from center on the beat
    int rad = (int)(20 + beatFlash * 140.f);
    canvas.drawCircle(W / 2, H / 2, rad, hsv565(g_hue + beatFlash * 40.f, 0.6f, beatFlash * 0.7f));
    canvas.drawCircle(W / 2, H / 2, rad / 2, hsv565(g_hue + 80.f, 0.5f, beatFlash * 0.4f));
  }

  const uint16_t baseCols[4] = {
    rgb565(0, 160, 150), rgb565(180, 30, 160),
    rgb565(70, 200, 35), rgb565(35, 70, 150)
  };
  const int pw = W / 2, ph = (H - 20) / 2;

  for (int i = 0; i < 4; i++) {
    int px = (i % 2) * pw;
    int py = 12 + (i / 2) * ph;
    uint16_t c = baseCols[i];
    if (g_padArmed[i] && ((millis() / 200) & 1))
      c = rgb565(255, 255, 120);
    // slight lift on beat without being the metronome itself
    if (beatFlash > 0.5f) {
      uint8_t r = ((c >> 11) & 0x1F) << 3;
      uint8_t g = ((c >> 5) & 0x3F) << 2;
      uint8_t b = (c & 0x1F) << 3;
      r = (uint8_t)fminf(255.f, r + beatFlash * 40.f);
      g = (uint8_t)fminf(255.f, g + beatFlash * 40.f);
      b = (uint8_t)fminf(255.f, b + beatFlash * 30.f);
      c = rgb565(r, g, b);
    }
    canvas.fillRoundRect(px + 6, py + 6, pw - 12, ph - 12, 10, c);
    canvas.setTextColor(rgb565(15, 12, 25));
    canvas.setTextSize(2);
    canvas.setCursor(px + pw / 2 - 24, py + ph / 2 - 8);
    canvas.print(PAD_NAME[i]);
    canvas.setTextSize(1);
    if (g_padSample[i]) {
      canvas.setCursor(px + 14, py + ph - 24);
      canvas.print("SMP");
    }
  }

  if (g_tempoMode) {
    canvas.fillRoundRect(60, 98, 200, 36, 8, rgb565(20, 12, 36));
    canvas.setTextColor(hsv565(160, 0.5f, 0.9f));
    canvas.setTextSize(1);
    canvas.setCursor(78, 110);
    canvas.printf("tempo  %.0f   tap · B holds", g_bpm);
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
  canvas.print("breathe...");
  canvas.pushSprite(0, 0);
  delay(350);
  canvas.setCursor(90, 100);
  canvas.print("listen...");
  canvas.pushSprite(0, 0);
  delay(350);
  canvas.setCursor(90, 100);
  canvas.print("now...");
  canvas.pushSprite(0, 0);
  delay(300);
  canvas.fillSprite(rgb565(80, 10, 10));
  canvas.setCursor(110, 100);
  canvas.print("capture");
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
    canvas.print("too quiet");
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
  uint32_t now = millis();
  uint32_t elapsed = (now - g_loopStart) % g_loopLenMs;
  g_loopPulse = (float)elapsed / (float)g_loopLenMs;

  // Fire each recorded hit once per cycle using absolute cycle index
  static uint32_t lastCycle = 0;
  uint32_t cycle = (now - g_loopStart) / g_loopLenMs;
  if (cycle != lastCycle) {
    lastCycle = cycle;
    // reset played flags via play index
    g_loopPlayI = 0;
  }
  while (g_loopPlayI < g_loopN) {
    uint16_t at = g_loopAt[g_loopPlayI];
    // window: event time reached this cycle and not far past
    if (elapsed + 8 >= at) {
      if (elapsed <= at + 40)
        playPad((Pad)g_loopEv[g_loopPlayI], false);
      g_loopPlayI++;
    } else break;
  }
}

static void modeDrum() {
  // Background base
  canvas.fillSprite(rgb565(12, 8, 20));

  // VISUAL METRONOME — background only, never triggers audio or pad lights
  if (g_loopOn || g_tempoMode) {
    float phase = g_tempoMode
      ? fmodf((float)millis(), 60000.f / g_bpm) / (60000.f / g_bpm)
      : g_loopPulse;
    // 4 beat marks across full width behind pads
    for (int beat = 0; beat < 4; beat++) {
      float center = (beat + 0.5f) / 4.f;
      float d = fabsf(phase - center);
      if (d > 0.5f) d = 1.f - d;
      float glow = 1.f - d * 6.f;
      if (glow < 0) glow = 0;
      // brighter on downbeat (beat 0)
      float bri = glow * (beat == 0 ? 0.45f : 0.22f);
      if (bri > 0.02f) {
        int x0 = beat * (W / 4);
        canvas.fillRect(x0, 14, W / 4, H - 28,
                        hsv565(200 + beat * 25.f, 0.5f, bri));
      }
    }
    // playhead line
    int phx = (int)(phase * (W - 1));
    canvas.drawFastVLine(phx, 14, H - 28, rgb565(255, 240, 120));
  }

  const uint16_t baseCols[4] = {
    rgb565(0, 160, 150), rgb565(180, 30, 160),
    rgb565(70, 200, 35), rgb565(35, 70, 150)
  };
  const int pw = W / 2, ph = (H - 20) / 2;

  for (int i = 0; i < 4; i++) {
    int px = (i % 2) * pw;
    int py = 12 + (i / 2) * ph;
    uint16_t c = baseCols[i];
    // arm blink only — NOT loop metronome
    if (g_padArmed[i] && ((millis() / 200) & 1))
      c = rgb565(255, 255, 120);
    canvas.fillRoundRect(px + 6, py + 6, pw - 12, ph - 12, 10, c);
    canvas.setTextColor(rgb565(15, 12, 25));
    canvas.setTextSize(2);
    canvas.setCursor(px + pw / 2 - 24, py + ph / 2 - 8);
    canvas.print(PAD_NAME[i]);
    canvas.setTextSize(1);
    if (g_padSample[i]) {
      canvas.setCursor(px + 14, py + ph - 24);
      canvas.print("SMP");
    }
  }

  if (g_tempoMode) {
    canvas.fillRoundRect(60, 98, 200, 36, 8, rgb565(20, 12, 36));
    canvas.setTextColor(hsv565(160, 0.5f, 0.9f));
    canvas.setTextSize(1);
    canvas.setCursor(78, 110);
    canvas.printf("tempo  %.0f   tap · B holds", g_bpm);
  }
}


// ax,ay = attach point as 0..1 within the sprite (where it joins the body)
static void blitPart(M5Canvas &c, const uint16_t *pix, int pw, int ph,
                     int wx, int wy, float ax = 0.5f, float ay = 0.5f) {
  int ox = wx - (int)(pw * ax);
  int oy = wy - (int)(ph * ay);
  for (int y = 0; y < ph; y++) {
    int dy = oy + y;
    if (dy < 14 || dy >= H - 14) continue;
    for (int x = 0; x < pw; x++) {
      uint16_t col = pix[y * pw + x];
      if (!col) continue;
      int dx = ox + x;
      if (dx >= 0 && dx < W) c.drawPixel(dx, dy, col);
    }
  }
}


// Articulated mantis — dances to live mic / music
static void modeMantis() {
  // Need mic for music reactivity (and sing lip-sync)
  if (!M5.Mic.isEnabled()) {
    M5.Speaker.end();
    M5.Mic.begin();
  }

  canvas.fillSprite(rgb565(8, 4, 18));

  // Onset / beat from mic: track level jumps
  static float prevLvl = 0;
  static float dancePhase = 0;
  static float armPhase = 0;
  float onset = g_level - prevLvl;
  if (onset < 0) onset = 0;
  prevLvl = g_level;

  // Phase advances with music energy — quiet = slow idle, loud = fast dance
  float tempo = 1.2f + g_level * 7.f + g_peak * 4.f + onset * 12.f;
  dancePhase += 0.04f * tempo;
  armPhase += 0.05f * (1.5f + g_level * 6.f + onset * 8.f);

  float beat = fabsf(sinf(dancePhase));
  float kick = g_level * 1.4f + g_peak * 1.2f + onset * 2.f;

  // stage reacts to music
  for (int i = 0; i < 8; i++) {
    int gy = H - 14 - i * 4;
    float bri = 0.04f + kick * 0.1f * (1.f - i / 8.f) + beat * 0.05f * g_level;
    canvas.drawFastHLine(20, gy, W - 40, hsv565(g_hue + i * 12.f, 0.65f, bri));
  }

  float hop = 0.f;
  if (!g_mantisSing)
    hop = g_peak * 18.f + onset * 22.f + beat * g_level * 10.f;
  else
    hop = g_peak * 5.f;

  float leanX = g_lookX * 24.f;
  int rootX = W / 2 + (int)leanX;
  int rootY = H / 2 + 28 - (int)hop;

  // Legs: frame index driven by dancePhase (music clock)
  int legFrameL = ((int)(dancePhase * 1.8f)) & 7;
  int legFrameR = ((int)(dancePhase * 1.8f + 4.f)) & 7;
  // on strong onset, jump a step
  if (onset > 0.12f) {
    legFrameL = (legFrameL + 2) & 7;
    legFrameR = (legFrameR + 2) & 7;
  }
  const uint16_t *legs[] = {
    part_leg0, part_leg1, part_leg2, part_leg3,
    part_leg4, part_leg5, part_leg6, part_leg7
  };
  const int legW[] = {
    PART_LEG0_W, PART_LEG1_W, PART_LEG2_W, PART_LEG3_W,
    PART_LEG4_W, PART_LEG5_W, PART_LEG6_W, PART_LEG7_W
  };
  const int legH[] = {
    PART_LEG0_H, PART_LEG1_H, PART_LEG2_H, PART_LEG3_H,
    PART_LEG4_H, PART_LEG5_H, PART_LEG6_H, PART_LEG7_H
  };

  // ---- body hierarchy (anchors = joint points) ----
  // root = hips
  const int hipsX = rootX;
  const int hipsY = rootY;

  // Legs: attach near TOP of leg sprite to hips (ay ~ 0.1)
  int spread = 18 + (int)(kick * 5.f);
  blitPart(canvas, legs[legFrameL], legW[legFrameL], legH[legFrameL],
           hipsX - spread, hipsY + 6, 0.5f, 0.12f);
  blitPart(canvas, legs[legFrameR], legW[legFrameR], legH[legFrameR],
           hipsX + spread, hipsY + 6, 0.5f, 0.12f);
  blitPart(canvas, legs[(legFrameL + 2) & 7], legW[(legFrameL + 2) & 7], legH[(legFrameL + 2) & 7],
           hipsX - 8, hipsY + 10, 0.5f, 0.12f);
  blitPart(canvas, legs[(legFrameR + 2) & 7], legW[(legFrameR + 2) & 7], legH[(legFrameR + 2) & 7],
           hipsX + 8, hipsY + 10, 0.5f, 0.12f);

  // Abdomen centered on hips, slightly up
  int abX = hipsX + (int)(sinf(dancePhase * 0.9f) * 6.f * (0.3f + kick));
  int abY = hipsY - 6;
  blitPart(canvas, part_abdomen, PART_ABDOMEN_W, PART_ABDOMEN_H, abX, abY, 0.5f, 0.55f);

  // Torso sits on abdomen
  int tX = hipsX + (int)(g_lookX * 4.f);
  int tY = abY - PART_ABDOMEN_H / 3 - 8 - (int)(g_peak * 3.f);
  blitPart(canvas, part_torso, PART_TORSO_W, PART_TORSO_H, tX, tY, 0.5f, 0.55f);

  // Shoulders = upper sides of torso
  int shY = tY - PART_TORSO_H / 5;
  int shL = tX - PART_TORSO_W / 3;
  int shR = tX + PART_TORSO_W / 3;

  // Arms — OUT poses attach at INNER edge so the scythe extends outward
  bool armsUp = (sinf(armPhase) > 0.15f) || (onset > 0.1f);
  if (g_mantisSing) armsUp = (g_level > 0.2f) || ((int)(armPhase) & 1);
  int armBob = (int)(sinf(armPhase * 2.f) * 4.f * (0.4f + kick));
  int armOut = 4 + (int)(kick * 6.f);  // push outward with energy

  if (armsUp) {
    // raised: attach near top-inner of arm sprite
    blitPart(canvas, part_arm_L_up, PART_ARM_L_UP_W, PART_ARM_L_UP_H,
             shL - armOut, shY + armBob, 0.72f, 0.28f);
    blitPart(canvas, part_arm_R_up, PART_ARM_R_UP_W, PART_ARM_R_UP_H,
             shR + armOut, shY - armBob, 0.28f, 0.28f);
  } else {
    // outstretched: left arm's RIGHT side docks to left shoulder → extends LEFT
    blitPart(canvas, part_arm_L_out, PART_ARM_L_OUT_W, PART_ARM_L_OUT_H,
             shL - armOut, shY + 6 + armBob, 0.88f, 0.4f);
    // right arm's LEFT side docks to right shoulder → extends RIGHT
    blitPart(canvas, part_arm_R_out, PART_ARM_R_OUT_W, PART_ARM_R_OUT_H,
             shR + armOut, shY + 6 - armBob, 0.12f, 0.4f);
  }

  // Head: bottom of head docks to neck (top of torso)
  int hX = tX + (int)(g_lookX * 10.f);
  int hY = tY - PART_TORSO_H / 3 - 2 - (int)(fabsf(sinf(dancePhase * 2.f)) * 3.f * (0.4f + g_level));
  blitPart(canvas, part_head, PART_HEAD_W, PART_HEAD_H, hX, hY, 0.5f, 0.88f);

  if (g_mantisSing) {
    float low = 0, mid = 0, high = 0;
    for (int i = 0; i < MIC_N; i++) {
      float a = fabsf((float)g_mic[i]);
      if (i < MIC_N / 4) low += a;
      else if (i < MIC_N / 2) mid += a;
      else high += a;
    }
    float inv = 4.f / (float)MIC_N / 6000.f;
    low *= inv; mid *= inv; high *= inv * 1.2f;
    static float sL = 0, sM = 0, sH = 0;
    sL = sL * 0.5f + low * 0.5f;
    sM = sM * 0.55f + mid * 0.45f;
    sH = sH * 0.6f + high * 0.4f;
    int mw = 5 + (int)(sM * 12.f);
    int mh = 2 + (int)(sL * 14.f);
    // Mouth locked to head sprite face: same attach as blitPart (ax=0.5, ay=0.88)
    // Head top-left in world:
    int headOx = hX - (int)(PART_HEAD_W * 0.5f);
    int headOy = hY - (int)(PART_HEAD_H * 0.88f);
    // Face mouth sits just below eyes — ~55% down the head sprite, centered
    int mx = headOx + PART_HEAD_W / 2;
    int my = headOy + (PART_HEAD_H * 55) / 100;
    canvas.fillEllipse(mx, my, mw + 1, mh + 1, rgb565(30, 110, 50));
    canvas.fillEllipse(mx, my, mw, mh, rgb565(12, 6, 18));
    if (mh > 5)
      canvas.fillEllipse(mx, my + mh / 4, mw / 2, mh / 3, rgb565(150, 45, 70));
    if (sH > 0.3f && mh > 4) {
      for (int ti = -2; ti <= 2; ti++)
        canvas.drawFastVLine(mx + ti * (mw / 3), my - mh / 2, 2, rgb565(220, 220, 200));
    }
  }
}

static void drawChrome() {
  // Product chrome — not a lab HUD
  static const char *names[] = {"swarm", "eye", "tunnel", "pulse", "drum", "mantis"};

  // Soft top fade for title
  for (int y = 0; y < 14; y++) {
    uint8_t a = (uint8_t)((14 - y) * 3);
    canvas.drawFastHLine(0, y, W, rgb565(a / 3, a / 5, a / 2));
  }

  canvas.setTextSize(1);
  canvas.setTextColor(hsv565(g_hue, 0.55f, 0.75f));
  canvas.setCursor(8, 3);
  canvas.print("synapse");
  canvas.setTextColor(hsv565(g_hue + 40.f, 0.4f, 0.55f));
  canvas.setCursor(64, 3);
  canvas.print(names[g_mode]);

  // Soft listening orb (not a VU meter)
  int orb = 2 + (int)(g_level * 5.f + g_peak * 3.f);
  if (orb > 8) orb = 8;
  canvas.fillCircle(W - 14, 7, orb, hsv565(g_hue + g_level * 60.f, 0.7f, 0.45f + g_level * 0.4f));
  canvas.drawCircle(W - 14, 7, 8, rgb565(40, 50, 60));

  // Bottom bar over button zones — A | B | C
  for (int y = H - 14; y < H; y++) {
    uint8_t a = (uint8_t)((y - (H - 14)) * 4);
    canvas.drawFastHLine(0, y, W, rgb565(a / 4, a / 6, a / 3));
  }

  // B action label — centered above physical B
  const char *bLabel = "";
  switch (g_mode) {
    case MODE_SWARM: {
      const char *sv[] = {"flock", "orbit", "chaos"};
      bLabel = sv[g_swarmVar];
      break;
    }
    case MODE_EYE:
      bLabel = g_eyeTrack ? "gaze" : "still";
      break;
    case MODE_TUNNEL: {
      const char *tm[] = {"dive", "recede", "fractal", "portal"};
      bLabel = tm[g_tunnelMode];
      break;
    }
    case MODE_PULSE: {
      const char *pp[] = {"wave", "mirror", "star", "ribbon", "chaos"};
      bLabel = pp[g_pulsePat];
      break;
    }
    case MODE_MANTIS:
      bLabel = g_mantisSing ? "sing" : "dance";
      break;
    case MODE_DRUM: {
      if (g_tempoMode) bLabel = "hold";
      else {
        bool anyArm = g_padArmed[0] || g_padArmed[1] || g_padArmed[2] || g_padArmed[3];
        bLabel = anyArm ? "rec" : (g_loopOn ? "stop" : "loop");
      }
      break;
    }
    default: break;
  }

  canvas.setTextColor(hsv565(g_hue, 0.35f, 0.55f));
  canvas.setCursor(10, H - 11);
  canvas.print("<");
  // center B label above button B
  int blen = 0;
  for (const char *q = bLabel; *q; q++) blen++;
  canvas.setTextColor(hsv565(g_hue + 20.f, 0.7f, 0.9f));
  canvas.setCursor(W / 2 - blen * 3, H - 11);
  canvas.print(bLabel);
  canvas.setTextColor(hsv565(g_hue, 0.35f, 0.55f));
  canvas.setCursor(W - 16, H - 11);
  canvas.print(">");
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

  if (g_mode == MODE_DRUM) {
    sampleImu();
    if (!g_tempoMode) serviceLoop();
  } else {
    sampleAudio();
    sampleImu();
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
