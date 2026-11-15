// CanSat board (ESP32-S3 DevKitC-1): pin map, sensors, SD card helpers.
#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_BMP280.h>

// HGLRC M100 PRO GPS (UART)
#define GPS_RX_PIN    1     // ESP RX <- GPS TX
#define GPS_TX_PIN    2     // ESP TX -> GPS RX
#define GPS_BAUD      9600
// HW-290 MPU6050 (I2C)
#define MPU_SDA_PIN   5
#define MPU_SCL_PIN   6
// SD card (hardware SPI)
#define SD_MISO_PIN   7
#define SD_SCK_PIN    8
#define SD_SS_PIN     9
#define SD_MOSI_PIN   10
// BMP280 (software SPI)
#define BMP_CS_PIN    11
#define BMP_MOSI_PIN  12
#define BMP_MISO_PIN  13
#define BMP_SCK_PIN   14

extern HardwareSerial   gpsSerial;
extern TinyGPSPlus      gpsDev;
extern Adafruit_MPU6050 mpuDev;
extern Adafruit_BMP280  bmpDev;
extern bool mpuOk, bmpOk, sdOk;

void cansatInit();                                    // USB serial, GPS UART, MPU6050, BMP280, SD card
void sdHeader(const char *path, const char *header);  // create the file with a header row if it is missing
bool sdAppend(const char *path, const char *line);    // add one line; false if the write failed
void sdList();                                        // USB serial: FILES, FILE,<name>,<bytes> per file, END_FILES
void sdDump(const String &name);                      // USB serial: BEGIN,<name>, the file, END,<name>
