// dance_net.h - Mantis Synapse -> MantisNow DANCE sender (see MANTISNOW.md).
// Lets T-Display "Dance Party" mantises dance in time with this one.
#pragma once
#include <stdint.h>
void danceNetService(bool mantisModeActive);   // call every loop: radio up/down
void danceNetBeat(uint16_t bpm, uint8_t beatInBar);
void danceNetMove(uint8_t moveId, uint8_t intensity);
