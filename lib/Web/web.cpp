#include "web.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include "page.h"

static WebServer   server(80);
static const char *hdr     = "";
static const char *line    = "";
static bool        fireReq = false;
static bool        running = false;

static String path() {
  String n = server.arg("name");
  return n.startsWith("/") ? n : "/" + n;
}

static void handleData() {
  server.send(200, "text/plain", String(hdr) + "\n" + line);
}

static void handleFiles() {
  String out;
  File root = SD.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile())
    if (!f.isDirectory()) out += String(f.name()) + "," + f.size() + "\n";
  server.send(200, "text/plain", out);
}

static void handleGet() {
  File f = SD.open(path());
  if (!f || f.isDirectory()) { server.send(404, "text/plain", "not found"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + server.arg("name") + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void handleDelete() {
  server.send(SD.remove(path()) ? 200 : 404, "text/plain", "");
}

static void handleFire() {
  fireReq = true;
  server.send(200, "text/plain", "ok");
}

void webStart(const char *ssid, const char *pass) {
  static bool routed = false;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, pass);
  if (!routed) {
    server.on("/",       HTTP_GET,  [] { server.send_P(200, "text/html", PAGE); });
    server.on("/data",   HTTP_GET,  handleData);
    server.on("/files",  HTTP_GET,  handleFiles);
    server.on("/get",    HTTP_GET,  handleGet);
    server.on("/delete", HTTP_POST, handleDelete);
    server.on("/fire",   HTTP_POST, handleFire);
    routed = true;
  }
  server.begin();
  running = true;
}

void webStop() {
  if (!running) return;
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running = false;
}

bool webLoop(const char *header, const char *data) {
  if (!running) return false;
  hdr = header; line = data;
  server.handleClient();
  bool f = fireReq;
  fireReq = false;
  return f;
}
