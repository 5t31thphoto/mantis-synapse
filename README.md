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
**Long-press B** changes the eye: basic → cat (a slit that opens with sound, slow blinks when it's quiet) → dragon
(molten iris, knife slit; poke it a few times and it smoulders, throwing embers, sparks and smoke).

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
* **flow** — a sealed chamber seen through a window: the liquid settles wherever is really down (lying flat, it
  spreads into a sheet on the far glass); stir it with a finger; moving liquid glows, still liquid rests.
  Sound thins it and tints it. Long-press: neon → honey → mercury (surface tension you can see).
* **splash** — looking down into a koi pool: ripples refract the mosaic floor and the fish, and throw caustics and
  glints. Tilt sloshes the whole pool, tap drops a splash (the koi scatter), drag leaves a wake, hold a finger still
  and the koi come to feed. Long-press: gentle rain.
* **sand** — sand art between glass panes. Turn it over and watch the layers stream through the water into new
  landscapes; fingertip pushes grains aside; a sound lets a bubble go. Long-press: a new sand picture.
* **waves** — a wave-machine tank with a paper boat. Tilt to slosh, drag through the surface, hum to raise the swell.
  **Storms:** sustained sound and a turbulent sea (your hands, sloshing, big swells) build a storm; the storm
  whips the sea up in return with gusts and bigger waves, darkens and lowers the clouds, and brings driving rain.
  Lightning strikes at a distance: close strikes show a bolt and hit with an instant crack, far ones flash in the
  clouds; either way the thunder arrives later the further away it was, and rolls through the motor.
  Go quiet and the storm slowly passes.
  **Hold B** for another boat: the paper boat (classic), the mantis on a jet ski (hunts steep faces and launches off
  them, flips off the walls), in a wooden speedboat (drives and shreds; bumps are fine, but charge a wall hard enough
  and it splinters, then drops back in), or on a surfboard (paddles to the biggest wave, stands up and rides it...
  or wipes out). Rider sprites: src/rider_sprites.h (generated from the sprite sheet).
  Long-press: night, with moon glitter and bioluminescent crests.
* **aquarium** — blacklight pebbles that pulse with the bass, plankton you can stir, a pulsing jellyfish, fish that
  wander, dart from taps and loud sounds and come to look at a held finger, bubbles that wobble, merge and pop.
  Tap a bubble to pop it, tap water to release some, hold to stream them. Long-press: UV on / off.

**MANTIS** — the whole bug, built from separated sprites on a skeleton (IK legs, planted feet, squash & stretch),
dancing beat-locked moves chosen by energy. Tap its head to pet it (happy eyes, blush, hearts). Idle, it breathes,
looks around and waves. **B: sing** — close-up, holding a mic, mouth lip-syncing your voice (open with loudness,
wide on bright sounds), notes floating out.
**The dancer** reads the music: it picks a style for each phrase (groove, pop & lock, hype, smooth), switches to
fast footwork (running man, T-step) when it hears a flurry of quick hits, and lands an emote now and then on the
fourth beat after three strong ones (claw clap, pinch-snap, point, flex, air guitar, peace claws; a prayer bow to
close a phrase). Its pincers open and close.
**B** cycles dance → sing → **cave**: the mantis stands at the mouth of a cave and mouths along as you talk. Press
**B** and it leans in, claws cupped, and records you; then it says your message into the cave in its own voice, and
the cave echoes it back. Hold **B** to leave the cave.

## Drums moved

The drum machine now lives in its own firmware, **Mantis Studio** (drums, lead, bass, audio track, mixer, and the
dancing mantis). Synapse keeps the microphone on permanently, so every mode hears the room without dropouts, and
the per-pixel effects render on both CPU cores.

## Portal flights

Every dimension is deeper than the last: you start in real space (black sky, dusty nebulae, white-blue stars)
and it grows stranger with every portal, all the way to witch-space. Between gates you fly past stellar
landmarks: suns, ringed planets, gas giants, rarely a white dwarf or a pulsar, and very rarely a black hole whose
lensing bends the view (and the gates).

Fly the ether and steer through the hoops that appear in the distance (tilt, drag, or hold a finger where you
want to go; the reticle leans with your steering, and lining up close to a hoop gets a gentle assist). Thread **three in a row** (the dots at the
top fill up; a miss resets the chain) and a portal appears far ahead. The next dimension is already visible inside
it: fly into it and you're there, no loading screen. Speed follows the music; tilt or touch to steer.

## Garden

Two gardens that grow from how you spend your time in Synapse (there are no numbers - the growth is the feedback).
**B** switches between them. They keep growing while the Core2 is off if its clock is set.

* **crystals** - quartz grows from quiet, amethyst from sound, bismuth from play (rooms, portals, hoops), fluorite
  from motion, opal from care. Dust settles over real time and slows them: swipe to brush it off. Hold the mist
  bottle to tend them. Tap a crystal and it rings.
* **succulents** - five plants, a zen sand bed to rake with a finger and a little rock waterfall. The soil dries over
  a few real days: tap it to water (not too much - they like it dry-ish). Drag a fallen leaf away. Hold the pool to
  refill the waterfall. Healthy plants grow, sprout pups and eventually bloom. The light follows the time of day.

## Meditate

A mantis meditation you feel more than see. First it teaches its five haptic words, one at a time in your hand
(breathe in, breathe out, well done, your heartbeat, we're finishing - tap for the next, B to skip). Then:
"lie down somewhere quiet and place me flat on your chest". It begins by itself when it feels you breathing:
it mirrors your breath, then slowly lengthens it (only while you follow), goes silent to listen for your heartbeat
through your chest (it echoes it back only if it genuinely found it), and closes with three slow pulses and
singing-bowl bells. The screen dims while you rest. Lifting the Core2 pauses it; **B** ends it gently.

## Rooms

A separate mode (after MANTIS): small physical puzzles with no instructions. The header names the room; the
"next" label over **B** stays grey until you've solved it. Every visit starts at Mantis NRG, and after the last
room it loops back there.

| room | what it wants |
|---|---|
| nrg · popcorn · ketchup | shake it: the soda blows / the kernels pop until the lid flies / turn the ketchup upside down AND shake |
| breeze · candles · dandelion | blow: the pinwheel / out every candle / every seed away |
| arcade · pump · drumroll | mash: the red button / pump the balloon until it floats off / keep a drum roll going for the crash |
| wrap · soap · stars | poke them all: bubble wrap / drifting soap bubbles / light every star (a constellation appears) |
| clap · echo · clapper | clap: along with the mantis / repeat its pattern / clap-clap for the lamp |
| seed · fishbowl · marble | tip: pour the can / fill the fishbowl / roll the marble through the maze |
| shade · burrow · window | drag: the cloud off the sun / the rock off the burrow / wipe the foggy window |
| align · level · mirror | tilt precisely: lock the rows / centre the bubble / bounce the beam into the crystal |
| knock · coconut · egg | knock (the Core2's case counts): the door / crack the coconut / hatch the egg |
| globe · 8-ball · kitten | shake, then keep still: the snow globe / the magic 8-ball's answer / rock the kitten to sleep |
| hum · pitch · levitate | hum: shatter the glass / match the mantis's note / lift the stone |
| hush · sneak · snowfall | be quiet: the firefly lands / the ant sneaks past the guard / the snow builds a snowman |

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
  storm lightning brings a crack (if it's close) and thunder that arrives after its delay and rolls.
* **Calibration** - a heartbeat that quickens as the ring closes, the motor goes completely still while the gyro
  calibrates (so it can't disturb the measurement), then two soft pulses: done.

## Tilt and steering

Everything that uses gravity (the calm room's liquids, sand, bubbles, swarm chaos) follows real gravity:
tip the Core2 and things slide to the side that is really down.

The flying modes (dive, recede, fractal, portal) steer by how far you **rotate the Core2 away from the way you're
holding it**: tip a little, steer a little; tip more, steer more - even past 90 degrees - and it keeps steering for as
long as you hold the tilt. Each time you enter a flying mode it centres on your grip as soon as you hold still for a
moment ("centered"). To re-centre any time, hold a finger still in the middle of the screen: after 2 s a ring closes
in, then it takes your current grip as centre ("hold it how you like - calibrating"; this also runs M5Unified's gyro
offset calibration and saves both).

Touch works as a fallback in every flying mode: drag to move (grab the space and pull it), and in the portal you can
also hold a finger where you want to fly.

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
