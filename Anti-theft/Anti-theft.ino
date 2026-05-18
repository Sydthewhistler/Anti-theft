/******************************************************************************
 * Détection de vol — UCA21
 * Basé sur le code de Fabien Ferrero (Aug, 2021)
 * 
 * Détection intelligente :
 *   - Compteur monte sur mouvement, descend doucement au calme
 *   - INCREMENT_MVT augmente si mouvement par petites rafales
 *     (empêche de tromper le système par petits coups répétés)
 *   - Alarme déclenchée après ~8s de mouvement continu à 6.25 Hz
 ******************************************************************************/

#define DEBUG

#include <FastLED.h>
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

// ─────────────────────────────────────────────────────────────────
//  PARAMÈTRES DE DÉTECTION
// ─────────────────────────────────────────────────────────────────

#define SEUIL_DIFF      1.5   // variation de magnitude comptant comme mouvement
                              // augmenter → moins sensible

#define INCREMENT_BASE  3     // points ajoutés par échantillon au départ
#define INCREMENT_MAX   15    // plafond de l'incrément (évite l'emballement)
#define INCR_PENALITE   3     // bonus d'incrément à chaque reprise après pause

#define DECREMENT_CALME 1     // points retirés par échantillon au calme

#define SEUIL_ALARME    150   // score nécessaire pour déclencher l'alarme
                              // = 8s de mouvement continu à 6.25 Hz
#define COMPTEUR_MAX    150   // plafond du compteur (= SEUIL_ALARME)

// Nombre d'échantillons calmes consécutifs pour définir une "pause"
// 6 échantillons ≈ 1 seconde à 6.25 Hz
#define PAUSE_SEUIL     6

// Nombre d'échantillons calmes pour considérer un vrai calme prolongé
// et réinitialiser INCREMENT_MVT
// 18 échantillons ≈ 3 secondes à 6.25 Hz
#define CALME_RESET     18

// ─────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");

  // Init FastLED — nécessaire pour alimenter le bus I2C sur la UCA21
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
}

void loop() {
  myIMU.standby(false);

  int16_t dataHighresX = 0;
  int16_t dataHighresY = 0;
  int16_t dataHighresZ = 0;

  // Variables persistantes entre les appels
  static float prevMagnitude    = 1.0;
  static int   compteur         = 0;
  static bool  alarme           = false;
  static int   incrementMvt     = INCREMENT_BASE;
  static int   echanCalme       = 0;    // échantillons calmes consécutifs
  static bool  etaitEnMouvement = false;

  // ── Lecture accéléromètre ──
  myIMU.readRegisterInt16(&dataHighresX, KXTJ3_XOUT_L);
  myIMU.readRegisterInt16(&dataHighresY, KXTJ3_YOUT_L);
  myIMU.readRegisterInt16(&dataHighresZ, KXTJ3_ZOUT_L);

  // Conversion en g (mode LOW_POWER → diviseur 1024)
  float x = dataHighresX / 1024.0;
  float y = dataHighresY / 1024.0;
  float z = dataHighresZ / 1024.0;

  // Magnitude du vecteur accélération
  float magnitude = sqrt(x * x + y * y + z * z);

  // Variation par rapport à l'échantillon précédent
  float diff = abs(magnitude - prevMagnitude);
  prevMagnitude = magnitude;

  // ── Mise à jour du compteur ──
  if (diff > SEUIL_DIFF) {
    // ── Mouvement détecté ──

    // Reprise après une pause → INCREMENT augmente (pénalise les petits coups)
    if (!etaitEnMouvement && echanCalme >= PAUSE_SEUIL) {
      incrementMvt += INCR_PENALITE;
      if (incrementMvt > INCREMENT_MAX)
        incrementMvt = INCREMENT_MAX;

      #ifdef DEBUG
      Serial.print("  ⚡ Reprise après pause → INCREMENT = ");
      Serial.println(incrementMvt);
      #endif
    }

    compteur += incrementMvt;
    if (compteur > COMPTEUR_MAX)
      compteur = COMPTEUR_MAX;

    echanCalme       = 0;
    etaitEnMouvement = true;

  } else {
    // ── Calme ──
    echanCalme++;

    // Calme prolongé → on remet INCREMENT à sa valeur de base
    if (echanCalme >= CALME_RESET) {
      if (incrementMvt != INCREMENT_BASE) {
        incrementMvt = INCREMENT_BASE;
        #ifdef DEBUG
        Serial.println("  😴 Calme prolongé → INCREMENT reset à " + String(INCREMENT_BASE));
        #endif
      }
    }

    compteur -= DECREMENT_CALME;
    if (compteur < 0)
      compteur = 0;

    etaitEnMouvement = false;
  }

  // ── Décision alarme ──
  if (!alarme && compteur >= SEUIL_ALARME) {
    alarme = true;
    Serial.println("🚨 VOL DÉTECTÉ !");
    // → setState(ALARME) dans le projet final
  }

  // Retour au calme si compteur retombe à 0
  if (alarme && compteur == 0) {
    alarme = false;
    Serial.println("✅ Retour au calme.");
  }

  // ── Debug série ──
  #ifdef DEBUG
  Serial.print("diff=");        Serial.print(diff, 2);
  Serial.print("  cpt=");       Serial.print(compteur);
  Serial.print("/");            Serial.print(SEUIL_ALARME);
  Serial.print("  incr=");      Serial.print(incrementMvt);
  Serial.print("  calme=");     Serial.print(echanCalme);
  Serial.print("  → ");
  if      (alarme)          Serial.println("ALARME 🚨");
  else if (diff > SEUIL_DIFF) Serial.println("mouvement...");
  else                      Serial.println("calme");
  #endif

  myIMU.standby(true);
  delay(160); // période ≈ 6.25 Hz
}