/******************************************************************************
 * Antivol IoT — UCA21
 ******************************************************************************/

#include <RadioLib.h>
#include "kxtj3-1057.h"
#include "Wire.h"

#define LOW_POWER

float   sampleRate = 6.25;
uint8_t accelRange = 2;
KXTJ3 myIMU(0x0E);

#define I2C_PWR_PIN 4
#define BUZZER_PIN  9
#define BUTTON_PIN  2

// ─────────────────────────────────────────────
//  LORA — MODULE RFM95W sur UCA21
// ─────────────────────────────────────────────
RFM95 radio = new Module(10, 6, 8, -1); // NSS, DIO0, RST, DIO1

// ─────────────────────────────────────────────
//  LORAWAN — CLÉS ABP
// ─────────────────────────────────────────────
uint32_t devAddr  = 0x260B9C2F;

uint8_t nwkSKey[] = {
  0x84, 0x86, 0xC7, 0x16, 0x7A, 0x66, 0x6D, 0x95,
  0x3B, 0x8A, 0xE5, 0x66, 0x16, 0x63, 0xC9, 0xF4
};

uint8_t appSKey[] = {
  0x01, 0xAB, 0xC5, 0x9D, 0x2E, 0xE5, 0x47, 0x2F,
  0xE6, 0x83, 0xC8, 0x0D, 0x54, 0x54, 0xA5, 0x39
};

LoRaWANNode node(&radio, &EU868);

// ─────────────────────────────────────────────
//  PARAMÈTRES DE DÉTECTION
// ─────────────────────────────────────────────
#define SEUIL_DIFF      4608
#define INCREMENT_BASE  3
#define INCREMENT_MAX   15
#define INCR_PENALITE   3
#define DECREMENT_CALME 1
#define SEUIL_ALARME    150
#define COMPTEUR_MAX    150
#define PAUSE_SEUIL     6
#define CALME_RESET     18

bool dernierEtatBouton = HIGH;
bool systemeActif      = false;

// ─────────────────────────────────────────────
//  LORA — ENVOI
// ─────────────────────────────────────────────
void SendNotif() {
  uint8_t payload[] = { 0x01, 0x00, 0x01 };
  node.sendReceive(payload, sizeof(payload), 1);
}

// ─────────────────────────────────────────────
//  BOUTON
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
  pinMode(I2C_PWR_PIN, OUTPUT);
  digitalWrite(I2C_PWR_PIN, HIGH);
  delay(300);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin();
  myIMU.begin(sampleRate, accelRange);

  // Init LoRa
  radio.begin();
  node.beginABP(devAddr, nwkSKey, appSKey);
}

void loop() {

  static int32_t prevMagnitude    = 0;
  static int     compteur         = 0;
  static bool    alarme           = false;
  static int     incrementMvt     = INCREMENT_BASE;
  static int     echanCalme       = 0;
  static bool    etaitEnMouvement = false;

  if (boutonVientDEtreAppuye()) {
    if (alarme) {
      alarme           = false;
      compteur         = 0;
      incrementMvt     = INCREMENT_BASE;
      echanCalme       = 0;
      etaitEnMouvement = false;
      prevMagnitude    = 0;
      digitalWrite(BUZZER_PIN, LOW);
    } else if (systemeActif) {
      systemeActif     = false;
      compteur         = 0;
      incrementMvt     = INCREMENT_BASE;
      echanCalme       = 0;
      etaitEnMouvement = false;
      prevMagnitude    = 0;
      digitalWrite(BUZZER_PIN, LOW);
    } else {
      systemeActif = true;
    }
  }

  if (!systemeActif) {
    delay(50);
    return;
  }

  myIMU.standby(false);

  int16_t X = 0, Y = 0, Z = 0;
  myIMU.readRegisterInt16(&X, KXTJ3_XOUT_L);
  myIMU.readRegisterInt16(&Y, KXTJ3_YOUT_L);
  myIMU.readRegisterInt16(&Z, KXTJ3_ZOUT_L);

  int32_t magnitude = (int32_t)abs(X) + abs(Y) + abs(Z);
  int32_t diff      = abs(magnitude - prevMagnitude);
  prevMagnitude     = magnitude;

  if (diff > SEUIL_DIFF) {
    if (!etaitEnMouvement && echanCalme >= PAUSE_SEUIL) {
      incrementMvt += INCR_PENALITE;
      if (incrementMvt > INCREMENT_MAX)
        incrementMvt = INCREMENT_MAX;
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

  if (!alarme && compteur >= SEUIL_ALARME) {
    alarme = true;
    digitalWrite(BUZZER_PIN, HIGH);
    SendNotif();
  }

  if (alarme && compteur == 0) {
    alarme = false;
    digitalWrite(BUZZER_PIN, LOW);
  }

  myIMU.standby(true);
  delay(160);
}