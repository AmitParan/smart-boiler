#!/usr/bin/env python3
"""
serial_logger.py — capture + parse the debug Serial output of the Smart Boiler
Master or Slave board into a timestamped CSV, ready for graphing.

Does NOT touch the firmware. Reads whatever the board already prints on its
USB debug Serial port (the "[M<-S] ..." / "[S->M] ..." style lines defined by
boiler_protocol.h and printed from comms_master.cpp / plc_comms.cpp).

Run this independently on each computer, pointed at the board plugged into
that machine. Produces two files per run:
  - <label>_raw_<timestamp>.log  — every line, verbatim, with a local timestamp
  - <label>_data_<timestamp>.csv — parsed rows, one per STATUS/CMD frame or event

Usage:
  python serial_logger.py --list
  python serial_logger.py --port COM11 --label master
  python serial_logger.py --port COM7  --label slave --baud 115200

Stop with Ctrl+C. The CSV is flushed after every line, so it's safe to open
the file for viewing (read-only) while the logger is still running.
"""

import argparse
import csv
import datetime as dt
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial is required. Install with:  pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Line formats (mirrors the Serial.printf calls in comms_master.cpp / plc_comms.cpp)
# ---------------------------------------------------------------------------

STATUS_RE = re.compile(
    r"\[(?P<dir>[SM]<?-?>?[SM])\]\s+seq=\s*(?P<seq>\d+)\s*\|\s*"
    r"t1=\s*(?P<t1>-?\d+\.\d)\s+t2=\s*(?P<t2>-?\d+\.\d)\s+t3=\s*(?P<t3>-?\d+\.\d)\s*\|\s*"
    r"flow=\s*(?P<flow>-?\d+\.\d)\s+pwr=\s*(?P<pwr>-?\d+)W\s*\|\s*sts=0x(?P<sts>[0-9A-Fa-f]{2})"
)

CMD_RE = re.compile(
    r"\[(?P<dir>[SM]<?-?>?[SM])\]\s+seq=\s*(?P<seq>\d+)\s*\|\s*"
    r"pwmInt=\s*(?P<pwmint>\d+)%\s+pwmBst=\s*(?P<pwmbst>\d+)%\s*\|\s*"
    r"flags=0x(?P<flags>[0-9A-Fa-f]{2})(?:\s*\|\s*\[(?P<state>[^\]]*)\])?"
)

DROP_RE = re.compile(
    r"\[(?P<dir>[SM]<?-?>?[SM])\]\s+DROP:\s+expected seq=(?P<expected>\d+)\s+got seq=(?P<got>\d+)"
)

CRC_RE = re.compile(
    r"CRC error \(calc 0x(?P<calc>[0-9A-Fa-f]{2}) recv 0x(?P<recv>[0-9A-Fa-f]{2})\)"
)

TIMEOUT_RE = re.compile(r"No STATUS received within 3s|timeout")

CSV_FIELDS = [
    "timestamp", "msg_type", "direction", "seq",
    "t_internal_c", "t_boiler_c", "t_boost_c",
    "flow_lpm", "power_w", "status_hex",
    "pwm_internal_pct", "pwm_boost_pct", "cmd_flags_hex", "state_label",
    "detail", "raw_line",
]


def classify(line: str):
    """Return a dict of CSV_FIELDS values for a recognised line, or None."""
    m = STATUS_RE.search(line)
    if m:
        return {
            "msg_type": "STATUS",
            "direction": m.group("dir"),
            "seq": m.group("seq"),
            "t_internal_c": m.group("t1"),
            "t_boiler_c": m.group("t2"),
            "t_boost_c": m.group("t3"),
            "flow_lpm": m.group("flow"),
            "power_w": m.group("pwr"),
            "status_hex": m.group("sts"),
        }

    m = CMD_RE.search(line)
    if m:
        return {
            "msg_type": "CMD",
            "direction": m.group("dir"),
            "seq": m.group("seq"),
            "pwm_internal_pct": m.group("pwmint"),
            "pwm_boost_pct": m.group("pwmbst"),
            "cmd_flags_hex": m.group("flags"),
            "state_label": m.group("state") or "",
        }

    m = DROP_RE.search(line)
    if m:
        return {
            "msg_type": "DROP",
            "direction": m.group("dir"),
            "detail": f"expected={m.group('expected')} got={m.group('got')}",
        }

    m = CRC_RE.search(line)
    if m:
        return {
            "msg_type": "CRC_ERROR",
            "detail": f"calc=0x{m.group('calc')} recv=0x{m.group('recv')}",
        }

    if TIMEOUT_RE.search(line):
        return {"msg_type": "TIMEOUT", "detail": line.strip()}

    return None


def list_serial_ports():
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    print("Available serial ports:")
    for p in ports:
        print(f"  {p.device:10s} {p.description}")


def run(port: str, baud: int, label: str, outdir: str):
    import os
    os.makedirs(outdir, exist_ok=True)

    run_stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    raw_path = os.path.join(outdir, f"{label}_raw_{run_stamp}.log")
    csv_path = os.path.join(outdir, f"{label}_data_{run_stamp}.csv")

    print(f"Opening {port} @ {baud} baud ...")
    ser = serial.Serial(port, baud, timeout=1)

    print(f"Raw log : {raw_path}")
    print(f"CSV data: {csv_path}")
    print("Logging started. Press Ctrl+C to stop.\n")

    with open(raw_path, "a", encoding="utf-8") as raw_f, \
         open(csv_path, "a", newline="", encoding="utf-8") as csv_f:

        writer = csv.DictWriter(csv_f, fieldnames=CSV_FIELDS)
        if csv_f.tell() == 0:
            writer.writeheader()

        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue

                ts = dt.datetime.now().isoformat(sep=" ", timespec="milliseconds")
                raw_f.write(f"{ts}  {line}\n")
                raw_f.flush()

                parsed = classify(line)
                if parsed is not None:
                    row = {k: "" for k in CSV_FIELDS}
                    row["timestamp"] = ts
                    row["raw_line"] = line
                    row.update(parsed)
                    writer.writerow(row)
                    csv_f.flush()

                print(f"{ts}  {line}")

        except KeyboardInterrupt:
            print("\nStopped by user.")
        finally:
            ser.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="Serial port, e.g. COM11")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200)")
    ap.add_argument("--label", default="board", help="Prefix for output files, e.g. master / slave")
    ap.add_argument("--outdir", default="logs", help="Output directory (default ./logs)")
    ap.add_argument("--list", action="store_true", help="List available serial ports and exit")
    args = ap.parse_args()

    if args.list or not args.port:
        list_serial_ports()
        if not args.port:
            print("\nRe-run with --port <PORT> --label <master|slave> to start logging.")
        return

    run(args.port, args.baud, args.label, args.outdir)


if __name__ == "__main__":
    main()
