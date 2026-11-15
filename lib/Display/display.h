// SH1106 OLED on the T-Beam Supreme (I2C0, 0x3C).
#pragma once

void displayBegin();                                   // start the OLED
void displayShow(const char *big, const char *small);  // one large line, one small line under it
