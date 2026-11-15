// WiFi access point + web page: live data, SD files, solenoid test.
#pragma once
#include <Arduino.h>

void webStart(const char *ssid, const char *pass);   // start the access point and page
void webStop();                                       // stop the page and turn WiFi off
bool webLoop(const char *header, const char *data);   // serve clients; true = solenoid test pressed
