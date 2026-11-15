#include "cansat.h"
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

HardwareSerial   gpsSerial(1);
TinyGPSPlus      gpsDev;
TwoWire          mpuWire(1);
Adafruit_MPU6050 mpuDev;
Adafruit_BMP280  bmpDev(BMP_CS_PIN, BMP_MOSI_PIN, BMP_MISO_PIN, BMP_SCK_PIN);
SPIClass         sdSpi(FSPI);
bool mpuOk = false, bmpOk = false, sdOk = false;

void cansatInit() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);   // wait up to 3 s for USB CDC

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  mpuWire.begin(MPU_SDA_PIN, MPU_SCL_PIN, 400000);
  mpuOk = mpuDev.begin(0x68, &mpuWire);
  if (mpuOk) {
    mpuDev.setAccelerometerRange(MPU6050_RANGE_16_G);
    mpuDev.setGyroRange(MPU6050_RANGE_1000_DEG);
    mpuDev.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  bmpOk = bmpDev.begin();
  if (bmpOk) {
    bmpDev.setSampling(Adafruit_BMP280::MODE_NORMAL,
                       Adafruit_BMP280::SAMPLING_X2,
                       Adafruit_BMP280::SAMPLING_X16,
                       Adafruit_BMP280::FILTER_X16,
                       Adafruit_BMP280::STANDBY_MS_63);
  }

  sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_SS_PIN);
  sdOk = SD.begin(SD_SS_PIN, sdSpi);
}

void sdHeader(const char *path, const char *header) {
  if (!sdOk || SD.exists(path)) return;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  f.println(header);
  f.close();
}

bool sdAppend(const char *path, const char *line) {
  File f = SD.open(path, FILE_APPEND);
  if (!f) return false;
  f.println(line);
  f.close();
  return true;
}

void sdList() {
  File root = SD.open("/");
  if (!root) { Serial.println("MSG,SD: could not open root"); return; }
  Serial.println("FILES");
  for (File f = root.openNextFile(); f; f = root.openNextFile())
    if (!f.isDirectory()) Serial.printf("FILE,%s,%u\n", f.name(), (unsigned)f.size());
  Serial.println("END_FILES");
  root.close();
}

void sdDump(const String &name) {
  File f = SD.open(name.startsWith("/") ? name : "/" + name);
  if (!f) { Serial.println("MSG,Could not open " + name); return; }
  Serial.println("BEGIN," + name);
  while (f.available()) Serial.write(f.read());
  f.close();
  Serial.println();
  Serial.println("END," + name);
}
