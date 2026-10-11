// dance_net.cpp - broadcasts MantisNow kind 0x30 DANCE frames (MANTISNOW.md,
// decision 6) while the dancing mantis is on screen. Send-only; the radio is
// brought up when mantis mode starts and fully released when it ends, so it
// never coexists with anything else that wants WiFi.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include "dance_net.h"

static const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static bool s_up = false;
static uint16_t s_seq = 0;
static uint8_t s_lastMove = 0xFF;
static uint32_t s_t0 = 0;

struct __attribute__((packed)) MnHeader {
    uint8_t magic, version, kind, flags;
    uint32_t srcNode, dstNode, pktId;
    uint16_t seq;
    uint8_t ttl, chanOrSub;
    char callsign[8];
};

static void send(uint8_t sub, const void* pl, int n) {
    if (!s_up) return;
    uint8_t buf[28 + 16];
    MnHeader h; memset(&h, 0, sizeof(h));
    h.magic = 0x4D; h.version = 1; h.kind = 0x30;
    h.srcNode = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFFull);
    h.dstNode = 0xFFFFFFFFu; h.pktId = esp_random(); h.seq = ++s_seq; h.chanOrSub = sub;
    memcpy(h.callsign, "SYNAPSE", 7);
    memcpy(buf, &h, 28);
    if (n > 0) memcpy(buf + 28, pl, (size_t)n);
    esp_now_send(BCAST, buf, (size_t)(28 + n));
}

static void up() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    if (esp_now_init() != ESP_OK) return;
    esp_now_peer_info_t p; memset(&p, 0, sizeof(p));
    memcpy(p.peer_addr, BCAST, 6); p.channel = 0; p.encrypt = false;
    if (!esp_now_is_peer_exist(BCAST)) esp_now_add_peer(&p);
    s_up = true; s_t0 = millis(); s_lastMove = 0xFF;
    uint16_t bpm = 0; send(2, &bpm, 2);                       // START
}

static void down() {
    if (s_up) { send(3, nullptr, 0); delay(20); esp_now_deinit(); s_up = false; }   // STOP
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

void danceNetService(bool active) {
    if (active && !s_up) up();
    else if (!active && s_up) down();
}

void danceNetBeat(uint16_t bpm, uint8_t beatInBar) {
    uint8_t pl[7]; uint32_t ms = millis() - s_t0;
    memcpy(pl, &bpm, 2); pl[2] = beatInBar & 3; memcpy(pl + 3, &ms, 4);
    send(0, pl, 7);
}

void danceNetMove(uint8_t moveId, uint8_t intensity) {
    if (moveId == s_lastMove) return;
    s_lastMove = moveId;
    uint8_t pl[4] = { moveId, intensity, 0, 0 };
    send(1, pl, 4);
}
