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
float kick = 0, snare = 0, hat = 0, flat = 0, vocal = 0, vocalEnv = 0;
static float s_fl[3] = {0.1f, 0.1f, 0.1f}, s_prevG[3], s_evLp = 0, s_evMod = 0, s_evPow = 1e-6f, s_modPow = 0, s_evMin = 1e9f, s_evMax = 1.f;
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
// ---- general speaker playback (bells, echo) + speaker hold (meditation) ----
static const int16_t *s_pbBuf = nullptr; static int s_pbLen = 0; static uint32_t s_pbRate = SR; static uint8_t s_pbVol = 150;
static bool s_pbPending = false, s_hold = false;
static int16_t *s_bell[3] = {nullptr, nullptr, nullptr}; static int s_bellLen = 0;
// ---- echo / yakback ----
static bool s_echoOn = false;
static int16_t *s_echo = nullptr, *s_echoOut = nullptr;
static const int ECHO_MAX = SR * 4;
static int s_echoN = 0, s_echoFxI = 0, s_echoState = 0;     // 0 listening, 1 recording, 2 playing
static uint32_t s_echoQuietAt = 0, s_echoPlayAt = 0, s_echoPlayMs = 0;
static float s_echoRate = 1.f;
static uint8_t s_env[256];
// ---- lip-sync: 4 ms speech envelope (dB above the room floor) + width from zero-crossings ----
static float s_msub[8], s_mwid[8]; static uint32_t s_mAt = 0;
// ---- the echo cave ----
static bool s_caveOn = false, s_caveRec = false, s_caveHeard = false;
static int s_caveState = 0;                 // 0 idle, 1 recording, 2 mantis speaking, 3 cave echoing
static uint32_t s_caveQuietAt = 0, s_caveStart = 0;
static int s_caveVoiceLen = 0, s_caveTotal = 0;
static inline float dbMouth(float rms, float floor_) {
  float r = rms / (floor_ * 1.3f + 30.f);
  if (r <= 1.f) return 0.f;
  return clampf(20.f * log10f(r) / 26.f, 0.f, 1.f);
}

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
  // ---- band-group onsets: kick (62-250 Hz), snare (560 Hz-3.2 kHz), hats (3.7-7.5 kHz) ----
  if (fromMic) {
    static const int G0[3] = {0, 8, 27}, G1[3] = {3, 25, 32};
    for (int g = 0; g < 3; g++) {
      float e = 0; for (int b = G0[g]; b < G1[g]; b++) e += bands[b]; e /= (G1[g] - G0[g]);
      float d = e - s_prevG[g]; s_prevG[g] = e;
      float thr = s_fl[g] * 1.8f + 0.04f;
      float hit = (d > thr && level > 0.04f) ? clampf_((d - thr) * 3.f + 0.3f, 0.f, 1.f) : 0.f;
      if (g == 0) kick = fmaxf(kick, hit); else if (g == 1) snare = fmaxf(snare, hit); else hat = fmaxf(hat, hit);
      s_fl[g] = s_fl[g] * 0.9f + fmaxf(d, 0.f) * 0.1f;
    }
    // ---- spectral flatness (geometric / arithmetic mean): broadband fuzz = distortion ----
    float lg = 0, ar = 0;
    for (int b = 8; b < 32; b++) { lg += logf(raw[b] + 1.f); ar += raw[b] + 1.f; }
    float fl = expf(lg / 24.f) / (ar / 24.f);
    flat += (fl - flat) * 0.1f;
    // ---- vocals: energy in the speech/singing band (250 Hz-3.2 kHz), harmonic (low flatness there),
    //      and an envelope modulated at syllable rate (2-8 Hz; speech/song peak near 4 Hz) ----
    float ev = 0, et = 1.f, vl = 0, va = 0;
    for (int b = 3; b < 26; b++) { ev += raw[b]; vl += logf(raw[b] + 1.f); va += raw[b] + 1.f; }
    for (int b = 0; b < 32; b++) et += raw[b];
    float vflat = expf(vl / 23.f) / (va / 23.f);
    float share = ev / et;
    float evl = logf(ev + 1.f);                                   // log envelope, ~31 Hz block rate
    float lp = s_evLp; s_evLp += (evl - s_evLp) * 0.28f;          // ~1.5 Hz low-pass (the slow part)
    float hp = evl - lp;                                          // ... removed
    s_evMod += (hp - s_evMod) * 0.75f;                            // ~8 Hz low-pass -> 2-8 Hz band
    s_modPow += (s_evMod * s_evMod - s_modPow) * 0.06f;
    float modK = clampf_(sqrtf(s_modPow) * 3.f, 0.f, 1.f);
    static int pPrev = -1; static float pMove = 0;
    int pk = 2; for (int b = 3; b < 10; b++) if (raw[b] > raw[pk]) pk = b;       // dominant low harmonic (~f0 range)
    pMove += (((pPrev >= 0 && pk != pPrev) ? 1.f : 0.f) - pMove) * 0.03f; pPrev = pk;
    float melodic = clampf_((pMove - 0.045f) * 14.f, 0.f, 1.f);                    // a voice moves in pitch; a looped pluck doesn't
    float v = clampf_((share - 0.45f) / 0.3f, 0.f, 1.f) * clampf_((0.8f - vflat) / 0.45f, 0.f, 1.f) * modK * melodic * clampf_(level * 3.f, 0.f, 1.f);
    vocal += (v - vocal) * (v > vocal ? 0.08f : 0.03f);
    s_evMin = fminf(s_evMin * 0.999f + evl * 0.001f, evl); s_evMax = fmaxf(s_evMax * 0.998f + evl * 0.002f, evl);
    vocalEnv = clampf_((evl - s_evMin - 0.6f) / fmaxf(0.5f, s_evMax - s_evMin - 0.6f), 0.f, 1.f);
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
  for (int k = 0; k < 8; k++) {                                  // 8 x 64-sample windows (4 ms each)
    float acc = 0; int zc = 0, pv = 0;
    for (int i = k * 64; i < k * 64 + 64; i++) { int v = s_micBuf[i] - dc; acc += (float)v * v; if (i > k * 64 && ((v ^ pv) < 0) && abs(v) > 150) zc++; pv = v; }
    s_msub[k] = dbMouth(sqrtf(acc / 64.f), s_floor);
    s_mwid[k] = clampf((zc / 64.f - 0.05f) / 0.2f, 0.f, 1.f);
  }
  s_mAt = millis();
  if (s_caveRec && s_echo) {
    uint32_t now = millis();
    int n = MIC_REC; if (s_echoN + n > ECHO_MAX) n = ECHO_MAX - s_echoN;
    for (int i = 0; i < n; i++) s_echo[s_echoN + i] = (int16_t)clampf_((s_micBuf[i] - dc) * 2.f, -32000.f, 32000.f);
    s_echoN += n;
    if (level > 0.1f) { s_caveHeard = true; s_caveQuietAt = now; }
    bool done = s_echoN >= SR * 3 || (s_caveHeard && now - s_caveQuietAt > 700 && s_echoN > SR / 3) || (!s_caveHeard && now - s_caveStart > 4000);
    if (done) { s_caveRec = false; s_caveState = s_caveHeard ? 4 : 0; }   // 4 = build the playback
  }
  if (s_echoOn && s_echo) {
    uint32_t now = millis();
    if (s_echoState == 0 && level > 0.14f) { s_echoState = 1; s_echoN = 0; }
    if (s_echoState == 1) {
      int n = MIC_REC; if (s_echoN + n > ECHO_MAX) n = ECHO_MAX - s_echoN;
      for (int i = 0; i < n; i++) s_echo[s_echoN + i] = (int16_t)clampf_((s_micBuf[i] - dc) * 2.f, -32000.f, 32000.f);
      s_echoN += n;
      if (level > 0.08f) s_echoQuietAt = now;
      if ((now - s_echoQuietAt > 650 && s_echoN > SR / 3) || s_echoN >= ECHO_MAX) s_echoState = 3;   // done talking: play it back
      if (now - s_echoQuietAt > 650 && s_echoN <= SR / 3) s_echoState = 0;                          // just a noise
    }
  }
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

static void synthBells() {
  s_bellLen = SR * 3;
  static const float F[3] = {220.f, 293.66f, 392.f};                  // A3, D4, G4: a soft open fifth family
  for (int b = 0; b < 3; b++) {
    s_bell[b] = (int16_t *)heap_caps_malloc(s_bellLen * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_bell[b]) continue;
    for (int i = 0; i < s_bellLen; i++) {
      float t = (float)i / SR, f = F[b];
      float v = sinf(6.2831853f * f * t) * expf(-t * 0.9f) * 0.55f
              + sinf(6.2831853f * f * 2.76f * t) * expf(-t * 1.8f) * 0.25f * (0.8f + 0.2f * sinf(t * 7.f))   // singing-bowl beating
              + sinf(6.2831853f * f * 5.4f * t) * expf(-t * 3.5f) * 0.12f;
      float att = t < 0.004f ? t / 0.004f : 1.f;
      s_bell[b][i] = (int16_t)(v * att * 20000.f);
    }
  }
}
float bpm() { return 60000.f / s_period; }
void begin() {
  fftInit(); synthSfx(); synthBells();
  s_echo = (int16_t *)heap_caps_malloc(ECHO_MAX * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_echoOut = (int16_t *)heap_caps_malloc(ECHO_MAX * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  toMic();
}
void playBuf(const int16_t *d, int n, uint32_t rate, uint8_t vol) { s_pbBuf = d; s_pbLen = n; s_pbRate = rate; s_pbVol = vol; s_pbPending = true; }
void bell(int which, uint8_t vol) { if (s_bell[which % 3]) playBuf(s_bell[which % 3], s_bellLen, SR, vol); }
void speakerHold(bool on) { s_hold = on; if (!on && s_spk) s_sfxUntil = millis(); }
void echo(bool on) { s_echoOn = on; s_echoState = 0; }
void cave(bool on) { s_caveOn = on; s_caveRec = false; s_caveState = 0; }
void caveRecord() { if (s_caveState == 0 && s_echo) { s_caveRec = true; s_caveHeard = false; s_echoN = 0; s_caveStart = s_caveQuietAt = millis(); s_caveState = 1; } }
int caveState() { return s_caveState == 4 ? 1 : s_caveState; }
static float bufEnv(int pos, float *wide) {                        // the envelope right at the playback head
  if (pos < 0 || pos + 64 >= s_caveTotal) { if (wide) *wide = 0; return 0.f; }
  float acc = 0; int zc = 0, pv = 0;
  for (int i = pos; i < pos + 64; i++) { int v = s_echoOut[i]; acc += (float)v * v; if (i > pos && ((v ^ pv) < 0) && abs(v) > 300) zc++; pv = v; }
  if (wide) *wide = clampf((zc / 64.f - 0.05f) / 0.2f, 0.f, 1.f);
  return clampf(20.f * log10f(sqrtf(acc / 64.f) / 400.f + 1e-3f) / 30.f, 0.f, 1.f);
}
float mouthNow(float *wide) {
  uint32_t now = millis();
  if (s_caveState == 2) return bufEnv((int)((now - s_caveStart) * (SR / 1000.f)), wide);
  if (s_caveState == 3) { if (wide) *wide = 0; return 0.f; }
  if (s_echoState == 2) { if (wide) *wide = 0.3f; return clampf(level * 1.2f, 0.f, 1.f); }
  if (!s_mic) { if (wide) *wide = 0; return 0.f; }
  float k = (now - s_mAt) / 4.f; int i = (int)k; if (i > 7) i = 7; if (i < 0) i = 0;
  int j = i < 7 ? i + 1 : 7; float f = clampf(k - i, 0.f, 1.f);
  if (wide) *wide = s_mwid[i] + (s_mwid[j] - s_mwid[i]) * f;
  return s_msub[i] + (s_msub[j] - s_msub[i]) * f;
}
float caveEcho() { if (s_caveState != 3) return 0.f; return bufEnv((int)((millis() - s_caveStart) * (SR / 1000.f)), nullptr); }
int echoState() { return s_echoState == 3 ? 2 : s_echoState; }
int echoFx() { return s_echoFxI; }
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
  if (s_caveState == 4 && s_echoOut) {
    // the mantis says it into the cave (its own voice: pitched up), then the cave answers
    const float PF = 1.35f;
    int vl = (int)(s_echoN / PF); if (vl > ECHO_MAX / 2) vl = ECHO_MAX / 2;
    for (int i = 0; i < vl; i++) { float x = i * PF; int a = (int)x; float f = x - a; int b = a + 1 < s_echoN ? a + 1 : a; s_echoOut[i] = (int16_t)(s_echo[a] + (s_echo[b] - s_echo[a]) * f); }
    const int D[3] = {SR * 33 / 100, SR * 66 / 100, SR}; const float G[3] = {0.55f, 0.33f, 0.2f}, LPK[3] = {0.5f, 0.35f, 0.24f};
    int total = vl + D[2] + SR * 35 / 100; if (total > ECHO_MAX) total = ECHO_MAX;
    static float *mixb = nullptr; if (!mixb) mixb = (float *)heap_caps_malloc(ECHO_MAX * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mixb) {
      for (int i = 0; i < total; i++) mixb[i] = i < vl ? s_echoOut[i] : 0.f;
      for (int k = 0; k < 3; k++) {                                  // each echo darker and quieter, with a little diffusion
        float lp = 0;
        for (int i = 0; i < total; i++) {
          int j = i - D[k]; float x = (j >= 0 && j < vl) ? s_echoOut[j] : 0.f;
          int j2 = j - SR * 23 / 1000; if (j2 >= 0 && j2 < vl) x += 0.45f * s_echoOut[j2];
          lp += (x - lp) * LPK[k];
          mixb[i] += lp * G[k];
        }
      }
      float pk = 1.f; for (int i = 0; i < total; i++) pk = fmaxf(pk, fabsf(mixb[i]));
      float g = fminf(1.f, 30000.f / pk);
      for (int i = 0; i < total; i++) s_echoOut[i] = (int16_t)(mixb[i] * g);
      s_caveVoiceLen = vl; s_caveTotal = total;
      playBuf(s_echoOut, total, SR, 210);
      s_caveStart = now; s_caveState = 2;
    } else s_caveState = 0;
  }
  if (s_caveState == 2 || s_caveState == 3) {
    int pos = (int)((now - s_caveStart) * (SR / 1000.f));
    if (pos >= s_caveTotal) s_caveState = 0; else s_caveState = pos < s_caveVoiceLen ? 2 : 3;
  }
  if (s_echoState == 3 && s_echoOut) {                          // build the playback: chipmunk / monster / backwards
    s_echoFxI = (s_echoFxI + 1) % 3;
    int n = s_echoN;
    for (int i = 0; i < n; i++) s_echoOut[i] = s_echoFxI == 2 ? s_echo[n - 1 - i] : s_echo[i];
    s_echoRate = s_echoFxI == 0 ? 1.6f : (s_echoFxI == 1 ? 0.66f : 1.2f);
    for (int k = 0; k < 256; k++) {                             // envelope for the mantis's lips
      int a = k * n / 256, b = (k + 1) * n / 256, pk = 0;
      for (int i = a; i < b; i += 4) { int v = abs(s_echoOut[i]); if (v > pk) pk = v; }
      s_env[k] = (uint8_t)(pk >> 7);
    }
    s_echoPlayMs = (uint32_t)(n * 1000.f / (SR * s_echoRate));
    playBuf(s_echoOut, n, (uint32_t)(SR * s_echoRate), 200);
    s_echoPlayAt = now; s_echoState = 2;
  }
  if (s_pbPending) {
    s_pbPending = false;
    if (s_mic) { M5.Mic.end(); s_mic = false; s_micBusy = false; }
    if (!M5.Speaker.isEnabled()) M5.Speaker.begin();
    M5.Speaker.setVolume(s_pbVol);
    s_spk = true;
    M5.Speaker.playRaw(s_pbBuf, (size_t)s_pbLen, s_pbRate, false, 1, 0, true);
    uint32_t ms = (uint32_t)(s_pbLen * 1000.f / s_pbRate) + 80;
    s_sfxUntil = now + ms;
  }
  if (s_echoState == 2) {                                       // lip-sync to the playback
    uint32_t e = now - s_echoPlayAt;
    int k = s_echoPlayMs ? (int)(e * 256 / s_echoPlayMs) : 256;
    if (k < 256) level = s_env[k] / 180.f; else { s_echoState = 0; level = 0; }
  }
  if (s_spk && now > s_sfxUntil && !s_hold) toMic();
  onset = 0; kick = snare = hat = 0;
  if (s_mic) {
    if (!s_micBusy) { M5.Mic.record(s_micBuf, MIC_REC, SR); s_micBusy = true; s_micAt = now; }
    else if (now - s_micAt >= 30 && M5.Mic.isRecording() == 0) {
      micBlock(dt);
      M5.Mic.record(s_micBuf, MIC_REC, SR);
      s_micAt = now;
    }
  } else if (s_echoState != 2) { level *= 0.9f; }
  // calm energy: slow attack, slower release, compressed (for the physics room)
  float c = sqrtf(clampf_(level, 0.f, 1.2f));
  calm += (c - calm) * clampf_(dt * (c > calm ? 1.5f : 0.6f), 0.f, 1.f);
  beatPos += dt * 1000.f / s_period;
  if (now - s_lastOnsetMs > 2500) beatConf *= expf(-dt * 1.2f);
  if (beatPos > 1e6f) beatPos -= 1e6f;
}

}  // namespace aud
