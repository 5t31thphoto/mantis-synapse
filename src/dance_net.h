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
// The type is only ever written as a template argument (followed by ',' or
// '>'), never followed by '(' - so a type named like a function-like macro
// (rooms.cpp has `struct F`, and Arduino defines F(...)) can never expand.
#include <stddef.h>
#include <esp_heap_caps.h>
template <class T, size_t N> struct dn_arr { typedef T (&ref)[N]; typedef T (*ptr)[N]; };
#define DN_BOOT_ARRAY(T, name, N) \
    dn_arr<T, (N)>::ref name = *reinterpret_cast<dn_arr<T, (N)>::ptr>( \
        heap_caps_calloc((N), sizeof(dn_arr<T, (N)>::ref) / (N), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))

void danceNetService(bool partyOn);            // call every loop: radio up only while partying
void danceNetBeat(uint16_t bpm, uint8_t beatInBar);
void danceNetMove(uint8_t moveId, uint8_t intensity);
bool danceNetUp();
