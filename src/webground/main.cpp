// Web ground station: ESP32-S3 + SX1262 LoRa, dashboard over the phone hotspot.
#include <Arduino.h>
#include <LittleFS.h>
#include <stationlink.h>
#include <stationweb.h>
#include <stationcloud.h>
#include "secrets.h"         // WIFI_SSID, WIFI_PASS, CLOUD_HOST, CLOUD_USER, CLOUD_PASS (see include/secrets.example.h)

#define SLOT_MS      100    // send within this long after a rocket packet (rocket is listening)
#define QUIET_MS     1500   // no packet for this long: rocket is not transmitting, send at once
#define LED_MS       100    // LED flash per packet
#define GS_POWER     22     // dBm, ground station power; check the limit for your band and region
#define LOG_FILE     "/station.csv"
#define LOG_HEADER   "ID,TIME_S,PACKET,ALT_M,PRESSURE_PA,TEMP_C,VOLTAGE_V,GNSS_TIME_S,LAT,LON,GNSS_ALT_M,SATS,AX,AY,AZ,ROLL,PITCH,SPIN_DPS,STATE,VSPEED_MS,MAG_OK,ACK,RSSI_DBM,SNR_DB"
#define AP_SSID      "ROCKET-GS"  // own WiFi, only while the hotspot is missing
#define AP_PASS      "change-me-1234"
#define CLOUD_PORT   8883               // EMQX secure MQTT
#define CLOUD_TOPIC  "rocket/telemetry"

String   pending = "";      // command waiting for the rocket's listen gap
String   line    = "";      // newest packet with its signal strength, until logged
uint32_t lastRx  = 0;       // millis() of the last packet from the rocket
bool     logging = false;   // flash storage ready
bool     wifiOn  = false;   // WiFi on/off

void followRocket(const String &cmd) {   // switch this radio's channel along with the rocket
  if (cmd == "FREQ_MAIN")   linkSetFreq(LORA_FREQ_MAIN);
  if (cmd == "FREQ_BACKUP") linkSetFreq(LORA_FREQ_BACKUP);
}

void wifi() {           // serve the dashboard; a command from it waits for the listen gap, an Online test goes up
  String cmd, time;
  if (stationWebLoop(cmd)) pending = cmd;
  if (stationWebTest(time)) cloudSend("PING," + time);
}

void transmit() {       // send right after a rocket packet, or at once if the rocket is quiet
  uint32_t since = millis() - lastRx;
  if (!pending.length() || linkBusy() || (since > SLOT_MS && since < QUIET_MS)) return;
  if (!linkSend(pending)) return;
  followRocket(pending);
  pending = "";
}

void listen() {         // receive a packet, add its signal strength, give it to the page
  String in;
  if (!linkPoll(in)) return;
  lastRx = millis();
  line = in + "," + String(linkRssi(), 0) + "," + String(linkSnr(), 1);
  stationWebAdd(line);
  cloudSend(line);
}

void printData() {      // same line as received, to USB serial
  if (line.length()) Serial.println(line);
}

void logger() {         // append the newest packet to flash, after transmit() so the listen gap is not missed
  if (!line.length()) return;
  File f = logging ? LittleFS.open(LOG_FILE, FILE_APPEND) : File();
  if (f) {
    if (f.size() == 0) f.println(LOG_HEADER);
    f.println(line);
    f.close();
  }
  line = "";
}

void led() {            // flash on each packet
  digitalWrite(LED_PIN, lastRx && millis() - lastRx < LED_MS);
}

void storageBegin() {   // mount the flash file system, formatted on first use
  logging = LittleFS.begin(true);
}

void wifiBegin() {      // join the hotspot and start the page
  stationWebStart(WIFI_SSID, WIFI_PASS, AP_SSID, AP_PASS);
  wifiOn = true;
}

void cloudBegin() {     // start uploading to the online service
  cloudStart(CLOUD_HOST, CLOUD_PORT, CLOUD_USER, CLOUD_PASS, CLOUD_TOPIC);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  storageBegin();
  linkBegin(LORA_FREQ_MAIN, GS_POWER);
  wifiBegin();
  cloudBegin();
}

void loop() {
  listen();
  wifi();
  transmit();
  printData();
  logger();
  led();
}
