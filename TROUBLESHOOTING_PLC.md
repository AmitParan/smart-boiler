# PLC Communication Troubleshooting — KQ-330 Link

**Use this when master ↔ slave stop communicating.** Work top-to-bottom, in order.
Do **not** start changing firmware — history shows the code is stable; the fault is
almost always the **physical modem link**.

---

## 0. Symptom reference (what "broken" looks like)

| Where | Healthy | Broken |
|-------|---------|--------|
| Master monitor | `[M<-S]` lines with matching seq | `[COMMS] No STATUS received within 3s` |
| Slave monitor  | `[S<-M]` lines / mode switches | only `[SLAVE]` heartbeat, no `[S<-M]` |
| Ping test (slave) | `[SLAVE GOT] "PING5"` clean text | garbage bytes `0xFF 0x7F 0xFE…` |

**Key rule proven in Aug 2026:** if the ping test shows the master sending **clean**
`PING` but the slave receiving **garbage**, the ESPs and code are fine — the
**KQ-330 modem link is corrupting the data.** Fix the hardware, not the firmware.

---

## 1. First: read, don't touch (2 min)

1. Open **both** serial monitors @ 115200.
2. Master: is it printing `[M->S]` every second? (It's transmitting — good.)
3. Slave: is it printing its heartbeat? (It's alive — good.)
4. Note whether either side shows the *other* side's data. If neither does →
   the link is down; continue.

---

## 2. Isolate the ESPs from the modems (prove the code is fine)

Flash the **ping-test branch** on both boards — pure raw UART, no project code:
```
git checkout ping-test
# flash master, flash slave
```
Watch the slave monitor:
- **Clean `PING` text** → code + ESPs are perfect. Go to Section 3 (modem link).
- **Garbage bytes** → the corruption is downstream of the ESP (modem link). Section 3.
- **Zero bytes, master ON** → nothing crosses at all → Section 3 (dead link / power / phase).

> To confirm the bytes come from the master: power the master OFF. The slave's
> `raw bytes` count should drop to 0. If it stays > 0, that's independent line noise.

Restore real firmware afterwards: `git checkout v7` (or `v9_merge`) and reflash.

---

## 3. Test the modem link ALONE with a USB-to-UART adapter (the key tool)

A USB↔UART adapter (FTDI / CP2102 / CH340) lets you drive the modems from a PC
terminal with **no ESP involved at all**. This is the fastest way to catch a bad
modem, bad coupling, or a noisy line.

> ⚠️ Level warning: the KQ-330 DIN/DOUT are **5V TTL** (see README §2.3). Use a
> **5V-logic** adapter, or a 3.3V adapter that is 5V-tolerant on RX. Match the
> adapter baud to the modems (start at **9600 8N1**).

### Test 3A — End-to-end modem loopback (no ESPs)
Wire one adapter across BOTH modems:
```
Adapter TX  → Modem A DIN
Modem A  ── powerline ──  Modem B
Modem B DOUT → Adapter RX
Adapter GND → common GND with the modems
```
Both modems powered from mains, **same power strip**. Open a serial terminal on
the adapter @ 9600. Type `PING` (or use RealTerm/PuTTY to send text).
- **You see clean `PING` echoed back** → the modem link WORKS. The fault is at the
  ESP↔modem wiring/solder. Go to Section 4.
- **Garbage or nothing** → the modem link itself is bad. Go to Section 5.

### Test 3B — One modem at a time (if 3A fails)
Confirm each modem individually: adapter TX → Modem DIN, scope/second adapter on
the powerline side if available. Simplest: swap in a known-good modem and repeat 3A.

### Test 3C — Check ESP → modem UART
Disconnect the modem's DIN from the ESP TX. Connect adapter RX → ESP TX pin
(master GPIO17 or slave GPIO11). You should see the ESP's clean output on the PC.
Confirms the ESP TX pin + wire are good.

---

## 4. If the modem link is good but ESP↔modem is suspect

1. **Reseat / re-solder** the 3 wires between each ESP and its modem: TX, RX, GND.
   A single cold joint on **DOUT** produces the `0xFF`-heavy garbage we saw.
2. Verify pins in `config.h`: master TX=17 RX=13; slave TX=11 RX=10; baud 9600.
3. Master TX (17) → Modem DIN; Modem DOUT → Master RX (13). **Do not swap.**
4. Same for slave: TX(11)→DIN, DOUT→RX(10).
5. Check the slave RX level shifter (U3) and master GPIO13 (5V TTL, no pull-up).

---

## 5. If the modem link itself is bad (Test 3A garbage)

Try in order, re-testing 3A after each:
1. **Same electrical phase** — both modems on the **same outlet / power strip**.
   Different phases = no/garbled link (README §2.3). This is the #1 cause.
2. **Remove noise sources** — unplug chargers, LED lamps, laptop bricks, motors,
   switching supplies on the same circuit. They swamp the PLC carrier.
3. **Power-cycle both modems** — unplug from mains 10 s, replug. Confirm each
   modem's power LED is lit.
4. **Check modem baud/config** — confirm both KQ-330 units are set to the **same**
   UART baud (9600) and the same channel/mode (DIP switches or config, per the
   modem's datasheet). A drifted/reset modem garbles everything.
5. **Swap a modem** — substitute a known-good KQ-330 to find a dead unit.
6. **Coupling** — verify the mains-coupling network (caps/transformer) to each
   modem is intact and connected.

---

## 6. Once the link is clean

1. `git checkout v7` (known-good) or `v9_merge` (latest) on both boards.
2. Reflash master and slave.
3. Confirm all four log lines appear with matching seq numbers:
   ```
   [M->S] ... | [S<-M] ...      (master→slave)
   [S->M] ... | [M<-S] ...      (slave→master)
   ```

---

## Appendix — Known-good facts (do not re-investigate)

- Protocol, pins, baud, and receive code are **identical** across v3 / v7 / v9.
- `boiler_protocol.h` is **byte-identical** between master and slave.
- Timing constants (DO NOT CHANGE): 1000 ms CMD cadence, 3000 ms STATUS wait,
  200 ms guard, 2 ms/byte, 500 ms quiet, 5000 ms watchdog.
- Master build is pinned (pioarduino 53.03.11 / Arduino core 3.1.1).
  The **slave** platform is unpinned in v3/v7 (`platform = espressif32`); if a
  clean rebuild ever behaves oddly, pin it to match the master.
- Aug 2026 incident: root cause was the **modem link corrupting data** (ping test:
  clean TX in, `0xFF`-garbage out). The code was never the problem.
