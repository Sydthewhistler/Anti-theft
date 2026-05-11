/******************************************************************************
 * Antivol IoT — UCA21
 * Basé sur le code de Fabien Ferrero (Aug, 2021)
 ******************************************************************************/

#define DEBUG  // commenter pour désactiver tous les Serial

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
  if (ev == EV_TXCOMPLETE) {
    #ifdef DEBUG
    Serial.println(F("LoRa: TX complete"));
    #endif
    loraTxDone = true;
  }
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
  if (LMIC.opmode & OP_TXRXPEND) {
    #ifdef DEBUG
    Serial.println(F("LoRa: TX en cours"));
    #endif
    return;
  }

  unsigned char mydata[3];
  mydata[0] = 0x01;
  mydata[1] = 0x00;
  mydata[2] = 0x01;

  LMIC_setTxData2(1, mydata, sizeof(mydata), 0);

  #ifdef DEBUG
  Serial.println(F("LoRa: envoi..."));
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

void setEtat(int nouvelEtat) {
  etatCourant = nouvelEtat;
  switch (nouvelEtat) {
    case PASSIF:
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, HIGH);
      resetDetection();
      #ifdef DEBUG
      Serial.println(F("-> PASSIF"));
      #endif
      break;
    case ACTIF:
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      resetDetection();
      #ifdef DEBUG
      Serial.println(F("-> ACTIF"));
      #endif
      break;
    case ALERTE:
      digitalWrite(BUZZER_PIN, HIGH);
      #ifdef DEBUG
      Serial.println(F("-> ALERTE !"));
      #endif
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

bool volDetecte(int32_t magnitude) {
  int32_t diff = abs(magnitude - prevMagnitude);
  prevMagnitude = magnitude;

  if (diff > SEUIL_DIFF) {
    if (!etaitEnMouvement && echanCalme >= PAUSE_SEUIL) {
      incrementMvt += INCR_PENALITE;
      if (incrementMvt > INCREMENT_MAX) incrementMvt = INCREMENT_MAX;
      #ifdef DEBUG
      Serial.print(F("Reprise -> incr="));
      Serial.println(incrementMvt);
      #endif
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

  #ifdef DEBUG
  Serial.print(F("diff="));  Serial.print(diff);
  Serial.print(F(" cpt="));  Serial.print(compteur);
  Serial.print(F("/"));      Serial.print(SEUIL_ALARME);
  Serial.print(F(" incr=")); Serial.print(incrementMvt);
  Serial.print(F(" -> "));
  Serial.println(diff > SEUIL_DIFF ? F("MVT") : F("calme"));
  #endif

  return (compteur >= SEUIL_ALARME);
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  #ifdef DEBUG
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("Starting..."));
  #endif

  pinMode(I2C_PWR_PIN, OUTPUT);
  digitalWrite(I2C_PWR_PIN, HIGH);
  delay(300);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Wire.begin();

  if (myIMU.begin(sampleRate, accelRange) != 0) {
    #ifdef DEBUG
    Serial.println(F("IMU: erreur init"));
    #endif
  } else {
    #ifdef DEBUG
    Serial.println(F("IMU: OK"));
    #endif
  }

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

  #ifdef DEBUG
  Serial.println(F("LoRa: pret"));
  Serial.println(F("Ready."));
  #endif

  setEtat(PASSIF);
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {

  os_runloop_once();

  if (boutonVientDEtreAppuye()) {
    #ifdef DEBUG
    Serial.println(F("Bouton appuye"));
    #endif
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