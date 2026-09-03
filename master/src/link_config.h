#ifndef LINK_CONFIG_H
#define LINK_CONFIG_H

// ===========================================================================
//  Transport configuration — shared by Master and Slave.
//  Keep this file IDENTICAL on both sides.
//
//  The project supports two physical transports carrying the exact same
//  binary frames defined in boiler_protocol.h:
//
//    LINK_PLC   — KQ-330 modem over the 220 V power line (UART, 9600 baud)
//    LINK_WIFI  — UDP datagrams over WiFi   (build with -DLINK_WIFI)
//
//  Select the transport with a build flag in platformio.ini:
//      build_flags = -DLINK_WIFI      ; WiFi
//      (flag absent)                  ; PLC
// ===========================================================================

// ---------------------------------------------------------------------------
//  UDP ports (LINK_WIFI only)
//  Two ports, one per direction, so neither side ever hears its own traffic.
// ---------------------------------------------------------------------------
#define LINK_UDP_PORT_CMD      4210u   // Master -> Slave  (slave listens here)
#define LINK_UDP_PORT_STATUS   4211u   // Slave  -> Master (master listens here)

// ---------------------------------------------------------------------------
//  Discovery
//  Neither IP address is configured anywhere. The slave broadcasts STATUS
//  until it has heard from the master; the master learns the slave's address
//  from the first STATUS it receives and unicasts CMD from then on. DHCP may
//  hand out new addresses after a reboot — this rediscovers automatically.
// ---------------------------------------------------------------------------
#define LINK_PEER_TIMEOUT_MS   10000u  // forget a peer address after this silence

// ---------------------------------------------------------------------------
//  Largest frame either direction: STATUS is 17 bytes on the wire.
// ---------------------------------------------------------------------------
#define LINK_MAX_FRAME         32u

#endif // LINK_CONFIG_H
