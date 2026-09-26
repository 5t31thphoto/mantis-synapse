// ============================================================
//  SYNAPSE — mantis psychedelic fidget for M5Stack Core2
//  Mic + IMU + touch + haptic via M5Unified. Double-buffered.
// ============================================================
#include <M5Unified.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const int W = 320, H = 240;
static const int N_PART = 64;
static const int MIC_N = 128;
static const int LOOP_MAX = 48;

enum Mode : uint8_t {
  MODE_SWARM = 0, MODE_EYE, MODE_TUNNEL, MODE_PULSE, MODE_DRUM, MODE_COUNT
};

enum SwarmVar : uint8_t { SV_FLOCK = 0, SV_ORBIT, SV_CHAOS, SV_COUNT };

struct Particle {
  float x, y, vx, vy;
  float hue;
};

static M5Canvas canvas(&M5.Display);
static Particle g_p[N_PART];
static Mode g_mode = MODE_SWARM;
static SwarmVar g_swarmVar = SV_FLOCK;
static float g_t = 0;
static float g_level = 0, g_peak = 0;
static float g_ax = 0, g_ay = 0, g_az = 1;
static float g_shake = 0;
static float g_hue = 160;
static uint32_t g_lastBtn = 0;
static int16_t g_mic[MIC_N];
static bool g_eyeTrack = true;
static bool g_tunnelWarp = true;
static bool g_pulseMirror = true;

// --- haptic non-blocking ---
static uint32_t g_hapUntil = 0;
static uint8_t g_hapLevel = 0;
static void hap(uint8_t level, uint16_t ms) {
  g_hapLevel = level;
  g_hapUntil = millis() + ms;
  M5.Power.setVibration(level);
}
static void hapService() {
  if (g_hapLevel && millis() >= g_hapUntil) {
    M5.Power.setVibration(0);
    g_hapLevel = 0;
  }
}

// --- drum machine ---
enum Pad : uint8_t { PAD_HAT_C = 0, PAD_HAT_O, PAD_KICK, PAD_SNARE };
static const char *PAD_NAME[] = {"CH", "OH", "KICK", "SNR"};
static int16_t *g_padSample[4] = {nullptr, nullptr, nullptr, nullptr};
static int g_padSampleLen[4] = {0, 0, 0, 0};
static bool g_padArmed[4] = {false, false, false, false};
static uint32_t g_padHoldStart = 0;
static int g_padHoldId = -1;
static bool g_loopOn = false;
static uint8_t g_loopEv[LOOP_MAX];   // pad id
static uint16_t g_loopAt[LOOP_MAX];  // ms from loop start
static int g_loopN = 0;
static uint32_t g_loopStart = 0;
static uint32_t g_loopLenMs = 2000;
static int g_loopPlayI = 0;
static float g_loopPulse = 0; // 0..1 visual

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

static void seedParticles() {
  for (int i = 0; i < N_PART; i++) {
    g_p[i].x = (float)(rand() % W);
    g_p[i].y = (float)(rand() % H);
    g_p[i].vx = ((rand() % 100) - 50) * 0.03f;
    g_p[i].vy = ((rand() % 100) - 50) * 0.03f;
    g_p[i].hue = g_hue + (rand() % 60) - 30;
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
  if (g_level > 0.65f) hap(70, 25);
}

static void sampleImu() {
  if (!M5.Imu.isEnabled()) return;
  auto d = M5.Imu.getImuData();
  // Core2: accel in g; map so tilting top of screen toward you moves eye up
  g_ax = g_ax * 0.8f + d.accel.x * 0.2f;
  g_ay = g_ay * 0.8f + d.accel.y * 0.2f;
  g_az = g_az * 0.8f + d.accel.z * 0.2f;
  float mag = sqrtf(d.accel.x * d.accel.x + d.accel.y * d.accel.y + d.accel.z * d.accel.z);
  float sh = fabsf(mag - 1.0f);
  g_shake = g_shake * 0.85f + sh * 0.15f;
  if (g_shake > 0.55f) {
    g_hue += 25.f;
    if (g_hue >= 360.f) g_hue -= 360.f;
    hap(160, 40);
    for (int i = 0; i < N_PART; i++) {
      g_p[i].vx += ((rand() % 100) - 50) * 0.06f;
      g_p[i].vy += ((rand() % 100) - 50) * 0.06f;
    }
  }
}

// --- startup jingle ---
static void playStartup() {
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(180);
  const int seq[][2] = {
    {523, 60}, {659, 60}, {784, 60}, {1047, 120}, {0, 40},
    {784, 50}, {1047, 160}
  };
  for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
    if (seq[i][0] == 0) delay(seq[i][1]);
    else {
      M5.Speaker.tone(seq[i][0], seq[i][1]);
      delay(seq[i][1] + 15);
    }
  }
  M5.Speaker.stop();
  M5.Speaker.end();
  M5.Mic.begin();
}

// Simple pixel mantis for splash (silhouette)
static void drawMantisSplash(M5Canvas &c, int ox, int oy, float scale) {
  auto px = [&](int x, int y, uint16_t col) {
    int sx = ox + (int)(x * scale), sy = oy + (int)(y * scale);
    int s = (int)scale;
    if (s < 1) s = 1;
    c.fillRect(sx, sy, s, s, col);
  };
  uint16_t g1 = rgb565(40, 200, 70);
  uint16_t g2 = rgb565(20, 140, 50);
  uint16_t purp = rgb565(120, 40, 200);
  // body
  for (int y = 12; y < 36; y++)
    for (int x = 10; x < 22; x++)
      if ((x - 16) * (x - 16) + (y - 24) * (y - 24) / 4 < 40) px(x, y, g1);
  // head
  for (int y = 4; y < 14; y++)
    for (int x = 12; x < 20; x++)
      if ((x - 16) * (x - 16) + (y - 9) * (y - 9) < 20) px(x, y, g1);
  // eyes
  px(13, 8, purp); px(14, 8, purp); px(13, 9, purp);
  px(18, 8, purp); px(19, 8, purp); px(18, 9, purp);
  // antennae
  for (int i = 0; i < 8; i++) {
    px(12 - i / 2, 4 - i, g2);
    px(20 + i / 2, 4 - i, g2);
  }
  // arms folded
  for (int i = 0; i < 10; i++) {
    px(8 - i / 3, 14 + i / 2, g2);
    px(24 + i / 3, 14 + i / 2, g2);
  }
}

static void splash() {
  canvas.fillSprite(rgb565(6, 2, 14));
  drawMantisSplash(canvas, 100, 40, 3.5f);
  canvas.setTextSize(2);
  canvas.setTextColor(hsv565(160, 0.85f, 0.95f));
  canvas.setCursor(95, 175);
  canvas.print("SYNAPSE");
  canvas.setTextSize(1);
  canvas.setTextColor(rgb565(140, 160, 180));
  canvas.setCursor(70, 200);
  canvas.print("tilt · touch · make noise");
  canvas.pushSprite(0, 0);
  playStartup();
  delay(400);
}

// ========== MODES ==========
static void modeSwarm() {
  float gx = -g_ay * 0.4f;
  float gy = g_ax * 0.4f;
  float pulse = 0.5f + g_level * 1.2f;

  for (int i = 0; i < N_PART; i++) {
    Particle &p = g_p[i];
    float cx = W * 0.5f, cy = H * 0.5f;

    if (g_swarmVar == SV_FLOCK) {
      float dx = cx - p.x, dy = cy - p.y;
      p.vx += dx * 0.0015f * pulse + gx * 0.02f;
      p.vy += dy * 0.0015f * pulse + gy * 0.02f;
      // mild separation
      for (int j = i + 1; j < N_PART; j += 3) {
        float sx = p.x - g_p[j].x, sy = p.y - g_p[j].y;
        float d2 = sx * sx + sy * sy;
        if (d2 < 400.f && d2 > 1.f) {
          p.vx += sx * 0.002f;
          p.vy += sy * 0.002f;
        }
      }
    } else if (g_swarmVar == SV_ORBIT) {
      float dx = p.x - cx, dy = p.y - cy;
      p.vx += -dy * 0.004f * pulse + gx * 0.02f;
      p.vy += dx * 0.004f * pulse + gy * 0.02f;
      p.vx += -dx * 0.0008f;
      p.vy += -dy * 0.0008f;
    } else { // CHAOS
      p.vx += gx * 0.05f + sinf(g_t + i) * 0.08f * g_level;
      p.vy += gy * 0.05f + cosf(g_t * 1.3f + i) * 0.08f * g_level;
    }

    p.vx *= 0.96f;
    p.vy *= 0.96f;
    p.x += p.vx;
    p.y += p.vy;
    if (p.x < 2) { p.x = 2; p.vx *= -0.7f; }
    if (p.x > W - 3) { p.x = W - 3; p.vx *= -0.7f; }
    if (p.y < 14) { p.y = 14; p.vy *= -0.7f; }
    if (p.y > H - 16) { p.y = H - 16; p.vy *= -0.7f; }

    float h = p.hue + g_level * 60.f + g_t * 8.f;
    int r = 1 + (int)(g_level * 3.f);
    canvas.fillCircle((int)p.x, (int)p.y, r, hsv565(h, 0.85f, 0.5f + g_level * 0.45f));
  }

  // touch cursor as small mantis-ish chevron (not antennae)
  auto td = M5.Touch.getDetail();
  if (td.isPressed() && td.y > 16 && td.y < H - 18) {
    int tx = td.x, ty = td.y;
    uint16_t mc = hsv565(g_hue + 40, 0.9f, 0.95f);
    canvas.fillTriangle(tx, ty - 6, tx - 5, ty + 4, tx + 5, ty + 4, mc);
    for (int i = 0; i < N_PART; i++) {
      float dx = g_p[i].x - tx, dy = g_p[i].y - ty;
      if (dx * dx + dy * dy < 2500) {
        g_p[i].vx += dx * 0.015f;
        g_p[i].vy += dy * 0.015f;
      }
    }
  }
}

static void modeEyeFixed() {
  float lookX = g_eyeTrack ? constrain(-g_ay * 90.f, -70.f, 70.f) : 0;
  float lookY = g_eyeTrack ? constrain(g_ax * 70.f, -50.f, 50.f) : 0;
  int cx = W / 2 + (int)lookX;
  int cy = H / 2 + (int)lookY;
  for (int ring = 6; ring >= 0; ring--) {
    float d = 18.f + ring * 16.f + g_level * 12.f;
    uint16_t c = hsv565(g_hue + ring * 25.f + g_t * 15.f, 0.7f, 0.12f + ring * 0.04f + g_level * 0.15f);
    canvas.drawCircle(cx, cy, (int)d, c);
  }
  int er = 42 + (int)(g_level * 10);
  canvas.fillCircle(cx, cy, er, hsv565(g_hue + 180, 0.15f, 0.85f));
  int ir = 22 + (int)(g_peak * 8);
  canvas.fillCircle(cx, cy, ir, hsv565(g_hue + 40, 0.85f, 0.55f + g_level * 0.3f));
  int px = cx + (int)(lookX * 0.15f);
  int py = cy + (int)(lookY * 0.15f);
  int pr = 10 + (int)(g_level * 5.f);
  canvas.fillCircle(px, py, pr, rgb565(8, 6, 12));
  canvas.fillCircle(px - 3, py - 3, 3, rgb565(220, 230, 255));
}

static void modeTunnel() {
  static float z = 0;
  z += 0.07f + g_level * 0.2f;
  float spin = g_t * 0.35f + (g_tunnelWarp ? g_ay * 0.8f : 0);
  int cx = W / 2 + (int)(g_ay * -25.f);
  int cy = H / 2 + (int)(g_ax * 20.f);
  for (int ring = 14; ring >= 0; ring--) {
    float zz = fmodf(z + ring * 0.4f, 7.f);
    float sc = 10.f / (zz + 0.5f);
    int rad = (int)(sc * 20.f);
    uint16_t c = hsv565(g_hue + ring * 14.f + g_t * 20.f, 0.9f, 0.2f + g_level * 0.45f);
    for (int k = 0; k < 6; k++) {
      float a0 = spin + k * (float)M_PI / 3.f;
      float a1 = spin + (k + 1) * (float)M_PI / 3.f;
      canvas.drawLine(
        cx + (int)(cosf(a0) * rad), cy + (int)(sinf(a0) * rad * 0.85f),
        cx + (int)(cosf(a1) * rad), cy + (int)(sinf(a1) * rad * 0.85f), c);
    }
  }
}

static void modePulse() {
  // true mic waveform ring — not a fixed trefoil
  int cx = W / 2, cy = H / 2;
  int prevx = cx, prevy = cy;
  for (int i = 0; i < MIC_N; i++) {
    float ang = (float)i / MIC_N * 2.f * (float)M_PI + g_t * 0.5f;
    float sample = fabsf((float)g_mic[i]) / 16000.f;
    float r = 28.f + sample * 70.f + g_level * 25.f;
    if (g_pulseMirror) r += sinf(ang * 3.f + g_t) * 8.f * g_peak;
    int x = cx + (int)(cosf(ang) * r);
    int y = cy + (int)(sinf(ang) * r * 0.9f);
    uint16_t c = hsv565(g_hue + i * 2.f + g_t * 30.f, 0.9f, 0.35f + sample);
    if (i > 0) canvas.drawLine(prevx, prevy, x, y, c);
    prevx = x;
    prevy = y;
  }
  int core = 8 + (int)(g_level * 20);
  canvas.fillCircle(cx, cy, core, hsv565(g_hue + g_t * 40.f, 0.8f, 0.5f + g_level * 0.4f));
}

// --- drum synth ---
static void drumTone(Pad p) {
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(200);
  switch (p) {
    case PAD_KICK:
      hap(220, 70);
      M5.Speaker.tone(55, 90);
      delay(45);
      break;
    case PAD_SNARE:
      hap(110, 25);
      M5.Speaker.tone(220, 20);
      M5.Speaker.tone(2000, 35);
      delay(25);
      break;
    case PAD_HAT_C:
      M5.Speaker.tone(6500, 18);
      delay(12);
      break;
    case PAD_HAT_O:
      M5.Speaker.tone(4200, 55);
      delay(30);
      break;
  }
  M5.Speaker.stop();
  M5.Speaker.end();
  // restore mic for other modes when leaving drum — caller handles
}

static void playPad(Pad p, bool recordIntoLoop) {
  // play custom sample if present
  if (g_padSample[p] && g_padSampleLen[p] > 0) {
    M5.Mic.end();
    M5.Speaker.begin();
    M5.Speaker.setVolume(200);
    M5.Speaker.playRaw(g_padSample[p], g_padSampleLen[p], 16000, false);
    if (p == PAD_KICK) hap(200, 70);
    else if (p == PAD_SNARE) hap(90, 25);
    delay(40);
    M5.Speaker.end();
  } else {
    drumTone(p);
  }
  if (recordIntoLoop && g_loopOn && g_loopN < LOOP_MAX) {
    uint32_t at = millis() - g_loopStart;
    if (at > g_loopLenMs) at %= g_loopLenMs;
    g_loopEv[g_loopN] = (uint8_t)p;
    g_loopAt[g_loopN] = (uint16_t)at;
    g_loopN++;
  }
}

static void recordPadSample(int pad) {
  // ~0.35s mono 16k
  const int n = 5600;
  int16_t *buf = (int16_t *)heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_8BIT);
  if (!buf) buf = (int16_t *)malloc(n * sizeof(int16_t));
  if (!buf) return;
  M5.Speaker.end();
  M5.Mic.begin();
  hap(80, 30);
  // record in chunks
  int got = 0;
  while (got < n) {
    int chunk = min(256, n - got);
    if (M5.Mic.record(buf + got, chunk, 16000)) got += chunk;
    else break;
    hapService();
  }
  if (g_padSample[pad]) free(g_padSample[pad]);
  g_padSample[pad] = buf;
  g_padSampleLen[pad] = got;
  hap(150, 50);
  // confirmation beep
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.tone(1200, 40);
  delay(50);
  M5.Speaker.end();
}

static void serviceLoop() {
  if (!g_loopOn || g_loopN == 0) return;
  uint32_t elapsed = (millis() - g_loopStart) % g_loopLenMs;
  g_loopPulse = (float)elapsed / (float)g_loopLenMs;
  // play any events near current time (simple scan)
  static uint32_t lastE = 0;
  if (elapsed < lastE) {
    // wrapped
    g_loopPlayI = 0;
  }
  lastE = elapsed;
  while (g_loopPlayI < g_loopN && g_loopAt[g_loopPlayI] <= elapsed + 15) {
    if (g_loopAt[g_loopPlayI] + 30 >= elapsed) {
      // fire without re-recording into loop
      Pad p = (Pad)g_loopEv[g_loopPlayI];
      if (g_padSample[p] && g_padSampleLen[p] > 0) {
        M5.Mic.end();
        M5.Speaker.begin();
        M5.Speaker.setVolume(180);
        M5.Speaker.playRaw(g_padSample[p], g_padSampleLen[p], 16000, false);
        if (p == PAD_KICK) hap(180, 60);
        delay(20);
        M5.Speaker.end();
      } else {
        drumTone(p);
      }
    }
    g_loopPlayI++;
  }
  if (g_loopPlayI >= g_loopN && elapsed < 50) g_loopPlayI = 0;
}

static void modeDrum() {
  // loop pulse background — 1 bright + 3 dim
  float phase = g_loopOn ? g_loopPulse : fmodf(g_t * 0.25f, 1.f);
  for (int beat = 0; beat < 4; beat++) {
    float target = beat * 0.25f;
    float d = fabsf(phase - target);
    if (d > 0.5f) d = 1.f - d;
    float bright = (beat == 0) ? (1.f - d * 4.f) : (0.35f - d * 2.f);
    if (bright < 0) bright = 0;
    if (g_loopOn && bright > 0.05f) {
      uint16_t c = hsv565(200 + beat * 20, 0.6f, bright * (beat == 0 ? 0.35f : 0.12f));
      canvas.fillRect(0, 0, W, H, c); // will be overdrawn by pads — use as wash
    }
  }
  // actually draw subtle bars at bottom instead of full wash to avoid flicker feel
  canvas.fillSprite(rgb565(8, 6, 14));
  if (g_loopOn) {
    for (int beat = 0; beat < 4; beat++) {
      float target = beat * 0.25f;
      float d = fabsf(phase - target);
      if (d > 0.5f) d = 1.f - d;
      float bright = (beat == 0) ? (1.f - d * 5.f) : (0.4f - d * 3.f);
      if (bright < 0) bright = 0;
      int bh = (int)(bright * (beat == 0 ? 28 : 14));
      uint16_t c = hsv565(180 + beat * 30, 0.8f, 0.3f + bright * 0.7f);
      canvas.fillRect(20 + beat * 75, H - 40 - bh, 50, bh, c);
    }
  }

  // 4 pads
  const uint16_t cols[4] = {
    rgb565(0, 180, 160),   // teal CH
    rgb565(200, 40, 180),  // magenta OH
    rgb565(80, 220, 40),   // lime kick
    rgb565(40, 80, 160)    // deep blue snare
  };
  const int pw = W / 2, ph = (H - 20) / 2;
  for (int i = 0; i < 4; i++) {
    int px = (i % 2) * pw;
    int py = 12 + (i / 2) * ph;
    uint16_t c = cols[i];
    if (g_padArmed[i] && ((millis() / 200) & 1))
      c = rgb565(255, 255, 100);
    canvas.fillRoundRect(px + 4, py + 4, pw - 8, ph - 8, 10, c);
    canvas.setTextColor(rgb565(10, 10, 20));
    canvas.setTextSize(2);
    canvas.setCursor(px + pw / 2 - 24, py + ph / 2 - 8);
    canvas.print(PAD_NAME[i]);
    if (g_padSample[i]) {
      canvas.setTextSize(1);
      canvas.setCursor(px + 12, py + ph - 22);
      canvas.print("SMP");
    }
  }
}

static void drawChrome() {
  static const char *names[] = {"SWARM", "EYE", "TUNNEL", "PULSE", "DRUM"};
  canvas.setTextSize(1);
  canvas.setTextColor(hsv565(g_hue, 0.7f, 0.9f));
  canvas.setCursor(4, 2);
  canvas.printf("SYNAPSE  %s", names[g_mode]);

  // B-button hint
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
    case MODE_TUNNEL:
      canvas.printf("[B] warp:%s", g_tunnelWarp ? "ON" : "off");
      break;
    case MODE_PULSE:
      canvas.printf("[B] mirror:%s", g_pulseMirror ? "ON" : "off");
      break;
    case MODE_DRUM: {
      bool anyArm = g_padArmed[0] || g_padArmed[1] || g_padArmed[2] || g_padArmed[3];
      canvas.printf("[B] %s", anyArm ? "REC" : (g_loopOn ? "STOP" : "LOOP"));
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
  canvas.setCursor(250, H - 11);
  canvas.print("mode >");
}

static void nextMode(int dir) {
  int m = (int)g_mode + dir;
  if (m < 0) m = MODE_COUNT - 1;
  if (m >= MODE_COUNT) m = 0;
  g_mode = (Mode)m;
  hap(100, 25);
  if (g_mode != MODE_DRUM) {
    M5.Speaker.end();
    if (!M5.Mic.isEnabled()) M5.Mic.begin();
  }
}

static void btnB() {
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
      g_tunnelWarp = !g_tunnelWarp;
      hap(90, 20);
      break;
    case MODE_PULSE:
      g_pulseMirror = !g_pulseMirror;
      hap(90, 20);
      break;
    case MODE_DRUM: {
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
        hap(80, 25);
      }
      break;
    }
    default: break;
  }
}

static void handleInput() {
  M5.update();
  hapService();

  if (millis() - g_lastBtn > 180) {
    if (M5.BtnA.wasPressed()) { nextMode(-1); g_lastBtn = millis(); }
    if (M5.BtnC.wasPressed()) { nextMode(1); g_lastBtn = millis(); }
    if (M5.BtnB.wasPressed()) { btnB(); g_lastBtn = millis(); }
  }

  auto td = M5.Touch.getDetail();
  if (g_mode == MODE_DRUM) {
    if (td.wasPressed()) {
      int col = td.x < W / 2 ? 0 : 1;
      int row = td.y < (12 + (H - 20) / 2) ? 0 : 1;
      int id = row * 2 + col;
      if (id >= 0 && id < 4) {
        g_padHoldId = id;
        g_padHoldStart = millis();
        playPad((Pad)id, true);
      }
    }
    if (td.isPressed() && g_padHoldId >= 0) {
      if (millis() - g_padHoldStart > 3000) {
        // arm for record
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
  M5.Imu.begin();
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  srand((unsigned)esp_random());
  seedParticles();
  splash();
  M5.Mic.begin();
}

void loop() {
  handleInput();

  if (g_mode != MODE_DRUM) {
    sampleAudio();
    sampleImu();
  } else {
    sampleImu(); // still allow shake
    serviceLoop();
  }

  g_t += 0.03f + g_level * 0.02f;
  g_hue += 0.12f;
  if (g_hue >= 360.f) g_hue -= 360.f;

  // clear only via sprite (one push = no tear/flicker)
  canvas.fillSprite(rgb565(8, 4, 16));

  switch (g_mode) {
    case MODE_SWARM:  modeSwarm(); break;
    case MODE_EYE:    modeEyeFixed(); break;
    case MODE_TUNNEL: modeTunnel(); break;
    case MODE_PULSE:  modePulse(); break;
    case MODE_DRUM:   modeDrum(); break;
    default: break;
  }
  drawChrome();
  canvas.pushSprite(0, 0);
}
