# SYNAPSE — a small green god in a glass panel

Psychedelic praying-mantis fidget toy, calm physics room and dancing / singing mantis puppet for the **M5Stack Core2**.
Everything listens (mic spectrum, beats, voice), everything feels (IMU tilt, shake, touch, haptics).

## Build & install

* **CI**: push this folder (or the zip) to the repo. `github/workflows/ci.yml` builds with PlatformIO
  (`pio run -e m5stack-core2`), merges `firmware-merged.bin`, and deploys the web installer in `website/`.
* **Local**: `pio run -e m5stack-core2 -t upload`.
* **Browser**: open the GitHub Pages site in Chrome / Edge and click Install (esp-web-tools).

## Controls (everywhere)

| Input | Does |
|---|---|
| **A / C** | previous / next mode (or tap the left / right of the footer) |
| **B** | the mode's variation (shown centre-bottom) |
| **Tilt** | steer, look, bend |
| **Shake** | hue jump, dizzy eye / woozy mantis, palette scramble |
| **Sound** | drives colour, geometry, speed and the mantis |

## Modes

**SWARM** — particle flock (B: flock / orbit / chaos). Touch drops a tiny mantis in and scatters the flock.

**EYE** — a wet eyeball on a moiré field: one ring set is driven by sound (spacing, spin, colour), the other by tilt
(centre, colour). Their interference is the hypnosis. Curved lids blink, squint and startle at loud sounds.
Poke it and it flinches, squeals, goes bloodshot and **squirts tears**; hit the pupil for extra outrage.
Shake: random iris colour and a dizzy eye-roll that recovers. B: gaze tracking on / off.

**TUNNEL** — B cycles four sub-modes:
* **dive** — fly down a tube generated from the live spectrum (each depth slice is a moment of sound; beats become
  rings). The path winds; tilt (or drag) to stay inside — scrape the wall and you'll feel it.
* **recede** — fly backwards out of the tube while it is created at your face: tilt / drag bends it, sound paints it,
  and you watch your sculpture trail into the distance.
* **fractal** — morphing Julia sets with orbit-trap colouring (no flat fields). Tilt to steer, drag for big moves,
  tap for a new world. The camera is attracted to detail so exploring never sinks into a void. Quality adapts to hold framerate.
* **portal** — warp speed through a nebula. Aim at the wormhole (tilt / touch), fly in, spiral through, and emerge
  in a new dimension. Chain them for a combo.

**PULSE** — feedback visualizers, each B more cross-mapped: bloom → kaleido → spectrum star → phase space →
synesthesia (the spectrum sculpts a flow field; quiet is deep and slow, loud is fast pastel strobe; touch paints).

**CALM** — the physics room. Everything still listens, but through a slow, soft energy follower so sound nudges
rather than shoves. Gravity eases in; haptics are feather-light. **B** changes room, **long-press** (hold still ~0.6 s)
changes the room's variant:
* **flow** — a glowing liquid you pour by tilting and stir with a finger; moving liquid glows, still liquid rests.
  Sound thins it and tints it. Long-press: neon → honey → mercury (surface tension you can see).
* **splash** — a pool. Tap the water to splash it; tap above it to drop water from your fingertip. Long-press: gentle rain.
* **sand** — sand art between glass panes. Turn it over and watch the layers stream through the water into new
  landscapes; fingertip pushes grains aside; a sound lets a bubble go. Long-press: a new sand picture.
* **waves** — a wave-machine tank with a paper boat. Tilt to slosh, drag through the surface, hum to raise the swell.
  The sky is the room's sound: clouds gather, rain falls and rings the water, loud hits in a storm throw lightning.
  Long-press: night, with moon glitter and bioluminescent crests.
* **aquarium** — blacklight pebbles that pulse with the bass, plankton you can stir, a pulsing jellyfish, fish that
  wander, dart from taps and loud sounds and come to look at a held finger, bubbles that wobble, merge and pop.
  Tap a bubble to pop it, tap water to release some, hold to stream them. Long-press: UV on / off.

**MANTIS** — the whole bug, built from separated sprites on a skeleton (IK legs, planted feet, squash & stretch),
dancing beat-locked moves chosen by energy. Tap its head to pet it (happy eyes, blush, hearts). Idle, it breathes,
looks around and waves. **B: sing** — close-up, holding a mic, mouth lip-syncing your voice (open with loudness,
wide on bright sounds), notes floating out.

## Drums moved

The drum machine now lives in its own firmware, **Mantis Studio** (drums, lead, bass, audio track, mixer, and the
dancing mantis). Synapse keeps the microphone on permanently, so every mode hears the room without dropouts, and
the per-pixel effects render on both CPU cores.

## Portal flights

Fly the ether and steer through the hoops that appear in the distance. Thread **three in a row** (the dots at the
top fill up; a miss resets the chain) and a portal appears far ahead. The next dimension is already visible inside
it: fly into it and you're there, no loading screen. Speed follows the music; tilt or touch to steer.

## Haptics

The vibration motor is played, not switched: a small mixer shapes taps with soft tails, a continuous rumble
(strength, pulse rate, grit) and keyframed gestures.
* **Portal** - you feel it the moment it appears: a slow, heavy throb. It grows stronger, faster and grittier as it
  approaches until the pulses merge into a roar at the crossing - then silence, the instant you're through.
* **Hoops** - a rising "ta-DING" when you thread one, a swelling triple when the chain completes, a dull sinking
  wobble when you miss.
* **Dive** - scraping the tube wall is gritty and lasts exactly as long as you scrape.
* **Fractal** - a rebirth swells and vanishes.
* **Calm** - the flowing liquid has weight when it sloshes, big swells meeting the glass press softly, and
  lightning brings rolling distant thunder.
* **Calibration** - a heartbeat that quickens as the ring closes, the motor goes completely still while the gyro
  calibrates (so it can't disturb the measurement), then two soft pulses: done.

## Calibrate the tilt (do this once)

Lay the Core2 flat and still, press and **hold a finger still in the middle of the screen**. After 2 seconds a
ring closes in on your finger; then it shows **"hold still & flat - calibrating"** for about a second.
This runs M5Unified's own gyro offset calibration (its documented method: still on a desk) and saves it to NVS,
and it records your neutral pose for flying (dive, recede, fractal, portal steer from that pose, with a deadzone,
so holding the Core2 at a natural angle no longer keeps pushing one way). Gaze, parallax, swarm chaos and the
calm room's gravity use the real direction of gravity, so rotating the Core2 always pours the liquid to the bottom.

The microphone learns the room's background level at boot (and again whenever the mantis starts singing), so
quiet rooms stay calm and sustained music keeps everything moving.

## Code map

| file | |
|---|---|
| `src/main.cpp` | frame pipeline (double-buffered, LCD push on core 0 while input keeps polling), input, modes |
| `src/calm.cpp` | the physics room: particle fluid (position-based), sand automaton, wave tank, aquarium |
| `src/audio.cpp` | always-on mic: FFT (32 bands), spectral-flux onsets, beat PLL, calm follower, brief sfx |
| `src/wire.cpp` | crisp overlays: glitching chromatic wireframes, tesseract, tunnel rings, engraved mandala lattice |
| `src/fx.cpp` | 160×120 indexed demo engine: palettes, bilinear grid-warp feedback, LUTs, dual-core raster, 2× present |
| `src/mantis.cpp` | the puppet: FK skeleton, IK legs and mic arm, choreography, face (blink, blush, mouth), stage |
| `src/mantis_rig.h` | generated sprites + joint positions |
| `tools/` | `extract.py` (cuts parts from the sprite sheet), `build_rig.py` (cleans, scales, measures joints → `mantis_rig.h`) |
