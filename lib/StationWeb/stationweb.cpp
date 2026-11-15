#include "stationweb.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include "station_page.h"

#define WIFI_CHANNEL  11        // 2462 MHz, away from XBee channel 11 / 2405 MHz
#define HOTSPOT_MS    20000     // no hotspot for this long: host our own WiFi
#define HOST_NAME     "webground"
#define HISTORY       600       // lines kept for a page reload
#define LINES_PER_GET 100       // a page catches up in steps

static WebServer server(80);
static String    ring[HISTORY];
static uint32_t  total = 0;     // lines added since boot
static String    cmdIn = "";    // last command from the page
static String    testIn = "";   // time of the last Online test click
static const char *apName = "", *apPass = "";
static bool      joined = false;   // on the phone hotspot
static bool      apOn   = false;   // hosting our own WiFi
static uint32_t  lostAt = 0;       // millis() when the hotspot was last lost

static String path() {
  String n = server.arg("name");
  return n.startsWith("/") ? n : "/" + n;
}

static void handleLines() {     // first line = next index, then the lines from "from"
  uint32_t from = server.arg("from").toInt();
  if (from > total) from = 0;                       // station restarted
  if (total - from > HISTORY) from = total - HISTORY;
  uint32_t to = min(from + LINES_PER_GET, total);
  String out = String(to) + "\n";
  for (uint32_t i = from; i < to; i++) out += ring[i % HISTORY] + "\n";
  server.send(200, "text/plain", out);
}

static void handleCmd() {
  cmdIn = server.arg("c");
  server.send(200, "text/plain", "ok");
}

static void handleTest() {
  testIn = server.arg("t");
  server.send(200, "text/plain", "ok");
}

static void handleFiles() {
  String out;
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile())
    if (!f.isDirectory()) out += String(f.name()) + "," + f.size() + "\n";
  server.send(200, "text/plain", out);
}

static void handleGet() {
  File f = LittleFS.open(path());
  if (!f || f.isDirectory()) { server.send(404, "text/plain", "not found"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + String(f.name()) + "\"");
  server.streamFile(f, "text/csv");
  f.close();
}

static void handleDelete() {
  server.send(LittleFS.remove(path()) ? 200 : 404, "text/plain", "");
}

static void wifiKeep() {        // own WiFi only while the hotspot is missing
  bool on = WiFi.status() == WL_CONNECTED;
  if (on != joined) {
    joined = on;
    lostAt = millis();
    if (on) Serial.printf("[web] hotspot joined, open http://%s or http://%s.local\n", WiFi.localIP().toString().c_str(), HOST_NAME);
  }
  if (on && apOn) { WiFi.softAPdisconnect(true); apOn = false; }
  if (!on && !apOn && millis() - lostAt >= HOTSPOT_MS) {
    WiFi.softAP(apName, apPass, WIFI_CHANNEL);
    apOn = true;
    Serial.printf("[web] no hotspot, hosting %s at http://%s\n", apName, WiFi.softAPIP().toString().c_str());
  }
}

void stationWebStart(const char *ssid, const char *pass, const char *apSsid, const char *apPassword) {
  apName = apSsid; apPass = apPassword;
  WiFi.setHostname(HOST_NAME);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, pass);
  MDNS.begin(HOST_NAME);
  server.on("/",       HTTP_GET,  [] { server.send_P(200, "text/html", PAGE); });
  server.on("/lines",  HTTP_GET,  handleLines);
  server.on("/cmd",    HTTP_POST, handleCmd);
  server.on("/test",   HTTP_POST, handleTest);
  server.on("/files",  HTTP_GET,  handleFiles);
  server.on("/get",    HTTP_GET,  handleGet);
  server.on("/delete", HTTP_POST, handleDelete);
  server.begin();
}

bool stationWebLoop(String &cmd) {
  wifiKeep();
  server.handleClient();
  if (!cmdIn.length()) return false;
  cmd = cmdIn;
  cmdIn = "";
  return true;
}

bool stationWebTest(String &time) {
  if (!testIn.length()) return false;
  time = testIn;
  testIn = "";
  return true;
}

void stationWebAdd(const String &line) {
  ring[total % HISTORY] = line;
  total++;
}
