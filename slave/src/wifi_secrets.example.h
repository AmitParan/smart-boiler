// ===========================================================================
//  wifi_secrets.example.h — TEMPLATE (safe to commit)
//
//  The slave has no touchscreen, so unlike the master it cannot be given WiFi
//  credentials through a setup wizard. They are compiled in instead.
//
//  SETUP:
//    1. Copy this file to  wifi_secrets.h   (same folder)
//    2. Fill in your real SSID and password there
//    3. wifi_secrets.h is listed in .gitignore, so it is NEVER committed
//
//  NOTE: ESP32 radios are 2.4 GHz only. A 5 GHz-only network will not connect.
//        The slave must be on the SAME network as the master.
// ===========================================================================
#ifndef WIFI_SECRETS_H
#define WIFI_SECRETS_H

#define WIFI_SSID       "romiamit"
#define WIFI_PASSWORD   "PUT_YOUR_WIFI_PASSWORD_HERE"

#endif // WIFI_SECRETS_H
