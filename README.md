# Modular Lego Train Framework (ESP-lego-loco)

A high-performance, modular, object-oriented C++ framework for autonomous Lego model trains and intelligent layout infrastructure using **ESP32**, **ESP32-S3**, **ESP32-C3**, and **ESP8266**.

---

## 🚂 Key Highlights & Architecture

- **3 Core Firmware Profiles (Multi-ESP Compatible):**
  - 👑 **Master Gateway & Brain:** ESP32, ESP32-S3
  - 🚂 **Autonomous Locomotive Mobile Node:** ESP32-C3, ESP8266 (D1 Mini), ESP32
  - 🚉 **Smart Track Switch & Station Platform:** ESP32, ESP8266 (D1 Mini)
- **Web Flasher (Direct Browser USB Programming):**
  - Program any ESP board directly from Google Chrome, Edge, or Opera using the **Web Serial API**.
  - Single-file merged binaries (`_merged.bin`) bundle bootloader, partitions, and application for **0x0000** flashing.
  - Built-in live Serial Monitor (115200 baud) for instant boot diagnostics.
- **Autonomous Locomotives (Learning Lap):**
  - **Self-Guided Learning Lap:** Locomotives execute a calibration run at 35% throttle, discovering 38kHz IR beacons, measuring inter-beacon transit intervals ($\Delta t_i$), and confirming loop closure.
  - **Dynamic Speed & Braking Governance:** Trains automatically accelerate out of blocks and compute an anticipatory braking curve (*Top of Descent*) to gently stop centered on platform beacons.
  - **Decentralized Station ETA:** Locomotives compute their own arrival countdown (`etaSeconds`) based on instantaneous speed, transmitting it in periodic telemetry.
- **Smart Stations & Anti-Collision Headway Regulation:**
  - **Multi-Beacon Architecture:** Exactly 1 Station Arrival beacon (`ROLE_STATION_ARRIVAL`) at the platform triggers stops and dwell timers; configurable **Localizer Beacons** (`ROLE_LOCATOR`) track train progress along sectors without stopping.
  - **Optical Train Length Measurement ($L = v \times \Delta t$):** Beam break sensors measure occlusion duration between front nose and rear tail clearance, reporting composition length in cm.
  - **3-Zone Dynamic Traffic Regulation:** Stations track elapsed time between consecutive trains ($T_{\text{headway}}$):
    - **Green Aspect ($\ge 12$s):** Clear zone, trains run at nominal cruising speed.
    - **Yellow Aspect ($5\text{s} - 12\text{s}$):** Caution zone; transmits direct ESP-NOW speed trimming commands (40% throttle reduction).
    - **Red Aspect ($< 5$s or Platform Occupied):** Danger zone; commands immediate electrical braking and holding.
  - **Fail-Safe Turnout Interlocking & Tail Clearance:** Turnout servos are mechanically locked while a train breaks the beam. If the platform line is occupied, trains are auto-diverted to passing sidings **only if** the measured train length fits within the siding capacity.
- **Centralized Traffic Control (CTC) Synoptic Vector Map:**
  - Dynamic vector SVG schematic map auto-generated from discovered track beacons and controller telemetry.
  - Live train position interpolation with **proportional train marker scaling** matching the physical train length ($L_{\text{train}}$).
  - Real-time block occupancy illumination (cyan clear, glowing red occupied) and clickable switch junctions.
- **Advanced Configuration Studio (Replaces Legacy CSV Scenarios):**
  - CSV scenarios are completely deprecated. All parameters are managed via a unified JSON schema (`/config/config.json`) with 3 dedicated Web UI panels:
    1. 🌐 **Global / System:** Venue name, Wi-Fi SSID, channel, and anti-collision headway buffers.
    2. 🚂 **Locomotives:** Cruise speeds, learning speed (35%), acceleration/braking curves, dwell times, and learning lap commands.
    3. 🚉 **Stations & Turnouts:** Switch angles, dwell timers, siding capacity, and multi-beacon assignment table with train length toggle.
- **Strict EEPROM Pairing & Multi-Layout Isolation:**
  - Nodes derive their identifier directly from physical MAC addresses (`LOCO_4B5C`, `TRACK_1A2B`).
  - Freshly flashed nodes start in an `UNPAIRED` state. Bonding saves the Master's MAC to EEPROM with strict packet filtering, ensuring trains on Track A never respond to commands from Track B in shared club layouts.

---

## 🌐 Web Flasher: Direct Browser Firmware Installation

Program ESP microcontrollers directly from your browser without local Python, PlatformIO, or Arduino IDE toolchains!

### Supported Browsers
- Google Chrome (Desktop)
- Microsoft Edge (Desktop)
- Opera / Brave (Chromium-based)

### Available Firmware Images
| Profile | Role | Supported Chips | Merged Binary File |
| :--- | :--- | :--- | :--- |
| 👑 **Master** | Central Gateway, Web UI, Topology Brain | **ESP32**, **ESP32-S3** | `master_merged.bin`, `master_s3_merged.bin` |
| 🚂 **Loco** | Autonomous Traction, Learning Lap, LEDs, IR RX | **ESP32-C3**, **ESP8266**, **ESP32** | `loco_c3_merged.bin`, `loco_esp8266_merged.bin`, `loco_esp32_merged.bin` |
| 🚉 **Station** | Smart Turnout (Servo), Multi-Beacon, Length, OLED | **ESP32**, **ESP8266** | `track_merged.bin`, `track_esp8266_merged.bin` |

### ⚡ Flashing via Online Flasher (e.g. http://esptool.spacehuhn.com/)

1. Open **[http://esptool.spacehuhn.com/](http://esptool.spacehuhn.com/)** in Google Chrome or Microsoft Edge.
2. Connect your ESP to your computer via USB.
3. Click **"Connect"**, select your COM port, and set baud rate to **115200** (or **921600** for fast flashing).
4. Under **Files**, choose the merged `.bin` file from `tools/web-flasher/binaries/`:
   - **Master Gateway (ESP32):** `master_merged.bin`
   - **Master Gateway (ESP32-S3):** `master_s3_merged.bin`
   - **Locomotive (ESP32-C3):** `loco_c3_merged.bin`
   - **Locomotive (ESP8266 D1 Mini):** `loco_esp8266_merged.bin`
   - **Locomotive (ESP32 DevKit):** `loco_esp32_merged.bin`
   - **Track & Station (ESP32):** `track_merged.bin`
   - **Track & Station (ESP8266):** `track_esp8266_merged.bin`
5. **Flash Address / Offset:** Set address to **`0x0000`** (or `0x0`).
6. Click **"Program"** and wait for completion.
7. Once finished, press the **RST** button on your ESP board.

### How to Launch the Local Web Flasher
```bash
python3 -m http.server 8080 -d tools/web-flasher
```
Open **`http://localhost:8080`** in Google Chrome or Microsoft Edge.

---

## 📁 Repository Structure

```
ESP-lego-loco/
├── platformio.ini              # Environments for Master, Loco, and Track across ESP32/S3/C3/8266
├── scripts/
│   ├── merge_bin.py            # PlatformIO post-build hook to create merged web-flasher binaries
│   └── build_all_binaries.py   # Batch script to compile all profiles for web flashing
├── tools/
│   └── web-flasher/            # Standalone Web Serial Flasher & Terminal Monitor
│       ├── index.html          # Web flasher UI with 3 profile presets
│       ├── style.css           # Modern railway dark-theme styling
│       ├── flasher.js          # Web Serial API connection & logging logic
│       ├── manifest_master.json# Web Tools manifest for Master
│       ├── manifest_loco.json  # Web Tools manifest for Loco
│       ├── manifest_station.json # Web Tools manifest for Station
│       └── binaries/           # Output directory for compiled .bin images
├── lib/
│   ├── ConfigStore/            # LittleFS JSON Settings, Unified Config & Graph Topology
│   ├── ESPNowManager/          # Low-overhead ESP-NOW wrapper, discovery & protocol packets
│   ├── MotorController/        # L9110 PWM driver with momentum ramping & active braking
│   ├── LightingSystem/         # 3-Zone LED controller (Front, Rear, Cab) & auto-direction
│   ├── IRTelemetry/            # 38kHz beacon transmission, decoding & beam-break occupancy
│   ├── TrackManager/           # Servo turnout movement, platform dwell timer & occupancy
│   ├── StationDisplay/         # 0.96'' I2C OLED (128x64 SSD1306) timetable & signal aspects
│   └── WebServer/              # Async Web Server, REST API & WebSockets
├── src/
│   ├── master/main.cpp         # Master Gateway entry point & supervisor
│   ├── master/TopologyManager.h # Circuit Graph, Auto-Layout & Multi-Node Config
│   ├── master/TopologyManager.cpp
│   ├── loco/main.cpp           # Mobile Autonomous Locomotive firmware (Learning Lap)
│   └── track/main.cpp          # Track Switch, Headway Regulation & Smart Station firmware
└── data/                       # LittleFS Web UI assets (served by Master)
    ├── index.html              # Responsive dispatch dashboard, CTC Synoptic Map & Config Studio
    ├── style.css               # Modern dark-mode glassmorphism styling
    ├── app.js                  # WebSocket client, SVG Map Renderer & real-time controls
    └── config/                 # Dynamic system, node and topology JSON files
```

---

## ⚡ Hardware Profiles & Pinouts

### 1. Master Gateway (ESP32 / ESP32-S3)
- **Role:** Wi-Fi Access Point / Station Gateway + ESP-NOW Master Supervisor + Topology Manager.
- **Wi-Fi:** Default SSID `LegoTrain_Master` (IP: `192.168.4.1` in AP mode).
- **Storage:** LittleFS for static Web UI (`/index.html`, `/app.js`, `/style.css`) and JSON configs.

### 2. Locomotive Node (ESP32-C3, ESP8266 D1 Mini, or ESP32)
| Function | ESP32-C3 Pin | ESP8266 (D1 Mini) | ESP32 DevKit | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Motor IA (PWM)** | GPIO 4 | D1 (GPIO 5) | GPIO 18 | L9110 Driver Input A |
| **Motor IB (PWM)** | GPIO 5 | D2 (GPIO 4) | GPIO 19 | L9110 Driver Input B |
| **Headlights (Front)** | GPIO 6 | D5 (GPIO 14) | GPIO 21 | Warm White LEDs (PWM) |
| **Tail Lights (Rear)** | GPIO 7 | D6 (GPIO 12) | GPIO 22 | Red LEDs (PWM) |
| **Cab / Side Lights** | GPIO 8 | D7 (GPIO 13) | GPIO 23 | Interior/marker LEDs |
| **IR Receiver** | GPIO 3 | D3 (GPIO 0) | GPIO 15 | TSOP38238 / TSOP4838 (38kHz) |
| **Battery ADC (Optional)** | GPIO 0 | A0 (0-1V) | GPIO 34 | 1S LiPo voltage divider |

### 3. Track / Station Node (ESP32 or ESP8266 D1 Mini)
| Function | ESP32 Pin | ESP8266 (D1 Mini) | Notes |
| :--- | :--- | :--- | :--- |
| **Primary Servo Switch** | GPIO 18 | D4 (GPIO 2) | Track Turnout / Switch Servo (SW #1) |
| **Auxiliary Servos** | GPIO 25, 26, 27 | D7 (GPIO 13), D8 | Additional turnouts configurable via UI GPIO field |
| **IR Beacon TX** | GPIO 19 | D5 (GPIO 14) | 38kHz IR LED (Configurable per beacon) |
| **IR Beam-Break RX**| GPIO 23 | D6 (GPIO 12) | Track occupancy detector & length measurement |
| **OLED SDA** | GPIO 21 | D2 (GPIO 4) | 0.96'' SSD1306 (I2C) |
| **OLED SCL** | GPIO 22 | D1 (GPIO 5) | 0.96'' SSD1306 (I2C) |

---

## 🎛️ Unified Configuration Specification (`/config/config.json`)

All layout operations, train speeds, multi-switch turnouts, and multi-beacon allocations are governed by a single JSON document:

```json
{
  "system": {
    "layoutName": "Lego Central Layout",
    "wifiSsid": "LegoTrain_Master",
    "wifiChannel": 1,
    "headwaySafeSec": 12,
    "headwayCautionSec": 6,
    "headwaySpeedTrimPct": 40
  },
  "locomotives": [
    {
      "nodeId": "LOCO_4B5C",
      "name": "Cargo Express",
      "maxSpeed": 70,
      "learningSpeed": 35,
      "accelRate": 40.0,
      "decelRate": 60.0,
      "brakeOffsetMs": 450,
      "dwellTimeSec": 12,
      "lightMode": "AUTO",
      "measuredLengthCm": 48
    }
  ],
  "stations": [
    {
      "nodeId": "TRACK_1A2B",
      "name": "Central Station",
      "dwellTimeSec": 10,
      "autoDivertOnOccupied": true,
      "sidingCapacityCm": 65,
      "switches": [
        {
          "switchId": 1,
          "gpioPin": 18,
          "defaultPosition": "STRAIGHT",
          "servoStraightAngle": 75,
          "servoTurnoutAngle": 105,
          "description": "Main Entry Turnout"
        },
        {
          "switchId": 2,
          "gpioPin": 25,
          "defaultPosition": "STRAIGHT",
          "servoStraightAngle": 80,
          "servoTurnoutAngle": 110,
          "description": "Siding Exit Turnout"
        }
      ],
      "beaconCount": 2,
      "beacons": [
        { 
          "beaconId": 10, 
          "gpioPin": 19,
          "role": "ROLE_LOCATOR", 
          "description": "Sector 1 Approach Tracker", 
          "measureTrainLength": true 
        },
        { 
          "beaconId": 11, 
          "gpioPin": 19,
          "role": "ROLE_STATION_ARRIVAL", 
          "description": "Platform 1 Arrival Stop", 
          "measureTrainLength": true 
        }
      ]
    }
  ]
}
```

---

## 🚀 Building Binaries with PlatformIO

### Batch Compilation for Web Flasher
To compile all profiles across all supported chipsets in one command:
```bash
python scripts/build_all_binaries.py
```
This automatically merges and exports all flashable binaries to `tools/web-flasher/binaries/`.

### Individual Environment Flashing via CLI
```bash
# 1. Master Gateway (ESP32)
pio run -e master --target upload
pio run -e master --target uploadfs     # Upload LittleFS Web UI

# 2. Locomotive (ESP32-C3 or ESP8266)
pio run -e loco_c3 --target upload
pio run -e loco_esp8266 --target upload

# 3. Track & Station (ESP32 or ESP8266)
pio run -e track --target upload
pio run -e track_esp8266 --target upload
```

---

## 📱 Mobile Web UI: How to Access & Control Trains from Your Smartphone

The Master Gateway hosts a **100% mobile-friendly, touch-optimized web application** designed specifically for smartphones and tablets.

### 1. Connect to Master Wi-Fi
- Select network: **`LegoTrain_Master`** (Open network by default).

### 2. Open Control Interface
- **Captive Portal:** Tap the *"Sign in to Wi-Fi network"* popup on your phone.
- **mDNS:** Open browser at **`http://legoloco.local`**.
- **Direct IP:** Navigate to **`http://192.168.4.1`**.

### 3. Navigation Tabs
- 🚂 **Locos:** Multi-train throttle cards with battery voltage, block IDs, and quick pairing banner.
- 🔀 **Switches:** Track junctions, servo turnouts, dwell timers, and station pairing banner.
- 🗺️ **Circuit Map (CTC):** Vector SVG schematic synoptic map with real-time train tracking, length-scaled train rendering, and interactive turnouts.
- ⚙️ **Advanced Config:** 3-subtab configuration studio for Global layout settings, Locomotive kinetics & learning runs, and Station beacon assignments.
- 🛰️ **Fleet:** Auto-discovered nodes with RSSI signal, bonding status, and custom renaming.
- 📋 **Logs:** Live protocol message stream.

> [!TIP]
> **Zero Configuration Fallback:** The Master firmware includes an embedded mobile-friendly controller inside PROGMEM flash. Even if flashed without LittleFS data files, the web controller is immediately operational!