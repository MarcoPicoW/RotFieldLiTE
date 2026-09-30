#!/usr/bin/env python3
"""
Festival Phone - Dialer
------------------------------------------------------------------
Gira sul Pi Zero 2W. Fa da ponte tra l'RP2350 (UART GPIO14/15,
/dev/serial0) e il modem SIM7600G-H, collegato via USB (porta comandi
AT: /dev/ttyUSB2).

PROTOCOLLO RP2350 -> Pi (vedi festival_phone_dial_display.ino)
  - riga di sole cifre (es. "0791234567") -> compone il numero (ATD)
  - "HANGUP"  -> cornetta riappesa, chiude la chiamata (AT+CHUP)
  - "ANSWER"  -> cornetta sollevata durante lo squillo, risponde (ATA)

PROTOCOLLO Pi -> RP2350
  - "INCOMING"           -> chiamata in arrivo, numero ancora sconosciuto
  - "INCOMING <numero>"  -> chiamata in arrivo da <numero> (caller ID, +CLIP)
                            (ripetuto a ogni RING: keepalive per il campanello)
  - "CALLEND"            -> la chiamata (in arrivo o in corso) e' terminata
                            dall'altra parte / persa

Il modem viene letto da un thread dedicato: i messaggi spontanei (URC)
come RING, +CLIP, NO CARRIER arrivano in qualsiasi momento, non solo
come risposta a un comando.
"""

import re
import sys
import threading
import time
import serial

UART_DIAL_PORT = "/dev/serial0"   # collegato all'RP2350 (GPIO14 TX / GPIO15 RX)
UART_DIAL_BAUD = 115200

MODEM_AT_PORT = "/dev/ttyUSB2"    # porta comandi AT del SIM7600G-H (via USB)
MODEM_AT_BAUD = 115200

NUMBER_RE = re.compile(r"\d{1,24}")
CLIP_RE = re.compile(r'^\+CLIP:\s*"([^"]*)"')

# Il modem ripete RING ogni ~3-5s. Se non ne arriva uno per questo tempo e
# nessuno ha risposto, il chiamante ha rinunciato (fallback nel caso il modem
# non mandi NO CARRIER / MISSED_CALL).
RING_TIMEOUT_S = 8.0

# URC che indicano la fine di una chiamata (in arrivo, in uscita o in corso)
CALL_END_PREFIXES = ("NO CARRIER", "BUSY", "NO ANSWER", "VOICE CALL: END", "MISSED_CALL")


class Phone:
    def __init__(self, dial_uart, modem):
        self.dial_uart = dial_uart
        self.modem = modem
        self.lock = threading.Lock()
        self.state = "idle"        # idle | ringing | active
        self.caller = None
        self.last_ring = 0.0

    # ---------- uscite ----------
    def to_rp(self, msg):
        print(f"[dialer] -> RP2350: {msg}")
        self.dial_uart.write(msg.encode("ascii") + b"\n")

    def at(self, command):
        print(f"[dialer] -> modem: {command}")
        self.modem.write(command.encode("ascii") + b"\r\n")

    # ---------- eventi dal modem (thread dedicato) ----------
    def modem_reader(self):
        while True:
            line = self.modem.readline().decode("ascii", "replace").strip()
            with self.lock:
                if line:
                    print(f"[modem] {line}")
                    self.handle_modem_line(line)
                self.check_ring_timeout()

    def handle_modem_line(self, line):
        if line == "RING" or line.startswith("+CRING"):
            self.last_ring = time.monotonic()
            if self.state == "idle":
                self.state = "ringing"
            # Ripetuto a ogni RING: fa da keepalive per il watchdog dell'RP2350,
            # che altrimenti ferma il campanello.
            if self.state == "ringing":
                self.to_rp(f"INCOMING {self.caller}" if self.caller else "INCOMING")
            return

        m = CLIP_RE.match(line)
        if m:
            number = m.group(1)
            if self.state in ("idle", "ringing"):
                self.state = "ringing"
                self.last_ring = time.monotonic()
                if number and number != self.caller:
                    self.caller = number
                    self.to_rp(f"INCOMING {number}")
            return

        if line.startswith(CALL_END_PREFIXES):
            self.end_call()

    def check_ring_timeout(self):
        if self.state == "ringing" and time.monotonic() - self.last_ring > RING_TIMEOUT_S:
            print("[dialer] niente RING da un po': chiamata persa")
            self.end_call()

    def end_call(self):
        if self.state != "idle":
            self.state = "idle"
            self.caller = None
            self.to_rp("CALLEND")

    # ---------- comandi dall'RP2350 (thread principale) ----------
    def hangup(self):
        print("[dialer] riaggancio (cornetta giu')")
        with self.lock:
            # idle PRIMA di AT+CHUP: il "VOICE CALL: END" che ne segue non
            # deve tornare all'RP2350 come CALLEND (l'ha chiusa lui).
            self.state = "idle"
            self.caller = None
            self.at("AT+CHUP")

    def answer(self):
        with self.lock:
            if self.state != "ringing":
                print(f"[dialer] ANSWER ignorato (stato: {self.state})")
                return
            print(f"[dialer] rispondo a {self.caller or 'sconosciuto'}")
            self.state = "active"
            self.at("ATA")

    def dial(self, number):
        print("[dialer] chiudo eventuale chiamata in corso")
        with self.lock:
            self.state = "idle"
            self.caller = None
            self.at("AT+CHUP")
        time.sleep(1.0)   # lascia arrivare (e ignorare) l'eventuale VOICE CALL: END

        print(f"[dialer] compongo {number}")
        with self.lock:
            self.state = "active"
            self.at(f"ATD{number};")


def main():
    # Sotto systemd stdout non e' un terminale: senza questo i print restano
    # nel buffer e non compaiono in journalctl.
    sys.stdout.reconfigure(line_buffering=True)

    dial_uart = serial.Serial(UART_DIAL_PORT, UART_DIAL_BAUD, timeout=1)
    modem = serial.Serial(MODEM_AT_PORT, MODEM_AT_BAUD, timeout=1)
    phone = Phone(dial_uart, modem)

    threading.Thread(target=phone.modem_reader, daemon=True).start()
    phone.at("AT+CLIP=1")   # abilita il caller ID (+CLIP dopo ogni RING)

    print(f"[dialer] in ascolto su {UART_DIAL_PORT}, modem su {MODEM_AT_PORT}")
    while True:
        line = dial_uart.readline().decode("ascii", "replace").strip()
        if not line:
            continue
        if line == "HANGUP":
            phone.hangup()
        elif line == "ANSWER":
            phone.answer()
        elif NUMBER_RE.fullmatch(line):
            phone.dial(line)
        else:
            print(f"[dialer] ignorato (comando sconosciuto): {line!r}")


if __name__ == "__main__":
    main()
