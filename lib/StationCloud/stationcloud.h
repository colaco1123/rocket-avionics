// Web ground station upload: sends each packet to the online service from a background task.
#pragma once
#include <Arduino.h>

void cloudStart(const char *host, int port, const char *user, const char *pass, const char *topic);   // start the upload task
void cloudSend(const String &line);                         // queue a line, never blocks the radio
