// ============================================================
//  SYNAPSE — praying-mantis psychedelic fidget for M5Stack Core2
//  Audio + IMU + touch + haptics. Modes: Swarm / Eye / Tunnel / Pulse
// ============================================================
#include <M5Unified.h>
#include <math.h>
#include <string.h>

static const int W = 320, H = 240;
static const int N_PART = 96;
static const int N_TRAIL = 48;
static const int MIC_N = 256;

enum Mode { MODE_SWARM = 0, MODE_EYE, MODE_TUNNEL, MODE_PULSE, MODE_COUNT };

struct Particle {
  float x, y, vx, vy;
  uint16_t hue;
  uint8_t life;
};

struct Trail {
  int16_t x, y;
  uint16_t c;
  uint8_t a;
};

static Particle g_p[N_PART];
static Trail g_trail[N_TRAIL];
static int g_trailI = 0;
static Mode g_mode = MODE_SWARM;
static float g_time = 0;
static float g_bass = 0, g_mid = 0, g_level = 0;
static float g_ax = 0, g_ay = 0, g_az = 1;
static float g_shake = 0;
static float g_hueBase = 120; // teal-ish
static bool g_frozen = false;
static uint32_t g_lastBtn = 0;
static int16_t g_mic[MIC_N];
static float g_pulsePhase = 0;
static float g_eyeBlink = 0;
static float g_tunnelZ = 0;
static bool g_micOn = false;

// RGB565 helpers
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static uint16_t hsv565(float h, float s, float v) {
  while (h < 0) h += 360;
  while (h >= 360) h -= 360;
  float c = v * s;
  float x = c * (1 - fabsf(fmodf(h / 60.f, 2.f) - 1));
  float m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else { r = c; b = x; }
  return rgb565((uint8_t)((r + m) * 255), (uint8_t)((g + m) * 255), (uint8_t)((b + m) * 255));
}

static void haptic(int ms = 30, int lvl = 160) {
  M5.Power.setVibration(lvl);
  delay(ms);
  M5.Power.setVibration(0);
}

static void seedParticles() {
  for (int i = 0; i < N_PART; i++) {
    g_p[i].x = (float)(rand() % W);
    g_p[i].y = (float)(rand() % H);
    g_p[i].vx = ((rand() % 100) - 50) * 0.04f;
    g_p[i].vy = ((rand() % 100) - 50) * 0.04f;
    g_p[i].hue = (uint16_t)(g_hueBase + (rand() % 80) - 40);
    g_p[i].life = 200 + (rand() % 55);
  }
}

static void ensureMic() {
  if (g_micOn) return;
  M5.Speaker.end();
  M5.Mic.begin();
  g_micOn = true;
}

static void sampleAudio() {
  ensureMic();
  memset(g_mic, 0, sizeof(g_mic));
  if (!M5.Mic.record(g_mic, MIC_N, 16000)) return;
  float sum = 0, hi = 0;
  for (int i = 0; i < MIC_N; i++) {
    float a = fabsf((float)g_mic[i]);
    sum += a;
    if (a > hi) hi = a;
  }
  float avg = sum / MIC_N;
  // envelope
  float lvl = avg / 8000.f;
  if (lvl > 1.2f) lvl = 1.2f;
  g_level = g_level * 0.82f + lvl * 0.18f;
  g_bass = g_bass * 0.88f + (avg / 6000.f) * 0.12f;
  g_mid = g_mid * 0.85f + (hi / 20000.f) * 0.15f;
  if (g_level > 0.55f && (rand() % 12) == 0) {
    // micro-haptic on peaks
    M5.Power.setVibration(90);
  } else {
    M5.Power.setVibration(0);
  }
}

static void sampleImu() {
  auto a = M5.Imu.getImuData();
  // low-pass
  g_ax = g_ax * 0.85f + a.accel.x * 0.15f;
  g_ay = g_ay * 0.85f + a.accel.y * 0.15f;
  g_az = g_az * 0.85f + a.accel.z * 0.15f;
  float mag = sqrtf(a.accel.x * a.accel.x + a.accel.y * a.accel.y + a.accel.z * a.accel.z);
  float sh = fabsf(mag - 1.0f);
  g_shake = g_shake * 0.9f + sh * 0.1f;
  if (g_shake > 0.45f) {
    g_hueBase += 17.f;
    if (g_hueBase > 360) g_hueBase -= 360;
    // burst velocities
    for (int i = 0; i < N_PART; i++) {
      g_p[i].vx += ((rand() % 100) - 50) * 0.08f * g_shake;
      g_p[i].vy += ((rand() % 100) - 50) * 0.08f * g_shake;
    }
    if (g_shake > 0.7f) haptic(25, 200);
  }
}

static void addTrail(int x, int y, uint16_t c) {
  g_trail[g_trailI].x = (int16_t)x;
  g_trail[g_trailI].y = (int16_t)y;
  g_trail[g_trailI].c = c;
  g_trail[g_trailI].a = 255;
  g_trailI = (g_trailI + 1) % N_TRAIL;
}

static void drawTrails() {
  for (int i = 0; i < N_TRAIL; i++) {
    if (g_trail[i].a < 8) continue;
    uint8_t a = g_trail[i].a;
    // fade by redrawing darker points
    M5.Display.fillCircle(g_trail[i].x, g_trail[i].y, 2 + (a >> 6), g_trail[i].c);
    g_trail[i].a = (uint8_t)(a * 0.88f);
  }
}

static void modeSwarm() {
  float gx = -g_ay * 0.35f; // tilt gravity
  float gy = -g_ax * 0.35f;
  float pulse = 0.6f + g_level * 1.4f;
  for (int i = 0; i < N_PART; i++) {
    Particle &p = g_p[i];
    // soft attractor at center, breathe with audio
    float cx = W * 0.5f + sinf(g_time * 0.7f + i) * 30.f * g_mid;
    float cy = H * 0.5f + cosf(g_time * 0.5f + i * 0.2f) * 20.f * g_bass;
    float dx = cx - p.x, dy = cy - p.y;
    p.vx += dx * 0.0018f * pulse + gx;
    p.vy += dy * 0.0018f * pulse + gy;
    p.vx *= 0.96f;
    p.vy *= 0.96f;
    p.x += p.vx;
    p.y += p.vy;
    if (p.x < 0) { p.x = 0; p.vx *= -0.8f; }
    if (p.x >= W) { p.x = W - 1; p.vx *= -0.8f; }
    if (p.y < 0) { p.y = 0; p.vy *= -0.8f; }
    if (p.y >= H) { p.y = H - 1; p.vy *= -0.8f; }
    float h = p.hue + g_hueBase * 0.2f + g_level * 80.f + g_time * 12.f;
    float v = 0.45f + g_level * 0.55f;
    uint16_t col = hsv565(h, 0.85f, v);
    int r = 1 + (int)(g_level * 4.f) + (i & 1);
    M5.Display.fillCircle((int)p.x, (int)p.y, r, col);
    if ((i & 3) == 0) addTrail((int)p.x, (int)p.y, col);
  }
  // mantis silhouette hint — two antennae
  int mx = W / 2 + (int)(g_ay * -40);
  int my = 36;
  uint16_t ant = hsv565(g_hueBase + 40, 0.9f, 0.7f + g_level * 0.3f);
  M5.Display.drawLine(mx, my, mx - 28, my - 22 - (int)(g_level * 10), ant);
  M5.Display.drawLine(mx, my, mx + 28, my - 22 - (int)(g_level * 10), ant);
  M5.Display.fillCircle(mx - 28, my - 22 - (int)(g_level * 10), 3, ant);
  M5.Display.fillCircle(mx + 28, my - 22 - (int)(g_level * 10), 3, ant);
}

static void modeEye() {
  // Compound-eye field + iris locked to tilt
  int cx = W / 2 + (int)(g_ay * -50);
  int cy = H / 2 + (int)(g_ax * 40);
  float breath = 1.f + g_level * 0.8f;
  // blink
  g_eyeBlink *= 0.92f;
  if ((rand() % 400) == 0) g_eyeBlink = 1.f;

  for (int y = 0; y < H; y += 4) {
    for (int x = 0; x < W; x += 4) {
      float dx = (x - cx) * 0.04f;
      float dy = (y - cy) * 0.04f;
      float d = sqrtf(dx * dx + dy * dy);
      float a = atan2f(dy, dx);
      float hex = sinf(dx * 3.f + g_time) * cosf(dy * 3.f - g_time * 0.7f);
      float h = g_hueBase + d * 40.f + hex * 30.f + g_time * 20.f;
      float v = 0.15f + 0.5f * (1.f - fminf(d / 8.f, 1.f)) + g_level * 0.35f;
      v *= (1.f - g_eyeBlink * 0.85f);
      if (v < 0.05f) continue;
      M5.Display.fillRect(x, y, 4, 4, hsv565(h, 0.75f, v * breath * 0.5f));
    }
  }
  // pupil
  int pr = (int)(14 + g_bass * 20);
  M5.Display.fillCircle(cx, cy, pr + 6, hsv565(g_hueBase + 180, 0.4f, 0.2f));
  M5.Display.fillCircle(cx, cy, pr, rgb565(5, 8, 12));
  M5.Display.fillCircle(cx - pr / 3, cy - pr / 3, pr / 4, hsv565(g_hueBase, 0.3f, 0.9f));
}

static void modeTunnel() {
  g_tunnelZ += 0.08f + g_level * 0.25f + g_shake * 0.15f;
  float spin = g_time * 0.4f + g_ay * 0.5f;
  int cx = W / 2 + (int)(g_ay * -20);
  int cy = H / 2 + (int)(g_ax * 20);
  for (int ring = 18; ring >= 0; ring--) {
    float z = fmodf(g_tunnelZ + ring * 0.35f, 6.5f);
    float sc = 8.f / (z + 0.4f);
    int rad = (int)(sc * 22.f);
    float h = g_hueBase + ring * 18.f + g_time * 30.f + g_level * 50.f;
    uint16_t c = hsv565(h, 0.9f, 0.25f + g_level * 0.5f + (18 - ring) * 0.02f);
    // hex-ish ring via multiple lines
    for (int k = 0; k < 6; k++) {
      float a0 = spin + k * (float)M_PI / 3.f;
      float a1 = spin + (k + 1) * (float)M_PI / 3.f;
      int x0 = cx + (int)(cosf(a0) * rad);
      int y0 = cy + (int)(sinf(a0) * rad * 0.85f);
      int x1 = cx + (int)(cosf(a1) * rad);
      int y1 = cy + (int)(sinf(a1) * rad * 0.85f);
      M5.Display.drawLine(x0, y0, x1, y1, c);
    }
  }
  // center star
  for (int i = 0; i < 8; i++) {
    float a = spin * 2.f + i * (float)M_PI / 4.f;
    int len = 8 + (int)(g_bass * 40);
    M5.Display.drawLine(cx, cy,
                        cx + (int)(cosf(a) * len),
                        cy + (int)(sinf(a) * len),
                        hsv565(g_hueBase + 90, 1.f, 0.8f));
  }
}

static void modePulse() {
  // Radial mandala driven by mic
  int cx = W / 2, cy = H / 2;
  g_pulsePhase += 0.12f + g_level * 0.3f;
  for (int ang = 0; ang < 360; ang += 6) {
    float rad = (float)ang * (float)M_PI / 180.f + g_pulsePhase;
    // sample faux waveform from mic buffer
    int mi = (ang * MIC_N / 360) % MIC_N;
    float wave = fabsf((float)g_mic[mi]) / 18000.f;
    float r = 20.f + wave * 90.f + g_bass * 40.f + sinf(g_pulsePhase * 2.f + ang * 0.05f) * 12.f;
    int x = cx + (int)(cosf(rad) * r);
    int y = cy + (int)(sinf(rad) * r * 0.9f);
    float h = g_hueBase + ang * 0.5f + g_time * 40.f;
    M5.Display.fillCircle(x, y, 2 + (int)(wave * 3), hsv565(h, 0.95f, 0.4f + wave));
  }
  // inner core
  int core = 10 + (int)(g_level * 28);
  M5.Display.fillCircle(cx, cy, core, hsv565(g_hueBase + g_time * 50.f, 0.8f, 0.5f + g_level * 0.4f));
  M5.Display.fillCircle(cx, cy, core / 2, rgb565(10, 5, 20));
}

static void drawChrome() {
  // mode label
  static const char *names[] = {"SWARM", "EYE", "TUNNEL", "PULSE"};
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(hsv565(g_hueBase, 0.6f, 0.85f));
  M5.Display.setCursor(6, 4);
  M5.Display.printf("SYNAPSE  %s", names[g_mode]);
  // level meter
  int lw = (int)(g_level * 80);
  if (lw > 80) lw = 80;
  M5.Display.drawRect(W - 90, 4, 82, 8, rgb565(40, 40, 50));
  M5.Display.fillRect(W - 89, 5, lw, 6, hsv565(g_hueBase + g_level * 60.f, 0.9f, 0.8f));
  // footer zones
  M5.Display.setCursor(12, H - 12);
  M5.Display.setTextColor(rgb565(100, 120, 130));
  M5.Display.print("< mode");
  M5.Display.setCursor(130, H - 12);
  M5.Display.print(g_frozen ? "[HOLD]" : "hold");
  M5.Display.setCursor(240, H - 12);
  M5.Display.print("mode >");
}

static void nextMode(int dir) {
  int m = (int)g_mode + dir;
  if (m < 0) m = MODE_COUNT - 1;
  if (m >= MODE_COUNT) m = 0;
  g_mode = (Mode)m;
  haptic(20, 120);
  seedParticles();
}

static void handleInput() {
  M5.update();
  auto t = M5.Touch.getDetail();
  if (t.isPressed()) {
    float h = g_hueBase + (t.x * 0.4f) + g_time * 10.f;
    uint16_t c = hsv565(h, 1.f, 0.9f);
    addTrail(t.x, t.y, c);
    // inject energy into nearby particles
    for (int i = 0; i < N_PART; i++) {
      float dx = g_p[i].x - t.x;
      float dy = g_p[i].y - t.y;
      float d2 = dx * dx + dy * dy;
      if (d2 < 3600) {
        g_p[i].vx += dx * 0.02f;
        g_p[i].vy += dy * 0.02f;
        g_p[i].hue = (uint16_t)h;
      }
    }
    if (t.y > H - 28) {
      if (t.x < 100) nextMode(-1);
      else if (t.x > 220) nextMode(1);
    }
  }

  if (millis() - g_lastBtn < 200) return;
  if (M5.BtnA.wasPressed()) { nextMode(-1); g_lastBtn = millis(); }
  if (M5.BtnC.wasPressed()) { nextMode(1); g_lastBtn = millis(); }
  if (M5.BtnB.wasPressed()) {
    g_frozen = !g_frozen;
    haptic(35, 180);
    g_lastBtn = millis();
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = true;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(rgb565(4, 2, 10));
  M5.Display.setTextColor(hsv565(160, 0.8f, 0.9f));
  M5.Display.setTextSize(2);
  M5.Display.setCursor(70, 90);
  M5.Display.print("SYNAPSE");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(55, 120);
  M5.Display.setTextColor(rgb565(140, 180, 170));
  M5.Display.print("tilt · touch · make noise");
  delay(700);
  srand(esp_random());
  seedParticles();
  memset(g_trail, 0, sizeof(g_trail));
  ensureMic();
}

void loop() {
  handleInput();
  if (!g_frozen) {
    sampleAudio();
    sampleImu();
    g_time += 0.033f + g_level * 0.02f;
    g_hueBase += 0.15f + g_mid * 0.5f;
    if (g_hueBase > 360) g_hueBase -= 360;
  } else {
    M5.Power.setVibration(0);
  }

  // motion-blur style clear — leave ghosts
  M5.Display.fillScreen(rgb565(6, 3, 14));
  drawTrails();

  switch (g_mode) {
    case MODE_SWARM:  modeSwarm(); break;
    case MODE_EYE:    modeEye(); break;
    case MODE_TUNNEL: modeTunnel(); break;
    case MODE_PULSE:  modePulse(); break;
    default: break;
  }
  drawChrome();
}
