// ===========================================================================
//  link_wifi.cpp — SLAVE transport over WiFi (UDP)
//
//  Compiled only when LINK_WIFI IS defined.
//
//  Why this is so much simpler than link_plc.cpp: UDP preserves message
//  boundaries. One datagram in, one datagram out — so there is no receive
//  state machine, no inter-byte delay, and no half-duplex guard time. All of
//  that was KQ-330 hardware behaviour, not protocol behaviour.
//
//  DISCOVERY (no IP address is configured anywhere)
//    - Slave broadcasts STATUS to x.x.x.255 until it has heard from a master.
//    - The first CMD that arrives reveals the master's address; the slave
//      unicasts to it from then on.
//    - If the master goes quiet for LINK_PEER_TIMEOUT_MS the address is
//      forgotten and the slave falls back to broadcasting, so a master reboot
//      onto a new DHCP address recovers on its own.
// ===========================================================================
#include "link.h"
#include "link_config.h"
#include <Arduino.h>

#ifdef LINK_WIFI

#include <WiFi.h>
#include <WiFiUdp.h>

#if __has_include("wifi_secrets.h")
  #include "wifi_secrets.h"
#else
  #error "wifi_secrets.h is missing. Copy wifi_secrets.example.h to wifi_secrets.h and fill in your WiFi credentials (that file is gitignored)."
#endif

static WiFiUDP   udp;
static bool      udp_started    = false;
static IPAddress master_ip;
static bool      master_known   = false;
static uint32_t  master_last_ms = 0u;

static void wifiConnectBlocking() {
    Serial.printf("[LINK] WiFi connecting to \"%s\" ...\n", WIFI_SSID);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);          // latency matters more than power here
    WiFi.disconnect(true, false);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[LINK] WiFi connected  IP=%s  ch=%d  RSSI=%d dBm\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.channel(), WiFi.RSSI());
        udp.begin(LINK_UDP_PORT_CMD);
        udp_started = true;
        Serial.printf("[LINK] UDP listening on %u, sending on %u\n",
                      (unsigned)LINK_UDP_PORT_CMD, (unsigned)LINK_UDP_PORT_STATUS);
    } else {
        Serial.println("[LINK] WiFi connect FAILED - will retry");
    }
}

void link_begin() {
    wifiConnectBlocking();
}

bool link_ready() {
    return udp_started && (WiFi.status() == WL_CONNECTED);
}

void link_service() {
    // Reconnect if the association drops. Rate-limited so a down network
    // cannot monopolise the comms task.
    if (WiFi.status() != WL_CONNECTED) {
        static uint32_t last_retry = 0u;
        udp_started = false;
        if (millis() - last_retry >= 5000UL) {
            last_retry = millis();
            Serial.println("[LINK] WiFi link down - reconnecting ...");
            wifiConnectBlocking();
        }
        return;
    }

    // Forget a silent master so we fall back to broadcast and rediscover it.
    if (master_known && (millis() - master_last_ms) > LINK_PEER_TIMEOUT_MS) {
        Serial.println("[LINK] master silent - reverting to broadcast discovery");
        master_known = false;
    }
}

void link_send(const uint8_t* data, uint8_t len) {
    if (!link_ready()) return;

    IPAddress dst;
    if (master_known) {
        dst = master_ip;
    } else {
        dst = WiFi.localIP();
        dst[3] = 255;                       // subnet broadcast, e.g. 10.0.0.255
    }

    udp.beginPacket(dst, LINK_UDP_PORT_STATUS);
    udp.write(data, len);
    udp.endPacket();
}

uint8_t link_poll(uint8_t* buf, uint8_t maxlen) {
    if (!link_ready()) return 0u;

    int sz = udp.parsePacket();
    if (sz <= 0) return 0u;

    int n = udp.read(buf, maxlen);
    if (n <= 0) return 0u;

    // Learn (or refresh) the master's address from whoever just sent to us.
    IPAddress from = udp.remoteIP();
    if (!master_known || from != master_ip) {
        master_ip    = from;
        master_known = true;
        Serial.printf("[LINK] master discovered at %s\n", master_ip.toString().c_str());
    }
    master_last_ms = millis();

    return (uint8_t)n;
}

const char* link_name() {
    return "WiFi";
}

#endif // LINK_WIFI
