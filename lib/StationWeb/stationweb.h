// Web ground station: joins the phone hotspot, dashboard page, recent lines and stored files.
#pragma once
#include <Arduino.h>

void stationWebStart(const char *ssid, const char *pass, const char *apSsid, const char *apPass);   // join the hotspot, start the page
bool stationWebLoop(String &cmd);                           // serve the page; true = a command came in
bool stationWebTest(String &time);                          // true = the Online test button was pressed, with its time
void stationWebAdd(const String &line);                     // keep a line for the page
