/******************************************************************************
 * Antivol IoT — UCA21
 * Basé sur le code de Fabien Ferrero (Aug, 2021)
 ******************************************************************************/

#define DEBUG

#include <FastLED.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include "kxtj3-1057.h"
#include "Wire.h"

#define LOW_POWER
#define KXTJ3_DEBUG Serial

float   sampleRate = 6.25;
uint8_t accelRange = 2;
KXTJ3 myIMU(0x0E);

#define DATA_PIN          4
#define LED_TYPE          WS2811
#define COLOR_ORDER       GRB
#define NUM_LEDS          21
CRGB leds[NUM_LEDS];
#define BRIGHTNESS        16
#define FRAMES_PER_SECOND 120
uint8_t gHue = 0;

#define BUZZER_PIN        9
#define BUTTON_PIN        2

// ─────────────────────────────────────────────
//  LORA — CLÉS ABP
// ─────────────────────────────────────────────
static const u4_t DEVADDR = 0x260B9C2F;

static const PROGMEM u1_t NWKSKEY[16] = {
  0x84, 0x86, 0xC7, 0x16, 0x7A, 0x66, 0x6D, 0x95,
  0x3B, 0x8A, 0xE5, 0x66, 0x16, 0x63, 0xC9, 0xF4
};

static const u1_t PROGMEM APPSKEY[16] = {
  0x01, 0xAB, 0xC5, 0x9D, 0x2E, 0xE5, 0x47, 0x2F,
  0xE6, 0x83, 0xC8, 0x0D, 0x54, 0x54, 0xA5, 0x39
};

void os_getArtEui(u1_t* buf) {}
void os_getDevEui(u1_t* buf) {}
void os_getDevKey(u1_t* buf) {}

const lmic_pinmap lmic_pins = {
  .nss  = 10,
  .rxtx = LMIC_UNUSED_PIN,
  .rst  = 8,
  .dio  = {6, 6, 6},
};

static osjob_t sendjob;
static bool loraTxDone = false;

// ─────────────────────────────────────────────
//  PARAMÈTRES DE DÉTECTION
// ─────────────────────────────────────────────
#define SEUIL_DIFF      1.5
#define INCREMENT_BASE  3
#define INCREMENT_MAX   15
#define INCR_PENALITE   3
#define DECREMENT_CALME 1
#define SEUIL_ALARME    150
#define COMPTEUR_MAX    150
#define PAUSE_SEUIL     6
#define CALME_RESET     18

// ─────────────────────────────────────────────
//  VARIABLES BOUTON
// ─────────────────────────────────────────────
bool dernierEtatBouton = HIGH;
bool systemeActif      = false;

// ─────────────────────────────────────────────
//  LORA — ÉVÉNEMENTS
// ─────────────────────────────────────────────
void onEvent(ev_t ev) {
  if (ev == EV_TXCOMPLETE) {
    #ifdef DEBUG
    Serial.println(F("LoRa: TX complete"));
    #endif
    loraTxDone = true;
  }
}

// ─────────────────────────────────────────────
//  LORA — ENVOI
// ─────────────────────────────────────────────
void SendNotif() {
  if (LMIC.opmode & OP_TXRXPEND) {
    #ifdef DEBUG
    Serial.println(F("LoRa: TX deja en cours"));
    #endif
    return;
  }

  unsigned char mydata[3];
  mydata[0] = 0x01;
  mydata[1] = 0x00;
  mydata[2] = 0x01;

  LMIC_setTxData2(1, mydata, sizeof(mydata), 0);

  #ifdef DEBUG
  Serial.println(F("LoRa: envoi alerte..."));
  #endif

  loraTxDone = false;
  unsigned long debut = millis();
  while (!loraTxDone && millis() - debut < 10000) {
    os_runloop_once();
  }

  #ifdef DEBUG
  if (loraTxDone)
    Serial.println(F("LoRa: OK"));
  else
    Serial.println(F("LoRa: timeout"));
  #endif
}

// ─────────────────────────────────────────────
//  FONCTION BOUTON — INCHANGÉE
// ─────────────────────────────────────────────
bool boutonVientDEtreAppuye() {
  bool etat = digitalRead(BUTTON_PIN);
  if (dernierEtatBouton == HIGH && etat == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      dernierEtatBouton = LOW;
      return true;
    }
  }
  if (etat == HIGH) dernierEtatBouton = HIGH;
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");

  // Buzzer — INCHANGÉ
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Bouton — INCHANGÉ
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // FastLED — INCHANGÉ
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);

  // IMU — INCHANGÉ
  if (myIMU.begin(sampleRate, accelRange) != 0)
    Serial.println("Failed to initialize IMU.");
  else
    Serial.println("IMU initialized.");

  myIMU.intConf(123, 1, 10, HIGH);

  uint8_t readData = 0;
  myIMU.readRegister(&readData, KXTJ3_WHO_AM_I);
  Serial.print("Who am I? 0x");
  Serial.println(readData, HEX);

  // ── Init LoRa ──
  os_init();
  LMIC_reset();
  LMIC_setClockError(MAX_CLOCK_ERROR * 2 / 100);

  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];
  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  LMIC_setSession(0x1, DEVADDR, nwkskey, appskey);

  LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);
  LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);
  LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);

  LMIC_setLinkCheckMode(0);
  LMIC.dn2Dr = DR_SF9;
  LMIC_setDrTxpow(DR_SF7, 14);

  Serial.println("LoRa ready.");
  Serial.println("Systeme ETEINT. Appuie sur BT0 pour armer.");
}

void loop() {

  os_runloop_once();

  // ─────────────────────────────────────────
  //  GESTION BOUTON — INCHANGÉE
  // ─────────────────────────────────────────
  static float prevMagnitude    = 1.0;
  static int   compteur         = 0;
  static bool  alarme           = false;
  static int   incrementMvt     = INCREMENT_BASE;
  static int   echanCalme       = 0;
  static bool  etaitEnMouvement = false;

  if (boutonVientDEtreAppuye()) {
    if (alarme) {
      alarme           = false;
      compteur         = 0;
      incrementMvt     = INCREMENT_BASE;
      echanCalme       = 0;
      etaitEnMouvement = false;
      prevMagnitude    = 1.0;
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("Alarme reset. Systeme reste ARME.");
    } else if (systemeActif) {
      systemeActif     = false;
      compteur         = 0;
      incrementMvt     = INCREMENT_BASE;
      echanCalme       = 0;
      etaitEnMouvement = false;
      prevMagnitude    = 1.0;
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("Systeme ETEINT.");
    } else {
      systemeActif = true;
      Serial.println("Systeme ARME.");
    }
  }

  // ─────────────────────────────────────────
  //  DÉTECTION — INCHANGÉE
  // ─────────────────────────────────────────
  if (!systemeActif) {
    delay(50);
    return;
  }

  myIMU.standby(false);

  int16_t dataHighresX = 0;
  int16_t dataHighresY = 0;
  int16_t dataHighresZ = 0;

  myIMU.readRegisterInt16(&dataHighresX, KXTJ3_XOUT_L);
  myIMU.readRegisterInt16(&dataHighresY, KXTJ3_YOUT_L);
  myIMU.readRegisterInt16(&dataHighresZ, KXTJ3_ZOUT_L);

  float x = dataHighresX / 1024.0;
  float y = dataHighresY / 1024.0;
  float z = dataHighresZ / 1024.0;

  float magnitude = sqrt(x * x + y * y + z * z);
  float diff = abs(magnitude - prevMagnitude);
  prevMagnitude = magnitude;

  if (diff > SEUIL_DIFF) {
    if (!etaitEnMouvement && echanCalme >= PAUSE_SEUIL) {
      incrementMvt += INCR_PENALITE;
      if (incrementMvt > INCREMENT_MAX)
        incrementMvt = INCREMENT_MAX;
      #ifdef DEBUG
      Serial.print("  Reprise -> INCREMENT = ");
      Serial.println(incrementMvt);
      #endif
    }
    compteur += incrementMvt;
    if (compteur > COMPTEUR_MAX) compteur = COMPTEUR_MAX;
    echanCalme       = 0;
    etaitEnMouvement = true;
  } else {
    echanCalme++;
    if (echanCalme >= CALME_RESET) {
      if (incrementMvt != INCREMENT_BASE) {
        incrementMvt = INCREMENT_BASE;
        #ifdef DEBUG
        Serial.println("  Calme prolonge -> INCREMENT reset");
        #endif
      }
    }
    compteur -= DECREMENT_CALME;
    if (compteur < 0) compteur = 0;
    etaitEnMouvement = false;
  }

  // ── Décision alarme ──
  if (!alarme && compteur >= SEUIL_ALARME) {
    alarme = true;
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("VOL DETECTE ! Buzzer ON");
    SendNotif();
  }

  if (alarme && compteur == 0) {
    alarme = false;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("Retour au calme. Buzzer OFF");
  }

  #ifdef DEBUG
  Serial.print("diff=");    Serial.print(diff, 2);
  Serial.print("  cpt=");   Serial.print(compteur);
  Serial.print("/");        Serial.print(SEUIL_ALARME);
  Serial.print("  incr=");  Serial.print(incrementMvt);
  Serial.print("  calme="); Serial.print(echanCalme);
  Serial.print("  buzz=");  Serial.print(alarme ? "ON" : "OFF");
  Serial.print("  -> ");
  if      (alarme)            Serial.println("ALARME");
  else if (diff > SEUIL_DIFF) Serial.println("mouvement...");
  else                        Serial.println("calme");
  #endif

  myIMU.standby(true);
  delay(160);
}