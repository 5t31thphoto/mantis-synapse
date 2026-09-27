// ============================================================
//  SYNAPSE — MEDITATE: a mantis meditation you feel more than see.
//  1 learn the language   five haptic "words", felt in your hand first
//  2 place                "lie down somewhere quiet, place me flat on your chest"
//                          begins by itself once it feels you breathing
//  3 mirror  (~60 s)      the motor follows your own breath
//  4 guide   (~3 min)     it slowly lengthens the breath, only while you follow
//  5 listen  (~45 s)      silence: it listens for your heartbeat through your chest
//  6 echo    (~30 s)      your heartbeat, echoed back (if it found one)
//  7 close                three slow pulses, bells; the session grows the quartz
// Breath: the chest lifts and tilts the Core2 a hair on every breath; we track
// that tilt at ~250 Hz. Heart: tiny chest-wall vibrations (seismocardiography),
// band-passed and autocorrelated. Breath is dependable; the heart is a best effort.
// ============================================================
#include "app.h"
#include "audio.h"
#include "stats.h"
#include "wire.h"
#include <string.h>

namespace {
enum Ph : uint8_t { P_LEARN = 0, P_PLACE, P_TUNE, P_MIRROR, P_GUIDE, P_LISTEN, P_ECHO, P_CLOSE, P_DONE, P_PAUSED };
Ph s_ph = P_LEARN, s_resume = P_MIRROR;
float s_pt = 0, s_total = 0;                  // time in phase, session time
int s_card = 0;
uint8_t s_bright = 128;
bool s_dim = false;

// ---------- breath tracking ----------
float lpx = 0, lpy = 0, lpz = 1, bsx = 0, bsy = 0, bsz = 1;   // fast low-pass, slow baseline
float cxx = 1e-6f, cyy = 1e-6f, cxy = 0;                      // covariance of the tilt wobble
float s_b = 0, s_amp = 0.002f, s_bn = 0, s_sign = 1.f;       // breath signal, amplitude, normalized, direction
float s_period = 5.f, s_lastPeak = 0, s_lastTrough = 0; bool s_rising = true;
float s_breathSeen = 0;                                      // seconds of convincing breathing
// ---------- heart (SCG) ----------
struct BQ { float b0, b1, b2, a1, a2, z1, z2; float run(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
BQ hp1, lp1;
float s_env = 0; int s_dec = 0;
float s_hbuf[400]; int s_hi = 0;                              // 8 s of envelope at 50 Hz
float s_hr = 0, s_hrConf = 0, s_lastHr = 0; int s_hrGood = 0;
float s_beatAt = -9, s_envMax = 1e-6f, s_lastBeat = -9;
float s_clock = 0;
float s_fs = 0;                                              // measured motion sample rate (Hz)
void biquad(BQ &q, bool high, float fc, float fs) {
  float w = 6.2831853f * fc / fs, c = cosf(w), s = sinf(w), al = s / (2.f * 0.707f), a0 = 1.f + al;
  if (high) { q.b0 = (1 + c) / 2 / a0; q.b1 = -(1 + c) / a0; q.b2 = (1 + c) / 2 / a0; }
  else { q.b0 = (1 - c) / 2 / a0; q.b1 = (1 - c) / a0; q.b2 = (1 - c) / 2 / a0; }
  q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0; q.z1 = q.z2 = 0;
}
void heartAnalyze() {
  if (s_fs < 150.f) { s_hrGood = 0; return; }                  // too slow to hear a heart: don't pretend
  if (s_envMax < 0.0004f) { s_hrGood = 0; return; }           // nothing there to count
  float m = 0; for (int i = 0; i < 400; i++) m += s_hbuf[i]; m /= 400.f;
  float a0 = 0; for (int i = 0; i < 400; i++) { float v = s_hbuf[i] - m; a0 += v * v; }
  if (a0 < 1e-12f) return;
  float best = 0; int bl = 0;
  for (int L = 17; L <= 75; L++) {                             // 40..176 bpm
    float a = 0;
    for (int i = 0; i + L < 400; i++) a += (s_hbuf[(s_hi + i) % 400] - m) * (s_hbuf[(s_hi + i + L) % 400] - m);
    a /= a0;
    if (a > best) { best = a; bl = L; }
  }
  float hr = bl ? 3000.f / bl : 0;
  s_hrConf = best;
  if (best > 0.3f && hr > 40 && hr < 150) {
    s_hrGood = (fabsf(hr - s_lastHr) < 9.f) ? s_hrGood + 1 : 1;
    s_lastHr = hr;
    if (s_hrGood >= 2) s_hr = s_hr > 0 ? s_hr * 0.7f + hr * 0.3f : hr;
  } else s_hrGood = 0;
}
}  // namespace

// called from the input loop at ~250 Hz while this mode is open
void medSample(float ax, float ay, float az, float dt) {
  s_clock += dt;
  if (dt > 0) s_fs += (1.f / dt - s_fs) * 0.01f;
  // breath: tilt wobble around a slow baseline, along its main axis
  float k1 = dt * 6.f, k2 = dt * 0.05f;
  lpx += (ax - lpx) * k1; lpy += (ay - lpy) * k1; lpz += (az - lpz) * k1;
  bsx += (lpx - bsx) * k2; bsy += (lpy - bsy) * k2; bsz += (lpz - bsz) * k2;
  float dx = lpx - bsx, dy = lpy - bsy, dz = lpz - bsz;
  float kc = dt * 0.08f;
  cxx += (dx * dx - cxx) * kc; cyy += (dy * dy - cyy) * kc; cxy += (dx * dy - cxy) * kc;
  float th = 0.5f * atan2f(2.f * cxy, cxx - cyy);
  float b = dx * cosf(th) + dy * sinf(th) + dz * 0.5f;
  s_b = b;
  s_amp += (fabsf(b) * 1.5f - s_amp) * dt * 0.15f;
  if (s_amp < 0.0006f) s_amp = 0.0006f;
  s_bn = clampf(s_sign * b / (s_amp * 1.2f), -1.f, 1.f);
  // peaks and troughs -> breathing period
  float hys = 0.35f;
  if (s_rising && s_bn < 1.f - hys - 0.3f && s_clock - s_lastTrough > 0.8f) {
    s_rising = false; float per = s_clock - s_lastPeak;
    if (s_lastPeak > 0 && per > 2.f && per < 16.f) { s_period = s_period * 0.7f + per * 0.3f; s_breathSeen += per; }
    s_lastPeak = s_clock;
  } else if (!s_rising && s_bn > -1.f + hys + 0.3f && s_clock - s_lastPeak > 0.8f) { s_rising = true; s_lastTrough = s_clock; }
  if (s_clock - s_lastPeak > 18.f) s_breathSeen = 0;
  // heart: chest-wall vibration, 5..25 Hz, rectified envelope at 50 Hz
  float v = lp1.run(hp1.run(az));
  s_env += (fabsf(v) - s_env) * dt * 40.f;
  if (++s_dec >= 5) {
    s_dec = 0; s_hbuf[s_hi] = s_env; s_hi = (s_hi + 1) % 400;
    s_envMax = fmaxf(s_env, s_envMax * 0.995f);
    if (s_env > s_envMax * 0.6f && s_clock - s_lastBeat > 0.33f) s_lastBeat = s_clock;
  }
}

namespace {
// ---------- the haptic words ----------
void swell(float u) { hapRumble(0.06f + 0.28f * clampf(u, 0.f, 1.f), 40.f, 0.f); }   // smooth, no throb
const char *CARD_T[5] = {"breathe in", "breathe out", "well done", "your heartbeat", "we're finishing"};
void cardFeel(int c, float t) {
  float cyc = fmodf(t, 4.f);
  switch (c) {
    case 0: if (cyc < 3.f) swell(cyc / 3.f); break;
    case 1: if (cyc < 3.f) swell(1.f - cyc / 3.f); break;
    case 2: if (cyc < 0.02f) hapGesture(HG_SETTLE); break;
    case 3: if (fmodf(t, 1.f) < 0.02f) hapGesture(HG_LUBDUB); break;
    default: if (cyc < 0.02f) hapGesture(HG_THREE); break;
  }
}
void icon(int c, float t, int x, int y) {
  float cyc = fmodf(t, 4.f);
  switch (c) {
    case 0: { int r = (int)(8 + 22 * clampf(cyc / 3.f, 0, 1)); canvas.drawCircle(x, y, r, wire::TEAL); canvas.drawCircle(x, y, r - 1, wire::TEAL); break; }
    case 1: { int r = (int)(30 - 22 * clampf(cyc / 3.f, 0, 1)); canvas.drawCircle(x, y, r, wire::PLUM); canvas.drawCircle(x, y, r - 1, 0x780F); break; }
    case 2: { float u = fmodf(t, 4.f); if (u < 0.6f) { canvas.fillCircle(x - 10, y, 7, wire::LIME); } if (u > 0.2f && u < 0.8f) canvas.fillCircle(x + 10, y, 7, wire::LIME); break; }
    case 3: { float u = fmodf(t, 1.f); int r = u < 0.15f ? 16 : (u > 0.25f && u < 0.35f ? 12 : 9);
              canvas.fillCircle(x - r / 2, y - 3, r / 2 + 1, rgb565(230, 60, 90)); canvas.fillCircle(x + r / 2, y - 3, r / 2 + 1, rgb565(230, 60, 90));
              canvas.fillTriangle(x - r, y - 1, x + r, y - 1, x, y + r, rgb565(230, 60, 90)); break; }
    default: for (int k = 0; k < 3; k++) { bool on = cyc > k * 0.9f && cyc < k * 0.9f + 0.6f; if (on) canvas.fillCircle(x - 20 + k * 20, y, 6, wire::LIME); else canvas.drawCircle(x - 20 + k * 20, y, 6, wire::TEAL); } break;
  }
}
void text(const char *s, int y, uint16_t c, int sz = 1) { canvas.setTextSize(sz); canvas.setTextColor(c); canvas.setCursor(W / 2 - (int)strlen(s) * 3 * sz, y); canvas.print(s); canvas.setTextSize(1); }
void go(Ph p) { s_ph = p; s_pt = 0; }
void setDim(bool on) {
#ifndef HOST
  if (on && !s_dim) { s_bright = M5.Display.getBrightness(); M5.Display.setBrightness(35); }
  if (!on && s_dim) M5.Display.setBrightness(s_bright ? s_bright : 128);
#endif
  s_dim = on;
}

// ---------- the scene: a lotus of light that breathes, the mantis sage behind it ----------
void scene(float open, float heartPulse, float bright) {
  for (int y = 14; y < H - 14; y += 3) {
    float u = (float)(y - 14) / (H - 28);
    canvas.fillRect(0, y, W, 3, rgb565((uint8_t)(6 + 10 * u * bright), (uint8_t)(4 + 12 * bright), (uint8_t)(18 + 22 * (1 - u) * bright)));
  }
  // sage silhouette: head and folded arms, very dim
  uint16_t sil = rgb565((uint8_t)(10 + 18 * bright), (uint8_t)(24 + 30 * bright), (uint8_t)(22 + 26 * bright));
  canvas.fillTriangle(160 - 34, 44, 160 + 34, 44, 160, 84, sil);
  canvas.fillCircle(160 - 22, 44, 11, sil); canvas.fillCircle(160 + 22, 44, 11, sil);
  canvas.fillEllipse(160, 150, 34, 60, sil);
  // lotus
  int petals = 8;
  for (int ring = 0; ring < 2; ring++)
    for (int k = 0; k < petals; k++) {
      float a = k * 6.2831853f / petals + ring * 0.39f + s_total * 0.02f;
      float L = (ring ? 38.f : 58.f) * (0.45f + 0.55f * open);
      int px = 160 + (int)(cosf(a) * L * 0.5f), py = 150 + (int)(sinf(a) * L * 0.32f);
      canvas.fillEllipse(px, py, (int)(L * 0.32f) + 1, (int)(L * 0.14f) + 1,
                         ring ? hsv565(300.f, 0.5f, 0.25f + 0.45f * bright * open) : hsv565(175.f, 0.6f, 0.2f + 0.4f * bright * open));
    }
  canvas.fillCircle(160, 150, 6 + (int)(4 * open + 6 * heartPulse), hsv565(95.f, 0.6f, 0.4f + 0.5f * bright));
}
}  // namespace

void medEnter() {
  go(P_LEARN); s_card = 0; s_total = 0;
  biquad(hp1, true, 5.f, 250.f); biquad(lp1, false, 25.f, 250.f);
  s_hr = 0; s_hrGood = 0; s_breathSeen = 0; s_sign = 1.f;
}
void medLeave() { setDim(false); aud::speakerHold(false); hapRumble(0, 1, 0); }
void medButton() {
  if (s_ph == P_LEARN) { go(P_PLACE); return; }                        // skip the lesson
  if (s_ph == P_DONE || s_ph == P_PLACE) { medEnter(); return; }
  go(P_CLOSE);                                                          // end gently
}
void medTouch(int, int) { if (s_ph == P_LEARN) { if (++s_card >= 5) go(P_PLACE); else s_pt = 0; } else if (s_ph == P_DONE) medEnter(); }
const char *medName() { return "meditate"; }

void medDraw() {
  float dt = fminf(g_dt, 0.05f);
  s_pt += dt; s_total += dt;
  bool onChest = g_gravZ > 0.82f;
  bool still = fabsf(g_gyroX) + fabsf(g_gyroY) + fabsf(g_gyroZ) < 35.f;
  // picked up mid-session -> pause
  if (s_ph >= P_TUNE && s_ph <= P_ECHO && (!onChest || !still)) { s_resume = s_ph; go(P_PAUSED); setDim(false); }
  switch (s_ph) {
    case P_LEARN: {
      scene(0.5f, 0, 1.f);
      canvas.fillRoundRect(40, 34, 240, 170, 14, rgb565(8, 14, 22));
      canvas.drawRoundRect(40, 34, 240, 170, 14, wire::TEAL);
      text("feel this", 48, rgb565(120, 140, 150));
      icon(s_card, s_pt, 160, 110);
      text(CARD_T[s_card], 158, wire::LIME, 2);
      char b[8]; snprintf(b, 8, "%d / 5", s_card + 1); text(b, 186, rgb565(100, 110, 120));
      cardFeel(s_card, s_pt);
      break;
    }
    case P_PLACE: {
      scene(0.3f + 0.2f * sinf(s_total), 0, 1.f);
      text("lie down somewhere quiet", 34, rgb565(200, 210, 220));
      text("and place me flat", 50, rgb565(200, 210, 220));
      text("on your chest", 66, rgb565(200, 210, 220));
      text(onChest && still ? "...I can feel you breathing" : "I'll begin when I feel you breathing", 196, onChest && still ? wire::LIME : rgb565(110, 120, 130));
      if (onChest && still && s_breathSeen > 10.f) {
        aud::speakerHold(true); aud::bell(0, 110); setDim(true); go(P_TUNE);
      }
      break;
    }
    case P_TUNE: {                                                        // one cued inhale teaches us which way "in" moves
      static float startB = 0;
      if (s_pt < dt * 1.5f) startB = s_b;
      float u = clampf(s_pt / 4.f, 0.f, 1.f);
      scene(u, 0, 0.6f);
      if (s_pt < 4.f) swell(u);
      else if (s_pt < 4.1f) { s_sign = (s_b - startB) >= 0 ? 1.f : -1.f; }
      else if (s_pt < 9.f) swell(1.f - (s_pt - 4.f) / 5.f);
      else go(P_MIRROR);
      break;
    }
    case P_MIRROR: {
      float open = 0.5f + 0.5f * s_bn;
      scene(open, 0, 0.6f);
      swell(open * 0.8f);                                                 // the motor follows your breath
      if (s_pt > 60.f) go(P_GUIDE);
      break;
    }
    case P_GUIDE: {
      static float cueT = 0, cueP = 5.f, sync = 0; static int goodRun = 0;
      if (s_pt < dt * 1.5f) { cueT = 0; cueP = clampf(s_period, 3.5f, 8.f); sync = 0; goodRun = 0; }
      cueT += dt;
      float ph = fmodf(cueT, cueP) / cueP, inF = 0.42f;                  // a little more out than in
      float cue = ph < inF ? ph / inF : 1.f - (ph - inF) / (1.f - inF);
      swell(cue);
      sync += (((cue * 2.f - 1.f) * s_bn) - sync) * dt * 0.4f;            // are we breathing together?
      if (ph < dt / cueP * 1.5f) {                                        // each new cycle
        if (sync > 0.35f) { goodRun++; if (cueP < 10.f) cueP = fminf(10.f, cueP * 1.06f); }   // follow along -> slower, deeper
        else { goodRun = 0; cueP += (clampf(s_period, 3.5f, 10.f) - cueP) * 0.3f; }          // lost you -> meet you where you are
        if (goodRun > 0 && goodRun % 4 == 0) hapGesture(HG_SETTLE);                          // well done
      }
      scene(cue, 0, 0.6f);
      if (s_pt > 180.f) { aud::bell(2, 70); go(P_LISTEN); }
      break;
    }
    case P_LISTEN: {                                                      // silence; listen for the heart
      scene(0.5f + 0.5f * s_bn, 0, 0.45f);
      static float an = 0; an += dt;
      if (an > 1.f) { an = 0; heartAnalyze(); }
      for (int k = 0; k < 3; k++) { int r = (int)fmodf(s_pt * 18.f + k * 30.f, 90.f); canvas.drawCircle(160, 150, 20 + r, hsv565(330.f, 0.4f, 0.25f * (1.f - r / 90.f))); }
      if (s_pt > 45.f) go(s_hr > 0 ? P_ECHO : P_CLOSE);
      break;
    }
    case P_ECHO: {                                                        // your heartbeat, handed back to you
      float beat = 60.f / fmaxf(40.f, s_hr);
      static float next = 0;
      if (s_pt < dt * 1.5f) next = 0.5f;
      float pulse = 0;
      if (s_pt >= next) { hapGesture(HG_LUBDUB); next += beat; }
      float since = s_pt - (next - beat); pulse = since < 0.25f ? 1.f - since / 0.25f : 0.f;
      static float an = 0; an += dt; if (an > 2.f) { an = 0; heartAnalyze(); }
      scene(0.5f + 0.5f * s_bn, pulse, 0.5f);
      if (s_pt > 30.f) go(P_CLOSE);
      break;
    }
    case P_CLOSE: {
      scene(0.8f, 0, clampf(0.5f + s_pt * 0.1f, 0.f, 1.f));
      if (s_pt < dt * 1.5f) { hapGesture(HG_THREE); aud::bell(0, 90); }
      if (s_pt > 2.f && s_pt < 2.f + dt * 1.5f) aud::bell(1, 80);
      if (s_pt > 4.f && s_pt < 4.f + dt * 1.5f) aud::bell(2, 70);
      if (s_pt > 7.f) {
        setDim(false); aud::speakerHold(false);
        stats::event(stats::EV_MEDITATE, clampf(s_total / 300.f, 0.1f, 1.5f)); stats::save();
        go(P_DONE);
      }
      break;
    }
    case P_DONE: {
      scene(0.9f, 0, 1.f);
      text("welcome back", 40, wire::LIME, 2);
      text("tap to begin again", 200, rgb565(110, 120, 130));
      break;
    }
    case P_PAUSED: {
      scene(0.3f, 0, 1.f);
      text("paused", 40, rgb565(200, 210, 220), 2);
      text("place me back on your chest", 70, rgb565(160, 170, 180));
      text("B ends the session", 200, rgb565(110, 120, 130));
      if (onChest && still && s_pt > 2.f) { setDim(true); go(s_resume); }
      if (s_pt > 60.f) go(P_CLOSE);
      break;
    }
  }
}
#ifdef HOST
int medPhase() { return (int)s_ph; }
float medPeriod() { return s_period; }
#endif
