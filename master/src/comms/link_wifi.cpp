// ===========================================================================
//  link_wifi.cpp — MASTER transport over WiFi (UDP)
//
//  Compiled only when LINK_WIFI IS defined.
//
//  IMPORTANT DIFFERENCE FROM THE SLAVE
//  The master does NOT manage the WiFi connection here. main.cpp already owns
//  it: it runs WIFI_AP_STA, hosts the "Boiler-Setup" access point, stores the
//  credentials the user enters in the setup wizard, reconnects on drop, and
//  needs the station link for NTP, weather and the solar forecast. This file
//  therefore only waits for that connection to come up and binds a UDP socket
//  on top of it. Touching WiFi.begin() here would fight the setup wizard.
//
//  Why this is so much simpler than link_plc.cpp: UDP preserves message
//  boundaries. One datagram in, one datagram out — so there is no receive
//  state machine, no inter-byte delay, and no half-duplex guard time. All of
//  that was KQ-330 hardware behaviour, not protocol behaviour.
//
//  DISCOVERY (no IP address is configured anywhere)
//    - The slave broadcasts STATUS until it has heard from a master.
//    - The master learns the slave's address from the first STATUS it
//      receives and unicasts CMD to it from then on.
//    - If the slave goes quiet for LINK_PEER_TIMEOUT_MS the address is
//      forgotten and the master falls back to broadcasting, so a slave reboot
//      onto a new DHCP address recovers on its own.
// ===========================================================================
#include "link.h"
#include "link_config.h"
#include <Arduino.h>

#ifdef LINK_WIFI

#include <WiFi.h>
#include <WiFiUdp.h>

static WiFiUDP   udp;
static bool      udp_started   = false;
static IPAddress slave_ip;
static bool      slave_known   = false;
static uint32_t  slave_last_ms = 0u;

// Bind the UDP socket once the station link that main.cpp manages is up.
static void startUdpIfPossible() {
    if (udp_started) return;
    if (WiFi.status() != WL_CONNECTED) return;

    udp.begin(LINK_UDP_PORT_STATUS);
    udp_started = true;
    Serial.printf("[LINK] WiFi transport up  IP=%s  ch=%d  RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
    Serial.printf("[LINK] UDP listening on %u, sending on %u\n",
                  (unsigned)LINK_UDP_PORT_STATUS, (unsigned)LINK_UDP_PORT_CMD);
    Serial.println("[LINK] waiting to discover the slave from its first STATUS ...");
}

void link_begin() {
    Serial.println("[LINK] WiFi transport selected (station managed by main.cpp)");
    startUdpIfPossible();   // may not be connected yet; link_service() retries
}

bool link_ready() {
    return udp_started && (WiFi.status() == WL_CONNECTED);
}

void link_service() {
    // The station may come up after boot (setup wizard) or drop and return.
    if (WiFi.status() != WL_CONNECTED) {
        if (udp_started) {
            Serial.println("[LINK] WiFi station down - UDP suspended");
            udp.stop();
            udp_started  = false;
            slave_known  = false;
        }
        return;
    }
    startUdpIfPossible();

    // Forget a silent slave so we fall back to broadcast and rediscover it.
    if (slave_known && (millis() - slave_last_ms) > LINK_PEER_TIMEOUT_MS) {
        Serial.println("[LINK] slave silent - reverting to broadcast discovery");
        slave_known = false;
    }
}

void link_send(const uint8_t* data, uint8_t len) {
    if (!link_ready()) return;

    IPAddress dst;
    if (slave_known) {
        dst = slave_ip;
    } else {
        dst = WiFi.localIP();
        dst[3] = 255;                       // subnet broadcast, e.g. 10.0.0.255
    }

    udp.beginPacket(dst, LINK_UDP_PORT_CMD);
    udp.write(data, len);
    udp.endPacket();
}

uint8_t link_poll(uint8_t* buf, uint8_t maxlen) {
    if (!link_ready()) return 0u;

    int sz = udp.parsePacket();
    if (sz <= 0) return 0u;

    int n = udp.read(buf, maxlen);
    if (n <= 0) return 0u;

    // Learn (or refresh) the slave's address from whoever just sent to us.
    IPAddress from = udp.remoteIP();
    if (!slave_known || from != slave_ip) {
        slave_ip    = from;
        slave_known = true;
        Serial.printf("[LINK] slave discovered at %s\n", slave_ip.toString().c_str());
    }
    slave_last_ms = millis();

    return (uint8_t)n;
}

const char* link_name() {
    return "WiFi";
}

#endif // LINK_WIFI
