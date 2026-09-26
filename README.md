# SYNAPSE

A praying-mantis themed psychedelic **fidget firmware** for the **M5Stack Core2**.

Tilt it. Touch it. Make noise. Play drums. It’s meant to be addictive — not a settings menu with a skin.

## Hardware

- **M5Stack Core2** (ESP32, touch LCD, IMU, mic, speaker, haptic, TF slot)
- Optional microSD for custom drum samples

## Modes

| Mode | What it does |
|------|----------------|
| **SWARM** | Fluid blob particles. IMU gravity. Mic paints color. **B** cycles flock / orbit / chaos. |
| **EYE** | Compound-eye field; iris tracks tilt. **B** toggles tracking. |
| **TUNNEL** | Hex tunnel; bank and spin with the IMU. **B** toggles warp. |
| **PULSE** | Live mic waveform ring. **B** toggles mirror. |
| **DRUM** | Four pads — closed hat, open hat, kick, snare. Haptic on the kick. |

### Controls (all modes)

- **A / C** (or footer touch) — previous / next mode  
- **B** — mode-specific feature (see table)  
- **Shake** — hue jump + haptic burst  
- **Mic** — drives reactive color and pulse  

### Drum mode

| Pad | Position | Sound |
|-----|----------|--------|
| Closed hat | Top-left | Click |
| Open hat | Top-right | Sizzle |
| Kick | Bottom-left | Low tone + haptic “sub” |
| Snare | Bottom-right | Crack |

- **Tap** a pad to play  
- **Hold ~3 s** to arm that pad (blinks) → **B** = **REC**  
- REC: short countdown, capture window, auto-trim silence, save to SD  
- With nothing armed, **B** starts/stops **LOOP** (stop clears; next start is a fresh loop)  
- Loop shows as a **visual pulse** behind the pads (bright + dimmer beats)

#### SD samples (`/drums`)

On first run (with a card inserted), SYNAPSE creates:

```
/drums/
  SAMPLES.txt      ← format notes
  hat_closed.raw
  hat_open.raw
  kick.raw
  snare.raw
```

- Format: **signed 16-bit mono PCM, 16 kHz, little-endian, no WAV header**  
- One file per pad; recording overwrites that pad’s file  
- You can also copy `.raw` files onto the card yourself  

## Build & flash

```bash
pio run -e m5stack-core2
pio run -e m5stack-core2 -t upload
```

Or use the GitHub Pages site after CI: **Install** via USB (Chrome/Edge) or download the merged `.bin`.

## CI

Push to `main` / `master` (or drop a root payload zip). Actions builds the firmware and deploys the flasher under Pages.

## Credits

Pixel mantis mascot provided for splash and branding. Built with **M5Unified** / **M5GFX**.
