// CanSat bench test firmware: sensors, 5 Hz SD log, USB serial commands. No WiFi/BLE/radio.
#include <Arduino.h>
#include <cansat.h>

#define LOG_MS        200       // 5 Hz log
#define LOG_FILE      "/cansat_log.csv"
#define SEA_LEVEL_HPA 1013.25f  // placeholder, calibrate at the pad
#define LOG_HEADER    "millis,packet,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_C,pressure_Pa,alt_m,gps_fix,gps_lat,gps_lon,gps_alt_m,gps_sats,gps_time"

// IMU (g, deg/s)
float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
// Barometer
float temperature = 0, pressure = 0, altitude = 0;
// GNSS
bool   gpsFix = false;
double lat = 0, lon = 0;
float  gpsAltitude = 0;
int    sats = 0;
char   gpsTime[16] = "no-fix";
// Mission
uint32_t packetCount = 0;
char     line[256];

void imu() {            // acceleration in g, gyro rates in deg/s
  if (!mpuOk) return;
  sensors_event_t a, g, t;
  mpuDev.getEvent(&a, &g, &t);
  ax = a.acceleration.x / 9.80665f; ay = a.acceleration.y / 9.80665f; az = a.acceleration.z / 9.80665f;
  gx = g.gyro.x * RAD_TO_DEG; gy = g.gyro.y * RAD_TO_DEG; gz = g.gyro.z * RAD_TO_DEG;
}

void barometer() {      // temperature, pressure, altitude from sea-level pressure
  if (!bmpOk) return;
  temperature = bmpDev.readTemperature();
  pressure    = bmpDev.readPressure();
  altitude    = bmpDev.readAltitude(SEA_LEVEL_HPA);
}

void gps() {            // feed the GNSS parser and copy the fix
  while (gpsSerial.available()) gpsDev.encode(gpsSerial.read());
  gpsFix = gpsDev.location.isValid();
  lat = gpsFix ? gpsDev.location.lat() : 0.0;
  lon = gpsFix ? gpsDev.location.lng() : 0.0;
  gpsAltitude = gpsDev.altitude.isValid() ? gpsDev.altitude.meters() : 0.0f;
  sats = gpsDev.satellites.isValid() ? gpsDev.satellites.value() : 0;
  if (gpsDev.time.isValid())
    snprintf(gpsTime, sizeof(gpsTime), "%02d:%02d:%02d", gpsDev.time.hour(), gpsDev.time.minute(), gpsDev.time.second());
}

void buildLine() {      // one CSV line, same columns as LOG_HEADER
  snprintf(line, sizeof(line), "%lu,%lu,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%.1f,%.2f,%d,%.6f,%.6f,%.1f,%d,%s",
           millis(), (unsigned long)packetCount, ax, ay, az, gx, gy, gz,
           temperature, pressure, altitude, gpsFix ? 1 : 0, lat, lon, gpsAltitude, sats, gpsTime);
}

void printData() {      // same line as logged, to USB serial
  Serial.println(line);
}

void logger() {         // append the line to the SD card
  if (sdOk && !sdAppend(LOG_FILE, line)) Serial.println("MSG,WARNING: SD write failed this cycle");
}

void status() {         // which sensors started
  Serial.printf("MSG,MPU6050 %s  BMP280 %s  SD %s  GPS UART started\n",
                mpuOk ? "OK" : "FAIL", bmpOk ? "OK" : "FAIL", sdOk ? "OK" : "FAIL");
}

void commands() {       // USB serial: LIST, DUMP <file>, STATUS, HELP
  if (!Serial.available()) return;
  String in = Serial.readStringUntil('\n');
  in.trim();
  if      (in.equalsIgnoreCase("LIST"))            sdList();
  else if (in.substring(0, 5).equalsIgnoreCase("DUMP ")) sdDump(in.substring(5));
  else if (in.equalsIgnoreCase("STATUS"))          status();
  else if (in.length())                            Serial.println("MSG,Commands: LIST | DUMP <file> | STATUS | HELP");
}

void setup() {
  cansatInit();
  sdHeader(LOG_FILE, LOG_HEADER);
  status();
}

void loop() {
  static uint32_t logAt = 0;

  gps();
  commands();

  if (millis() - logAt >= LOG_MS) {
    logAt = millis();
    packetCount++;
    imu();
    barometer();
    buildLine();
    printData();
    logger();
  }
}
