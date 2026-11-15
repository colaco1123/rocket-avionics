// LoRa link: parameters shared by both ends plus a small non-blocking
// SX1262 wrapper. Change radio parameters here only.
#pragma once
#include <Arduino.h>
#include <RadioLib.h>

// Radio settings: both ends must match
#define LORA_FREQ_MAIN    866.5
#define LORA_FREQ_BACKUP  865.25
#define LORA_BW           125.0
#define LORA_SF           9
#define LORA_CR           5
#define LORA_SYNC         0x44      // private sync word
#define LORA_PREAMBLE     8
#define LORA_PWR_LOW      10
#define LORA_PWR_HIGH     14

bool  linkBegin(float freqMHz, int8_t powerDbm);
bool  linkSend(const String &msg);        // false if busy or failed
bool  linkBusy();
bool  linkPoll(String &in);               // call every loop; true = packet in
void  linkSetFreq(float freqMHz);         // applied now, or after current TX
void  linkSetPower(int8_t dbm);           // same
float linkFreq();
float linkRssi();
float linkSnr();
