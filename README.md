# RotFieldLiTE

![status](https://img.shields.io/badge/status-in%20development-orange)
![platform](https://img.shields.io/badge/SBC-Raspberry%20Pi%20Zero%202%20W-c51a4a)
![modem](https://img.shields.io/badge/modem-SIM7600G--H-blue)
![power](https://img.shields.io/badge/power-solar%2050W%20%7C%20LiFePO4-brightgreen)
![network](https://img.shields.io/badge/network-LTE%20VoLTE-informational)

A vintage rotary-dial telephone, mounted on a pole at the center of a camping
group, that behaves like a fixed landline: group members call it instead of
wandering around looking for people. Original mechanics (rotary dial, handset,
bell striker, hook switch), electronics fully rebuilt. Solar powered, built to
sit in a field for days.

```
# Name decode
# Rot    -> Rotary dial
# Field  -> Fieldphone are the old militaryphone, portable and durable
# LiTE   -> Line (its landline heritage) + LTE (the network it now runs on)
#           + lite (light, field-portable)
#
# In short: a rotary landline, gone wireless over LTE, for the festival field.
#
# Legal-humorous note: the Rothschilds are not involved but the Mossad may still listen to your calls.
```

## Concept

Normally this phone would be wired to the **landline**. Here it is modified to run
**wireless** over the mobile network: it still behaves like a fixed line (fixed
number, always "on the line", you call *it*), but with no cable. Hence the oxymoron
in the name.

## Design constraints

- **Switzerland**: 2G is fully shut down by every carrier (Swisscom, Sunrise,
  Salt). Any 2G-only module is out. Requires VoLTE or circuit-switched voice
  over LTE.
- **Solar**: 50 W panel, Victron MPPT 75/10, LiFePO4 12 V 20 Ah battery,
  12 V -> 5 V buck converter. Power efficiency matters.
- **Vintage look, new electronics**: mechanical parts are retained (dial, handset
  shell, bell, hook switch); all internal electronics are replaced.

## Hardware architecture

| Block | Component | Notes |
|---|---|---|
| SBC | Raspberry Pi Zero 2 W | only sends AT commands, over **USB** to the SIM7600 (see note below) |
| Cellular module | Waveshare SIM7600G-H HAT | global-band variant; the E-H European one would suffice for a fixed CH install |
| Audio | handset mic + speaker -> TRRS jack -> NAU8810 codec on the HAT | call audio handled in hardware by the SIM7600 |
| Bell driver | L298N H-bridge, fed ~26 V by an XL6009 boost from the Pi's 5 V pin | coil sees ~24 V after the L298N drop; the Pi never touches audio, only AT |
| Dial/hook MCU | Waveshare RP2040-Zero (RP2350) | reads the rotary dial pulses and hook switch, drives the local ST7789 display, forwards the dialed number / hangup to the Pi over UART |
| Bell MCU | RP2350 (same board as dial/hook, GP0/GP1 -> L298N IN1/IN2) | bipolar 25 Hz square wave, 1 s ON / 4 s OFF, only while ringing with the handset down |
| Hook switch | GPIO26 on the RP2350 | edge -> Pi gets `HANGUP` over UART -> `ATH` on the SIM7600 |
| Incoming-call detection | `RING` URC codes over UART | not GPIO |
| Test | Waveshare RP2040-Zero | How Power flow in the mechanics parts without risking to burn a Pi |

### SIM7600 is on USB, not the GPIO UART

The HAT exposes the SIM7600 as a USB modem (`lsusb`: Qualcomm/Option SimTech), with
five `/dev/ttyUSB0-4` ports. **AT commands go on `/dev/ttyUSB2`** at 115200 baud —
that's the port the Pi actually talks to, not `/dev/serial0`.

The Pi's GPIO14/15 hardware UART (`/dev/serial0`) is used for something else
entirely: the link **between the RP2350 and the Pi Zero 2 W** (dialed number and
hangup signal, see below). By default that UART is grabbed by a login console
(`serial-getty@ttyS0.service`), which must be disabled first:

```
sudo raspi-config nonint do_serial_cons 1     # disable login console on the UART
sudo systemctl disable --now serial-getty@ttyS0.service
sudo reboot
```

After that, `/dev/ttyS0` (aliased `/dev/serial0`) should show `crw-rw---- root:dialout`,
and the Pi user must be in the `dialout` group (default on Raspberry Pi OS).

**Wiring RP2350 <-> Pi (Serial1, 115200 8N1):**

```
GP12 (TX, RP2350) -> RXD Pi Zero 2W (GPIO15 / physical pin 10)
GP13 (RX, RP2350) -> TXD Pi Zero 2W (GPIO14 / physical pin 8)
GND common between the two boards
```

### Why direct analog audio

The handset mic and speaker connect straight to the HAT's NAU8810 codec via TRRS.
The SIM7600 handles full-duplex audio in hardware; the Pi only sends AT commands.
Chosen after ruling out I2S ADC bridging, the Pi 4 jack, a USB mic, and
PCM-over-USB: fewer digital hops, less complexity.

### Why an H-bridge for the bell

The ringer is a **bistable polarized single-coil** design: to swing the clapper
between the two gongs it needs **true polarity reversal**, not pulsed
unidirectional current. Hence the H-bridge (an L298N already on hand) instead of
a single MOSFET.
Plus I always wanted to use it for something that is not a DC motor.

> ⚠️ **L298N, things to watch**
> - ~2 V Darlington drop: reduces effective coil voltage. May be a problem in the future, when we run on batteries;
> - verify the onboard flyback diodes on the specific module before relying on them;
> - no integrated current limiting.

### Bell power: 5 V -> 24 V boost

Bench result: the ringer needs **at least 12 V** and sounds noticeably better at
**24 V**, drawing only **~27 mA peak** (~0.65 W). That is small enough to take it
from the Pi's 5 V pin through a boost converter instead of running a separate
wire from the battery:

- ~0.15 A from the 5 V rail while ringing, ~30 mA average (1 s ON / 4 s OFF);
- **XL6009** boost set to ~26 V (the L298N drops ~2 V, so the coil sees ~24 V).
  An MT3608 would do the current but 26 V is too close to its 28 V maximum;
- the L298N is fine at this voltage (VS up to 46 V; keep the onboard 5 V
  regulator jumper fitted, it accepts up to ~35 V).

```
Pi Zero pin 2 (5V) --+-- XL6009 IN+          XL6009 OUT+ (26V) --+-- L298N +12V/VS
                     +-- 220uF/10V - GND                         +-- 220uF/35V - GND
Pi Zero pin 6 (GND) ---- XL6009 IN-          XL6009 OUT- -------- L298N GND

L298N IN1 <- GP0 (RP2350)   L298N IN2 <- GP1   ENA: jumper fitted
L298N OUT1/OUT2 -> ringer coil
Common GND: Pi, RP2350, XL6009, L298N
```

Set the XL6009 output to ~26 V with a multimeter **before** connecting the
L298N. The bench supply's ammeter is slow, so real peaks at each polarity
reversal may be somewhat higher: still well within the 5 V budget.

### Future: low-power bell driver (portable version)

The L298N gets warm even when the bell is idle. With ENA jumpered and
IN1/IN2 LOW the chip stays enabled, and its logic supply comes from the
module's onboard 78M05, which drops 26 V -> 5 V linearly. Datasheet estimate
(not measured): ~13 mA on VS plus ~24 mA of logic current (plus the power LED)
through the regulator, i.e. **~1 W continuous**, ~24 Wh/day: more than 10 % of
the 256 Wh battery, for a bell that rings a few seconds a day. Fine for now,
to be fixed in the portable version:

1. **Power-gate the whole bell branch** (XL6009 + driver) from an RP2350 GPIO,
   so it draws nothing between calls:

   ```
   Pi 5V --+------------ S [P-MOSFET AO3401 / IRLML6402] D --+-- XL6009 IN+
           |                     G                           +-- 10-47uF
          100k                   |
           +---------------------+
                                 D [N-MOSFET 2N7000 / BSS138]
   GP27 (RP2350) -- 1k --+------ G
                        100k     S
                         |       |
                        GND     GND
   ```

   - GP27 HIGH -> N-FET on -> P-FET gate pulled low -> branch powered. The
     N-FET is needed because 3.3 V can't turn off a P-FET sourced at 5 V.
   - The 100k pull-down keeps the branch **off** while the RP2350 is in
     reset/boot or hung.
   - Keep the big 220 uF on the Pi side of the switch and only a small cap
     after it, otherwise the inrush on switch-on can brown out the Pi.
   - IN1/IN2 must stay LOW while the driver is unpowered (`coilOff()` already
     does this), so the GPIOs don't back-power it through its input clamps.
   - Firmware: power on when ringing starts, wait ~200-300 ms (non-blocking)
     for the XL6009 to settle before the first strike, `coilOff()` and power
     off when ringing stops.

2. **Replace the L298N with a DRV8871** MOSFET H-bridge: same IN1/IN2 polarity
   control (sketch almost unchanged), 45 V max, no ~2 V Darlington drop, no
   onboard linear regulator, and it auto-sleeps at ~1 uA when IN1 = IN2 = LOW.

Considered and rejected: the SparkFun **EasyDriver** (A3967). It's a stepper
driver: polarity is set through STEP/DIR pulses (coil A only flips every 2
full steps), its logic still runs from an onboard linear regulator on the
motor rail, its ~30 V motor-supply limit is close to our 26 V, and its current
chopper is pointless for a ~27 mA coil.

## Software

### `Test/festival_phone_dial_display/festival_phone_dial_display.ino` (RP2350)

Reads the rotary dial (pulse + gate contacts), shows the composed number on the
ST7789 display, and talks to the Pi over `Serial1` (UART, see wiring above):

- After 3 s of silence following the last digit, sends the full dialed number
  as a plain digits-only line (e.g. `0791234567`).
- When the hook switch (GPIO26) goes low (handset back down), sends a
  `HANGUP` line.
- The display shows `chiamata in corso` once the number has been sent, until a
  new digit is dialed or the hook toggles again.
- Incoming calls: on `INCOMING [number]` from the Pi the display lights up and
  shows the caller (or `sconosciuto` if withheld); lifting the handset sends
  `ANSWER`. `CALLEND` from the Pi clears the screen (or shows
  `chiamata terminata` if the handset is still up).
- Bell: drives the L298N on GP0/GP1 with the non-blocking version of
  `Test/suoneria_rp2040.ino` (bipolar 25 Hz, 1 s ON / 4 s OFF, onboard
  NeoPixel green while striking). Rings only while a call is incoming *and*
  the handset is down, stops the instant it is lifted, and stops on its own
  if no `INCOMING` arrives from the Pi for 12 s (the Pi repeats it on every
  `RING`), so a stuck Pi can't leave the bell ringing.

### `PiZero/festival_phone_dialer.py` (Pi Zero 2 W)

Bridges the RP2350 UART link to the SIM7600 AT port:

- Reads lines from `/dev/serial0` (RP2350 link).
- A digits-only line -> `AT+CHUP` (clear any previous call) then `ATD<number>;`
  on `/dev/ttyUSB2`.
- A `HANGUP` line -> `AT+CHUP` on `/dev/ttyUSB2`.
- An `ANSWER` line -> `ATA` (only while ringing).
- A background thread reads the modem's URCs: `RING` / `+CLIP` (caller ID,
  enabled with `AT+CLIP=1` at startup) -> `INCOMING [number]` to the RP2350,
  repeated on every `RING` as a keepalive for the bell;
  `NO CARRIER`, `VOICE CALL: END`, `MISSED_CALL`, or 8 s without a `RING`
  -> `CALLEND`.

Runs on the Pi as the systemd unit `festival-phone-dialer.service`
(`Restart=always`, user `rotfieldlite`, script at `~/festival_phone_dialer.py`);
logs with `journalctl -u festival-phone-dialer.service`. Verified end-to-end:
outbound dialing, incoming call with caller ID on the display, bell ringing,
answering by lifting the handset.

## Roadmap (10 phases)

Full detail lives in [`festival-phone-roadmap.md`](./festival-phone-roadmap.md)
(not yet written).

1. ~~OS and UART setup~~ — done, but not via a Bluetooth/PL011 swap: the
   SIM7600 turned out to be USB, not GPIO UART. The GPIO14/15 UART instead
   carries the RP2350 <-> Pi link, which required disabling the serial login
   console (`serial-getty@ttyS0`) that grabs it by default.
2. Dial-pulse and hook-switch GPIO logic — done on the RP2350 side, with a
   serial line to the Pi as the "stub" (number + `HANGUP`).
3. ~~Bell driver circuit and bench characterization~~ — done: min 12 V,
   best at 24 V, ~27 mA peak; driven by the RP2350 through the L298N, powered
   by an XL6009 boost from the Pi's 5 V (see "Bell power")
4. Solar chain verification (panel -> MPPT -> LiFePO4 -> buck)
5. ~~Receive and integrate the SIM7600G-H HAT~~ — done (USB-attached, AT port
   confirmed on `/dev/ttyUSB2`)
6. AT initialization and two-way voice-call validation — outbound dial,
   incoming call + answer (`ATA`) and hangup (`AT+CHUP`) tested; still to test:
   missed call and remote-side hangup during a call
7. Handset transducer integration (harvested electret + 8 Ohm 28-40 mm speaker)
8. ~~Full Python script: dial-pulse reading + call management~~ — done in
   `PiZero/festival_phone_dialer.py` (outbound, incoming with caller ID,
   answer on lift, bell keepalive)
9. ~~Autostart via systemd~~ — done (`festival-phone-dialer.service`)
10. Mechanical assembly and field testing

## Bell characterization (bench)

Using the Peaktech P 6226 supply (0-30 V, 0-10 A):
- ramp voltage from zero to find the pull-in threshold;
- manually swap leads to confirm it strikes both gongs;
- read current to compute coil resistance.

Result: pull-in at **12 V** minimum, much better sound at **24 V**, ~**27 mA**
peak current at 24 V.

## Guiding principles

- The Swiss 2G sunset is a hard filter: it rules out SIM800/900, the A7670E
  (CSFB-only for voice), and any module without VoLTE or circuit-switched voice
  over LTE.
- Direct analog audio is the right architecture: no PCM-over-USB, no multi-hop
  digital bridging.
- Telling a bistable ringer from a standard one is critical: the single-coil
  bistable needs polarity reversal, hence the H-bridge.

## License

TBD.
