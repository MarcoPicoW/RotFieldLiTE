/*
  Festival Phone - Lettura disco combinatore + display ST7789 1.47"
  ------------------------------------------------------------------
  Board  : Waveshare RP2040-Zero (arduino-pico core, Earle Philhower)
  Display: 1.47" ST7789 172x320 SPI (IPS)
  IDE    : Arduino IDE

  Librerie richieste (Library Manager):
    "Adafruit GFX Library"
    "Adafruit ST7735 and ST7789 Library"

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

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

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
// Debounce a campionamento (livello stabile per N ms), stessa tecnica del
// gate: piu' robusto del vecchio interrupt+timeout contro rimbalzi irregolari.
const uint16_t PULSE_DEBOUNCE_MS = 20;     // il livello deve restare stabile 20ms
const uint16_t GATE_DEBOUNCE_MS  = 15;     // il gate commuta 1 volta per cifra
const uint8_t  MAX_DIGITS        = 24;

// ---------- DEBUG ----------
// Se attivo, stampa su Serial ogni impulso valido riconosciuto.
#define DEBUG_PULSES 1

// ---------- COLORI ----------
#define COL_TITLE   ST77XX_CYAN
#define COL_NUM     ST77XX_GREEN
#define COL_DIM     0x7BEF   // grigio
#define COL_STATUS  ST77XX_YELLOW

// ---------- DISPLAY ----------
// L'offset di centraggio (pannello 172 nel GRAM 240) e' calcolato
// automaticamente da gfx.init(172, 320) per questa classe di pannelli.
Adafruit_ST7789 gfx = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

// ---------- STATO ----------
uint16_t pulseCount = 0;

String number = "";
bool   dialing = false;
int    gateStable = -1;

void setup() {
#if DEBUG_PULSES
  Serial.begin(115200);
  delay(3000);              // tempo per aprire il Serial Monitor dopo il reset da upload
  Serial.println("=== Festival Phone: debug pulses attivo, boot OK ===");
#endif

  pinMode(PIN_PULSE, INPUT_PULLUP);
  pinMode(PIN_GATE,  INPUT_PULLUP);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Imposta i pin SPI0 PRIMA di SPI.begin()
  SPI.setSCK(TFT_SCK);
  SPI.setTX(TFT_MOSI);
  SPI.begin();

  gfx.init(172, 320);      // risoluzione nativa del pannello (offset di centraggio calcolato automaticamente)
  gfx.setRotation(1);      // 1 = landscape 320x172

  drawScreen();

  gateStable = digitalRead(PIN_GATE);
}

void loop() {
  static int      lastGateRead   = -1;
  static uint32_t lastGateChange = 0;

  int r = digitalRead(PIN_GATE);
  if (r != lastGateRead) {
    lastGateRead = r;
    lastGateChange = millis();
  }
  if (r != gateStable && (millis() - lastGateChange) >= GATE_DEBOUNCE_MS) {
    gateStable = r;
    handleGate(gateStable);
  }

  // Debounce a livello stabile per il contatto impulsi: conta solo quando il
  // pin resta LOW ininterrottamente per PULSE_DEBOUNCE_MS (filtra i rimbalzi
  // meccanici, anche quelli irregolari, meglio del vecchio interrupt+timeout).
  static int      lastPulseRead   = HIGH;
  static uint32_t lastPulseChange = 0;
  static int      pulseStable     = HIGH;

  int p = digitalRead(PIN_PULSE);
  if (p != lastPulseRead) {
    lastPulseRead = p;
    lastPulseChange = millis();
  }
  if (p != pulseStable && (millis() - lastPulseChange) >= PULSE_DEBOUNCE_MS) {
    int prevStable = pulseStable;
    pulseStable = p;
    if (prevStable == HIGH && pulseStable == LOW) {
      pulseCount++;
#if DEBUG_PULSES
      Serial.print("PULSE valido, totale cifra=");
      Serial.println(pulseCount);
#endif
    }
  }
}

void handleGate(int level) {
  bool nowDialing = (level == GATE_DIALING);

  if (nowDialing && !dialing) {
    // inizio composizione di una cifra
    dialing = true;
    pulseCount = 0;
    drawScreen();
  }
  else if (!nowDialing && dialing) {
    // fine composizione: leggo il conteggio
    dialing = false;
    finalizeDigit(pulseCount);
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
  gfx.fillScreen(ST77XX_BLACK);

  gfx.setTextColor(COL_TITLE);
  gfx.setTextSize(2);
  gfx.setCursor(6, 8);
  gfx.print("Festival Phone");

  gfx.drawFastHLine(0, 30, gfx.width(), COL_DIM);

  gfx.setTextWrap(true);
  gfx.setTextSize(4);
  gfx.setCursor(6, 50);
  if (number.length()) {
    gfx.setTextColor(COL_NUM);
    gfx.print(number);
  } else {
    gfx.setTextColor(COL_DIM);
    gfx.print("---");
  }

  gfx.setTextColor(COL_STATUS);
  gfx.setTextSize(2);
  gfx.setCursor(6, gfx.height() - 22);
  gfx.print(dialing ? "comporre..." : "pronto");
}
