/******************************************************************************
 * Détection de vol — UCA21
 * Avec pavé numérique pour désactiver l'alarme
 ******************************************************************************/

#define DEBUG

#include <FastLED.h>
#include <Keypad.h>
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

#define BUZZER_PIN        8
#define BUTTON_PIN        2

// ─────────────────────────────────────────────
//  PAVÉ NUMÉRIQUE 4×4
// ─────────────────────────────────────────────
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {A0, A1, A2, A3};  // R1→R4
byte colPins[COLS] = {5,  7,  9,  3 };  // C1→C4

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ─────────────────────────────────────────────
//  CODE DE DÉSARMEMENT
// ─────────────────────────────────────────────
const String CODE_SECRET = "1234";  // ← change ton code ici
String saisie = "";

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
//  VARIABLES
// ─────────────────────────────────────────────
bool dernierEtatBouton = HIGH;
bool systemeActif      = false;

// ─────────────────────────────────────────────
//  FONCTION BOUTON
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

// ─────────────────────────────────────────────
//  FONCTION PAVÉ — appelée uniquement en alarme
// ─────────────────────────────────────────────
// Retourne true si le bon code est entré
bool lireCodeClavier(bool& alarme, int& compteur, int& incrementMvt,
                     int& echanCalme, bool& etaitEnMouvement, float& prevMagnitude) {
  char key = keypad.getKey();
  if (!key) return false;

  if (key == '*') {
    // * = effacer la saisie
    saisie = "";
    Serial.println("Saisie effacee");

  } else if (key == '#') {
    // # = valider
    Serial.print("Code saisi : ");
    Serial.println(saisie);

    if (saisie == CODE_SECRET) {
      Serial.println("Code correct ! Alarme desactivee.");
      // Reset alarme
      alarme           = false;
      compteur         = 0;
      incrementMvt     = INCREMENT_BASE;
      echanCalme       = 0;
      etaitEnMouvement = false;
      prevMagnitude    = 1.0;
      digitalWrite(BUZZER_PIN, LOW);
      saisie = "";
      return true;
    } else {
      Serial.println("Code incorrect !");
      saisie = "";
    }

  } else {
    // Chiffre ou lettre → ajoute à la saisie
    saisie += key;
    Serial.print("Saisie : ");
    Serial.println(saisie);
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);

  if (myIMU.begin(sampleRate, accelRange) != 0)
    Serial.println("Failed to initialize IMU.");
  else
    Serial.println("IMU initialized.");

  myIMU.intConf(123, 1, 10, HIGH);

  uint8_t readData = 0;
  myIMU.readRegister(&readData, KXTJ3_WHO_AM_I);
  Serial.print("Who am I? 0x");
  Serial.println(readData, HEX);

  Serial.println("Systeme ETEINT. BT0 pour armer.");
  Serial.print("Code : ");
  Serial.println(CODE_SECRET);
}

void loop() {

  static float prevMagnitude    = 1.0;
  static int   compteur         = 0;
  static bool  alarme           = false;
  static int   incrementMvt     = INCREMENT_BASE;
  static int   echanCalme       = 0;
  static bool  etaitEnMouvement = false;

  // ─────────────────────────────────────────
  //  GESTION BOUTON
  // ─────────────────────────────────────────
  if (boutonVientDEtreAppuye()) {
    if (alarme) {
      // En alarme : le bouton ne fait rien → utiliser le pavé
      Serial.println("Entre le code sur le pave pour desactiver.");
    } else if (systemeActif) {
      systemeActif     = false;
      compteur         = 0;
      incrementMvt     = INCREMENT_BASE;
      echanCalme       = 0;
      etaitEnMouvement = false;
      prevMagnitude    = 1.0;
      digitalWrite(BUZZER_PIN, LOW);
      saisie           = "";
      Serial.println("Systeme ETEINT.");
    } else {
      systemeActif = true;
      Serial.println("Systeme ARME.");
    }
  }

  // ─────────────────────────────────────────
  //  PAVÉ NUMÉRIQUE — uniquement en alarme
  // ─────────────────────────────────────────
  if (alarme) {
    lireCodeClavier(alarme, compteur, incrementMvt,
                    echanCalme, etaitEnMouvement, prevMagnitude);
  }

  // ─────────────────────────────────────────
  //  DÉTECTION
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
    if (echanCalme >= CALME_RESET && incrementMvt != INCREMENT_BASE) {
      incrementMvt = INCREMENT_BASE;
      #ifdef DEBUG
      Serial.println("  Calme -> INCREMENT reset");
      #endif
    }
    compteur -= DECREMENT_CALME;
    if (compteur < 0) compteur = 0;
    etaitEnMouvement = false;
  }

  if (!alarme && compteur >= SEUIL_ALARME) {
    alarme = true;
    saisie = "";
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("VOL DETECTE ! Entre le code pour desactiver.");
  }

  if (alarme && compteur == 0) {
    // Calme prolongé mais alarme toujours active
    // (ne s'éteint que via le bon code)
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