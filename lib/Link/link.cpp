#include "link.h"
#include "pins.h"

#define TX_TIMEOUT_MS 3000   // never stay deaf if TX-done IRQ is missed

static SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN,
                                 RADIO_RST_PIN, RADIO_BUSY_PIN);

static volatile bool irq = false;
static void IRAM_ATTR onDio1() { irq = true; }

static bool     txBusy   = false;
static uint32_t txAt     = 0;
static float    freqNow  = LORA_FREQ_MAIN;
static float    freqNext = 0;
static int8_t   pwrNext  = 0;
static bool     pwrDirty = false;
static float    rssi = 0, snr = 0;

// Radio must be in standby when this runs.
static void applyPending() {
  if (freqNext) { radio.setFrequency(freqNext); freqNow = freqNext; freqNext = 0; }
  if (pwrDirty) { radio.setOutputPower(pwrNext); pwrDirty = false; }
}

bool linkBegin(float freqMHz, int8_t powerDbm) {
  freqNow = freqMHz;
  int st = radio.begin(freqMHz, LORA_BW, LORA_SF, LORA_CR,
                       LORA_SYNC, powerDbm, LORA_PREAMBLE);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[link] begin failed %d\n", st);
    return false;
  }
  radio.setDio2AsRfSwitch(true);
  radio.setCurrentLimit(140);
  radio.setCRC(true);
  radio.setDio1Action(onDio1);    // one ISR for TX-done and RX-done
  radio.startReceive();
  Serial.printf("[link] OK %.2f MHz SF%d sync 0x%02X %d dBm\n", freqMHz, LORA_SF, LORA_SYNC, powerDbm);
  return true;
}

bool linkBusy() { return txBusy; }

bool linkSend(const String &msg) {
  if (txBusy) return false;
  radio.standby();
  irq = false;                    // drop any stale RX interrupt
  applyPending();
  if (radio.startTransmit(msg.c_str()) != RADIOLIB_ERR_NONE) {
    radio.startReceive();
    return false;
  }
  txBusy = true;
  txAt   = millis();
  return true;
}

bool linkPoll(String &in) {
  if (txBusy) {
    bool timedOut = millis() - txAt > TX_TIMEOUT_MS;
    if (!irq && !timedOut) return false;
    if (timedOut && !irq) Serial.println("[link] tx timeout, recovering");
    irq = false;
    radio.finishTransmit();       // leaves radio in standby
    txBusy = false;
    applyPending();               // freq/power changes wait for TX to end
    radio.startReceive();
    return false;
  }

  if (!irq) return false;
  irq = false;
  int st = radio.readData(in);
  if (st == RADIOLIB_ERR_NONE) {
    rssi = radio.getRSSI();
    snr  = radio.getSNR();
  }
  radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    if (st == RADIOLIB_ERR_CRC_MISMATCH) Serial.println("[link] crc error");
    return false;
  }
  in.trim();
  return true;
}

void linkSetFreq(float f) {
  freqNext = f;
  if (!txBusy) { radio.standby(); applyPending(); radio.startReceive(); }
}

void linkSetPower(int8_t dbm) {
  pwrNext = dbm; pwrDirty = true;
  if (!txBusy) { radio.standby(); applyPending(); radio.startReceive(); }
}

float linkFreq() { return freqNext ? freqNext : freqNow; }
float linkRssi() { return rssi; }
float linkSnr()  { return snr; }
