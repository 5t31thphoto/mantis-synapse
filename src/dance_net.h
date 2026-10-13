// dance_net.h - Mantis Synapse -> MantisNow DANCE sender (see MANTISNOW.md).
// Lets T-Display "Dance Party" mantises dance in time with this one.
//
#pragma once
#include <stdint.h>
// DN_BOOT_ARRAY(T, name, N) declares a static buffer that lives in internal
// RAM taken once at boot (heap_caps_calloc, zeroed) instead of link-time DRAM.
// `name` is a reference to a T[N], so indexing, range-for and sizeof behave
// exactly like the plain array. This moves ~20 KB out of .dram0.bss - room for
// the radio driver ESP-NOW needs - into heap that Bluetooth would otherwise
// reserve (the Arduino core releases it to the heap when BT is never started).
// Same internal RAM, same speed, allocated once, never freed.
#include <esp_heap_caps.h>
#define DN_BOOT_ARRAY(T, name, N) \
    T (&name)[N] = *reinterpret_cast<T(*)[N]>(heap_caps_calloc(1, sizeof(T[N]), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))

void danceNetService(bool partyOn);            // call every loop: radio up only while partying
void danceNetBeat(uint16_t bpm, uint8_t beatInBar);
void danceNetMove(uint8_t moveId, uint8_t intensity);
bool danceNetUp();
