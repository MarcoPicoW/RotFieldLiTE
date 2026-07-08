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
| SBC | Raspberry Pi Zero 2 W | only sends AT commands over UART |
| Cellular module | Waveshare SIM7600G-H HAT | global-band variant; the E-H European one would suffice for a fixed CH install |
| Audio | handset mic + speaker -> TRRS jack -> NAU8810 codec on the HAT | call audio handled in hardware by the SIM7600 |
| Bell driver | L298N H-bridge | the Pi never touches audio, only AT |
| Bell MCU | Raspberry Pi Zero 2 W | bipolar 25 Hz square wave, 1 s ON / 4 s OFF |
| Hook switch | GPIO input prob. HAAl sensor | triggers `ATA` / `ATD` / `AT+CHUP` |
| Incoming-call detection | `RING` URC codes over UART | not GPIO |
| Test | Waveshare RP2040-Zero | How Power flow in the mechanics parts without risking to burn a Pi |

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

## Roadmap (10 phases)

Full detail lives in [`festival-phone-roadmap.md`](./festival-phone-roadmap.md).

1. OS and UART setup (Bluetooth/PL011 swap on the Zero 2 W to free the hardware UART)
2. Dial-pulse and hook-switch GPIO logic, with serial stubs
3. Bell driver circuit (RP2040-Zero + L298N) and bench characterization
4. Solar chain verification (panel -> MPPT -> LiFePO4 -> buck)
5. Receive and integrate the SIM7600G-H HAT
6. AT initialization and two-way voice-call validation
7. Handset transducer integration (harvested electret + 8 Ohm 28-40 mm speaker)
8. Full Python script: dial-pulse reading + call management
9. Autostart via systemd
10. Mechanical assembly and field testing

## Bell characterization (bench)

Using the Peaktech P 6226 supply (0-30 V, 0-10 A):
- ramp voltage from zero to find the pull-in threshold;
- manually swap leads to confirm it strikes both gongs;
- read current to compute coil resistance.

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
