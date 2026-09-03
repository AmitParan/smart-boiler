// ===========================================================================
//  WIFI PROBE — SLAVE (ESP32-C6)          *** TEMPORARY DIAGNOSTIC SKETCH ***
//
//  PURPOSE: prove the slave can join the WiFi network and that UDP datagrams
//           reach the PC, BEFORE any protocol code is written.
//
//  Runs alone (build_src_filter in platformio.ini) — no FreeRTOS tasks,
//  no sensors, no SSRs. Delete the WIFI PROBE block in platformio.ini to go
//  back to the normal firmware.
//
//  WHAT IT DOES:
//    1. Refuses to run if wifi_secrets.h still holds the template placeholders
//    2. Joins the network from wifi_secrets.h
//    3. Prints IP, gateway, channel and RSSI
//    4. Broadcasts "SLAVE-HELLO <n>" on UDP port 4211 once a second
//    5. Prints anything that arrives on UDP port 4210
// ===========================================================================
#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "wifi_secrets.h"

#define PORT_CMD     4210   // slave listens here (master -> slave)
#define PORT_STATUS  4211   // slave sends here   (slave -> master)

static WiFiUDP   udp;
static uint32_t  last_tx = 0;
static uint32_t  hello_n = 0;

// Detect the untouched template so we fail loudly instead of retrying with a
// placeholder SSID forever.
static bool credentialsAreTemplate() {
    return (strcmp(WIFI_SSID, "your_network_name") == 0) ||
           (strcmp(WIFI_PASSWORD, "your_password") == 0) ||
           (strcmp(WIFI_PASSWORD, "PUT_YOUR_WIFI_PASSWORD_HERE") == 0);
}

static void scanAndReport() {
    // A scan only returns results when the radio is NOT mid-association.
    // Otherwise it silently returns 0 and looks like "no networks exist".
    WiFi.disconnect(true, false);
    delay(200);
    WiFi.scanDelete();

    Serial.println("[WIFI] Scanning 2.4 GHz band ...");
    int n = WiFi.scanNetworks();

    if (n <= 0) {
        Serial.println("[WIFI]   (scan returned NOTHING - radio problem, or no 2.4 GHz APs in range)");
        return;
    }
    Serial.printf("[WIFI]   %d network(s) visible:\n", n);
    for (int i = 0; i < n; i++) {
        Serial.printf("[WIFI]   %2d) %-32s ch%-3d %4d dBm %s\n",
                      i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
    }
    Serial.println("[WIFI]   ^ this is the 2.4 GHz view. Your network MUST appear here.");
    WiFi.scanDelete();
}

static void connectWiFi() {
    Serial.printf("[WIFI] Connecting to \"%s\" ...\n", WIFI_SSID);

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);              // keep latency low and stable
    WiFi.disconnect(true, false);      // clear any half-open association first
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] CONNECTED");
        Serial.printf("[WIFI]   IP       : %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WIFI]   Gateway  : %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("[WIFI]   Subnet   : %s\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("[WIFI]   Channel  : %d\n", WiFi.channel());
        Serial.printf("[WIFI]   RSSI     : %d dBm\n", WiFi.RSSI());
        Serial.printf("[WIFI]   MAC      : %s\n", WiFi.macAddress().c_str());
        udp.begin(PORT_CMD);
        Serial.printf("[UDP ] Listening on port %d, broadcasting on port %d\n",
                      PORT_CMD, PORT_STATUS);
    } else {
        Serial.printf("[WIFI] *** FAILED TO CONNECT to \"%s\" ***\n", WIFI_SSID);
        Serial.println("[WIFI] Wrong password, or the network has no 2.4 GHz band.");
        scanAndReport();
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== SLAVE WIFI PROBE ===");

    if (credentialsAreTemplate()) {
        Serial.println();
        Serial.println("**********************************************************");
        Serial.println("*  WIFI CREDENTIALS NOT SET                              *");
        Serial.println("*                                                        *");
        Serial.println("*  slave/src/wifi_secrets.h still contains the template   *");
        Serial.println("*  placeholders. Open that file, put your real SSID and   *");
        Serial.println("*  password in it, SAVE, then re-upload.                  *");
        Serial.println("*                                                        *");
        Serial.println("*  Edit  wifi_secrets.h  -- NOT wifi_secrets.example.h    *");
        Serial.println("**********************************************************");
        Serial.println();
        // Still scan, so you can confirm the 2.4 GHz network is reachable.
        WiFi.mode(WIFI_STA);
        scanAndReport();
        Serial.println("[WIFI] Halted. Fill in wifi_secrets.h and re-upload.");
        while (true) { delay(1000); }
    }

    connectWiFi();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        static uint32_t last_retry = 0;
        if (millis() - last_retry >= 10000UL) {
            last_retry = millis();
            Serial.println("[WIFI] Link down - retrying ...");
            connectWiFi();
        }
        delay(100);
        return;
    }

    // 1. Broadcast a hello once a second so the PC tool can hear us
    if (millis() - last_tx >= 1000UL) {
        last_tx = millis();
        hello_n++;
        char msg[64];
        int len = snprintf(msg, sizeof(msg), "SLAVE-HELLO %lu", (unsigned long)hello_n);

        IPAddress bcast = WiFi.localIP();
        bcast[3] = 255;                       // subnet broadcast, e.g. 10.0.0.255

        udp.beginPacket(bcast, PORT_STATUS);
        udp.write((const uint8_t*)msg, len);
        bool ok = udp.endPacket();

        Serial.printf("[UDP TX] \"%s\" -> %s:%d  %s\n",
                      msg, bcast.toString().c_str(), PORT_STATUS,
                      ok ? "sent" : "FAILED");
    }

    // 2. Print anything that arrives
    int sz = udp.parsePacket();
    if (sz > 0) {
        uint8_t buf[128];
        int n = udp.read(buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            Serial.printf("[UDP RX] %d bytes from %s:%d -> \"%s\"\n",
                          n, udp.remoteIP().toString().c_str(), udp.remotePort(),
                          (char*)buf);
        }
    }

    delay(5);
}
