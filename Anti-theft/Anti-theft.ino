/******************************************************************************
 * Antivol IoT — UCA21 — VERSION OPTIMISÉE
 * Accéléromètre + Buzzer D2 + Pavé numérique + LoRaWAN ABP
 *
 * Buzzer    : D2
 * Keypad R  : A0, A1, A2, A3
 * Keypad C  : D5, D7, D9, D3
 *
 * LoRa :
 * NSS  = D10
 * RST  = D8
 * DIO  = D6
 *
 * Utilisation :
 * *     = armer / désarmer si pas d’alarme
 * 1234# = désactiver l’alarme
 * *     = effacer la saisie pendant l’alarme
 ******************************************************************************/

#define CFG_EU 1

// Optimisation LMIC
#define DISABLE_PING
#define DISABLE_BEACONS
#define DISABLE_JOIN

#define LOW_POWER

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include "Wire.h"
#include "kxtj3-1057.h"

#define DEBUG 0

// ─────────────────────────────────────────────
//  LORAWAN — ABP TTN
// ─────────────────────────────────────────────
static const u4_t DEVADDR = 0x260B9C2F;

static const PROGMEM u1_t NWKSKEY[16] = {
  0x84, 0x86, 0xC7, 0x16,
  0x7A, 0x66, 0x6D, 0x95,
  0x3B, 0x8A, 0xE5, 0x66,
  0x16, 0x63, 0xC9, 0xF4
};

static const PROGMEM u1_t APPSKEY[16] = {
  0x01, 0xAB, 0xC5, 0x9D,
  0x2E, 0xE5, 0x47, 0x2F,
  0xE6, 0x83, 0xC8, 0x0D,
  0x54, 0x54, 0xA5, 0x39
};

// Obligatoire même en ABP
void os_getArtEui(u1_t* buf) {}
void os_getDevEui(u1_t* buf) {}
void os_getDevKey(u1_t* buf) {}

// Pin mapping LoRa UCA21 / RFM95W
const lmic_pinmap lmic_pins = {
  .nss  = 10,
  .rxtx = LMIC_UNUSED_PIN,
  .rst  = 8,
  .dio  = {6, 6, 6},
};

// Payloads envoyés à TTN
#define EVT_ARMED     1
#define EVT_DISARMED  2
#define EVT_ALARM     3
#define EVT_BAD_CODE  4

uint8_t txPayload[1];
bool pendingLoRa = false;
uint8_t pendingEvent = 0;

// ─────────────────────────────────────────────
//  ACCÉLÉROMÈTRE
// ─────────────────────────────────────────────
float sampleRate = 6.25;
uint8_t accelRange = 2;
KXTJ3 myIMU(0x0E);

// ─────────────────────────────────────────────
//  PINS
// ─────────────────────────────────────────────
#define BUZZER_PIN 2

// Pavé numérique 4x4
const byte ROWS = 4;
const byte COLS = 4;

const byte rowPins[ROWS] = {A0, A1, A2, A3};
const byte colPins[COLS] = {5, 7, 9, 3};

const char keymap[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// Code secret
const char CODE_SECRET[] = "1234";
char saisie[6];
byte saisieIndex = 0;

// ─────────────────────────────────────────────
//  PARAMÈTRES DÉTECTION
// ─────────────────────────────────────────────
#define SEUIL_DIFF       4608L
#define INCREMENT_BASE   3
#define INCREMENT_MAX    15
#define INCR_PENALITE    3
#define DECREMENT_CALME  1
#define SEUIL_ALARME     150
#define COMPTEUR_MAX     150
#define PAUSE_SEUIL      6
#define CALME_RESET      18

#define SENSOR_INTERVAL  160

// ─────────────────────────────────────────────
//  VARIABLES SYSTÈME
// ─────────────────────────────────────────────
bool systemeActif = false;
bool alarme = false;

long prevMagnitude = 0;
int compteur = 0;
int incrementMvt = INCREMENT_BASE;
int echanCalme = 0;
bool etaitEnMouvement = false;

unsigned long lastSensorRead = 0;

// Clavier
char lastKey = 0;
bool keyAlreadySent = false;
unsigned long lastKeyChange = 0;

// ─────────────────────────────────────────────
//  BUZZER
// ─────────────────────────────────────────────
void buzzerOn() {
  digitalWrite(BUZZER_PIN, HIGH);
}

void buzzerOff() {
  digitalWrite(BUZZER_PIN, LOW);
}

// ─────────────────────────────────────────────
//  RESET DÉTECTION
// ─────────────────────────────────────────────
void resetDetection() {
  prevMagnitude = 0;
  compteur = 0;
  incrementMvt = INCREMENT_BASE;
  echanCalme = 0;
  etaitEnMouvement = false;
}

// ─────────────────────────────────────────────
//  LORA
// ─────────────────────────────────────────────
void queueLoRaEvent(uint8_t eventCode) {
  if (LMIC.opmode & OP_TXRXPEND) {
    pendingLoRa = true;
    pendingEvent = eventCode;
    return;
  }

  txPayload[0] = eventCode;
  LMIC_setTxData2(1, txPayload, 1, 0);

  Serial.print(F("LoRa queued event="));
  Serial.println(eventCode);
}

void onEvent(ev_t ev) {
  if (ev == EV_TXCOMPLETE) {
    Serial.println(F("EV_TXCOMPLETE"));

    if (pendingLoRa) {
      pendingLoRa = false;
      queueLoRaEvent(pendingEvent);
    }

  } else {
    Serial.print(F("EV="));
    Serial.println((unsigned)ev);
  }
}

// ─────────────────────────────────────────────
//  ARMEMENT / DÉSARMEMENT / ALARME
// ─────────────────────────────────────────────
void armerSysteme() {
  systemeActif = true;
  alarme = false;
  resetDetection();
  buzzerOff();

  Serial.println(F("Systeme ARME"));
  queueLoRaEvent(EVT_ARMED);
}

void desarmerSysteme() {
  systemeActif = false;
  alarme = false;
  resetDetection();
  buzzerOff();

  saisieIndex = 0;
  saisie[0] = '\0';

  Serial.println(F("Systeme DESARME"));
  queueLoRaEvent(EVT_DISARMED);
}

void declencherAlarme() {
  alarme = true;
  saisieIndex = 0;
  saisie[0] = '\0';

  buzzerOn();

  Serial.println(F("VOL DETECTE"));
  queueLoRaEvent(EVT_ALARM);
}

// ─────────────────────────────────────────────
//  CLAVIER SANS LIBRAIRIE
// ─────────────────────────────────────────────
void initKeypadPins() {
  for (byte r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }

  for (byte c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }
}

char scanKeypadRaw() {
  for (byte r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(5);

    for (byte c = 0; c < COLS; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        digitalWrite(rowPins[r], HIGH);
        return keymap[r][c];
      }
    }

    digitalWrite(rowPins[r], HIGH);
  }

  return 0;
}

char getKeypadKey() {
  char currentKey = scanKeypadRaw();

  if (currentKey != lastKey) {
    lastKey = currentKey;
    lastKeyChange = millis();
    keyAlreadySent = false;
  }

  if (currentKey != 0 && !keyAlreadySent && (millis() - lastKeyChange) > 40) {
    keyAlreadySent = true;
    return currentKey;
  }

  if (currentKey == 0) {
    keyAlreadySent = false;
  }

  return 0;
}

void lireClavier() {
  char key = getKeypadKey();
  if (!key) return;

  Serial.print(F("Key="));
  Serial.println(key);

  // * = armer / désarmer si pas d’alarme
  // * = effacer la saisie si alarme
  if (key == '*') {
    if (alarme) {
      saisieIndex = 0;
      saisie[0] = '\0';
      Serial.println(F("Saisie effacee"));
    } else {
      if (systemeActif) {
        desarmerSysteme();
      } else {
        armerSysteme();
      }
    }
    return;
  }

  // # = valider le code uniquement pendant l’alarme
  if (key == '#') {
    if (alarme) {
      if (strcmp(saisie, CODE_SECRET) == 0) {
        Serial.println(F("Code OK"));
        desarmerSysteme();
      } else {
        Serial.println(F("Code faux"));
        queueLoRaEvent(EVT_BAD_CODE);
      }

      saisieIndex = 0;
      saisie[0] = '\0';
    }
    return;
  }

  // Hors alarme, on ignore les autres touches
  if (!alarme) {
    return;
  }

  // En alarme, on ajoute les touches au code
  if (saisieIndex < sizeof(saisie) - 1) {
    saisie[saisieIndex++] = key;
    saisie[saisieIndex] = '\0';

    Serial.print(F("Saisie="));
    Serial.println(saisie);
  } else {
    saisieIndex = 0;
    saisie[0] = '\0';
    Serial.println(F("Saisie trop longue, effacee"));
  }
}

// ─────────────────────────────────────────────
//  ACCÉLÉROMÈTRE
// ─────────────────────────────────────────────
void lireAccelerometre() {
  myIMU.standby(false);

  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;

  myIMU.readRegisterInt16(&x, KXTJ3_XOUT_L);
  myIMU.readRegisterInt16(&y, KXTJ3_YOUT_L);
  myIMU.readRegisterInt16(&z, KXTJ3_ZOUT_L);

  myIMU.standby(true);

  long magnitude = labs((long)x) + labs((long)y) + labs((long)z);

  if (prevMagnitude == 0) {
    prevMagnitude = magnitude;
    return;
  }

  long diff = labs(magnitude - prevMagnitude);
  prevMagnitude = magnitude;

  if (diff > SEUIL_DIFF) {
    if (!etaitEnMouvement && echanCalme >= PAUSE_SEUIL) {
      incrementMvt += INCR_PENALITE;

      if (incrementMvt > INCREMENT_MAX) {
        incrementMvt = INCREMENT_MAX;
      }
    }

    compteur += incrementMvt;

    if (compteur > COMPTEUR_MAX) {
      compteur = COMPTEUR_MAX;
    }

    echanCalme = 0;
    etaitEnMouvement = true;

  } else {
    echanCalme++;

    if (echanCalme >= CALME_RESET) {
      incrementMvt = INCREMENT_BASE;
    }

    compteur -= DECREMENT_CALME;

    if (compteur < 0) {
      compteur = 0;
    }

    etaitEnMouvement = false;
  }

  if (!alarme && compteur >= SEUIL_ALARME) {
    declencherAlarme();
  }

#if DEBUG
  Serial.print(F("diff="));
  Serial.print(diff);
  Serial.print(F(" cpt="));
  Serial.println(compteur);
#endif
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("Antivol IoT UCA21"));

  pinMode(BUZZER_PIN, OUTPUT);
  buzzerOff();

  initKeypadPins();

  Wire.begin();

  if (myIMU.begin(sampleRate, accelRange) != 0) {
    Serial.println(F("IMU FAIL"));
  } else {
    Serial.println(F("IMU OK"));
  }

  myIMU.intConf(123, 1, 10, HIGH);

  uint8_t readData = 0;
  myIMU.readRegister(&readData, KXTJ3_WHO_AM_I);

  Serial.print(F("IMU ID=0x"));
  Serial.println(readData, HEX);

  Serial.println(F("Init LoRa"));

  os_init();
  Serial.println(F("os_init OK"));

  LMIC_reset();
  Serial.println(F("LMIC_reset OK"));

  LMIC_setClockError(MAX_CLOCK_ERROR * 2 / 100);

#ifdef PROGMEM
  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];

  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));

  LMIC_setSession(0x1, DEVADDR, nwkskey, appskey);
#else
  LMIC_setSession(0x1, DEVADDR, NWKSKEY, APPSKEY);
#endif

  Serial.println(F("Session ABP OK"));

#if defined(CFG_EU)
  LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);
  LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);
  LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);
#endif

  Serial.println(F("Channels OK"));

  LMIC_setLinkCheckMode(0);
  LMIC.dn2Dr = DR_SF9;
  LMIC_setDrTxpow(DR_SF9, 20);

  Serial.println(F("Pret"));
  Serial.println(F("* = armer/desarmer"));
  Serial.println(F("Alarme : 1234#"));
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {
  os_runloop_once();

  lireClavier();

  if (!systemeActif || alarme) {
    return;
  }

  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = millis();
    lireAccelerometre();
  }
}