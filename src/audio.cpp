// ============================================================
//  SYNAPSE — audio (listen-only, see audio.h)
// ============================================================
#include "app.h"
#include "audio.h"
#include <string.h>

namespace aud {

static const int SR = 16000;
static const int MIC_REC = 512;

float level = 0, peak = 0, calm = 0, zcr = 0, onset = 0, beatPos = 0, beatConf = 0;
float bands[32];
float bass = 0, mid = 0, treble = 0, centroid = 0;
int16_t scope[256];

static bool s_spk = false, s_mic = false, s_micBusy = false;
static uint32_t s_micAt = 0;
static int16_t s_micBuf[MIC_REC];
static float s_floor = 400.f, s_prevBands[32], s_fluxAvg = 0.1f, s_period = 500.f;
static uint32_t s_lastOnsetMs = 0;
static const int CAL_BLOCKS = 28;                 // ~0.9 s of listening
static int s_calN = CAL_BLOCKS;                   // calibrate at boot
static float s_calAcc = 0, s_calMin = 1e9f, s_loudRef = 1200.f, s_calCap = 1000.f;
static int16_t *s_sfx = nullptr; static int s_sfxLen = 0;
static uint32_t s_sfxRate = SR, s_sfxUntil = 0;
static bool s_sfxPending = false;

bool speakerLive() { return s_spk; }
static inline float clampf_(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static float s_re[256], s_im[256], s_win[256], s_cos[128], s_sin[128];
static uint8_t s_rev[256];
static uint8_t s_bandLo[33];
static float s_agc = 1.f;

static void fftInit() {
  for (int i = 0; i < 256; i++) {
    s_win[i] = 0.5f - 0.5f * cosf(6.2831853f * i / 255.f);
    int r = 0; for (int b = 0; b < 8; b++) if (i & (1 << b)) r |= 1 << (7 - b);
    s_rev[i] = (uint8_t)r;
  }
  for (int i = 0; i < 128; i++) { s_cos[i] = cosf(6.2831853f * i / 256.f); s_sin[i] = -sinf(6.2831853f * i / 256.f); }
  for (int b = 0; b <= 32; b++) {
    int lo = (int)(1.5f * powf(120.f / 1.5f, b / 32.f));
    if (b > 0 && lo <= s_bandLo[b - 1]) lo = s_bandLo[b - 1] + 1;
    s_bandLo[b] = (uint8_t)(lo > 127 ? 127 : lo);
  }
}
static void analyze(const int16_t *x, bool fromMic, float dt) {
  for (int i = 0; i < 256; i++) { s_re[s_rev[i]] = x[i] * s_win[i]; s_im[s_rev[i]] = 0; }
  for (int size = 2; size <= 256; size <<= 1) {
    int half = size >> 1, step = 256 / size;
    for (int i = 0; i < 256; i += size)
      for (int j = 0; j < half; j++) {
        float wr = s_cos[j * step], wi = s_sin[j * step];
        int a = i + j, b = a + half;
        float tr = s_re[b] * wr - s_im[b] * wi, ti = s_re[b] * wi + s_im[b] * wr;
        s_re[b] = s_re[a] - tr; s_im[b] = s_im[a] - ti;
        s_re[a] += tr; s_im[a] += ti;
      }
  }
  float mx = 1.f, flux = 0, wsum = 0, tot = 0;
  float raw[32];
  for (int b = 0; b < 32; b++) {
    float acc = 0; int cnt = 0;
    for (int k = s_bandLo[b]; k < s_bandLo[b + 1] || cnt == 0; k++) {
      acc += sqrtf(s_re[k] * s_re[k] + s_im[k] * s_im[k]); cnt++;
      if (k >= 127) break;
    }
    raw[b] = acc / cnt;
    if (raw[b] > mx) mx = raw[b];
  }
  // AGC: fast up, slow down -> always lively, never pinned
  s_agc = mx > s_agc ? s_agc * 0.6f + mx * 0.4f : s_agc * (1.f - 0.25f * dt) + mx * 0.25f * dt;
  if (s_agc < 12000.f) s_agc = 12000.f;
  float q = fromMic ? clampf_(level * 3.f, 0.f, 1.f) : 1.f;   // silence stays calm, any real sound opens fully
  for (int b = 0; b < 32; b++) {
    float v = clampf_(raw[b] / s_agc * 1.15f, 0.f, 1.3f) * (0.25f + 0.75f * q);
    float d = v - s_prevBands[b];
    if (d > 0) flux += d * (b < 8 ? 1.6f : 1.f);
    s_prevBands[b] = v;
    bands[b] = v > bands[b] ? bands[b] * 0.35f + v * 0.65f : bands[b] * 0.78f + v * 0.22f;
    wsum += bands[b] * b; tot += bands[b];
  }
  bass = (bands[0] + bands[1] + bands[2] + bands[3] + bands[4] + bands[5]) / 6.f;
  mid = 0; for (int b = 8; b < 18; b++) mid += bands[b]; mid /= 10.f;
  treble = 0; for (int b = 20; b < 32; b++) treble += bands[b]; treble /= 12.f;
  centroid = tot > 0.01f ? clampf_(wsum / tot / 31.f, 0.f, 1.f) : centroid * 0.9f;

  if (fromMic) {
    uint32_t now = millis();
    float thr = s_fluxAvg * 1.7f + 0.12f;
    if (flux > thr && now - s_lastOnsetMs > 140 && level > 0.04f) {
      float str = clampf_((flux - thr) * 1.5f + 0.35f, 0.f, 1.f);
      onset = fmaxf(onset, str);
      float ioi = (float)(now - s_lastOnsetMs);
      s_lastOnsetMs = now;
      while (ioi > 0 && ioi < 300.f) ioi *= 2.f;
      while (ioi > 1000.f) ioi *= 0.5f;
      if (ioi >= 300.f && ioi <= 1000.f) {
        float k = fabsf(ioi - s_period) < s_period * 0.22f ? 0.18f : (beatConf < 0.3f ? 0.35f : 0.04f);
        s_period += (ioi - s_period) * k;
      }
      float frac = beatPos - floorf(beatPos + 0.5f);
      if (fabsf(frac) < 0.4f) { beatPos -= frac * 0.45f; beatConf = fminf(1.f, beatConf + 0.12f * (1.f - fabsf(frac))); }
      else beatConf *= 0.85f;
    }
    s_fluxAvg = s_fluxAvg * 0.92f + flux * 0.08f;
  }
}

static void micBlock(float dt) {
  int64_t sum = 0;
  for (int i = 0; i < MIC_REC; i++) sum += s_micBuf[i];
  int dc = (int)(sum / MIC_REC);
  float e = 0, pk = 0; int zc = 0; int prev = 0;
  for (int i = 0; i < MIC_REC; i++) {
    int v = s_micBuf[i] - dc;
    float a = (float)(v < 0 ? -v : v);
    e += a; if (a > pk) pk = a;
    if (i && ((v ^ prev) < 0) && (a > 200.f)) zc++;
    prev = v;
    if (i >= MIC_REC - 256) scope[i - (MIC_REC - 256)] = (int16_t)v;
  }
  e /= MIC_REC;
  // ambient floor: measured by calibrateAmbient(); afterwards it only drifts while the room is
  // genuinely quiet, so sustained music can never be mistaken for silence.
  if (s_calN > 0) {
    s_calAcc += e; s_calMin = fminf(s_calMin, e);
    if (--s_calN == 0) { s_floor = clampf_((s_calAcc / CAL_BLOCKS) * 0.6f + s_calMin * 0.4f, 60.f, s_calCap); s_loudRef = 1200.f; }
  } else if (e < s_floor * 1.6f) s_floor = e < s_floor ? s_floor * 0.9f + e * 0.1f : s_floor * 0.995f + e * 0.005f;
  float above = fmaxf(0.f, e - s_floor * 1.2f);
  // loudness AGC: follows the room's loud parts up fast, lets go slowly; never more sensitive than 1200
  s_loudRef = above > s_loudRef ? s_loudRef * 0.7f + above * 0.3f : s_loudRef * 0.9985f;
  if (s_loudRef < 1200.f) s_loudRef = 1200.f;
  float lv = s_calN > 0 ? 0.f : clampf_(above / (s_loudRef * 0.8f), 0.f, 1.5f);
  level = lv > level ? level * 0.45f + lv * 0.55f : level * 0.82f + lv * 0.18f;
  peak = peak * 0.75f + clampf_(pk / 22000.f, 0.f, 1.5f) * 0.25f;
  float z = clampf_((float)zc / (MIC_REC * 0.35f), 0.f, 1.f);
  zcr = zcr * 0.6f + z * 0.4f;
  analyze(scope, true, dt);
}


static void synthSfx() {
  s_sfxLen = SR * 34 / 100;
  s_sfx = (int16_t *)heap_caps_malloc(s_sfxLen * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_sfx) s_sfx = (int16_t *)malloc(s_sfxLen * 2);
  float sp = 0;
  for (int i = 0; i < s_sfxLen; i++) {
    float t = (float)i / SR, u = t / 0.34f;
    float f = 1300.f + 1500.f * sinf(u * 3.1416f) + 90.f * sinf(t * 6.2831853f * 31.f);
    sp += f / SR;
    float sq = (fmodf(sp, 1.f) < 0.32f) ? 1.f : -1.f;
    float env = (u < 0.05f ? u / 0.05f : 1.f) * (1.f - u) * (1.f - u);
    s_sfx[i] = (int16_t)(sq * env * 16000.f);
  }
}

static void toMic() {
  s_spk = false;
  if (M5.Speaker.isEnabled()) M5.Speaker.end();
  if (!M5.Mic.isEnabled()) M5.Mic.begin();
  s_mic = true; s_micBusy = false;
}

void begin() { fftInit(); synthSfx(); toMic(); }
void calibrateAmbient(float cap) { s_calN = CAL_BLOCKS; s_calAcc = 0; s_calMin = 1e9f; s_calCap = cap; }
bool calibrating() { return s_calN > 0; }

void sfx(float pitch) {
  s_sfxRate = (uint32_t)(SR * clampf_(pitch, 0.5f, 2.f));
  s_sfxPending = true;
  s_sfxUntil = millis() + 420;
}

void service(float dt) {
  uint32_t now = millis();
  if (s_sfxPending) {
    s_sfxPending = false;
    if (s_mic) { M5.Mic.end(); s_mic = false; s_micBusy = false; }
    if (!M5.Speaker.isEnabled()) M5.Speaker.begin();
    M5.Speaker.setVolume(170);
    s_spk = true;
    M5.Speaker.playRaw(s_sfx, (size_t)s_sfxLen, s_sfxRate, false, 1, 0, true);
  }
  if (s_spk && now > s_sfxUntil) toMic();
  onset = 0;
  if (s_mic) {
    if (!s_micBusy) { M5.Mic.record(s_micBuf, MIC_REC, SR); s_micBusy = true; s_micAt = now; }
    else if (now - s_micAt >= 30 && M5.Mic.isRecording() == 0) {
      micBlock(dt);
      M5.Mic.record(s_micBuf, MIC_REC, SR);
      s_micAt = now;
    }
  } else { level *= 0.9f; }
  // calm energy: slow attack, slower release, compressed (for the physics room)
  float c = sqrtf(clampf_(level, 0.f, 1.2f));
  calm += (c - calm) * clampf_(dt * (c > calm ? 1.5f : 0.6f), 0.f, 1.f);
  beatPos += dt * 1000.f / s_period;
  if (now - s_lastOnsetMs > 2500) beatConf *= expf(-dt * 1.2f);
  if (beatPos > 1e6f) beatPos -= 1e6f;
}

}  // namespace aud
