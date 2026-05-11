/******************************************************************************
 * Antivol IoT — UCA21
 * Basé sur le code de Fabien Ferrero (Aug, 2021)
 ******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include "kxtj3-1057.h"
#include "Wire.h"

#define LOW_POWER

// ─────────────────────────────────────────────
//  LORA — CLÉS ABP (à remplir depuis TTN)
// ─────────────────────────────────────────────
static const u4_t DEVADDR = 0x00000000;

static const PROGMEM u1_t NWKSKEY[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const u1_t PROGMEM APPSKEY[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
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
static bool    loraTxDone = false;

// ─────────────────────────────────────────────
//  ACCÉLÉROMÈTRE
// ─────────────────────────────────────────────
float   sampleRate = 6.25;
uint8_t accelRange = 2;
KXTJ3   myIMU(0x0E);

// ─────────────────────────────────────────────
//  PÉRIPHÉRIQUES
// ─────────────────────────────────────────────
#define BUZZER_PIN  9
#define BUTTON_PIN  2
#define LED_PIN     13
#define I2C_PWR_PIN 4

// ─────────────────────────────────────────────
//  PARAMÈTRES DE DÉTECTION
// ─────────────────────────────────────────────
// Valeurs brutes int16 — 1g ≈ 1024 unités en LOW_POWER
// SEUIL_DIFF = 1.5g → 1.5 * 1024 = 1536 (distance Manhattan |X|+|Y|+|Z|)
#define SEUIL_DIFF      1536
#define INCREMENT_BASE  3
#define INCREMENT_MAX   15
#define INCR_PENALITE   3
#define DECREMENT_CALME 1
#define SEUIL_ALARME    150
#define COMPTEUR_MAX    150
#define PAUSE_SEUIL     6
#define CALME_RESET     125

// ─────────────────────────────────────────────
//  MACHINE À ÉTATS
// ─────────────────────────────────────────────
#define PASSIF 0
#define ACTIF  1
#define ALERTE 2
int etatCourant = PASSIF;

// ─────────────────────────────────────────────
//  VARIABLES GLOBALES
// ─────────────────────────────────────────────
static int32_t prevMagnitude    = 0;
static int     compteur         = 0;
static int     incrementMvt     = INCREMENT_BASE;
static int     echanCalme       = 0;
static bool    etaitEnMouvement = false;

bool          dernierEtatBouton = HIGH;
unsigned long dernierBlink      = 0;
bool          blinkState        = false;
#define BLINK_INTERVAL 300

// ─────────────────────────────────────────────
//  LORA — ÉVÉNEMENTS
// ─────────────────────────────────────────────
void onEvent(ev_t ev) {
  if (ev == EV_TXCOMPLETE) loraTxDone = true;
}

// ─────────────────────────────────────────────
//  FONCTIONS
// ─────────────────────────────────────────────

void resetDetection() {
  compteur         = 0;
  incrementMvt     = INCREMENT_BASE;
  echanCalme       = 0;
  etaitEnMouvement = false;
  prevMagnitude    = 0;
}

void SendNotif() {
  if (LMIC.opmode & OP_TXRXPEND) return;

  unsigned char mydata[3];
  mydata[0] = 0x01;
  mydata[1] = 0x00;
  mydata[2] = 0x01;

  LMIC_setTxData2(1, mydata, sizeof(mydata), 0);

  loraTxDone = false;
  unsigned long debut = millis();
  while (!loraTxDone && millis() - debut < 10000) {
    os_runloop_once();
  }
}

void setEtat(int nouvelEtat) {
  etatCourant = nouvelEtat;
  switch (nouvelEtat) {
    case PASSIF:
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, HIGH);
      resetDetection();
      break;
    case ACTIF:
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      resetDetection();
      break;
    case ALERTE:
      digitalWrite(BUZZER_PIN, HIGH);
      SendNotif();
      break;
  }
}

bool boutonVientDEtreAppuye() {
  bool etatActuel = digitalRead(BUTTON_PIN);
  if (dernierEtatBouton == HIGH && etatActuel == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      dernierEtatBouton = LOW;
      return true;
    }
  }
  if (etatActuel == HIGH) dernierEtatBouton = HIGH;
  return false;
}

// Utilise la distance Manhattan (|X|+|Y|+|Z|) au lieu de sqrt
// Evite la librairie math et les floats → gain ~1KB
bool volDetecte(int32_t magnitude) {
  int32_t diff = abs(magnitude - prevMagnitude);
  prevMagnitude = magnitude;

  if (diff > SEUIL_DIFF) {
    if (!etaitEnMouvement && echanCalme >= PAUSE_SEUIL) {
      incrementMvt += INCR_PENALITE;
      if (incrementMvt > INCREMENT_MAX) incrementMvt = INCREMENT_MAX;
    }
    compteur += incrementMvt;
    if (compteur > COMPTEUR_MAX) compteur = COMPTEUR_MAX;
    echanCalme       = 0;
    etaitEnMouvement = true;
  } else {
    echanCalme++;
    if (echanCalme >= CALME_RESET && incrementMvt != INCREMENT_BASE)
      incrementMvt = INCREMENT_BASE;
    compteur -= DECREMENT_CALME;
    if (compteur < 0) compteur = 0;
    etaitEnMouvement = false;
  }
  return (compteur >= SEUIL_ALARME);
}

void setup() {
  // Alimente le bus I2C sur UCA21 via D4
  pinMode(I2C_PWR_PIN, OUTPUT);
  digitalWrite(I2C_PWR_PIN, HIGH);
  delay(300);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Wire.begin();
  myIMU.begin(sampleRate, accelRange);

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

  setEtat(PASSIF);
}


void loop() {

  os_runloop_once();

  if (boutonVientDEtreAppuye()) {
    switch (etatCourant) {
      case PASSIF: setEtat(ACTIF);  break;
      case ACTIF:  setEtat(PASSIF); break;
      case ALERTE: setEtat(PASSIF); break;
    }
  }

  switch (etatCourant) {

    case PASSIF:
      break;

    case ACTIF: {
      myIMU.standby(false);
      int16_t X = 0, Y = 0, Z = 0;
      myIMU.readRegisterInt16(&X, KXTJ3_XOUT_L);
      myIMU.readRegisterInt16(&Y, KXTJ3_YOUT_L);
      myIMU.readRegisterInt16(&Z, KXTJ3_ZOUT_L);
      myIMU.standby(true);

      // Distance Manhattan — pas de float ni de sqrt
      int32_t magnitude = (int32_t)abs(X) + abs(Y) + abs(Z);

      if (volDetecte(magnitude)) setEtat(ALERTE);

      delay(160);
      break;
    }

    case ALERTE:
      if (millis() - dernierBlink >= BLINK_INTERVAL) {
        dernierBlink = millis();
        blinkState   = !blinkState;
        digitalWrite(LED_PIN, blinkState ? HIGH : LOW);
      }
      break;
  }
}