// Ground station: LoRa <-> USB serial bridge for dashboard/index.html.
#include <Arduino.h>
#include <board.h>
#include <link.h>
#include <display.h>

#define SLOT_MS      100    // send within this long after a rocket packet (rocket is listening)
#define QUIET_MS     1500   // no packet for this long: rocket is not transmitting, send at once
#define NO_DATA_MS   3000   // show NO DATA after this long without a packet
#define STATE_FIELD  18     // position of STATE in the telemetry line (sent as a number)
#define GS_POWER     22     // dBm, ground station power; check the limit for your band and region

const char *STATE_NAME[] = {"BENCH", "TEST", "ASCEND", "LAUNCH", "APOGEE", "DESCEND", "IMPACT"};
String   usbLine  = "";     // command being received from the dashboard
String   pending  = "";     // command waiting for the rocket's listen gap
String   subState = "";     // last STATE from the rocket (BENCH, TEST, ASCEND, ...)
uint32_t lastRx   = 0;      // millis() of the last packet from the rocket

String field(const String &line, int n) {  // n-th comma-separated field of a line
  int a = 0;
  for (int i = 0; i < n; i++) { a = line.indexOf(',', a) + 1; if (!a) return ""; }
  int b = line.indexOf(',', a);
  return b < 0 ? line.substring(a) : line.substring(a, b);
}

void followRocket(const String &cmd) {   // switch the ground radio's channel along with the rocket
  if (cmd == "FREQ_MAIN")   linkSetFreq(LORA_FREQ_MAIN);
  if (cmd == "FREQ_BACKUP") linkSetFreq(LORA_FREQ_BACKUP);
}

void usbToRadio() {     // take each line from the dashboard as the next command
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') { usbLine += c; continue; }
    if (usbLine.length()) pending = usbLine;
    usbLine = "";
  }
}

void sendCommand() {    // send right after a rocket packet, or at once if the rocket is quiet
  uint32_t since = millis() - lastRx;
  if (!pending.length() || linkBusy() || (since > SLOT_MS && since < QUIET_MS)) return;
  if (!linkSend(pending)) return;
  followRocket(pending);
  pending = "";
}

void radioToUsb() {     // pass each packet to the dashboard with its signal strength, keep its STATE
  String in;
  if (!linkPoll(in)) return;
  lastRx = millis();
  Serial.printf("%s,%.0f,%.1f\n", in.c_str(), linkRssi(), linkSnr());
  String st = field(in, STATE_FIELD);
  int n = st.toInt();
  if (st.length() && n >= 0 && n <= 6) subState = STATE_NAME[n];
}

void screen() {         // OLED: ARMED / DISARMED large, sub-state and signal small; redrawn only on change
  static String shown = "-";
  bool stale = !subState.length() || millis() - lastRx > NO_DATA_MS;
  bool disarmed = subState == "BENCH" || subState == "TEST";
  const char *big = stale ? "NO DATA" : disarmed ? "DISARMED" : "ARMED";
  String small = subState + (stale ? "" : "  " + String(linkRssi(), 0) + " dBm");
  if (shown == String(big) + small) return;
  shown = String(big) + small;
  displayShow(big, small.c_str());
}

void setup() {
  boardInit();
  displayBegin();
  linkBegin(LORA_FREQ_MAIN, GS_POWER);
}

void loop() {
  radioToUsb();
  usbToRadio();
  sendCommand();
  screen();
}
