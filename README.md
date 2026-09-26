# Mantis build template — handoff for the next project

This archive is a **starting point** for ESP32-class firmware (including M5Stack boards). It encodes the workflow we learned building Mantis-style products: **PlatformIO + GitHub Actions + zip-first overlays + GitHub Pages web flasher**.

The sample app in `src/` is only proof the pipeline works. **Ignore it as a product.** When you open a new conversation, attach **this zip** and describe **hardware + goal**; the structure below is what to follow.

---

## What you get

| Path | Role |
|------|------|
| `platformio.ini` | Board, flags, `lib_deps`, post-build merge |
| `merge_bin.py` | Merged binary for browser flashing at offset `0x0` |
| `src/main.cpp` | Firmware entry (replace entirely for a new product) |
| `website/` | Pages site + `firmware/manifest.json` + install button |
| `.github/workflows/ci.yml` | Unzip → build → stage bin → deploy Pages |
| `README.md` | This file — process, not product copy |

---

## Zip-first CI (non-negotiable pattern)

1. Optional **root** `*.zip` on push = payload overlay.
2. CI **extracts first**, flattens a single top-level folder if needed, **deletes the zip**, commits with `[skip ci]` when the tree changed.
3. **Then** `pio run`, copy merged bin to `website/firmware/`, deploy Pages.

No root zip → build the checked-out tree as-is.

**Update model:** zip only deltas; extract is add/replace. Do not rely on ephemeral CI workspaces as the source of truth — the consumed tree in the repo is the truth after extract.

---

## PlatformIO defaults in this skeleton

- `platform = espressif32@6.9.0` (pin versions; drift breaks builds)
- Example board: `m5stack-core2` — change `board =` for other modules
- `framework = arduino`
- `-DBOARD_HAS_PSRAM` when applicable
- M5 boards: prefer **M5Unified** / **M5GFX** over hand-rolled PMIC/touch
- `extra_scripts = post:merge_bin.py` for web-flasher-friendly images

Local:

```bash
pio run -e m5stack-core2
pio run -e m5stack-core2 -t upload
```

Add envs for extra boards rather than overloading one env when pinouts/chips differ (classic ESP32 vs S3, etc.).

---

## Headers and source layout

1. Board/HAL include first (`M5Unified.h`, or Arduino + board package)
2. C/C++ std and utilities
3. WiFi / HTTP / FS only if needed
4. Project headers in `"quotes"` with `#pragma once`

Start with a single `src/main.cpp`. Split when files grow: `src/foo.cpp` + matching headers. Keep heavy dependencies out of widely included headers.

```cpp
#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
}

void loop() {
  M5.update();  // always poll input / IMU / buttons
  // non-blocking work; yield during long network reads
}
```

---

## Rules we learned (apply on every Mantis-style job)

- **Input must stay alive:** never block for seconds without `M5.update()` / button poll; budget network reads.
- **Don’t redraw expensive work every frame** (e.g. re-decode a still JPEG on every UI refresh).
- **Merged bin** for Pages/web install; manifest `chipFamily` must match the silicon (ESP32 vs ESP32-S3).
- **One binary ≠ every pinout.** Different cameras/boards need different envs or probe tables.
- **Factory vs product:** browser/PC is for **flash only** unless the product explicitly needs more; day-to-day use should live on-device when that’s the design.
- **NVS / flash config** for per-unit identity when many devices share one app image.
- **CI zip consume** must write into the real repo (commit), not only an ephemeral runner copy, if the next push depends on extracted sources.

---

## Website / flasher

- `website/index.html` — product-facing copy + `<esp-web-install-button manifest="firmware/manifest.json">`
- `website/firmware/manifest.json` — points at `firmware-merged.bin`, offset `0` for merged images
- Users need Chrome/Edge and a data USB cable

Rewrite the HTML for each product; keep the install plumbing.

---

## New project checklist

1. Copy this repo or start from this zip.
2. Set `board` / `lib_deps` / `build_flags` for the hardware.
3. Replace `src/main.cpp` (and assets).
4. Rewrite `website/index.html`; keep manifest path and CI stage path in sync.
5. Enable **Pages from Actions**; ensure workflow permissions (`contents`, `pages`, `id-token`).
6. Push tree or push a **root payload zip** for overlay updates.

---

## How to use this in a future chat

Provide:

1. **This archive** (or the repo that grew from it)
2. **Hardware** — exact module(s), sensors, displays, cameras
3. **Goal** — what the firmware and site should do
4. **Constraints** — portable vs LAN, no extra screens, power, etc.

Then: adapt `platformio.ini`, implement `src/`, keep **ci.yml zip-first contract**, ship via Pages flasher.

---

## License

Free to reuse as a pipeline template for your own ESP32 / M5 work.
