#ifndef LINK_H
#define LINK_H

#include <stdint.h>

// ===========================================================================
//  link — transport abstraction for the Smart Boiler master/slave protocol
//
//  Everything above this interface (packet building, CRC, SystemManager, the
//  demo scenarios, the UI, the slave's sensor and SSR tasks) is completely
//  transport-agnostic. Exactly the same bytes defined in boiler_protocol.h
//  travel over whichever transport is compiled in:
//
//      link_plc.cpp   — KQ-330 modem, byte-by-byte over UART with a receive
//                       state machine that reassembles frames
//      link_wifi.cpp  — UDP, where one datagram already IS one frame
//
//  Chosen at build time by -DLINK_WIFI (see link_config.h).
// ===========================================================================

// Bring the transport up. Call once, before any send/poll.
void link_begin();

// True when the transport can actually carry traffic.
// PLC: true once the UART is open. WiFi: true once associated and bound.
bool link_ready();

// Periodic housekeeping (WiFi reconnect, peer ageing). Call from the comms
// loop. Cheap and non-blocking.
void link_service();

// Transmit one complete frame.
void link_send(const uint8_t* data, uint8_t len);

// Non-blocking receive. If a complete frame is available it is copied into
// buf and its length returned; otherwise returns 0.
// NOTE: this returns *framed* bytes only — it does not validate the CRC or
// the packet type. That stays with the protocol layer above.
uint8_t link_poll(uint8_t* buf, uint8_t maxlen);

// "PLC" or "WiFi" — for log lines.
const char* link_name();

#endif // LINK_H
