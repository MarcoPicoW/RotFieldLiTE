/*
  Festival Phone - Lettura disco combinatore + display ST7789 1.47"
  ------------------------------------------------------------------
  Board  : Waveshare RP2040-Zero (arduino-pico core, Earle Philhower)
  Display: 1.47" ST7789 172x320 SPI (IPS)
  IDE    : Arduino IDE

  Libreria richiesta:
    "GFX Library for Arduino" (Arduino_GFX_Library) - Library Manager

  Selezione board:
    Tools > Board > "Raspberry Pi Pico/RP2040" > Waveshare RP2040-Zero

  ------------------------------------------------------------------
  CABLAGGIO DISPLAY (SPI0)          CABLAGGIO DISCO
    SCK  -> GP2                       impulsi (nsi) -> GP14
    MOSI -> GP3                       gate (fuori-riposo) -> GP15
    CS   -> GP5                     (entrambi con INPUT_PULLUP interno,
    DC   -> GP6                      altro capo a GND)
    RST  -> GP7
    BL   -> GP8
    VCC  -> 3V3   GND -> GND

  Pin gia' occupati nel progetto (NON usare qui):
    GP0 / GP1  = L298N IN1/IN2 (driver campanello)
    GP16       = WS2812 NeoPixel onboard

  ------------------------------------------------------------------
  LOGICA DISCO
    - Contatto impulsi (nsi): a riposo HIGH, va LOW N volte  (0 = 10 impulsi)
    - Contatto fuori-riposo (gate): chiuso a riposo, si apre durante la
      composizione. Usato come finestra "composizione in corso" che
      inquadra il treno di impulsi (reset all'apertura, lettura alla chiusura).
*/

#include <Arduino_GFX_Library.h>

// ---------- PIN DISPLAY (SPI0) ----------
#define TFT_SCK   2
#define TFT_MOSI  3
#define TFT_CS    5
#define TFT_DC    6
#define TFT_RST   7
#define TFT_BL    8

// ---------- PIN DISCO ----------
#define PIN_PULSE 14   // contatto impulsi (nsi)
#define PIN_GATE  15   // contatto fuori-riposo (finestra composizione)

// ---------- LIVELLI LOGICI (configurabili) ----------
// Impulso attivo = LOW (a riposo HIGH): contiamo i fronti di discesa.
// Gate "composizione in corso" = contatto aperto -> con pull-up = HIGH.
// Se sul TUO cablaggio il gate risulta invertito, metti GATE_DIALING a LOW.
#define GATE_DIALING  HIGH

// ---------- TEMPI ----------
const uint32_t PULSE_DEBOUNCE_US = 4000;   // 4 ms, ben sotto il make (~33 ms)
const uint16_t GATE_DEBOUNCE_MS  = 15;     // il gate commuta 1 volta per cifra
const uint8_t  MAX_DIGITS        = 24;

// ---------- COLORI ----------
#define COL_TITLE   CYAN
#define COL_NUM     GREEN
#define COL_DIM     0x7BEF   // grigio
#define COL_STATUS  YELLOW

// ---------- DISPLAY ----------
// Se l'immagine risulta traslata di qualche pixel, ritocca gli offset "34".
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, TFT_RST, 1 /* rotation: 1 = landscape 320x172 */, true /* IPS */,
  172, 320,        // risoluzione pannello
  34, 0, 34, 0     // offset col/riga (pannello 172 centrato nel GRAM 240)
);

// ---------- STATO ----------
volatile uint16_t pulseCount = 0;
volatile uint32_t lastPulseUs = 0;

String number = "";
bool   dialing = false;
int    gateStable = -1;

// ISR: un fronte di discesa = un impulso (con debounce)
void onPulse() {
  uint32_t now = micros();
  if (now - lastPulseUs >= PULSE_DEBOUNCE_US) {
    pulseCount++;
    lastPulseUs = now;
  }
}

void setup() {
  pinMode(PIN_PULSE, INPUT_PULLUP);
  pinMode(PIN_GATE,  INPUT_PULLUP);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Imposta i pin SPI0 PRIMA di gfx->begin()
  SPI.setSCK(TFT_SCK);
  SPI.setTX(TFT_MOSI);
  gfx->begin();

  drawScreen();

  attachInterrupt(digitalPinToInterrupt(PIN_PULSE), onPulse, FALLING);
  gateStable = digitalRead(PIN_GATE);
}

void loop() {
  static int      lastRead = -1;
  static uint32_t lastChangeMs = 0;

  int r = digitalRead(PIN_GATE);
  if (r != lastRead) {
    lastRead = r;
    lastChangeMs = millis();
  }
  if (r != gateStable && (millis() - lastChangeMs) >= GATE_DEBOUNCE_MS) {
    gateStable = r;
    handleGate(gateStable);
  }
}

void handleGate(int level) {
  bool nowDialing = (level == GATE_DIALING);

  if (nowDialing && !dialing) {
    // inizio composizione di una cifra
    dialing = true;
    noInterrupts();
    pulseCount = 0;
    lastPulseUs = micros();
    interrupts();
    drawScreen();
  }
  else if (!nowDialing && dialing) {
    // fine composizione: leggo il conteggio
    dialing = false;
    noInterrupts();
    uint16_t c = pulseCount;
    interrupts();
    finalizeDigit(c);
  }
}

void finalizeDigit(uint16_t c) {
  if (c == 0) { drawScreen(); return; }   // nessun impulso valido
  if (c > 10) c = 10;                     // clamp di sicurezza
  int digit = (c == 10) ? 0 : c;

  number += String(digit);
  if (number.length() > MAX_DIGITS)
    number = number.substring(number.length() - MAX_DIGITS);

  drawScreen();
}

void drawScreen() {
  gfx->fillScreen(BLACK);

  gfx->setTextColor(COL_TITLE);
  gfx->setTextSize(2);
  gfx->setCursor(6, 8);
  gfx->print("Festival Phone");

  gfx->drawFastHLine(0, 30, gfx->width(), COL_DIM);

  gfx->setTextWrap(true);
  gfx->setTextSize(4);
  gfx->setCursor(6, 50);
  if (number.length()) {
    gfx->setTextColor(COL_NUM);
    gfx->print(number);
  } else {
    gfx->setTextColor(COL_DIM);
    gfx->print("---");
  }

  gfx->setTextColor(COL_STATUS);
  gfx->setTextSize(2);
  gfx->setCursor(6, gfx->height() - 22);
  gfx->print(dialing ? "comporre..." : "pronto");
}
