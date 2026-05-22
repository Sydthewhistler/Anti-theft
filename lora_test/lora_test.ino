#include <RadioLib.h>

RFM95 radio = new Module(10, 6, 8, -1);

uint32_t devAddr = 0x260B9C2F;

uint8_t nwkSKey[] = {
  0x84, 0x86, 0xC7, 0x16, 0x7A, 0x66, 0x6D, 0x95,
  0x3B, 0x8A, 0xE5, 0x66, 0x16, 0x63, 0xC9, 0xF4
};

uint8_t appSKey[] = {
  0x01, 0xAB, 0xC5, 0x9D, 0x2E, 0xE5, 0x47, 0x2F,
  0xE6, 0x83, 0xC8, 0x0D, 0x54, 0x54, 0xA5, 0x39
};

LoRaWANNode node(&radio, &EU868);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");

  int state = radio.begin(868.1);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Radio init failed: ");
    Serial.println(state);
    while (1);
  }
  Serial.println("Radio OK");

  state = node.beginABP(devAddr, nwkSKey, nwkSKey, nwkSKey, appSKey);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("LoRaWAN init failed: ");
    Serial.println(state);
    while (1);
  }
  Serial.println("LoRaWAN OK");
}

void loop() {
  uint8_t payload[] = { 0x01, 0x00, 0x01 };

  Serial.println("Envoi...");
  int state = node.sendReceive(payload, sizeof(payload), 1);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("OK !");
  } else {
    Serial.print("Erreur: ");
    Serial.println(state);
  }

  Serial.println("Attente 30s...");
  delay(30000);
}