#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <XPowersLib.h>
#include <TinyGPS++.h>
#include <Adafruit_BME280.h>
#include <SensorQMI8658.hpp>
#include <SensorQMC6310.hpp>
#include <SensorQMC5883L.hpp>
#include <SensorQMC5883P.hpp>
#include "pins.h"

extern XPowersAXP2101  pmu;
extern SPIClass        sensorSpi;   // shared by SD card and IMU
extern Adafruit_BME280 bmeDev;
extern SensorQMI8658   imuDev;
extern SensorQMC6310   magDev;
extern TinyGPSPlus     gpsDev;      // onboard L76K on Serial1
extern TinyGPSPlus     gps2Dev;     // M100 PRO on gps2Serial
extern HardwareSerial  gps2Serial;
extern MagnetometerBase *mag2Dev;   // M100 PRO compass (QMC5883L or QMC5883P), nullptr if not found
extern bool pmuOk, sdOk, bmeOk, imuOk, magOk, mag2Ok;

bool  boardInit();      // PMU power rails, I2C, radio SPI, both GNSS UARTs
bool  sdInit();         // starts sensorSpi, mounts SD. Call before IMU init.
void  sensorsInit();    // BME280, QMI8658, QMC6310, M100 PRO compass
float batteryVolts();   // primary pack voltage from the IO2 divider
bool  i2cFound(uint8_t addr);   // true if a device answers at this address on I2C0
