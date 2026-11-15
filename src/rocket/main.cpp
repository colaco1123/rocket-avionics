// Rocket flight computer, LilyGo T-Beam Supreme.
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include <board.h>
#include <link.h>
#include <web.h>

#define ROCKET_ID          "ROCKET-01"
#define SAMPLE_MS          20        // 50 Hz sensors + SD log
#define TELEM_MS           1000      // 1 Hz telemetry
#define LOG_FILE           "/flight.csv"
#define RESET_FILE         "/resets.csv"
#define LOG_LIMIT_MS       600000UL  // stop logging 10 min after ARM
#define TX_LIMIT_MS        5400000UL // all transmission off 90 min after launch
#define DEPLOY_DEFAULT_M   1000.0f   // start pulsing the solenoid here, or at apogee if lower; APOGEE=<m>
#define SOLENOID_PULSE_MS  1000      // solenoid open time per pulse
#define SOLENOID_PERIOD_MS 2000      // one open/close every 2 s until IMPACT
#define ORIENT_SAMPLES     50        // 1 s of accelerometer after ARM gives which way is up
#define LAUNCH_ACCEL       24.5f     // m/s2 along the up axis (2.5 g) = motor burning
#define LAUNCH_ALT_M       30.0f     // or this much altitude gain = launched
#define LAUNCH_CONFIRM     5         // samples in a row
#define APOGEE_DROP_M      3.0f      // this far under the peak = descending
#define APOGEE_CONFIRM     5         // samples in a row
#define APOGEE_FAILSAFE_MS 20000     // force APOGEE this long after launch: theoretical apogee 16 s + 4 s (VEGA failsafe 24 s)
#define APOGEE_HOLD_MS     2000      // stay in APOGEE this long, then DESCEND
#define IMPACT_STILL_M     2.0f      // altitude moving less than this ...
#define IMPACT_STILL_MS    5000      // ... for this long = landed
#define SPEED_MS           500       // vertical speed measured over this window
#define G                  9.80665f
#define WIFI_SSID          "ROCKET-FC"
#define WIFI_PASS          "change-me-1234"
#define RTC_MAGIC          0xF11E    // marks RTC memory as valid
#define MAG_CAL_MS         20000     // MAG_CAL: turn the rocket through every orientation for this long
#define TELEM_HEADER       "ID,TIME_S,PACKET,ALT_M,PRESSURE_PA,TEMP_C,VOLTAGE_V,GNSS_TIME_S,LAT,LON,GNSS_ALT_M,SATS,AX,AY,AZ,ROLL,PITCH,SPIN_DPS,STATE,VSPEED_MS,MAG_OK,ACK"
#define LOG_HEADER         TELEM_HEADER ",MAG1_X,MAG1_Y,MAG1_Z,MAG2_X,MAG2_Y,MAG2_Z,GPS_SRC,GPS2_LAT,GPS2_LON,GPS2_ALT_M,GPS2_SATS,WIFI"   // SD adds both compasses, the other GPS, WiFi
// STATE is sent as a number: 0 BENCH, 1 TEST, 2 ASCEND, 3 LAUNCH, 4 APOGEE, 5 DESCEND, 6 IMPACT
#define GPS_FRESH_MS       2000      // a GPS fix older than this does not count

// Barometer
float pressure = 0, temperature = 0, altitude = 0, padPressure = 101325;
float verticalSpeed = 0;        // m/s from altitude, + ascent, - descent
// IMU (m/s2, deg, deg/s)
float ax = 0, ay = 0, az = 0, roll = 0, pitch = 0, gx = 0, gy = 0, gz = 0;
float imuOff[6] = {0};          // accel x,y,z and gyro x,y,z offsets from ZERO_ACCEL
// Magnetometers (gauss, offsets removed): onboard QMC6310 and M100 PRO compass, both SD only (MAG_OK on the radio)
float mx = 0, my = 0, mz = 0, mx2 = 0, my2 = 0, mz2 = 0;
float magRaw[6] = {0};          // raw x,y,z of the onboard compass, then the M100 PRO compass
float magOff[6] = {0};          // hard-iron offsets from MAG_CAL, same order, saved in flash
float magMin[6], magMax[6];     // MAG_CAL extremes
uint32_t magCalUntil = 0;       // millis() when MAG_CAL ends, 0 = not calibrating
bool  compassOk   = false;      // ARM check: both compasses plausible and agreeing
// GNSS: the receiver with a fresh fix and more satellites feeds telemetry; the other ("2") goes to the SD log
double   lat = 0, lon = 0, lat2 = 0, lon2 = 0;
float    gpsAltitude = 0, gpsAltitude2 = 0;
uint32_t gpsTime = 0, sats = 0, sats2 = 0;
const char *gpsSource = "M100"; // which receiver feeds telemetry: M100 (M100 PRO) or L76K (onboard)
// Power
float voltage = 0;

// Mission
// DISARMED: BENCH, TEST   ARMED: ASCEND (on the pad, waiting for launch), LAUNCH, APOGEE, DESCEND, IMPACT
enum State { BENCH, TEST, ASCEND, LAUNCH, APOGEE, DESCEND, IMPACT };
const char *STATE_NAME[] = {"BENCH", "TEST", "ASCEND", "LAUNCH", "APOGEE", "DESCEND", "IMPACT"};
int      state        = BENCH;
int      savedState   = BENCH;  // last state written to flash
bool     armed        = false;  // ARM / DISARM, follows the state
float    deployAltitude = DEPLOY_DEFAULT_M;  // start pulsing from here, set with APOGEE=<m>
float    upX = 0, upY = 0, upZ = 1;          // unit vector pointing up, board axes, measured at ARM
int      orientCount  = ORIENT_SAMPLES;      // samples taken toward the up vector
bool     launched     = false;  // launch detected (thrust or altitude gain), survives a reset
uint32_t launchMs     = 0;      // mission time at launch, survives a reset
float    peakAltitude = 0;      // highest altitude since ARM, survives a reset
bool     apogeeHit    = false;  // apogee detected (descent or failsafe)
uint32_t apogeeMs     = 0;      // mission time at apogee
bool     deploying    = false;  // solenoid pulsing: deploy altitude or apogee until IMPACT
bool     openNose     = false;  // OPENNOSE command, TEST state only
bool     simMode      = false;  // simulation: barometer replaced by SIMP=<Pa> from the ground
float    simPressure  = 0;      // last simulated pressure
int      resetReason  = 0;      // esp_reset_reason() at boot
uint32_t missionMs    = 0;      // mission time, survives a reset
uint32_t timeOffset   = 0;      // mission time carried over a reset
uint32_t packetCount  = 0;
bool     transmitting = false;  // telemetry on/off (CX_ON / CX_OFF)
bool     highPower    = false;  // LoRa +14 dBm (ARM or PWR_HIGH) instead of +10 dBm
bool     backupChannel = false; // LoRa on the backup frequency (FREQ_BACKUP)
bool     logging      = false;  // SD logging, ARM to DISARM or 10 min
uint32_t armMs        = 0;      // mission time at ARM, survives a reset
bool     wifiOn       = false;  // WiFi on/off
bool     ack          = false;  // true once a command is confirmed
String   command      = "";     // last command received
char     packet[256];
File     logFile;
Preferences nvs;

// Kept through a reset (not a power-up)
RTC_NOINIT_ATTR uint32_t rtcMagic, rtcMs, rtcPackets, rtcTx, rtcArm, rtcBackup, rtcPower, rtcLaunched, rtcLaunchMs;
RTC_NOINIT_ATTR float    rtcPeak;

void barometer() {      // pressure, temperature, altitude above the pad (simulated pressure in SIM mode)
  if (!bmeOk && !simMode) return;
  float p = simMode ? simPressure : bmeDev.readPressure();
  if (p < 30000 || p > 110000) return;            // reject impossible readings
  pressure = p;
  if (bmeOk) temperature = bmeDev.readTemperature();
  altitude = 44330.0f * (1.0f - powf(pressure / padPressure, 0.1903f));
}

void speed() {          // vertical speed from altitude every 0.5 s, smoothed
  static float lastAlt = 0;
  static uint32_t lastMs = 0;
  if (missionMs - lastMs < SPEED_MS) return;
  if (lastMs) verticalSpeed += 0.5f * ((altitude - lastAlt) * 1000.0f / (missionMs - lastMs) - verticalSpeed);
  lastAlt = altitude; lastMs = missionMs;
}

void zeroAltitude() {   // pad pressure from 10 readings; skipped when armed (restored from flash)
  if (!bmeOk || armed) return;
  delay(200);
  float sum = 0;
  for (int i = 0; i < 10; i++) { sum += bmeDev.readPressure(); delay(20); }
  padPressure = sum / 10;
}

void zeroAccel() {      // at rest: 0.5 s average; accel keeps only 1 g along gravity, gyro goes to 0; saved
  if (!imuOk || armed) return;
  float s[6] = {0}, a[3], w[3];
  int n = 0;
  for (int i = 0; i < 25; i++) {
    delay(20);
    if (!imuDev.getAccelerometer(a[0], a[1], a[2]) || !imuDev.getGyroscope(w[0], w[1], w[2])) continue;
    for (int k = 0; k < 3; k++) { s[k] += a[k] * G; s[k + 3] += w[k]; }
    n++;
  }
  if (!n) return;
  for (int k = 0; k < 6; k++) s[k] /= n;
  float m = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
  for (int k = 0; k < 3; k++) { imuOff[k] = s[k] - G * s[k] / m; imuOff[k + 3] = s[k + 3]; }
  nvs.putBytes("imuOff", imuOff, sizeof(imuOff));
}

void setDeployAltitude(float m) {   // set and save the altitude where pulsing starts
  deployAltitude = m;
  nvs.putFloat("deploy", m);
}

void imu() {            // acceleration and gyro rates with offsets removed, roll/pitch from gravity
  if (!imuOk || !imuDev.getAccelerometer(ax, ay, az) || !imuDev.getGyroscope(gx, gy, gz)) return;
  ax = ax * G - imuOff[0]; ay = ay * G - imuOff[1]; az = az * G - imuOff[2];
  gx -= imuOff[3]; gy -= imuOff[4]; gz -= imuOff[5];
  roll  = atan2f(ay, az) * RAD_TO_DEG;
  pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
}

void magnetometer() {   // onboard QMC6310 magnetic field, offsets removed
  MagnetometerData md;
  if (!magOk || !magDev.readData(md)) return;
  magRaw[0] = md.magnetic_field.x; magRaw[1] = md.magnetic_field.y; magRaw[2] = md.magnetic_field.z;
  mx = magRaw[0] - magOff[0]; my = magRaw[1] - magOff[1]; mz = magRaw[2] - magOff[2];
}

void magnetometer2() {  // M100 PRO compass magnetic field, offsets removed
  MagnetometerData md;
  if (!mag2Ok || !mag2Dev->readData(md)) return;
  magRaw[3] = md.magnetic_field.x; magRaw[4] = md.magnetic_field.y; magRaw[5] = md.magnetic_field.z;
  mx2 = magRaw[3] - magOff[3]; my2 = magRaw[4] - magOff[4]; mz2 = magRaw[5] - magOff[5];
}

void startMagCal() {    // MAG_CAL: start 20 s of min/max tracking on both compasses
  for (int i = 0; i < 6; i++) { magMin[i] = 1e9; magMax[i] = -1e9; }
  magCalUntil = millis() + MAG_CAL_MS;
}

void magCalibrate() {   // while MAG_CAL runs track the extremes, then save the centre of each axis as its offset
  if (!magCalUntil) return;
  for (int i = 0; i < 6; i++) { magMin[i] = min(magMin[i], magRaw[i]); magMax[i] = max(magMax[i], magRaw[i]); }
  if ((int32_t)(millis() - magCalUntil) < 0) return;
  for (int i = 0; i < 6; i++) magOff[i] = (magMin[i] + magMax[i]) / 2;
  nvs.putBytes("magOff", magOff, sizeof(magOff));
  magCalUntil = 0;
}

void magCheck() {       // at ARM: each compass reads 0.15-0.8 G (Earth 0.25-0.65 G) and the two agree within 25 %
  float a = sqrtf(mx * mx + my * my + mz * mz), b = sqrtf(mx2 * mx2 + my2 * my2 + mz2 * mz2);
  compassOk = magOk && mag2Ok && a > 0.15f && a < 0.8f && b > 0.15f && b < 0.8f && fabsf(a - b) < 0.25f * max(a, b);
}

void gps() {            // M100 PRO GPS: feed its parser
  while (gps2Serial.available()) gps2Dev.encode(gps2Serial.read());
}

void gps2() {           // onboard L76K GPS: feed its parser
  while (Serial1.available()) gpsDev.encode(Serial1.read());
}

int gpsScore(TinyGPSPlus &g) {  // satellites in use, or -1 without a fix from the last 2 s
  return g.location.isValid() && g.location.age() < GPS_FRESH_MS ? (int)g.satellites.value() : -1;
}

void copyFix(TinyGPSPlus &g, double &la, double &lo, float &alt, uint32_t &sa) {  // position, altitude, satellites
  if (g.location.isValid()) { la = g.location.lat(); lo = g.location.lng(); }
  if (g.altitude.isValid()) alt = g.altitude.meters();
  sa = g.satellites.value();
}

void bestGps() {        // telemetry gets the receiver with more satellites (M100 PRO on a tie), the SD log the other
  bool m100 = gpsScore(gps2Dev) >= gpsScore(gpsDev);
  TinyGPSPlus &best = m100 ? gps2Dev : gpsDev, &other = m100 ? gpsDev : gps2Dev;
  copyFix(best, lat, lon, gpsAltitude, sats);
  copyFix(other, lat2, lon2, gpsAltitude2, sats2);
  if (best.time.isValid()) gpsTime = best.time.hour() * 3600 + best.time.minute() * 60 + best.time.second();
  gpsSource = m100 ? "M100" : "L76K";
}

void battery() {        // primary pack voltage
  voltage = batteryVolts();
}

void buildPacket() {    // telemetry line, extras after STATE, CR terminated
  snprintf(packet, sizeof(packet),
           "%s,%lu,%lu,%.1f,%.0f,%.1f,%.2f,%lu,%.4f,%.4f,%.1f,%lu,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%d,%.1f,%d,%s\r",
           ROCKET_ID, (unsigned long)(missionMs / 1000), (unsigned long)packetCount,
           altitude, pressure, temperature, voltage,
           (unsigned long)gpsTime, lat, lon, gpsAltitude, (unsigned long)sats,
           ax, ay, az, roll, pitch, gz, state, verticalSpeed, compassOk ? 1 : 0,
           ack ? command.c_str() : "");
}

void setChannel(bool backup) {  // switch LoRa between the main and backup frequency
  backupChannel = backup;
  linkSetFreq(backup ? LORA_FREQ_BACKUP : LORA_FREQ_MAIN);
}

void setPower(bool high) {      // LoRa transmit power: +14 dBm (high) or +10 dBm
  highPower = high;
  linkSetPower(high ? LORA_PWR_HIGH : LORA_PWR_LOW);
}

void armRocket() {      // ARM: ASCEND on the pad, high TX power, measure orientation, check compasses, clear flight flags
  magCheck();
  armed = true; state = ASCEND; armMs = missionMs;
  orientCount = 0; launched = false; peakAltitude = 0; apogeeHit = false; deploying = false;
  setPower(true);
}

void disarmRocket() {   // DISARM / RESET_STATE: back to BENCH, low TX power, stop deploying
  armed = false; state = BENCH;
  launched = false; apogeeHit = false; deploying = false;
  setPower(false);
}

void setSimPressure(float p) {  // SIMP=<Pa>: the first value is taken as the pad pressure
  if (!simPressure) padPressure = p;
  simPressure = p;
}

void simOff() {         // SIM_OFF: end the simulated flight, back to the real barometer
  simMode = false;
  disarmRocket();
  zeroAltitude();
}

void listen() {         // receive a command, apply it, set ack
  String in;
  if (!linkPoll(in)) return;
  ack = true;
  if      (in == "CX_ON")    transmitting = true;
  else if (in == "CX_OFF")   transmitting = false;
  else if (in == "WIFI_ON" && !armed && state == TEST) wifiOn = true;
  else if (in == "WIFI_OFF") wifiOn = false;
  else if (in == "ARM" && state == BENCH)               armRocket();
  else if (in == "DISARM" || in == "RESET_STATE")       disarmRocket();
  else if (in == "TEST" && !armed && state == BENCH)    state = TEST;
  else if (in == "OPENNOSE" && !armed && state == TEST) openNose = true;
  else if (in == "ZERO" && !armed)                      zeroAltitude();
  else if (in == "ZERO_ACCEL" && !armed)                zeroAccel();
  else if (in == "MAG_CAL" && !armed)                   startMagCal();
  else if (in == "PWR_HIGH")                            setPower(true);
  else if (in == "PWR_LOW")                             setPower(false);
  else if (in == "FREQ_MAIN")                           setChannel(false);
  else if (in == "FREQ_BACKUP")                         setChannel(true);
  else if (in == "SIM_ON" && !armed)                    { simMode = true; simPressure = 0; }
  else if (in == "SIM_OFF" && simMode)                  simOff();
  else if (in.startsWith("SIMP=") && simMode)           setSimPressure(in.substring(5).toFloat());
  else if (in.startsWith("APOGEE=") && !armed && in.substring(7).toFloat() > 0) setDeployAltitude(in.substring(7).toFloat());
  else ack = false;
  if (ack) command = in;
}

void radioTimeout() {   // all transmission off 90 min after launch, survives a reset
  if (launched && missionMs - launchMs >= TX_LIMIT_MS) transmitting = false;
}

void transmit() {       // send the packet over LoRa when telemetry is on
  if (!transmitting) return;
  if (linkSend(packet)) { packetCount++; ack = false; }
}

void printData() {      // same line as transmitted, to USB serial
  Serial.print(packet);
  Serial.print('\n');
}

void logger() {         // log every line from ARM until DISARM or 10 min after ARM, flush each second
  static uint32_t flushAt = 0;
  logging = armed && missionMs - armMs < LOG_LIMIT_MS;
  if (!logging || !sdOk) { if (logFile) logFile.close(); return; }
  if (!logFile) {
    logFile = SD.open(LOG_FILE, FILE_APPEND);
    if (!logFile) return;
    if (logFile.size() == 0) logFile.println(LOG_HEADER);
  }
  logFile.write((const uint8_t *)packet, strlen(packet) - 1);           // telemetry line without its CR
  logFile.printf(",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%.4f,%.4f,%.1f,%lu,%s\n",     // plus both compasses,
                 mx, my, mz, mx2, my2, mz2,
                 gpsSource, lat2, lon2, gpsAltitude2, (unsigned long)sats2,          // the other GPS,
                 wifiOn ? "ON" : "OFF");                                             // WiFi
  if (millis() - flushAt >= 1000) { flushAt = millis(); logFile.flush(); }
}

void logReset() {       // append mission time, reset reason and state to /resets.csv at every boot
  resetReason = esp_reset_reason();
  Serial.printf("reset reason %d, state %s\n", resetReason, STATE_NAME[state]);
  File f = sdOk ? SD.open(RESET_FILE, FILE_APPEND) : File();
  if (!f) return;
  if (f.size() == 0) f.println("TIME_S,RESET_REASON,STATE");
  f.printf("%lu,%d,%s\n", (unsigned long)(timeOffset / 1000), resetReason, STATE_NAME[state]);
  f.close();
  Serial.println("reset logged to SD");
}

void wifi() {           // access point + web page, TEST state only
  static bool running = false;
  if (state != TEST) wifiOn = false;
  if (wifiOn != running) {
    running = wifiOn;
    if (running) webStart(WIFI_SSID, WIFI_PASS);
    else webStop();
  }
  if (webLoop(TELEM_HEADER, packet)) openNose = true;
}

void orientation() {    // first second after ARM: average gravity = which way is up, saved for a reset
  static float sx, sy, sz;
  if (state != ASCEND || orientCount >= ORIENT_SAMPLES) return;
  if (orientCount == 0) sx = sy = sz = 0;
  sx += ax; sy += ay; sz += az;
  if (++orientCount < ORIENT_SAMPLES) return;
  float n = sqrtf(sx * sx + sy * sy + sz * sz);
  if (n < 1) { orientCount = 0; return; }        // no IMU data yet: start again
  upX = sx / n; upY = sy / n; upZ = sz / n;
  nvs.putFloat("upX", upX); nvs.putFloat("upY", upY); nvs.putFloat("upZ", upZ);
}

void launch() {         // ASCEND: thrust along the up axis, or altitude gain, confirmed = LAUNCH
  static int count = 0;
  if (state != ASCEND || launched) { count = 0; return; }
  bool thrust = orientCount >= ORIENT_SAMPLES && ax * upX + ay * upY + az * upZ > LAUNCH_ACCEL;
  count = (thrust || altitude > LAUNCH_ALT_M) ? count + 1 : 0;
  if (count >= LAUNCH_CONFIRM) { launched = true; launchMs = missionMs; state = LAUNCH; }
}

void apogee() {         // LAUNCH: confirmed drop under the peak, or the failsafe timer = APOGEE
  static int count = 0;
  if (state != LAUNCH) { count = 0; return; }
  if (altitude > peakAltitude) peakAltitude = altitude;
  count = (launched && altitude < peakAltitude - APOGEE_DROP_M) ? count + 1 : 0;
  bool failsafe = launched && missionMs - launchMs >= APOGEE_FAILSAFE_MS;
  if (count >= APOGEE_CONFIRM || failsafe) { apogeeHit = true; state = APOGEE; apogeeMs = missionMs; }
}

void impact() {         // APOGEE -> DESCEND after 2 s; DESCEND -> IMPACT once altitude holds still 5 s
  static float ref = 0;
  static uint32_t refMs = 0;
  if (state == APOGEE && missionMs - apogeeMs >= APOGEE_HOLD_MS) state = DESCEND;
  if (state != DESCEND) { refMs = 0; return; }
  if (!refMs || fabsf(altitude - ref) > IMPACT_STILL_M) { ref = altitude; refMs = missionMs; return; }
  if (missionMs - refMs >= IMPACT_STILL_MS && (bmeOk || simMode)) state = IMPACT;   // dead barometer: keep pulsing
}

void solenoid() {       // armed: pulse every 2 s from deploy altitude or apogee until IMPACT. TEST: one pulse per OPENNOSE
  static bool open = false;
  static uint32_t openedAt = 0;
  if (!armed || state == IMPACT) deploying = false;
  else if (state == APOGEE || state == DESCEND || (launched && altitude >= deployAltitude)) deploying = true;
  bool test = !armed && state == TEST && openNose;
  if (!open && (test || (deploying && millis() - openedAt >= SOLENOID_PERIOD_MS))) {
    digitalWrite(SOLENOID_PIN, HIGH);
    open = true; openedAt = millis(); openNose = false;
  }
  if (open && millis() - openedAt >= SOLENOID_PULSE_MS) {
    digitalWrite(SOLENOID_PIN, LOW);
    open = false;
  }
}

void loadState() {      // restore state, deploy altitude, IMU offsets and, if armed, pad pressure and up vector
  state = savedState = nvs.getInt("state", BENCH);
  if (state < BENCH || state > IMPACT) state = BENCH;
  armed = state >= ASCEND;
  deployAltitude = nvs.getFloat("deploy", DEPLOY_DEFAULT_M);
  nvs.getBytes("imuOff", imuOff, sizeof(imuOff));
  nvs.getBytes("magOff", magOff, sizeof(magOff));
  if (!armed) return;
  padPressure = nvs.getFloat("pad", padPressure);
  upX = nvs.getFloat("upX", 0); upY = nvs.getFloat("upY", 0); upZ = nvs.getFloat("upZ", 1);
}

void saveState() {      // save the flight state to flash when it changes, pad pressure at ARM
  if (state == savedState) return;
  nvs.putInt("state", state);
  if (state == ASCEND) nvs.putFloat("pad", padPressure);
  savedState = state;
}

void loadMission() {    // after a reset restore mission time, packets, radio, ARM/launch time, launch flag, peak
  if (esp_reset_reason() == ESP_RST_POWERON || rtcMagic != RTC_MAGIC) {
    rtcMagic = RTC_MAGIC; rtcMs = 0; rtcPackets = 0; rtcTx = 0; rtcArm = 0; rtcBackup = 0;
    rtcPower = armed; rtcLaunched = state >= LAUNCH; rtcLaunchMs = 0; rtcPeak = 0;   // state from flash says if we flew
  }
  timeOffset = rtcMs; packetCount = rtcPackets; transmitting = rtcTx; armMs = rtcArm; backupChannel = rtcBackup;
  highPower = rtcPower; launched = rtcLaunched; launchMs = rtcLaunchMs; peakAltitude = rtcPeak;
}

void saveMission() {    // keep mission time, packets, radio, ARM/launch time, launch flag, peak in RTC memory
  missionMs = timeOffset + millis();
  rtcMs = missionMs; rtcPackets = packetCount; rtcTx = transmitting; rtcArm = armMs; rtcBackup = backupChannel;
  rtcPower = highPower; rtcLaunched = launched; rtcLaunchMs = launchMs; rtcPeak = peakAltitude;
}

void setup() {
  boardInit();
  nvs.begin("fc", false);
  loadState();
  loadMission();
  sdInit();
  logReset();
  sensorsInit();
  linkBegin(backupChannel ? LORA_FREQ_BACKUP : LORA_FREQ_MAIN, highPower ? LORA_PWR_HIGH : LORA_PWR_LOW);
  zeroAltitude();
}

void loop() {
  static uint32_t sampleAt = 0, telemAt = 0;
  uint32_t now = millis();

  listen();
  wifi();
  solenoid();
  saveState();
  saveMission();
  radioTimeout();

  if (now - sampleAt >= SAMPLE_MS) {
    sampleAt = now;
    barometer();
    speed();
    imu();
    magnetometer();
    magnetometer2();
    magCalibrate();
    gps();
    gps2();
    bestGps();
    battery();
    orientation();
    launch();
    apogee();
    impact();
    buildPacket();
    logger();
  }

  if (now - telemAt >= TELEM_MS) {
    telemAt = now;
    transmit();
    printData();
  }
}
