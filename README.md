# Modular Lego Train Framework (ESP-lego-loco V2.0)

A high-performance, modular, object-oriented C++ framework for controlling Lego model trains and track layout infrastructure using **ESP32**, **ESP32-S3**, **ESP32-C3**, and **ESP8266**.

---

## 🚂 Key Highlights

- **3 Core Firmware Profiles (Multi-ESP Compatible):**
  - 👑 **Master Gateway & Brain:** ESP32, ESP32-S3
  - 🚂 **Locomotive Mobile Node:** ESP32-C3, ESP8266 (D1 Mini), ESP32
  - 🚉 **Track Switch & Station:** ESP32, ESP8266 (D1 Mini)
- **Web Flasher (Direct Browser USB Programming):**
  - Program any ESP board directly from Google Chrome, Edge, or Opera using the **Web Serial API**.
  - One-click installer with automatic chip detection and partition flashing.
  - Built-in live Serial Monitor (115200 baud) for instant boot diagnostics.
- **Hybrid Network Architecture:**
  - **Master Gateway:** Hosts an async Web Server with real-time WebSockets and coordinates nodes over **ESP-NOW**.
  - **Mobile Locomotives:** Zero-lag motor control via L9110 H-bridge driver with momentum ramping, 3 independent LED zones, and IR beacon track localization.
  - **Infrastructure / Stations:** Servo-controlled track switches (turnouts), IR beam-break presence/occupancy detection, 38kHz beacon transmitters, and 0.96'' OLED departure/ETA displays.
- **Auto-Discovery & Secure Node Pairing:**
  - Nodes derive their identifier directly from their physical MAC address (`LOCO_4B5C`, `TRACK_1A2B`).
  - **Pairing & EEPROM Bonding:** New nodes start in an `UNPAIRED` state. Users discover and pair them to a specific Master Gateway with a single click. Once bonded, nodes write the Master's MAC address to EEPROM and **strictly reject commands from any other Master or layout**, preventing accidental crosstalk in multi-track environments (e.g. clubs or conventions).
  - **Reset / Unpairing:** To unpair a node, simply re-flash its firmware with the *Erase all flash* option checked.
- **Operating Modes:**
  - **Manual Mode:** Full manual control of throttles (-100% to +100%), directional headlights, cab lights, and track switches via the dark-themed Web UI.
  - **Automatic Mode:** Master runs an event-driven **Scenario Manager** state machine using transactional CSV files stored in LittleFS.
  - **CSV Import / Export:** Easily backup, share, or edit automation scenarios directly from your browser.
- **Multi-Layout Isolation & Settings:**
  - Configure the Master's Wi-Fi SSID, Password, and Radio Channel (1-13) directly from the Web UI to ensure clean operation side-by-side with other train layouts.
- **Built-in Safety & Failsafe Watchdog:**
  - Locomotives automatically coast/brake to a stop if Master communication is lost for > 4 seconds.
  - Global Emergency Stop (E-STOP) halts all traction and commands signals to red.
- **Station ETA Computation:**
  - Master dynamically calculates train arrival times (ETA) based on train speed and block progress, broadcasting countdowns to track stations for display on OLED screens.

---

## 🌐 Web Flasher: Direct Browser Firmware Installation

You can program your ESP microcontrollers directly from your browser without installing the Arduino IDE, PlatformIO, or Python drivers!

### Supported Browsers
- Google Chrome (Desktop)
- Microsoft Edge (Desktop)
- Opera / Brave (Chromium-based)

### 3 Available Firmware Images
| Profile | Role | Supported Chips | Merged Binary File |
| :--- | :--- | :--- | :--- |
| 👑 **Master** | Central Gateway, Web UI, Scenario Brain | **ESP32**, **ESP32-S3** | `master_merged.bin`, `master_s3_merged.bin` |
| 🚂 **Loco** | Train Traction (L9110), 3-Zone LEDs, IR RX | **ESP32-C3**, **ESP8266**, **ESP32** | `loco_c3_merged.bin`, `loco_esp8266_merged.bin`, `loco_esp32_merged.bin` |
| 🚉 **Station** | Track Switch (Servo), IR Beacon, OLED | **ESP32**, **ESP8266** | `track_merged.bin`, `track_esp8266_merged.bin` |

### ⚡ Flashing via Online Flasher (e.g. http://esptool.spacehuhn.com/)

You can use third-party online Web Serial flasher tools such as **[esptool.spacehuhn.com](http://esptool.spacehuhn.com/)** or **[Adafruit WebSerial ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/)** to flash your boards directly:

1. Open **[http://esptool.spacehuhn.com/](http://esptool.spacehuhn.com/)** in Google Chrome, Microsoft Edge, or Opera.
2. Connect your ESP to your computer via USB.
3. Click **"Connect"**, select your serial COM port, and set the baud rate to **115200** (or **921600** for fast flashing).
4. Under **Files**, choose the merged `.bin` file from `tools/web-flasher/binaries/`:
   - **Master Gateway (ESP32):** `master_merged.bin`
   - **Master Gateway (ESP32-S3):** `master_s3_merged.bin`
   - **Locomotive (ESP32-C3):** `loco_c3_merged.bin`
   - **Locomotive (ESP8266 D1 Mini):** `loco_esp8266_merged.bin`
   - **Locomotive (ESP32):** `loco_esp32_merged.bin`
   - **Track & Station (ESP32):** `track_merged.bin`
   - **Track & Station (ESP8266):** `track_esp8266_merged.bin`
5. **Flash Address / Offset:** Set the address to **`0x0000`** (or `0x0`). *(Because these are merged binaries containing bootloader, partition table, and application in a single file!)*
6. Click **"Program"** and wait for the progress bar to reach 100%.
7. Once finished, press the **RST** button on your ESP board.

### How to Launch the Built-in Web Flasher
1. **Locally via Web Server:**
   ```bash
   # Start a lightweight local HTTP server
   python3 -m http.server 8080 -d tools/web-flasher
   ```
   Open **`http://localhost:8080`** in Google Chrome or Microsoft Edge.
2. **On GitHub Pages / Web Hosting:**
   Deploy the `tools/web-flasher/` folder to GitHub Pages. The `manifest_*.json` files work automatically with the `<esp-web-install-button>` standard.
3. Connect your ESP via USB cable, click **⚡ Flash Firmware** under your desired profile, select the serial port in the popup dialog, and flashing starts automatically!

---


## 📁 Repository Structure

```
ESP-lego-loco/
├── platformio.ini              # Environments for Master, Loco, and Track across ESP32/S3/C3/8266
├── scripts/
│   ├── merge_bin.py            # PlatformIO post-build hook to create merged web-flasher binaries
│   └── build_all_binaries.py   # Batch script to compile all 3 profiles for web flashing
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
│   ├── ConfigStore/            # LittleFS CSV Scenario Parser & JSON settings
│   ├── ESPNowManager/          # Low-overhead ESP-NOW wrapper, discovery & protocol packets
│   ├── MotorController/        # L9110 PWM driver with momentum ramping & active braking
│   ├── LightingSystem/         # 3-Zone LED controller (Front, Rear, Cab) & auto-direction
│   ├── IRTelemetry/            # 38kHz beacon transmission, decoding & beam-break occupancy
│   ├── TrackManager/           # Servo turnout movement, platform dwell timer & occupancy
│   ├── StationDisplay/         # 0.96'' I2C OLED (128x64 SSD1306) timetable & signal aspects
│   └── WebServer/              # Async Web Server, REST API & WebSockets
├── src/
│   ├── master/main.cpp         # Master Gateway & Brain firmware
│   ├── master/ScenarioEngine.h # Scenario State Machine & ETA calculation
│   ├── loco/main.cpp           # Mobile Locomotive firmware
│   └── track/main.cpp          # Track Switch & Station Platform firmware
└── data/                       # LittleFS Web UI assets (served by Master)
    ├── index.html              # Responsive dispatch dashboard & CSV Studio
    ├── style.css               # Modern dark-mode glassmorphism styling
    ├── app.js                  # WebSocket client & real-time controls
    └── scenarios/              # Default transactional CSV scenarios
        ├── default.csv
        ├── express_station_stop.csv
        └── two_trains_passing_loop.csv
```

---

## ⚡ Hardware Profiles & Pinouts

### 1. Master Gateway (ESP32 / ESP32-S3)
- **Role:** Wi-Fi Access Point / Station Gateway + ESP-NOW Master + Scenario Brain.
- **Wi-Fi:** Default SSID `LegoTrain_Master` (IP: `192.168.4.1` in AP mode).
- **Storage:** LittleFS for static Web UI and scenario `.csv` scripts.

### 2. Locomotive Node (ESP32-C3 or ESP8266 D1 Mini)
| Function | ESP32-C3 Pin | ESP8266 (D1 Mini) | Notes |
| :--- | :--- | :--- | :--- |
| **Motor IA (PWM)** | GPIO 4 | D1 (GPIO 5) | L9110 Driver Input A |
| **Motor IB (PWM)** | GPIO 5 | D2 (GPIO 4) | L9110 Driver Input B |
| **Headlights (Front)** | GPIO 6 | D5 (GPIO 14) | Warm White LEDs (PWM) |
| **Tail Lights (Rear)** | GPIO 7 | D6 (GPIO 12) | Red LEDs (PWM) |
| **Cab / Side Lights** | GPIO 8 | D7 (GPIO 13) | Interior/marker LEDs |
| **IR Receiver** | GPIO 3 | D3 (GPIO 0) | TSOP38238 / TSOP4838 (38kHz) |
| **Battery ADC (Optional)** | GPIO 0 | A0 (0-1V) | 1S LiPo voltage divider |

### 3. Track / Station Node (ESP32 or ESP8266 D1 Mini)
| Function | ESP32 Pin | ESP8266 (D1 Mini) | Notes |
| :--- | :--- | :--- | :--- |
| **Servo Switch** | GPIO 18 | D4 (GPIO 2) | Track Turnout / Switch Servo |
| **IR Beacon TX** | GPIO 19 | D5 (GPIO 14) | 38kHz IR LED (Block code transmitter) |
| **IR Beam-Break RX**| GPIO 23 | D6 (GPIO 12) | Track occupancy detector |
| **OLED SDA** | GPIO 21 | D2 (GPIO 4) | 0.96'' SSD1306 (I2C) |
| **OLED SCL** | GPIO 22 | D1 (GPIO 5) | 0.96'' SSD1306 (I2C) |

---

## 📜 Scenario Manager CSV Specification

Scenarios are stored in LittleFS as plain CSV files with transactional state machine rules:

```csv
STEP_ID, TRIGGER_TYPE, TRIGGER_VALUE, TARGET_NODE, ACTION, PARAMETER
```

### Supported Triggers
| Trigger | Description | Trigger Value Example |
| :--- | :--- | :--- |
| `START` | Executes immediately when the scenario starts | `0` |
| `IR_BEACON` | Triggered when a locomotive passes an IR track beacon | `5` (Beacon ID #5) |
| `TRACK_OCCUPIED` | Triggered when a track sensor / beam is broken | `TRACK_1A2B` or `*` (Any) |
| `TRACK_CLEARED` | Triggered when the train leaves the track block | `TRACK_1A2B` or `*` |
| `TIMER` | Executes after elapsed time from scenario start | `5000` (Milliseconds) |
| `DWELL_COMPLETE` | Triggered when station platform dwell finishes | `TRACK_1A2B` |

### Supported Actions
| Action | Parameter Example | Description |
| :--- | :--- | :--- |
| `SET_SPEED` | `50` (Fwd), `-30` (Rev), `0` (Stop) | Commands locomotive throttle |
| `SET_SWITCH` | `STRAIGHT` or `TURNOUT` | Moves servo track switch |
| `SET_LIGHTS` | `AUTO`, `FRONT_ON`, `ALL_OFF` | Adjusts LED lighting mode |
| `DWELL_WAIT` | `10` (Seconds) | Platform stop with automatic departure |
| `EMERGENCY_STOP`| `0` | Halts all locomotives immediately |
| `GOTO_STEP` | `1` | Loops scenario back to Step #1 |

---

## 🚀 Building Binaries with PlatformIO

### Batch Compilation for Web Flasher
To compile all 3 profiles across all supported chipsets in one command:
```bash
python3 scripts/build_all_binaries.py
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

The Master Gateway hosts a **100% mobile-friendly, touch-optimized web application** designed specifically for smartphones (iPhone, Android) and tablets, with large touch targets, tactile sliders, and a bottom navigation bar.

### 1. Connect to the Master's Wi-Fi
- On your smartphone, tablet, or laptop, open Wi-Fi settings.
- Select the network: **`LegoTrain_Master`** (Open, no password needed by default).

### 2. Open the Control Interface
- **Method 1 (Automatic Captive Portal):** On most smartphones (iOS / Android), a *"Sign in to Wi-Fi network"* prompt will appear automatically upon connecting. Tap it to immediately launch the controller!
- **Method 2 (mDNS Domain):** Open your phone's browser (Safari, Chrome, Firefox) and go to:
  ```
  http://legoloco.local
  ```
- **Method 3 (Direct IP):** If mDNS is unsupported on your device, type:
  ```
  http://192.168.4.1
  ```
- **Method 4 (Home Network Mode):** If configured in Station mode via `config/settings.json`, the Master joins your home Wi-Fi router. Simply navigate to `http://legoloco.local` from any device connected to your home Wi-Fi.

### 3. Smartphone Touch Controls
- **Tactile Throttle Slider:** Drag your thumb smoothly to adjust train power from **-100% (Reverse)** to **+100% (Forward)**.
- **Quick-Tap Presets:** Instantly command **`STOP`**, **`FWD 50%`**, **`REV 50%`** with large thumb buttons.
- **Always-Accessible Emergency Stop (E-STOP):** Prominent red button at the top/bottom to freeze all trains immediately.
- **Lighting Switches:** Single-tap buttons for Auto-Directional lighting, Headlights, and Cab illumination.
- **Track Turnout Toggles:** Switch track servos between **`STRAIGHT`** and **`TURNOUT`** with visual diagram feedback.
- **Mobile Bottom Navigation Bar:** Quickly jump between:
  - 🚂 **Locos:** Multi-train throttle cards with battery voltage, block IDs, and quick pairing banner.
  - 🔀 **Switches:** Track junctions, servo turnouts, dwell timers, and station pairing banner.
  - 📜 **Scenarios:** Visual state machine editor for automated routes with CSV import/export.
  - 🛰️ **Fleet:** Auto-discovered nodes with signal strength (RSSI), bonding status, and custom renaming.
  - ⚙️ **Settings:** Access Point SSID, Wi-Fi password, and radio channel configuration.
  - 📋 **Logs:** Live protocol message stream.

> [!TIP]
> **Zero Configuration Fallback:** The Master firmware includes an embedded mobile-friendly controller inside PROGMEM flash. Even if you flash only `master_merged.bin` via `http://esptool.spacehuhn.com/` without separately uploading the LittleFS data files, the web controller is immediately operational!