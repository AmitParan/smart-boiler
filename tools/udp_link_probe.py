#!/usr/bin/env python3
"""
udp_link_probe.py — PC-side probe for the Smart Boiler WiFi link.

Lets you test either board WITHOUT the other one present:

  * listen  — show every datagram the slave (or master) broadcasts
  * send    — inject a datagram, to check the board receives it

Ports (must match link_config.h):
    4210  master -> slave  (CMD)
    4211  slave  -> master (STATUS)

Examples:
    python tools/udp_link_probe.py listen                 # hear the slave
    python tools/udp_link_probe.py listen --port 4210     # hear the master
    python tools/udp_link_probe.py send  --text "HELLO-FROM-PC"
"""
import argparse
import socket
import sys
import time


def hexdump(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def printable(data: bytes) -> str:
    return "".join(chr(b) if 32 <= b < 127 else "." for b in data)


def do_listen(port: int) -> None:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", port))
    print(f"Listening on UDP port {port}  (Ctrl+C to stop)\n")
    count = 0
    while True:
        data, addr = s.recvfrom(2048)
        count += 1
        ts = time.strftime("%H:%M:%S")
        print(f"[{ts}] #{count:<4} {addr[0]}:{addr[1]}  {len(data)} bytes")
        print(f"           hex : {hexdump(data)}")
        print(f"           text: {printable(data)}")


def do_send(host: str, port: int, text: str, repeat: int, interval: float) -> None:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    payload = text.encode()
    for i in range(repeat):
        s.sendto(payload, (host, port))
        print(f"sent {len(payload)} bytes -> {host}:{port}  \"{text}\"")
        if i + 1 < repeat:
            time.sleep(interval)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_listen = sub.add_parser("listen", help="print incoming datagrams")
    p_listen.add_argument("--port", type=int, default=4211,
                          help="UDP port to bind (default 4211 = slave STATUS)")

    p_send = sub.add_parser("send", help="send a datagram")
    p_send.add_argument("--host", default="255.255.255.255",
                        help="destination IP (default: broadcast)")
    p_send.add_argument("--port", type=int, default=4210,
                        help="UDP port (default 4210 = master CMD)")
    p_send.add_argument("--text", default="HELLO-FROM-PC", help="payload text")
    p_send.add_argument("--repeat", type=int, default=1, help="how many times")
    p_send.add_argument("--interval", type=float, default=1.0, help="seconds between")

    args = ap.parse_args()
    try:
        if args.cmd == "listen":
            do_listen(args.port)
        else:
            do_send(args.host, args.port, args.text, args.repeat, args.interval)
    except KeyboardInterrupt:
        print("\nstopped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
