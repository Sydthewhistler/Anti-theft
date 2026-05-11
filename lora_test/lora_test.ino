/******************************************************************************
 * Test envoi LoRa — UCA21
 * Envoie "HELLO" toutes les 60 secondes via TTN (ABP)
 ******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

// ─────────────────────────────────────────────
//  CLÉS ABP
// ─────────────────────────────────────────────
static const u4_t DEVADDR = 0x260B8018;

static const PROGMEM u1_t NWKSKEY[16] = {
  0x12, 0xE3, 0xBA, 0xE2, 0x9B, 0x40, 0x58, 0x7F,
  0x7B, 0xB7, 0xD7, 0x06, 0xBA, 0xB8, 0x11, 0x45
};

static const u1_t PROGMEM APPSKEY[16] = {
  0x41, 0xD6, 0x9D, 0x10, 0x71, 0xB4, 0x4A, 0x16,
  0x8A, 0xD9, 0xB2, 0xCC, 0xBA, 0x0A, 0x4A, 0x49
};

void os_getArtEui(u1_t* buf) {}
void os_getDevEui(u1_t* buf) {}
void os_getDevKey(u1_t* buf) {}

// ── Pinout RFM95W sur UCA21 ──
const lmic_pinmap lmic_pins = {
  .nss  = 10,
  .rxtx = LMIC_UNUSED_PIN,
  .rst  = 8,
  .dio  = {6, 6, 6},
};

static osjob_t sendjob;
const unsigned TX_INTERVAL = 20;

void do_send(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("TX en cours, on attend..."));
  } else {
    static uint8_t mydata[] = { 0x48, 0x45, 0x4C, 0x4C, 0x4F }; // HELLO
    LMIC_setTxData2(1, mydata, sizeof(mydata), 0);
    Serial.println(F("Envoi en cours..."));
  }
}

void onEvent(ev_t ev) {
  switch (ev) {
    case EV_TXCOMPLETE:
      Serial.println(F("TX complete !"));
      if (LMIC.txrxFlags & TXRX_ACK)
        Serial.println(F("ACK recu"));
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      break;
    case EV_JOIN_FAILED:
      Serial.println(F("Join failed"));
      break;
    default:
      Serial.print(F("Event: "));
      Serial.println(ev);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("Test LoRa UCA21"));

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

  do_send(&sendjob);
  Serial.println(F("Setup OK, premier envoi..."));
}

void loop() {
  os_runloop_once();
}