#include "display.h"
#include <SH1106Wire.h>
#include <board.h>

static SH1106Wire oled(0x3C, I2C_SDA, I2C_SCL);

void displayBegin() {
  // Face board (OLED) is on the M.2 rails, same as LilyGo's setup in ROCKETRY/LoRaBoards.cpp.
  if (pmuOk) {
    pmu.setDC3Voltage(3300); pmu.enableDC3();
    pmu.setDC4Voltage(XPOWERS_AXP2101_DCDC4_VOL2_MAX); pmu.enableDC4();
    pmu.setDC5Voltage(3300); pmu.enableDC5();
  }
  delay(500);                 // let the rails settle before talking to the OLED
  oled.init();
  oled.flipScreenVertically();
}

void displayShow(const char *big, const char *small) {
  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.setFont(ArialMT_Plain_24);
  oled.drawString(64, 6, big);
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(64, 40, small);
  oled.display();
}
