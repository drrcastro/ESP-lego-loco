# Requirements Specification: Modular Lego Train Framework (ESP-lego-loco) - V2.0

> **Document Status:** Approved & Consolidated  
> **System Version:** 2.0.0  
> **Core Architecture:** Modular Object-Oriented C++ (PlatformIO)  
> **Supported Microcontrollers:** ESP32, ESP32-S3, ESP32-C3, ESP8266  

---

## 1. Architecture Overview and Operating Modes

The system implements a hybrid wireless network combining **Wi-Fi** (for mobile/desktop web dispatch and real-time WebSocket telemetry) and **ESP-NOW** (for ultra-fast, deterministic, low-latency communication between microcontrollers), coordinated centrally by a dedicated **Master Gateway** node.

### 1.1. Operating Modes
1. **Manual Mode:**
   - The user individually controls train speed (-100% to +100%), braking, 3-zone illumination, and track turnout switches via the mobile or desktop web interface.
   - Real-time bidirectional responsiveness via WebSockets.
2. **Automatic Mode:**
   - The Master coordinates traffic using an event-driven **Scenario Manager** state machine executing step-by-step actions defined in CSV files stored in LittleFS.
   - Automatic speed regulation and collision prevention via block occupancy detection.
   - Automated station dwell time countdowns and departure signaling.

---

## 2. Hardware Profiles and Responsibilities

The ecosystem is partitioned into 3 functional profiles with dedicated firmware images:

```
                      ┌──────────────────────────────────────┐
                      │       MASTER GATEWAY & BRAIN         │
                      │         (ESP32 / ESP32-S3)           │
                      │  - Async Web Server & WebSockets     │
                      │  - LittleFS CSV Scenario Engine      │
                      │  - Station ETA Dynamic Computation   │
                      │  - Fleet Topology & Pairing Authority│
                      └──────────────────┬───────────────────┘
                                         │ ESP-NOW (Synchronized Channel)
                 ┌───────────────────────┴───────────────────────┐
                 ▼                                               ▼
   ┌───────────────────────────┐                   ┌───────────────────────────┐
   │     LOCOMOTIVE (MOBILE)   │                   │    TRACK / STATION (FIXED)│
   │  (ESP32-C3/ESP8266/ESP32) │                   │      (ESP32/ESP8266)      │
   │ - L9110 PWM Motor Driver  │                   │ - Servo Turnout Switch    │
   │ - 3-Zone PWM LED Lighting │                   │ - 38kHz IR Beacon TX      │
   │ - Track Beacon IR Receiver│                   │ - Beam-Break Occupancy RX │
   │ - Failsafe Safety Watchdog│                   │ - 0.96'' I2C OLED Display │
   └───────────────────────────┘                   └───────────────────────────┘
```

### 2.1. Master Gateway & Brain (ESP32 / ESP32-S3)
- **Connectivity:**
  - Autonomous Wi-Fi Access Point (SoftAP `LegoTrain_Master`) and Station mode (STA) for home network integration.
  - DNS Server with Captive Portal for instant mobile redirection upon Wi-Fi connection.
  - mDNS responder for friendly domain access at `http://legoloco.local`.
  - ESP-NOW coordinator operating on the same synchronized Wi-Fi channel.
- **Scenario Manager:**
  - Full CRUD (Create, Read, Update, Delete) and web-based import/export of `.csv` scenario files in LittleFS.
  - Transactional step execution, multi-condition triggers, and loop jumps (`GOTO_STEP`).
- **Traffic Logic & ETA Prediction:**
  - Continuous calculation of train Estimated Time of Arrival (ETA) to stations based on speed and track block positions.
  - Periodic broadcast/unicast transmission of ETAs over ESP-NOW to station OLED displays.
- **Fleet Registry:**
  - Real-time in-memory tracking of all registered nodes, online/offline status, signal strength (RSSI), battery voltage, and bonding status.

### 2.2. Locomotive (Mobile Node - ESP32-C3, ESP8266, or ESP32)
- **Traction:**
  - Bidirectional PWM motor control tailored for the **L9110** H-bridge (IA and IB pins).
  - Progressive acceleration and deceleration curves simulating train mass and momentum to prevent derailments.
  - Static friction deadband compensation.
  - Active dynamic electrical braking (both motor inputs pulled HIGH) and coasting.
- **Failsafe Watchdog:**
  - 4-second safety watchdog: if communication with the Master is lost for > 4 seconds, the locomotive automatically ramps down speed to a full stop.
- **Illumination (3 Independent Zones):**
  - **Zone 1 (Front):** Warm white headlights.
  - **Zone 2 (Rear):** Red tail marker lights.
  - **Zone 3 (Cab / Interior):** Cab interior illumination and shunting lights.
  - Supported modes: Manual, Auto-Directional (automatic headlight/taillight switching matching forward/reverse direction), Shunting, and Hazard Flashing.
- **Localization:**
  - Downward-facing 38kHz IR receiver (TSOP38238 or compatible). Decodes block beacon IDs transmitted by track beacons and immediately reports block telemetry to the Master.
- **Battery Telemetry:** Analog LiPo battery voltage sensing with periodic reporting in millivolts.
- **Serial Ecosystem Diagnostics & Telemetry Logging (115200 Baud):**
  - **TX Logs:** Periodic telemetry reporting (`[Loco TX -> Bonded Master] Telemetry: Spd=X%, Dir=..., Block=#..., Bat=...mV`), presence announcements, and discovery replies.
  - **RX Logs:** Decoded incoming commands (`[Loco RX <- Master] MSG_LOCO_COMMAND: Target=..., Spd=..., Brake=..., Lights=...`), emergency stops, and track beacon events (`[Loco RX-IR] Track Beacon Detected: Block #...`).
  - **Security Logs:** Explicit warnings when commands are rejected due to unpaired status or unauthorized transmitter MAC addresses.

### 2.3. Track & Station (Fixed Infrastructure - ESP32 or ESP8266)
- **Localization & Occupancy Beacons (IR):**
  - 38kHz IR transmitter continuously broadcasting the encoded block ID (NEC protocol).
  - Beam-break IR receiver mounted across the rails opposite the transmitter: immediate "Track Occupied" signal when beam is interrupted by a train, and "Track Cleared" when beam is restored.
- **Track Switches (Turnouts):**
  - High-precision servo angular control for switching between `STRAIGHT` and `TURNOUT` routes.
  - Smooth interpolated movement with automatic post-motion servo detach to eliminate jitter, hum, heating, and power drain.
- **Station Display:**
  - 0.96'' OLED display (128x64 pixels via I2C, SSD1306).
  - Displays: Station Name, Expected Train, ETA Countdown in `mm:ss`, Track Occupancy status, and 3-aspect visual railway signal (Green, Yellow, Red).
- **Station Dwell Logic:**
  - Autonomous execution of scheduled station stops (*Dwell Time*) with visual on-screen countdown and departure notification.
- **Serial Ecosystem Diagnostics & Event Logging (115200 Baud):**
  - **TX Logs:** Periodic telemetry reporting (`[Track TX -> Bonded Master] Telemetry: Switch=..., Beam=..., BeaconCode=#...`) and dwell completion notifications.
  - **RX Logs:** Incoming switch commands (`[Track RX <- Master] MSG_TRACK_COMMAND: Target=..., Switch=..., DwellTime=...`), station ETA broadcasts with signal aspect, and pairing confirmations.
  - **Event Logs:** Real-time logging of physical switch servo transitions and IR beam-break occupancy state changes.

---

## 3. Node Discovery, Pairing, and Bonding Protocol (Locomotives & Stations)

### 3.1. Unique Hardware Identification (MAC)
Firmware images are shared across node profiles; unique identifiers are automatically derived from the device's physical Wi-Fi MAC address:
- **Locomotives:** `LOCO_XXXX` (e.g., `LOCO_4B5C`)
- **Infrastructure / Stations:** `TRACK_XXXX` (e.g., `TRACK_1A2B`)
- **Master Gateway:** `MASTER_XXXX` (e.g., `MASTER_C001`)

### 3.2. Node States (Unpaired vs Bonded)
The ecosystem enforces a strict security policy based on persistent EEPROM bonding:

1. **"Unpaired" State (Fresh Flash / Factory Default):**
   - Default state immediately following initial flashing with memory erase (*Erase Flash*).
   - **Locomotive:** Headlights and cab lights pulse gently in an alternating pattern to indicate standby mode. Motor commands (`MSG_LOCO_COMMAND`) are strictly rejected until the locomotive is paired with a Master, preventing runaway train accidents.
   - **Station / Infrastructure:** The 0.96'' OLED display shows `STATUS: UNPAIRED` and `AWAITING PAIRING` with a yellow signal aspect. Servo turnout commands (`MSG_TRACK_COMMAND`) are rejected to avoid inadvertent switch derailments.
   - Both nodes respond to discovery scans (`MSG_DISCOVERY_SCAN`) by emitting `MSG_DISCOVERY_ANNOUNCE` with `isPaired = 0`.

2. **"Bonded" State (Paired):**
   - The node permanently saves the Master Gateway's physical MAC address to EEPROM flash.
   - **Locomotive:** Confirms bonding with 3 rapid headlight flashes and switches immediately to standard directional lighting.
   - **Station / Infrastructure:** OLED display immediately updates to `STATUS: BONDED` and shows `MASTER BONDED` with a green signal aspect.
   - **Strict MAC Filtering:** The locomotive and station accept **exclusively** control packets (`MSG_LOCO_COMMAND`, `MSG_TRACK_COMMAND`, `MSG_STATION_ETA_BROADCAST`, `MSG_EMERGENCY_STOP`) originating from the bonded Master's MAC address.
   - All packets from unauthorized transmitters or adjacent layout Masters are dropped at the radio receiver level.

### 3.3. Pairing Procedure via Web UI
The process for pairing any new node (Locomotive or Station) to the Master Gateway:
1. **Power On Node:** Power the locomotive or station. The node enters unpaired standby mode.
2. **Open Master Web UI:** On your phone or PC, navigate to `http://192.168.4.1` or `http://legoloco.local`.
3. **Click "Scan for Devices" (or "Scan for Locomotives / Stations"):** The Master broadcasts an ESP-NOW scan (`MSG_DISCOVERY_SCAN`) prompting all nodes on the channel to announce themselves.
4. **Select Node to Pair:** The Web UI displays the discovered node in a highlighted banner with its type (🚂 Locomotive or 🚉 Station) and a **"Pair to this Master"** button.
5. **Confirmation & Registration:** Clicking the button causes the Master to send a `MSG_PAIR_CONFIRM` packet with its MAC and validation credentials. The node commits the pairing to EEPROM, provides visual confirmation (rapid LED flashes or OLED update), and begins accepting commands.

### 3.4. Multi-Track Layout Safety Isolation
- In exhibitions, clubs, or shared spaces with multiple independent Lego train circuits, this bonding prevents an operator on Track A from accidentally triggering turnouts or accelerating locomotives on Track B.
- A station or turnout bonded to Master B will **never** actuate upon emergency stop or switch commands issued by Master A, even if operating on the exact same radio channel.

### 3.5. Mandatory Reset & Unpairing Procedure (Reset via Reflash)
For strict operational security, **there is no remote Wi-Fi or ESP-NOW command to unpair a node**.  
To unpair a locomotive or station and transfer it to a different Master Gateway:
1. **Connect via USB:** Plug the ESP microcontroller into a computer USB port.
2. **Reflash with Flash Erase:**
   - **Option A (Web Flasher):** Open the Web Serial flasher at `http://localhost:8080` (or GitHub Pages), select the firmware profile, and ensure the checkbox **"Erase all flash before writing (Required to reset locomotive / station pairing)"** is checked before flashing.
   - **Option B (PlatformIO CLI):** Run in terminal:
     ```bash
     # For Locomotive (e.g. ESP32-C3):
     pio run -e loco_c3 -t erase
     pio run -e loco_c3 -t upload

     # For Station (e.g. ESP32):
     pio run -e track -t erase
     pio run -e track -t upload
     ```
3. On the subsequent boot, the EEPROM is clean (`isPaired = 0`), and the node returns to initial unpaired standby mode ready to be bonded to a new Master.

### 3.6. Custom Friendly Name Mapping
- The Master persists custom user-defined names in `/config/nodes.json` (e.g., `LOCO_4B5C` -> *"Orient Express"*, `TRACK_1A2B` -> *"Central Station - Platform 1"*). Names can be edited at any time in the Web UI.

### 3.7. Live Serial Diagnostics and Protocol Transparency
To support live system monitoring, debugging, and verification via the Web Flasher terminal monitor or USB serial consoles (at 115200 baud), all nodes must format their standard I/O traffic with standardized tags:
- **Outgoing Transmissions (`[Node TX -> Destination]`):**
  - Identifies destination (`Bonded Master`, `Master`, or `Broadcast`).
  - Summarizes key telemetry metrics (Speed, Direction, Block ID, Battery, Switch, Beam Break).
- **Incoming Commands (`[Node RX <- Source]`):**
  - Displays transmitter MAC address, command type, target ID, and decoded parameters.
  - Decodes emergency stops and safety overrides immediately.
- **Physical Events (`[Node Event]` / `[Node RX-IR]`):**
  - Logs track beacon detections (`Track Beacon Detected: Block #X`).
  - Logs IR beam interruptions and restoration in real time.
- **Security & Rejection Traces (`[Node RX]`):**
  - Logs packets rejected due to unpaired status or unauthorized transmitter MACs, aiding in troubleshooting multi-layout isolation.

---

## 4. Modular Project Architecture (PlatformIO)

```plaintext
ESP-lego-loco/
├── platformio.ini              # Environments for Master, Loco, and Track across ESP32/S3/C3/8266
├── lib/
│   ├── ConfigStore/            # LittleFS CSV Scenario Parser & JSON settings
│   ├── ESPNowManager/          # Low-overhead ESP-NOW wrapper, discovery, pairing & packets
│   ├── MotorController/        # L9110 PWM driver with momentum ramping & active braking
│   ├── LightingSystem/         # 3-Zone LED controller (Front, Rear, Cab) & auto-direction
│   ├── IRTelemetry/            # 38kHz beacon transmission, decoding & beam-break occupancy
│   ├── TrackManager/           # Servo turnout movement, platform dwell timer & occupancy
│   ├── StationDisplay/         # 0.96'' I2C OLED (128x64 SSD1306) timetable & signal aspects
│   └── WebServer/              # Async Web Server, REST API, WebSockets & fallback UI
├── src/
│   ├── master/main.cpp         # Master Gateway & Brain firmware entry point
│   ├── master/ScenarioEngine.h # Scenario State Machine & ETA calculation
│   ├── master/ScenarioEngine.cpp
│   ├── loco/main.cpp           # Mobile Locomotive firmware entry point
│   └── track/main.cpp          # Track Switch & Station Platform firmware entry point
├── data/                       # LittleFS Web UI assets (served by Master)
│   ├── index.html              # Responsive dispatch dashboard & CSV Studio
│   ├── style.css               # Modern dark-mode glassmorphism styling
│   ├── app.js                  # WebSocket client & real-time controls
│   └── scenarios/              # Default transactional CSV scenarios
├── scripts/
│   ├── merge_bin.py            # PlatformIO post-build hook to create merged web-flasher binaries
│   └── build_all_binaries.py   # Batch script to compile all profiles for web flashing
├── tools/
│   └── web-flasher/            # Standalone Web Serial Flasher & Terminal Monitor
│       ├── index.html          # Web flasher UI with 3 profile presets
│       ├── flasher.js          # Web Serial API connection, flashing & logging logic
│       ├── manifest_*.json     # Manifests for esp-web-tools
│       └── binaries/           # Output directory for compiled merged .bin images
```

---

## 5. Scenario Manager Data Format (CSV)

The CSV format for exporting and importing scenarios follows an event-driven state machine structure:

```csv
STEP_ID, TRIGGER_TYPE, TRIGGER_VALUE, TARGET_NODE, ACTION, PARAMETER
```

### 5.1. Triggers (TRIGGER_TYPE)
| Trigger | Description | Trigger Value Example |
| :--- | :--- | :--- |
| `START` | Executes immediately when the scenario begins | `0` |
| `IR_BEACON` | Triggered when a locomotive passes over an IR track beacon | `5` (Beacon ID #5) |
| `TRACK_OCCUPIED` | Triggered when an optical beam-break sensor is interrupted | `TRACK_1A2B` or `*` (Any) |
| `TRACK_CLEARED` | Triggered when the train clears the track sensor | `TRACK_1A2B` or `*` |
| `TIMER` | Triggered after elapsed milliseconds from scenario start | `5000` (Milliseconds) |
| `DWELL_COMPLETE` | Triggered when platform dwell timer completes | `TRACK_1A2B` |

### 5.2. Actions (ACTION)
| Action | Parameter Example | Description |
| :--- | :--- | :--- |
| `SET_SPEED` | `50`, `-30`, `0` | Sets target speed (-100% to +100%) for target locomotive |
| `SET_SWITCH` | `STRAIGHT`, `TURNOUT` | Commands turnout switch position |
| `SET_LIGHTS` | `AUTO`, `FRONT_ON`, `ALL_OFF` | Configures locomotive lighting mode |
| `DWELL_WAIT` | `10` | Starts 10-second station stop with OLED countdown |
| `EMERGENCY_STOP` | `0` | Immediate emergency halt of all trains |
| `GOTO_STEP` | `1` | Loops execution back to designated step ID |

### 5.3. Scenario Import & Export (Web UI)
- **Direct Export (.CSV):** Downloads the currently active scenario from the Master's LittleFS to the client PC/smartphone via browser Blob.
- **Direct Import (.CSV):** Uploads local `.csv` files directly to the Master's LittleFS filesystem, validates syntax, and updates the scenario dropdown instantly.

---

## 6. Web Flashing and Unified Firmware Binaries (.bin)

To facilitate flashing without local compiler or toolchain installation, the project provides **merged single-file binaries**:

### 6.1. Official Unified Firmware Images
1. **Master Gateway:**
   - ESP32 Standard: `master_merged.bin`
   - ESP32-S3: `master_s3_merged.bin`
2. **Locomotive (Loco):**
   - ESP32-C3: `loco_c3_merged.bin`
   - ESP8266 (D1 Mini): `loco_esp8266_merged.bin`
   - ESP32 Standard: `loco_esp32_merged.bin`
3. **Track & Station:**
   - ESP32 Standard: `track_merged.bin`
   - ESP8266 (D1 Mini): `track_esp8266_merged.bin`

### 6.2. Web Serial Flasher Compatibility
- All `_merged.bin` files bundle the bootloader, partition table, boot app0, and application into a single contiguous binary.
- **Flash Address / Offset:** Always **`0x0000`** (or `0x0`).
- Compatible with Chrome, Edge, and Opera via the native Web Serial API.

---

## 7. Master Web UI and Mobile-Friendly Dispatch Controller

The web interface served by the Master Gateway is optimized for mobile touch ergonomics:

### 7.1. Connectivity & Mobile Access
1. **Wi-Fi Network:** Master broadcasts SSID **`LegoTrain_Master`** (open by default).
2. **Captive Portal:** Connecting a smartphone triggers an automatic sign-in prompt directing straight to the dispatch controller.
3. **Direct Navigation:**
   - Via mDNS: **`http://legoloco.local`**
   - Via Static Gateway IP: **`http://192.168.4.1`**
4. **Home Network Mode (STA):** If configured to connect to a home Wi-Fi router, accessible via `http://legoloco.local` from any device on the local network.

### 7.2. Mobile Ergonomics
- **Thumb Throttle Slider:** Large touch-friendly slider (-100% to +100%) with debounced updates.
- **Quick-Tap Presets:** Natural button order matching slider direction — `REV 50%` on left, `STOP` centered, `FWD 50%` on right.
- **Permanent E-STOP Button:** Red emergency stop button accessible at the top header and on mobile ribbons.
- **Bottom Navigation Bar:** One-handed switching between 🚂 Locomotives, 🔀 Switches, 📜 Scenarios, 🛰️ Fleet, ⚙️ Settings, and 📋 Logs.
- **Embedded PROGMEM Fallback Controller:** If flashed without the LittleFS web assets, the Master serves an autonomous embedded web controller from flash memory with full traction, turnout, pairing, scenario import/export, and AP settings capabilities.

### 7.3. Network Configuration & Multi-Layout Isolation Panel
- **SoftAP Customization:**
  - **Customizable SSID:** Assign distinct names (e.g., `LegoTrain_Layout1`, `LegoTrain_Layout2`) to prevent confusion in public exhibitions.
  - **Wi-Fi Channel Selection (1 to 13):** Isolate multiple layouts operating in the same venue by assigning them to non-overlapping channels (e.g. Channel 1 vs Channel 6 vs Channel 11).
  - **Wi-Fi Security:** Configurable open network or WPA2-PSK password (8 to 63 characters).
  - **Persistence & Reboot:** Saved atomically to `/config/settings.json` with controlled reboot via `/api/restart`.

---

## 8. Pinout Mapping by Microcontroller

### Locomotive (Mobile Node)
| Function | ESP32-C3 | ESP8266 (D1 Mini) | ESP32 DevKit |
| :--- | :--- | :--- | :--- |
| Motor L9110 (IA) | GPIO 4 | D1 (GPIO 5) | GPIO 18 |
| Motor L9110 (IB) | GPIO 5 | D2 (GPIO 4) | GPIO 19 |
| Headlights (Front) | GPIO 6 | D5 (GPIO 14) | GPIO 21 |
| Taillights (Rear) | GPIO 7 | D6 (GPIO 12) | GPIO 22 |
| Cab / Marker Lights | GPIO 8 | D7 (GPIO 13) | GPIO 23 |
| Track IR Receiver | GPIO 3 | D3 (GPIO 0) | GPIO 15 |
| Battery ADC | GPIO 0 | A0 (0-1V) | GPIO 34 |

### Track & Station (Fixed Node)
| Function | ESP32 DevKit | ESP8266 (D1 Mini) |
| :--- | :--- | :--- |
| Servo Turnout Switch | GPIO 18 | D4 (GPIO 2) |
| 38kHz IR Beacon (TX) | GPIO 19 | D5 (GPIO 14) |
| Beam-Break Sensor (RX)| GPIO 23 | D6 (GPIO 12) |
| OLED Display (SDA) | GPIO 21 | D2 (GPIO 4) |
| OLED Display (SCL) | GPIO 22 | D1 (GPIO 5) |

---

## 9. Approved Core Libraries
- `bblanchon/ArduinoJson` (v7.x)
- `ESPAsyncWebServer` & `AsyncTCP`
- `z3t0/IRremote` (ESP32) & `crankyoldgit/IRremoteESP8266` (ESP8266)
- `adafruit/Adafruit_SSD1306` & `adafruit/Adafruit_GFX`
- `madhephaestus/ESP32Servo` (ESP32) & `Servo.h` (ESP8266)
