# Rocket avionics

Flight computer, ground stations, dashboards and CanSat test firmware for a solenoid-recovery
model rocket that flies to about 1 km, built on ESP32-S3 boards with 868 MHz LoRa telemetry.

Everything is one [PlatformIO](https://platformio.org/) project (Arduino framework, ESP32-S3).
Each board has one source file in `src/`; everything that is not main logic lives in a library in `lib/`.


## What is in here

| Folder | What it is |
|---|---|
| `src/rocket/` | **Flight computer** (LilyGo T-Beam Supreme). Sensors, flight states, solenoid deployment, SD log, 868 MHz LoRa telemetry. Build env `flight`. |
| `src/ground/` | **Ground station** (second T-Beam Supreme). LoRa to USB serial bridge for the PC dashboard, OLED status. Build env `ground`. |
| `src/webground/` | **Web ground station** (custom ESP32-S3-WROOM-1U board with an E22-900M22S). Serves the dashboard over WiFi, logs to flash, uploads telemetry to an MQTT broker. Build env `webground`. |
| `src/cansat/` | **CanSat bench test firmware** (ESP32-S3 DevKitC-1). MPU6050, BMP280, GNSS, 5 Hz SD log, USB commands. No radio. Build env `cansat`. |
| `dashboard/` | Browser dashboards (Chrome or Edge, Web Serial, work offline). `index.html` is the ground station, `live.html` adds the online feed, `cansat.html` is for the CanSat. `sim/` holds pressure profiles for simulation mode. |
| `lib/Board`, `lib/Link`, `lib/Web`, `lib/Display` | T-Beam board support, LoRa link, the rocket's bench WiFi page, OLED. |
| `lib/StationLink`, `lib/StationWeb`, `lib/StationCloud` | Web ground station radio, web page and MQTT upload. |
| `lib/CanSat` | CanSat pin map, sensors and SD helpers. |
| `lib/` (the rest) | Third-party libraries, vendored unchanged so the build never downloads them (see Credits). |
| `boards/` | PlatformIO board definition for the T-Beam Supreme. |

## Hardware

- **Rocket:** LilyGo T-Beam Supreme (ESP32-S3, SX1262, L76K GNSS, AXP2101, BME280, QMI8658, QMC6310, SD card), plus an HGLRC M100 PRO GNSS and compass, a 2S Li-ion pack through a 5 V buck converter, and one main-chute solenoid switched by an IRLU120N on IO21. Pack voltage is read on IO2 through a 100k/47k divider.
- **Backup:** Thrust Tech India VEGA FC5.1 (COTS altimeter) on its own battery, switching the same solenoid 1.5 s after its own apogee detection. It shares only ground and the coil with the T-Beam.

## Flight sequence

States are sent as a number in telemetry.

| # | State | Group | Entered when |
|---|---|---|---|
| 0 | BENCH | disarmed | Power-up, `DISARM` |
| 1 | TEST | disarmed | `TEST` command; allows `OPENNOSE` and the bench WiFi page |
| 2 | ASCEND | armed | `ARM`; on the pad, measuring which way is up, waiting for launch |
| 3 | LAUNCH | armed | 2.5 g along the up axis, or 30 m altitude gain, 5 samples in a row |
| 4 | APOGEE | armed | Altitude 3 m under the peak for 5 samples, or 20 s after launch (failsafe) |
| 5 | DESCEND | armed | 2 s after apogee |
| 6 | IMPACT | armed | Altitude still within 2 m for 5 s |

The solenoid pulses (1 s on, 1 s off) from the deploy altitude (default 1000 m) or from apogee,
whichever comes first, until IMPACT. It never latches and stays off at boot.
State, pad pressure, orientation, mission time, packet count and launch data survive a reset in flight.

## Radio and telemetry

| Parameter | Value |
|---|---|
| Frequency | 866.50 MHz main, 865.25 MHz backup |
| Modulation | LoRa, 125 kHz, SF9, CR 4/5, sync word 0x44 |
| Power | Rocket +10 dBm disarmed, +14 dBm armed. Ground stations +22 dBm (check the limit for your band and region) |
| Rate | 1 packet per second, silent until `CX_ON`, off 90 min after launch |

Packet (ASCII, comma separated, CR terminated):

```
ID,TIME_S,PACKET,ALT_M,PRESSURE_PA,TEMP_C,VOLTAGE_V,GNSS_TIME_S,LAT,LON,GNSS_ALT_M,SATS,AX,AY,AZ,ROLL,PITCH,SPIN_DPS,STATE,VSPEED_MS,MAG_OK,ACK
```

The ground stations append `RSSI_DBM,SNR_DB`. The rocket's SD log (`/flight.csv`, 50 Hz from ARM)
adds both compasses, the second GNSS and the WiFi state.

## Commands

Sent as plain text over LoRa. The rocket echoes an accepted command in the `ACK` field.

| Command | Effect | Allowed |
|---|---|---|
| `CX_ON` / `CX_OFF` | Telemetry on / off | Always |
| `ARM` / `DISARM` (`RESET_STATE`) | Arm on the pad / back to BENCH | `ARM` from BENCH |
| `TEST`, `OPENNOSE` | Test state; one solenoid pulse | Disarmed |
| `ZERO`, `ZERO_ACCEL`, `MAG_CAL` | Zero altitude; zero accel and gyro; 20 s compass calibration | Disarmed |
| `APOGEE=<m>` | Deploy start altitude, saved in flash | Disarmed |
| `FREQ_MAIN` / `FREQ_BACKUP` | Switch LoRa channel | Always |
| `PWR_HIGH` / `PWR_LOW` | Rocket LoRa +14 / +10 dBm | Always |
| `SIM_ON`, `SIMP=<Pa>`, `SIM_OFF` | Simulation: barometer replaced by pressures from the ground | `SIM_ON` disarmed |
| `WIFI_ON` / `WIFI_OFF` | Bench web page on the rocket (SD files, live data, solenoid test) | TEST only |

## Build and flash

Install PlatformIO, then from this folder:

```
pio run -e flight    -t upload     # rocket
pio run -e ground    -t upload     # T-Beam ground station
pio run -e webground -t upload     # web ground station
pio run -e cansat    -t upload     # CanSat test board
```

`pio run` with no `-e` builds only `flight`.

### Private settings

Passwords are kept out of the repository. Before building `webground` or using the online feed:

1. Copy `include/secrets.example.h` to `include/secrets.h` and fill in the hotspot and broker login.
2. Copy `dashboard/secrets.example.js` to `dashboard/secrets.js` and fill in the viewer login (only needed for `live.html`).

## Using the dashboards

1. Flash the ground T-Beam, plug it into the PC and open `dashboard/index.html` in Chrome or Edge.
2. Click **Connect COM port**, then **Telemetry on**.
3. Commands are resent until the rocket acknowledges them (`OPENNOSE` is sent once only).
4. **Arm** starts a log that is saved automatically at Disarm or after 10 minutes. **Save all data** saves everything received as `Flight.csv`.

**Simulation mode:** choose a pressure file from `dashboard/sim/` and click **Run simulation**.
The rocket flies the profile on the bench using its real flight logic. The solenoid really fires, so unplug the backup pack first.

`live.html` shows COM port data when present and the online MQTT feed otherwise. The online feed is one way: the rocket cannot be commanded over the internet.

## Known limits

- The onboard accelerometer tops out at 16 g.
- The bench WiFi code is compiled into the flight build, but can only start in the disarmed TEST state.
- The web ground station's page accepts commands from anyone on the same hotspot, so keep the hotspot private.
- There is no flyback diode across the solenoid ; fit one (for example a 1N5822) across the coil.

## Credits

Third-party libraries in `lib/`, each under its own licence (see the licence file in each folder):
RadioLib, TinyGPS++, Adafruit BME280, Adafruit BusIO, Adafruit Unified Sensor, SensorLib, XPowersLib and the
ThingPulse ESP8266/ESP32 OLED driver. `dashboard/mqtt.min.js` is MQTT.js.
The `webground` and `cansat` builds also fetch PubSubClient, Adafruit MPU6050 and Adafruit BMP280.

Before flying, change the WiFi passwords (`change-me-1234`), the rocket ID and the LoRa frequency and sync word to your own.
