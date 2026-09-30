/*
  Festival Phone - Lettura disco combinatore + display ST7789 1.47"
  ------------------------------------------------------------------
  Board  : Waveshare RP2040-Zero (arduino-pico core, Earle Philhower)
  Display: 1.47" ST7789 172x320 SPI (IPS)
  IDE    : Arduino IDE

  Librerie richieste (Library Manager):
    "Adafruit GFX Library"
    "Adafruit ST7735 and ST7789 Library"
    "Adafruit NeoPixel"

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

  CABLAGGIO CAMPANELLO (L298N canale A, logica da suoneria_rp2040.ino)
    L298N IN1 <- GP0
    L298N IN2 <- GP1
    L298N ENA :  jumper inserito (ponte H sempre abilitato)
    L298N OUT1/OUT2 -> bobina della suoneria
    GND comune tra L298N, RP2350 e negativo 12 V
    GP16 = WS2812 NeoPixel onboard: verde mentre il campanello batte

  UART VERSO IL PI ZERO 2W (Serial1, 115200 8N1)
    GP12 (TX, RP2350) -> RXD Pi Zero 2W (GPIO15 / pin 10)
    GP13 (RX, RP2350) -> TXD Pi Zero 2W (GPIO14 / pin 8)
    GND comune tra le due schede (entrambe logica 3.3V, niente level shifter)

  ------------------------------------------------------------------
  LOGICA DISCO
    - Contatto impulsi (nsi): a riposo HIGH, va LOW N volte  (0 = 10 impulsi)
    - Contatto fuori-riposo (gate): chiuso a riposo, si apre durante la
      composizione. Usato come finestra "composizione in corso" che
      inquadra il treno di impulsi (reset all'apertura, lettura alla chiusura).
    - Il numero viene inoltrato al Pi Zero 2W via UART quando passano 3s
      senza una nuova cifra (fine composizione presunta).
    - Cornetta riappesa (PIN_SCREEN_ON va LOW): invia "HANGUP" al Pi Zero 2W
      via UART, che a sua volta manda AT+CHUP al SIM7600 per chiudere la chiamata.

  CHIAMATE IN ARRIVO
    - Il Pi manda "INCOMING [numero]": lo schermo si accende e mostra il
      numero del chiamante (o "sconosciuto" se nascosto).
    - Cornetta sollevata mentre squilla: invia "ANSWER" -> il Pi risponde (ATA).
    - Se la cornetta era gia' alzata quando arriva la chiamata, riagganciare
      NON rifiuta la chiamata: basta rialzarla per rispondere.
    - Il Pi manda "CALLEND" quando l'altro riattacca o la chiamata e' persa.
    - Il campanello suona (1s ON / 4s OFF, onda quadra bipolare 25 Hz) solo
      mentre squilla E la cornetta e' appesa. Se il Pi smette di mandare
      INCOMING (lo ripete a ogni RING) per INCOMING_TIMEOUT_MS, lo squillo
      si ferma comunque: niente campanello infinito se il Pi si blocca.

  PROTOCOLLO UART RP2350 -> PI (vedi PiZero/festival_phone_dialer.py)
    - riga di sole cifre  -> il Pi compone quel numero (ATD<numero>; sul SIM7600)
    - riga "HANGUP"       -> il Pi chiude la chiamata in corso (AT+CHUP)
    - riga "ANSWER"       -> il Pi risponde alla chiamata in arrivo (ATA)
  PROTOCOLLO UART PI -> RP2350
    - "INCOMING [numero]" -> chiamata in arrivo (numero opzionale)
    - "CALLEND"           -> chiamata terminata dall'altra parte / persa
*/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_NeoPixel.h>

// ---------- PIN DISPLAY (SPI0) ----------
#define TFT_SCK   2
#define TFT_MOSI  3
#define TFT_CS    5
#define TFT_DC    6
#define TFT_RST   7
#define TFT_BL    8

// ---------- PIN DISCO ----------
#define PIN_PULSE 14   // contatto impulsi (nsi)
#define PIN_GATE  15    // contatto fuori-riposo (finestra composizione)

// ---------- PIN ACCENSIONE SCHERMO ----------
#define PIN_SCREEN_ON 26   // HIGH = schermo acceso, LOW = retroilluminazione spenta

// ---------- UART VERSO IL PI ZERO 2W ----------
#define PI_UART_TX 12
#define PI_UART_RX 13
const uint32_t NUMBER_SEND_TIMEOUT_MS = 3000;   // silenzio dopo l'ultima cifra prima di inviare
const uint16_t SCREEN_DEBOUNCE_MS     = 50;     // stabilita' richiesta sul sensore hook (filtra i disturbi RF del modem)

// ---------- CAMPANELLO (L298N) ----------
#define BELL_IN1 0
#define BELL_IN2 1
#define LED_PIN  16   // WS2812 onboard

// Se l'ancora scatta una volta e resta ferma invece di oscillare, invertire
// i fili IN1/IN2 (la suoneria bistabile e' polarizzata).
const uint16_t RING_FREQ_HZ   = 25;                          // da regolare a orecchio tra 20 e 25 Hz
const uint16_t HALF_PERIOD_MS = 1000 / (2 * RING_FREQ_HZ);   // 20 ms a 25 Hz
const uint16_t RING_ON_MS     = 1000;                        // durata di uno squillo
const uint16_t RING_OFF_MS    = 4000;                        // pausa tra gli squilli
const uint32_t INCOMING_TIMEOUT_MS = 12000;                  // sicurezza: nessun INCOMING dal Pi -> stop

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
Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------- STATO ----------
uint16_t pulseCount = 0;

String number = "";
bool   dialing = false;
bool   calling = false;   // true da quando il numero e' stato inoltrato al Pi, o dopo aver risposto
bool   ringing = false;   // chiamata in arrivo, non ancora risposta
bool   callEnded = false; // l'altra parte ha chiuso: mostra "chiamata terminata" finche' non si riaggancia
int    hookStable = LOW;  // stato stabile (debounced) di PIN_SCREEN_ON: HIGH = cornetta alzata
uint32_t lastIncomingMs = 0;   // ultimo INCOMING ricevuto dal Pi (watchdog squillo)
int    gateStable = -1;

uint32_t lastDigitTime = 0;
bool     numberSent    = true;   // niente da inviare finche' non arriva una cifra

void setup() {
#if DEBUG_PULSES
  Serial.begin(115200);
  delay(3000);              // tempo per aprire il Serial Monitor dopo il reset da upload
  Serial.println("=== Festival Phone: debug pulses attivo, boot OK ===");
#endif

  pinMode(PIN_PULSE, INPUT_PULLUP);
  pinMode(PIN_GATE,  INPUT_PULLUP);

  pinMode(BELL_IN1, OUTPUT);
  pinMode(BELL_IN2, OUTPUT);
  coilOff();
  led.begin();
  setLed(0, 0, 0);

  pinMode(TFT_BL, OUTPUT);
  pinMode(PIN_SCREEN_ON, INPUT_PULLDOWN);   // stato definito (LOW) quando il sensore hook non conduce (cornetta appesa)
  digitalWrite(TFT_BL, digitalRead(PIN_SCREEN_ON));

  Serial1.setTX(PI_UART_TX);
  Serial1.setRX(PI_UART_RX);
  Serial1.begin(115200);

  // Imposta i pin SPI0 PRIMA di SPI.begin()
  SPI.setSCK(TFT_SCK);
  SPI.setTX(TFT_MOSI);
  SPI.begin();

  gfx.init(172, 320);      // risoluzione nativa del pannello (offset di centraggio calcolato automaticamente)
  gfx.setRotation(1);      // 1 = landscape 320x172

  hookStable = digitalRead(PIN_SCREEN_ON);
  drawScreen();

  gateStable = digitalRead(PIN_GATE);
}

void loop() {
  pollPiUart();

  // Watchdog: il Pi ripete INCOMING a ogni RING; se tace, lo squillo e' finito
  // (o il Pi si e' bloccato) e il campanello non deve restare acceso.
  if (ringing && (millis() - lastIncomingMs) >= INCOMING_TIMEOUT_MS) {
#if DEBUG_PULSES
    Serial.println("Nessun INCOMING dal Pi: fine squillo");
#endif
    ringing = false;
    number = "";
    drawScreen();
  }

  updateBell();

  int screenOnRaw = digitalRead(PIN_SCREEN_ON);
  // Durante lo squillo lo schermo resta acceso anche a cornetta appesa,
  // per mostrare chi sta chiamando.
  digitalWrite(TFT_BL, (screenOnRaw == HIGH || ringing) ? HIGH : LOW);

  // Debounce a livello stabile (stessa tecnica di GATE/PULSE): il sensore hook
  // e' soggetto a disturbi RF del modem durante la chiamata, quindi un singolo
  // fronte non basta a fidarsi del cambio di stato.
  static int      lastScreenOnRead   = screenOnRaw;
  static uint32_t lastScreenOnChange = 0;

  if (screenOnRaw != lastScreenOnRead) {
    lastScreenOnRead = screenOnRaw;
    lastScreenOnChange = millis();
  }
  if (screenOnRaw != hookStable && (millis() - lastScreenOnChange) >= SCREEN_DEBOUNCE_MS) {
    hookStable = screenOnRaw;
    numberSent = true;   // annulla un invio in sospeso: il numero composto non vale piu'
    callEnded = false;

    if (ringing) {
      if (hookStable == HIGH) {
        // Cornetta sollevata mentre squilla: rispondo. Il numero del
        // chiamante resta sullo schermo.
        Serial1.println("ANSWER");
        ringing = false;
        calling = true;
#if DEBUG_PULSES
        Serial.println("ANSWER inviato al Pi Zero");
#endif
      }
      // Cornetta riappesa mentre squilla (era gia' alzata): niente HANGUP,
      // altrimenti rifiuteremmo la chiamata. Si risponde rialzandola.
    } else {
      // Ogni fronte stabile su PIN_SCREEN_ON cancella il numero composto finora:
      // permette di usarlo come tasto di reset in caso di errore.
      number = "";
      calling = false;   // riaggancio: si torna pronti per una nuova composizione

      if (hookStable == LOW) {
        // Cornetta riappesa (fuori-riposo): il Pi deve chiudere la chiamata.
        Serial1.println("HANGUP");
#if DEBUG_PULSES
        Serial.println("HANGUP inviato al Pi Zero");
#endif
      } else {
#if DEBUG_PULSES
        Serial.println("Cornetta sollevata");
#endif
      }
    }

    drawScreen();
  }

  // Numero completo: 3s senza una nuova cifra -> lo inoltro al Pi Zero 2W
  if (!numberSent && !dialing && number.length() && (millis() - lastDigitTime) >= NUMBER_SEND_TIMEOUT_MS) {
    Serial1.println(number);
#if DEBUG_PULSES
    Serial.print("Numero inviato al Pi Zero: ");
    Serial.println(number);
#endif
    numberSent = true;
    calling = true;
    drawScreen();
  }

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

// Legge le righe in arrivo dal Pi senza bloccare il loop (il disco va
// campionato di continuo).
void pollPiUart() {
  static String rx = "";
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      rx.trim();
      if (rx.length()) handlePiMessage(rx);
      rx = "";
    } else if (c != '\r' && rx.length() < 64) {
      rx += c;
    }
  }
}

void handlePiMessage(const String& msg) {
#if DEBUG_PULSES
  Serial.print("Dal Pi Zero: ");
  Serial.println(msg);
#endif

  if (msg.startsWith("INCOMING")) {
    if (calling) return;   // gia' in chiamata: ignoro (niente avviso di chiamata)

    String caller = msg.substring(8);
    caller.trim();
    lastIncomingMs = millis();

    // Il Pi puo' mandare INCOMING piu' volte (prima senza numero, poi con il
    // caller ID): ridisegno solo se cambia qualcosa, per evitare sfarfallii.
    if (ringing && (caller.length() == 0 || caller == number)) return;

    if (!ringing) number = "";
    if (caller.length()) number = caller;
    ringing = true;
    dialing = false;
    numberSent = true;   // annulla un eventuale numero composto in sospeso
    callEnded = false;
    drawScreen();
  }
  else if (msg == "CALLEND") {
    if (!ringing && !calling) return;
    ringing = false;
    calling = false;
    // A cornetta alzata lo dico esplicitamente, altrimenti torno a riposo.
    callEnded = (hookStable == HIGH);
    if (!callEnded) number = "";
    drawScreen();
  }
}

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

// Bobina a riposo: entrambi gli ingressi LOW -> nessuna corrente
void coilOff() {
  digitalWrite(BELL_IN1, LOW);
  digitalWrite(BELL_IN2, LOW);
}

// Versione non bloccante di ringBurst() di suoneria_rp2040.ino: onda quadra
// bipolare a RING_FREQ_HZ per RING_ON_MS, poi pausa RING_OFF_MS. Chiamata a
// ogni giro di loop, cosi' disco, cornetta e UART restano reattivi e lo
// squillo si ferma nell'istante in cui si alza la cornetta.
void updateBell() {
  static bool     active     = false;
  static bool     burstOn    = false;
  static bool     polarity   = false;
  static uint32_t cycleStart = 0;
  static uint32_t lastFlip   = 0;

  bool want = ringing && hookStable == LOW;
  uint32_t now = millis();

  if (!want) {
    if (active) {
      coilOff();
      setLed(0, 0, 0);
      active = burstOn = false;
    }
    return;
  }

  if (!active) {
    active = true;
    cycleStart = now;
    lastFlip = now - HALF_PERIOD_MS;   // primo colpo subito
  }

  bool inBurst = ((now - cycleStart) % (RING_ON_MS + RING_OFF_MS)) < RING_ON_MS;

  if (inBurst != burstOn) {
    burstOn = inBurst;
    if (burstOn) setLed(0, 60, 0);   // verde: il campanello batte
    else       { coilOff(); setLed(0, 0, 0); }
  }

  if (burstOn && (now - lastFlip) >= HALF_PERIOD_MS) {
    lastFlip = now;
    polarity = !polarity;
    digitalWrite(BELL_IN1, polarity ? HIGH : LOW);   // polarita' A -> gong 1
    digitalWrite(BELL_IN2, polarity ? LOW : HIGH);   // polarita' B -> gong 2
  }
}

void handleGate(int level) {
  bool nowDialing = (level == GATE_DIALING);

  // Mentre squilla il disco e' ignorato: non deve sporcare il numero del chiamante.
  if (ringing) return;

  if (nowDialing && !dialing) {
    // inizio composizione di una cifra
    dialing = true;
    pulseCount = 0;
    calling = false;   // si ricomincia a comporre: lo stato "chiamata" non e' piu' valido
    callEnded = false;
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

  lastDigitTime = millis();
  numberSent = false;

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
  } else if (ringing) {
    gfx.setTextColor(COL_DIM);
    gfx.setTextSize(3);
    gfx.print("sconosciuto");
  } else {
    gfx.setTextColor(COL_DIM);
    gfx.print("---");
  }

  gfx.setTextColor(COL_STATUS);
  gfx.setTextSize(2);
  gfx.setCursor(6, gfx.height() - 22);
  if (ringing)        gfx.print("chiamata in arrivo");
  else if (calling)   gfx.print("chiamata in corso");
  else if (callEnded) gfx.print("chiamata terminata");
  else if (dialing)   gfx.print("comporre...");
  else                gfx.print("pronto");
}
