// ============================================================
//  SYNAPSE — crisp full-res overlays (no feedback echo)
//  Glitching chromatic wireframes, a 4D tesseract crystal, tunnel rings,
//  and an engraved tessellating mandala. Mantis palette: teal / plum / lime.
// ============================================================
#pragma once
#include <stdint.h>

namespace wire {
static const uint16_t TEAL = 0x0396;   // #007373
static const uint16_t PLUM = 0x580B;   // #5d005d
static const uint16_t LIME = 0x8FE5;   // lime green
void line(int x0, int y0, int x1, int y1, uint16_t col, float glitch);   // chromatic + glitch
void tesseract(float cx, float cy, float size, float a1, float a2, float a3, float glitch, uint16_t col);
void tunnelRings(float vpX, float vpY, float phase, float twist, float energy, float glitch, bool recede);
void engrave(float cx, float cy, float spacing, float rot, int strength);  // lattice + flower of life, lifted into the image
}
