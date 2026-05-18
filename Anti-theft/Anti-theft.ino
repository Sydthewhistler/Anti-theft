/******************************************************************************
 * Détection de vol — UCA21
 * Basé sur le code de Fabien Ferrero (Aug, 2021)
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

#define BUZZER_PIN        8   // buzzer externe sur D8

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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

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

  static float prevMagnitude    = 1.0;
  static int   compteur         = 0;
  static bool  alarme           = false;
  static int   incrementMvt     = INCREMENT_BASE;
  static int   echanCalme       = 0;
  static bool  etaitEnMouvement = false;

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
      Serial.print("  Reprise apres pause -> INCREMENT = ");
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
    digitalWrite(BUZZER_PIN, HIGH); // ← buzzer ON
    Serial.println("VOL DETECTE ! Buzzer ON");
  }

  // Retour au calme
  if (alarme && compteur == 0) {
    alarme = false;
    digitalWrite(BUZZER_PIN, LOW); // ← buzzer OFF
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