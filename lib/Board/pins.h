// LilyGo T-Beam Supreme (ESP32-S3 + SX1262 + L76K + AXP2101) pin map.
#pragma once

// I2C0: OLED, BME280
#define I2C_SDA         17
#define I2C_SCL         18
// I2C1: AXP2101 PMU
#define I2C1_SDA        42
#define I2C1_SCL        41

// SX1262 on its own SPI bus
#define RADIO_SCLK_PIN  12
#define RADIO_MISO_PIN  13
#define RADIO_MOSI_PIN  11
#define RADIO_CS_PIN    10
#define RADIO_RST_PIN    5
#define RADIO_DIO1_PIN   1
#define RADIO_BUSY_PIN   4

// Second SPI bus: SD card + QMI8658 IMU
#define SPI_MOSI        35
#define SPI_SCK         36
#define SPI_MISO        37
#define SD_CS           47
#define IMU_CS          34

// L76K GNSS
#define GPS_RX_PIN       9
#define GPS_TX_PIN       8
#define GPS_EN_PIN       7
#define GPS_BAUD      9600

// HGLRC M100 PRO GNSS (second GPS, above the bulkhead); its compass is on I2C0 (SDA 17 / SCL 18)
#define GPS2_RX_PIN     39      // M100 PRO TX -> IO39
#define GPS2_TX_PIN     38      // IO38 -> M100 PRO RX (never IO45: strapping pin)
#define GPS2_BAUD     9600

#define BUTTON_PIN       0

// Rocket wiring
#define SOLENOID_PIN    21      // main chute solenoid gate, HIGH = fire
#define PACK_ADC_PIN     2      // pack divider 100k / 47k
