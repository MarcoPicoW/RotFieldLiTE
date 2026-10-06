# RotFieldLiTE — Roadmap

Ten phases from bare boards to a phone on a pole in the field. Status as of
October 2026. Hardware and wiring details live in the [README](./README.md).

| # | Phase | Status |
|---|---|---|
| 1 | OS and UART setup | ✅ done |
| 2 | Dial-pulse and hook-switch logic | ✅ done (RP2350 side) |
| 3 | Bell driver circuit and bench characterization | ✅ done |
| 4 | Solar chain verification | ⏳ to do |
| 5 | SIM7600G-H HAT integration | ✅ done |
| 6 | AT initialization and two-way voice calls | 🔶 partly done |
| 7 | Handset transducer integration | ⏳ to do |
| 8 | Full Python script: call management | ✅ done |
| 9 | Autostart via systemd | ✅ done |
| 10 | Mechanical assembly and field testing | ⏳ to do |

---

## 1. OS and UART setup — ✅ done

**Goal:** Raspberry Pi OS on the Pi Zero 2 W, with a working serial path to
the modem and to the dial MCU.

**What happened:** the original plan was a Bluetooth/PL011 swap so that the
SIM7600 could use the GPIO UART. It turned out the HAT talks to the Pi over
**USB** (`/dev/ttyUSB0-4`, AT commands on `/dev/ttyUSB2`), so no swap was
needed. The GPIO14/15 UART (`/dev/serial0`) now carries the RP2350 <-> Pi
link instead.

**Done when:**
- [x] serial login console disabled (`raspi-config nonint do_serial_cons 1`,
      `serial-getty@ttyS0.service` disabled)
- [x] `/dev/serial0` owned by `root:dialout`, Pi user in `dialout`
- [x] modem AT port identified as `/dev/ttyUSB2` at 115200 baud

## 2. Dial-pulse and hook-switch logic — ✅ done (RP2350 side)

**Goal:** read the rotary dial and hook switch reliably and hand clean events
to the Pi.

**What happened:** implemented on the RP2350 in
`Test/festival_phone_dial_display/festival_phone_dial_display.ino` rather than
on Pi GPIOs. Pulse and gate contacts are debounced, the number is shown on the
ST7789 display, and the RP2350 sends events over `Serial1`:

- digits-only line after 3 s of silence -> number to dial
- `HANGUP` when the hook switch (GPIO26) goes low
- `ANSWER` when the handset is lifted during an incoming call

**Done when:**
- [x] dialed digits decoded correctly
- [x] hook transitions debounced
- [x] Pi receives number / `HANGUP` / `ANSWER` lines

## 3. Bell driver circuit and bench characterization — ✅ done

**Goal:** make the original bistable polarized ringer strike both gongs from
the new electronics.

**What happened:** bench tests on a Peaktech P 6226 gave a pull-in threshold
of **12 V**, much better sound at **24 V**, ~**27 mA** peak. The ringer needs
true polarity reversal, so it is driven by an **L298N** H-bridge from RP2350
GP0/GP1 (bipolar 25 Hz square wave, 1 s ON / 4 s OFF), powered by an
**XL6009** boost set to ~26 V from the Pi's 5 V pin.

**Done when:**
- [x] minimum and preferred coil voltage measured
- [x] both gongs struck with polarity reversal
- [x] non-blocking ring cadence in firmware, with a 12 s watchdog so a stuck
      Pi can't leave the bell ringing

**Follow-up (portable version):** the L298N idles at roughly 1 W (datasheet
estimate, not measured). Plan: power-gate the bell branch from GP27 with a
P-/N-MOSFET pair and replace the L298N with a **DRV8871**. See "Future:
low-power bell driver" in the README.

## 4. Solar chain verification — ⏳ to do

**Goal:** confirm the system can run unattended in a field for days.

**Chain:** 50 W panel -> Victron MPPT 75/10 -> LiFePO4 12 V 20 Ah (256 Wh)
-> 12 V -> 5 V buck converter -> Pi Zero 2 W + HAT + RP2350 + bell branch.

**Done when:**
- [ ] idle and in-call current of the whole system measured at the 5 V and
      12 V sides
- [ ] daily energy budget computed against the 256 Wh battery and the panel's
      realistic yield
- [ ] buck converter checked for voltage sag during modem TX bursts and bell
      strikes (no Pi brownouts)
- [ ] multi-day run on the real chain without intervention

## 5. SIM7600G-H HAT integration — ✅ done

**Goal:** get the cellular module talking to the Pi.

**What happened:** the Waveshare SIM7600G-H 4G HAT (B) enumerates over USB
(Qualcomm/Option SimTech), exposing five `/dev/ttyUSB*` ports; AT commands
work on `/dev/ttyUSB2`.

**Done when:**
- [x] module enumerates on USB
- [x] AT port confirmed and responding

## 6. AT initialization and two-way voice calls — 🔶 partly done

**Goal:** reliable VoLTE voice calls in both directions on a Swiss network
(2G is shut down by every carrier, so voice must work over LTE).

**Tested:**
- [x] outbound dial (`ATD<number>;`)
- [x] incoming call with caller ID (`AT+CLIP=1`, `RING` / `+CLIP` URCs)
- [x] answer (`ATA`) and local hangup (`AT+CHUP`)

**Still to test:**
- [ ] missed call (caller gives up before the handset is lifted)
- [ ] remote-side hangup during an active call (`NO CARRIER` /
      `VOICE CALL: END` -> `CALLEND` -> display update)

## 7. Handset transducer integration — ⏳ to do

**Goal:** real call audio through the original handset.

**Plan:** harvested electret microphone + 8 Ω 28-40 mm speaker, wired to the
HAT's NAU8810 codec through the TRRS jack. The SIM7600 handles full-duplex
audio in hardware; the Pi never touches audio.

**Done when:**
- [ ] mic and speaker fitted in the handset shell
- [ ] both sides of a call clearly audible, at a sensible level
- [ ] no noticeable echo or hum (check grounding with the bell branch active)

## 8. Full Python script: call management — ✅ done

**Goal:** one service on the Pi that bridges the RP2350 and the modem.

**What happened:** `PiZero/festival_phone_dialer.py` reads the RP2350 UART and
the modem's URCs on a dedicated thread. It handles outbound dialing, incoming
calls with caller ID, answer on lift, `HANGUP`, and repeats
`INCOMING [number]` on every `RING` as a keepalive for the bell. A call ends
on `NO CARRIER`, `VOICE CALL: END`, `MISSED_CALL`, or 8 s without a `RING`.

**Done when:**
- [x] outbound, incoming, answer and hangup flows verified end-to-end

## 9. Autostart via systemd — ✅ done

**Goal:** the phone works after a power cycle with no one logging in.

**What happened:** runs as `festival-phone-dialer.service` (`Restart=always`,
user `rotfieldlite`); logs via `journalctl -u festival-phone-dialer.service`.

**Done when:**
- [x] service starts on boot and restarts on crash

## 10. Mechanical assembly and field testing — ⏳ to do

**Goal:** everything inside the original phone, on a pole, in a field.

**Done when:**
- [ ] electronics mounted inside the phone body; dial, handset, bell and hook
      switch keep their original feel
- [ ] enclosure and pole mount protect the electronics and the solar wiring
      from rain and dust
- [ ] antenna placement checked for signal from inside the enclosure
- [ ] full festival-style field test: calls in both directions over several
      days, on solar power only
