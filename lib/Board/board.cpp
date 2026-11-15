#include "board.h"
#include <Wire.h>
#include <SD.h>
#include <esp_sleep.h>

XPowersAXP2101  pmu;
SPIClass        sensorSpi(HSPI);
Adafruit_BME280 bmeDev;
SensorQMI8658   imuDev;
SensorQMC6310   magDev;
TinyGPSPlus     gpsDev;
TinyGPSPlus     gps2Dev;
HardwareSerial  gps2Serial(2);
MagnetometerBase *mag2Dev = nullptr;
bool pmuOk = false, sdOk = false, bmeOk = false, imuOk = false, magOk = false, mag2Ok = false;

bool boardInit() {
  Serial.begin(115200);

  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW);                  // never fire at boot

  pmuOk = pmu.begin(Wire1, AXP2101_SLAVE_ADDRESS, I2C1_SDA, I2C1_SCL);
  if (pmuOk) {
    // Cold boot: power-cycle sensor and SD rails so nothing holds a bus.
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
      pmu.disableALDO1();
      pmu.disableALDO2();
      pmu.disableBLDO1();
      delay(250);
    }
    pmu.setALDO1Voltage(3300); pmu.enableALDO1();   // sensors
    pmu.setALDO2Voltage(3300); pmu.enableALDO2();   // sensors
    pmu.setALDO3Voltage(3300); pmu.enableALDO3();   // SX1262
    pmu.setALDO4Voltage(3300); pmu.enableALDO4();   // GNSS
    pmu.setBLDO1Voltage(3300); pmu.enableBLDO1();   // SD card
    pmu.setBLDO2Voltage(3300); pmu.enableBLDO2();
    // DCDC3/4/5 feed the M.2 face board (OLED); displayBegin() turns them on.
    pmu.setChargingLedMode(XPOWERS_CHG_LED_BLINK_1HZ);   // onboard LED blinks while powered
  } else {
    Serial.println("[board] AXP2101 not found");
  }

  Wire.begin(I2C_SDA, I2C_SCL);
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);

  pinMode(GPS_EN_PIN, OUTPUT);
  digitalWrite(GPS_EN_PIN, HIGH);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  gps2Serial.begin(GPS2_BAUD, SERIAL_8N1, GPS2_RX_PIN, GPS2_TX_PIN);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  analogSetPinAttenuation(PACK_ADC_PIN, ADC_11db);
  return pmuOk;
}

bool sdInit() {
  // Both chip selects high before anything talks on the shared bus.
  pinMode(IMU_CS, OUTPUT); digitalWrite(IMU_CS, HIGH);
  pinMode(SD_CS,  OUTPUT); digitalWrite(SD_CS,  HIGH);
  sensorSpi.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  sdOk = SD.begin(SD_CS, sensorSpi);
  Serial.printf("[board] SD %s\n", sdOk ? "OK" : "--");
  return sdOk;
}

void sensorsInit() {
  bmeOk = bmeDev.begin(0x77, &Wire);
  if (bmeOk) {
    // Same as the SERIOUS ROCKETRY solenoid test: x16 pressure, IIR x16.
    bmeDev.setSampling(Adafruit_BME280::MODE_NORMAL,
                       Adafruit_BME280::SAMPLING_X1,     // temp
                       Adafruit_BME280::SAMPLING_X16,    // pressure
                       Adafruit_BME280::SAMPLING_NONE,   // humidity
                       Adafruit_BME280::FILTER_X16,
                       Adafruit_BME280::STANDBY_MS_0_5);
  }
  Serial.printf("[board] BME280 %s\n", bmeOk ? "OK" : "--");

  imuOk = imuDev.begin(sensorSpi, IMU_CS);
  if (imuOk) {
    imuDev.configAccelerometer(SensorQMI8658::ACC_RANGE_16G,
                               SensorQMI8658::ACC_ODR_1000Hz,
                               SensorQMI8658::LPF_MODE_0);
    imuDev.configGyroscope(SensorQMI8658::GYR_RANGE_1024DPS,
                           SensorQMI8658::GYR_ODR_896_8Hz,
                           SensorQMI8658::LPF_MODE_3);
    imuDev.enableAccelerometer();
    imuDev.enableGyroscope();
  }
  Serial.printf("[board] QMI8658 %s\n", imuOk ? "OK" : "--");

  // Magnetometer: QMC6310U at 0x1C, or QMC6310N at 0x3C/0x3D. Its chip ID
  // register reads 0x80, which is how it is told apart from the OLED.
  const uint8_t addrs[] = {0x1C, 0x3C, 0x3D};
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    Wire.write((uint8_t)0x00);
    if (Wire.endTransmission() != 0) continue;
    if (Wire.requestFrom((int)a, 1) != 1 || Wire.read() != 0x80) continue;
    if (magDev.begin(Wire, a, I2C_SDA, I2C_SCL)) {
      magDev.configMagnetometer(OperationMode::CONTINUOUS_MEASUREMENT,
                                MagFullScaleRange::FS_8G, 50.0f,
                                MagOverSampleRatio::OSR_8, MagDownSampleRatio::DSR_1);
      magOk = true;
      break;
    }
  }
  Serial.printf("[board] MAG (QMC6310) %s\n", magOk ? "OK" : "--");

  // Second compass on the M100 PRO GPS, same I2C0 bus: QMC5883L at 0x0D or QMC5883P at 0x2C.
  static SensorQMC5883L qmc5883l;
  static SensorQMC5883P qmc5883p;
  if (i2cFound(QMC5883L_SLAVE_ADDRESS) && qmc5883l.begin(Wire, QMC5883L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) mag2Dev = &qmc5883l;
  if (!mag2Dev && i2cFound(0x2C) && qmc5883p.begin(Wire, 0x2C, I2C_SDA, I2C_SCL)) mag2Dev = &qmc5883p;
  if (mag2Dev)
    mag2Ok = mag2Dev->configMagnetometer(OperationMode::CONTINUOUS_MEASUREMENT,
                                         MagFullScaleRange::FS_8G, 50.0f,
                                         MagOverSampleRatio::OSR_8, MagDownSampleRatio::DSR_1);
  Serial.printf("[board] MAG2 (M100 PRO) %s\n", mag2Ok ? "OK" : "--");
}

bool i2cFound(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

float batteryVolts() {
  return analogReadMilliVolts(PACK_ADC_PIN) * 147.0f / 47.0f / 1000.0f;
}
