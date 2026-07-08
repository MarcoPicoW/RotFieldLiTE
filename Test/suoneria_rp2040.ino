// ============================================================
//  Festival phone - bell ringer
//  Waveshare RP2040-Zero + L298N (channel A)
//  Arduino IDE - arduino-pico core (Earle Philhower)
// ------------------------------------------------------------
//  Wiring:
//    L298N IN1 <- GP0
//    L298N IN2 <- GP1
//    L298N ENA :  jumper fitted (H-bridge always enabled)
//    L298N OUT1/OUT2 -> bell coil terminals
//    Common ground between L298N, RP2040-Zero and 12 V negative
//
//  Debug LED: the RP2040-Zero has an onboard WS2812 RGB LED on
//  GP16. It turns green while the bell is ringing, off when idle.
//  Requires the "Adafruit NeoPixel" library.
//
//  Note: the ringer is a polarized bistable type. If the armature
//  snaps once and stays put instead of oscillating, swap the
//  IN1/IN2 wires (polarity direction matters).
// ============================================================

#include <Adafruit_NeoPixel.h>

const uint8_t IN1     = 0;    // GP0 -> L298N IN1
const uint8_t IN2     = 1;    // GP1 -> L298N IN2
const uint8_t LED_PIN = 16;   // onboard WS2812 RGB LED (GP16)

Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB + NEO_KHZ800);

// Clapper oscillation frequency: tune by ear between 20 and 25 Hz
const uint16_t RING_FREQ_HZ   = 25;
const uint16_t HALF_PERIOD_MS = 1000 / (2 * RING_FREQ_HZ);   // 20 ms at 25 Hz

// Ring cadence
const uint16_t RING_ON_MS  = 1000;   // duration of a single ring
const uint16_t RING_OFF_MS = 4000;   // pause between rings

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

// Coil at rest: both inputs low -> no current through the coil
void coilOff() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}

// Bipolar square wave: alternates polarity for 'durationMs'.
// LED is green while ringing (debug indicator).
void ringBurst(uint16_t durationMs) {
  setLed(0, 60, 0);            // green on: ringing
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    digitalWrite(IN1, HIGH);   // polarity A -> gong 1
    digitalWrite(IN2, LOW);
    delay(HALF_PERIOD_MS);
    digitalWrite(IN1, LOW);    // polarity B -> gong 2
    digitalWrite(IN2, HIGH);
    delay(HALF_PERIOD_MS);
  }
  coilOff();
  setLed(0, 0, 0);             // LED off: idle
}

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  coilOff();
  led.begin();
  setLed(0, 0, 0);
}

void loop() {
  ringBurst(RING_ON_MS);
  delay(RING_OFF_MS);
}
