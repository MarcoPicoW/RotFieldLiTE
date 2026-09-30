#include <Adafruit_NeoPixel.h>

// NeoPixel onboard della Waveshare RP2350-Zero: dati su GPIO16, verifica sul tuo esemplare/silkscreen
#define NEOPIXEL_PIN 16
#define INPUT_PIN 26

Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  pinMode(INPUT_PIN, INPUT);
  pixel.begin();
  pixel.setBrightness(50);
  pixel.show();

  for (int i = 0; i < 2; i++) {
    pixel.setPixelColor(0, pixel.Color(255, 255, 255));
    pixel.show();
    delay(100);
    pixel.setPixelColor(0, 0);
    pixel.show();
    delay(100);
  }
}

void loop() {
  if (digitalRead(INPUT_PIN) == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 255, 0));
  } else {
    pixel.setPixelColor(0, 0);
  }
  pixel.show();
}
