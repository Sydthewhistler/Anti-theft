#define CFG_EU 1

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

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

static uint8_t mydata[] = "Hello, world!";
static osjob_t sendjob;

const unsigned TX_INTERVAL = 30;

const lmic_pinmap lmic_pins = {
  .nss  = 10,
  .rxtx = LMIC_UNUSED_PIN,
  .rst  = 8,
  .dio  = {6, 6, 6},
};

void onEvent(ev_t ev) {
  switch (ev) {
    case EV_SCAN_TIMEOUT:    Serial.println(F("EV_SCAN_TIMEOUT"));    break;
    case EV_BEACON_FOUND:    Serial.println(F("EV_BEACON_FOUND"));    break;
    case EV_BEACON_MISSED:   Serial.println(F("EV_BEACON_MISSED"));   break;
    case EV_BEACON_TRACKED:  Serial.println(F("EV_BEACON_TRACKED"));  break;
    case EV_JOINING:         Serial.println(F("EV_JOINING"));         break;
    case EV_JOINED:          Serial.println(F("EV_JOINED"));          break;
    case EV_RFU1:            Serial.println(F("EV_RFU1"));            break;
    case EV_JOIN_FAILED:     Serial.println(F("EV_JOIN_FAILED"));     break;
    case EV_REJOIN_FAILED:   Serial.println(F("EV_REJOIN_FAILED"));   break;
    case EV_LOST_TSYNC:      Serial.println(F("EV_LOST_TSYNC"));      break;
    case EV_RESET:           Serial.println(F("EV_RESET"));           break;
    case EV_RXCOMPLETE:      Serial.println(F("EV_RXCOMPLETE"));      break;
    case EV_LINK_DEAD:       Serial.println(F("EV_LINK_DEAD"));       break;
    case EV_LINK_ALIVE:      Serial.println(F("EV_LINK_ALIVE"));      break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE"));
      if (LMIC.txrxFlags & TXRX_ACK)
        Serial.println(F("Received ack"));
      if (LMIC.dataLen) {
        Serial.print(F("Received "));
        Serial.print(LMIC.dataLen);
        Serial.println(F(" bytes of payload"));
      }
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      break;
    default:
      Serial.println(F("Unknown event"));
      break;
  }
}

void do_send(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {
    LMIC_setTxData2(1, mydata, sizeof(mydata) - 1, 0);
    Serial.println(F("Packet queued"));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("Starting"));

  os_init();
  LMIC_reset();
  LMIC_setClockError(MAX_CLOCK_ERROR * 2 / 100);

  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];
  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  LMIC_setSession(0x1, DEVADDR, nwkskey, appskey);

  #if defined(CFG_EU)
  LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);
  LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);
  LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);
  #endif

  LMIC_setLinkCheckMode(0);
  LMIC.dn2Dr = DR_SF9;
  LMIC_setDrTxpow(DR_SF9, 20);

  do_send(&sendjob);
}

void loop() {
  os_runloop_once();
}