// Web ground station LoRa link: same radio settings as lib/Link, on the ESP32-S3 custom board.
#pragma once
#include <Arduino.h>
#include <RadioLib.h>

// ESP32-S3-WROOM-1U-N16R8 board pins, from the schematic
#define LORA_SCK_PIN    8
#define LORA_MISO_PIN   12
#define LORA_MOSI_PIN   13
#define LORA_NSS_PIN    18
#define LORA_DIO1_PIN   9
#define LORA_BUSY_PIN   10
#define LORA_NRST_PIN   11
#define LORA_TXEN_PIN   6
#define LORA_RXEN_PIN   7
#define LED_PIN         17      // D6

// Must match lib/Link/link.h
#define LORA_FREQ_MAIN    866.5
#define LORA_FREQ_BACKUP  865.25
#define LORA_BW           125.0
#define LORA_SF           9
#define LORA_CR           5
#define LORA_SYNC         0x44      // private sync word
#define LORA_PREAMBLE     8
#define LORA_TCXO_V       1.8       // 0 is tried next, for a module with a plain crystal

bool  linkBegin(float freqMHz, int8_t powerDbm);
bool  linkSend(const String &msg);        // false if busy or failed
bool  linkBusy();
bool  linkPoll(String &in);               // call every loop; true = packet in
void  linkSetFreq(float freqMHz);         // applied now, or after current TX
void  linkSetPower(int8_t dbm);           // same
float linkRssi();
float linkSnr();
