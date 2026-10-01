# Requirements Specification: Modular Lego Train Framework (ESP-lego-loco)

> **Document Status:** Approved & Consolidated  
> **Core Architecture:** Modular Object-Oriented C++ (PlatformIO)  
> **Supported Microcontrollers:** ESP32, ESP32-S3, ESP32-C3, ESP8266  

---

## 1. Architecture Overview and Operating Modes

The system implements a hybrid wireless network combining **Wi-Fi** (for mobile/desktop web dispatch and real-time WebSocket telemetry) and **ESP-NOW** (for ultra-fast, deterministic, low-latency communication between microcontrollers). Control intelligence is balanced between supervisory coordination by the **Master Gateway** and autonomous edge execution on **Locomotives** and **Track Stations**.

### 1.1. Operating Modes
1. **Manual Mode:**
   - The user individually controls train speed (-100% to +100%), braking, 3-zone illumination, and track turnout switches via the mobile or desktop web interface.
   - Real-time bidirectional responsiveness via WebSockets.
2. **Autonomous Distributed Mode (Edge Train Learning & Smart Station Traffic Regulation):**
   - **Autonomous Locomotives:** Each train executes an initial **Learning Lap** to map the track circuit, sequence of IR beacons, and inter-station travel times. After calibration, the locomotive dynamically governs its own cruising speed, acceleration curves, and precision braking points.
   - **Smart Track Stations:** Fixed stations autonomously manage turnout servos to route trains and act as decentralized traffic dispatchers. Stations monitor the headway (time/distance separation) between consecutive locomotives; if two trains approach too closely, the station dynamically trims their speeds or holds the trailing train to prevent collisions without requiring Master intervention.
   - **Master SCADA & Parameter Management:** The Master provides global visibility, Web UI dispatching, and a centralized **Advanced Configuration Menu** (with dedicated tabs for Global, per-Locomotive, and per-Station parameters including multi-beacon allocation), eliminating rigid CSV script files.

---

## 2. Hardware Profiles and Responsibilities

The ecosystem is partitioned into 3 functional profiles with dedicated firmware images:

```
                      ┌──────────────────────────────────────┐
                      │       MASTER GATEWAY & SUPERVISOR    │
                      │         (ESP32 / ESP32-S3)           │
                      │  - Async Web Server & WebSockets UI  │
                      │  - Multi-Tab Advanced Config Center  │
                      │  - Auto-Generated Circuit Map (SVG)  │
                      │  - Fleet Topology & Global E-STOP    │
                      └──────────────────┬───────────────────┘
                                         │ ESP-NOW (Synchronized Channel)
                 ┌───────────────────────┴───────────────────────┐
                 ▼                                               ▼
   ┌───────────────────────────┐                   ┌───────────────────────────┐
   │    AUTONOMOUS LOCOMOTIVE  │  ESP-NOW Direct   │    SMART TRACK / STATION  │
   │  (ESP32-C3/ESP8266/ESP32) │◄─────────────────►│      (ESP32/ESP8266)      │
   │ - Circuit Learning Lap    │ Headway & Speed   │ - Autonomous Turnout Servo│
   │ - Dynamic Speed & Braking │ Regulation        │ - Multi-Beacon Management │
   │ - Inter-Beacon Time Map   │                   │ - Inter-Train Headway     │
   │ - Precision Station Stop  │                   │ - Anti-Collision Dispatch │
   │ - L9110 PWM Motor Driver  │                   │ - IR Beam-Break Occupancy │
   │ - 3-Zone PWM LED Lights   │                   │ - 38kHz IR Beacon TX      │
   │ - Local ETA Computation   │                   │ - 0.96'' I2C OLED Display │
   │ - Battery Telemetry       │                   │ - Platform Dwell Control  │
   └───────────────────────────┘                   └───────────────────────────┘
```

### 2.1. Master Gateway & Supervisor (ESP32 / ESP32-S3)
- **Connectivity:**
  - Autonomous Wi-Fi Access Point (SoftAP `LegoTrain_Master`) and Station mode (STA) for home network integration.
  - DNS Server with Captive Portal for instant mobile redirection upon Wi-Fi connection.
  - mDNS responder for friendly domain access at `http://legoloco.local`.
  - ESP-NOW coordinator operating on the same synchronized Wi-Fi channel.
- **Supervisory Dispatch & Fleet Registry:**
  - Real-time in-memory tracking of all registered nodes, online/offline status, signal strength (RSSI), battery voltage, and bonding status.
  - Global emergency stop broadcast (`MSG_EMERGENCY_STOP`) overriding all autonomous local behaviors immediately.
- **Advanced Configuration Management (Replaces Legacy CSV Scenarios):**
  - Completely deprecates static CSV scenarios in favor of dynamic, fine-grained parameter configuration via Web UI.
  - Hierarchical JSON persistence in LittleFS (`/config/config.json`) with live broadcast to nodes over ESP-NOW.
  - Three specialized configuration tabs: **Global / System**, **Per-Locomotive**, and **Per-Station**.
- **Circuit Map Auto-Generation & CTC Live Dispatcher:**
  - Aggregates topological segment data reported by locomotives (learned beacon sequences and inter-beacon transit times) and station turnout states.
  - Auto-generates a dynamic, interactive vector schematic diagram of the track layout in the Web UI.
  - Provides real-time train tracking and signal aspect visualization along the generated schematic tracks.

### 2.2. Autonomous Locomotive (Mobile Node - ESP32-C3, ESP8266, or ESP32)
- **Autonomous Circuit Learning Lap:**
  - Self-calibration mode: on command, the train performs a reconnaissance lap at constant calibration speed.
  - Automatically identifies and indexes the sequence of IR track beacons and records transit times ($T_{\text{segment}}$) between consecutive beacons/stations.
  - Stores the learned circuit profile and inter-beacon benchmarks in non-volatile memory (EEPROM/Flash).
- **Dynamic Speed & Kinetic Profile Management:**
  - Self-governed momentum ramping: calculates optimal acceleration and deceleration profiles tailored to the learned circuit.
  - Autonomous precision braking: triggers deceleration based on the learned inter-beacon timings to come to a smooth, exact stop at station platforms.
  - Dynamic speed adjustment: continuously adjusts cruising speed to adhere to target schedule and responds immediately to speed trim/hold orders issued by smart track stations.
  - Local ETA calculation: computes real-time ETA to the upcoming station stop and includes it in outgoing telemetry.
- **Traction & Motor Control:**
  - Bidirectional PWM motor control tailored for the **L9110** H-bridge (IA and IB pins).
  - Progressive acceleration and deceleration curves simulating train mass and momentum to prevent derailments.
  - Static friction deadband compensation.
  - Active dynamic electrical braking (both motor inputs pulled HIGH) and coasting.
- **Failsafe Watchdog:**
  - 4-second safety watchdog: if communication with the network is lost for > 4 seconds, the locomotive automatically ramps down speed to a full stop.
- **Illumination (3 Independent Zones):**
  - **Zone 1 (Front):** Warm white headlights.
  - **Zone 2 (Rear):** Red tail marker lights.
  - **Zone 3 (Cab / Interior):** Cab interior illumination and shunting lights.
  - Supported modes: Manual, Auto-Directional (automatic headlight/taillight switching matching forward/reverse direction), Shunting, and Hazard Flashing.
- **Localization:**
  - Downward-facing 38kHz IR receiver (TSOP38238 or compatible). Decodes block beacon IDs transmitted by track beacons and immediately reports block telemetry.
- **Battery Telemetry:** Analog LiPo battery voltage sensing with periodic reporting in millivolts.
- **Serial Ecosystem Diagnostics & Telemetry Logging (115200 Baud):**
  - **TX Logs:** Periodic telemetry reporting (`[Loco TX -> Bonded Master] Telemetry: Spd=X%, Dir=..., Block=#..., Bat=...mV`), presence announcements, and discovery replies.
  - **RX Logs:** Decoded incoming commands (`[Loco RX <- Master] MSG_LOCO_COMMAND: Target=..., Spd=..., Brake=..., Lights=...`), smart station speed adjustments (`[Loco RX <- Station] MSG_TRAFFIC_REGULATION: SpeedTrim=...`), emergency stops, and track beacon events (`[Loco RX-IR] Track Beacon Detected: Block #...`).
  - **Security Logs:** Explicit warnings when commands are rejected due to unpaired status or unauthorized transmitter MAC addresses.

### 2.3. Smart Track & Station (Fixed Infrastructure - ESP32 or ESP8266)
- **Multi-Turnout Switch Control & Route Management:**
  - High-precision servo angular control for multiple independent turnouts (e.g. entry switch, crossover, passing loop, yard ladders) driven by a single station controller.
  - Each turnout is explicitly identified by a **Switch ID** (1..N) and an assigned **PWM Servo GPIO Pin**, with independent straight and turnout calibration angles (`servoStraightAngle`, `servoTurnoutAngle`).
  - Autonomous switching logic based on traffic direction, platform occupancy, and pre-programmed routing policies (e.g. diverting a trailing train to a passing loop if the main line platform is occupied).
  - Hardware interlocking safety: turnout actuation is strictly locked while the beam-break sensor indicates train occupancy over the switch points (*Tail Clearance Interlock*).
- **Decentralized Traffic Regulation & Anti-Collision Headway:**
  - Real-time headway monitoring: tracks arrival timestamps and passage intervals between multiple trains across the station block.
  - Anti-collision speed regulation: when two locomotives are detected with headway below safety thresholds, the station directly transmits speed adjustments over ESP-NOW to the trailing locomotive (e.g. reduce throttle by 30%, command cautious approach, or hold at signal) to maintain safe separation.
  - Direct signal aspect control on the 0.96'' OLED display (Green = Clear, Yellow = Reduced Speed / Spacing Trim, Red = Stop / Block Occupied).
- **Station Arrival & Localization Beacons with Explicit GPIO Mapping (IR):**
  - Each beacon has an **explicitly configured GPIO pin** (e.g. IR LED modulation output or optical beam-break detector input) alongside its unique numeric **Beacon ID**.
  - **Single Station Arrival Beacon (`ROLE_STATION_ARRIVAL`):** Located at the station platform itself. Detects when the locomotive has reached its stopping position, triggering the platform dwell sequence and OLED countdown.
  - **Localizer Beacons (`ROLE_LOCATOR`):** Waypoint beacons distributed along open track sectors or block boundaries solely to track locomotive position and progress without forcing a stop.
  - **Automatic Train Length Measurement:**
    - Beacons / beam sensors measure the exact occlusion interval between when the front of the train breaks the beam/sensor ($t_{\text{block}}$) and when the tail/last wagon unblocks it ($t_{\text{unblock}}$):
      $$\Delta t_{\text{block}} = t_{\text{unblock}} - t_{\text{block}}$$
    - By correlating $\Delta t_{\text{block}}$ with the locomotive's reported instantaneous velocity $v$ (calibrated in cm/s during the learning lap), the system automatically calculates physical train length:
      $$L_{\text{train}} = v \times \Delta t_{\text{block}}$$
    - **Operational Value:** Automatically detects whether a locomotive is running solo or pulling wagons, prevents turnouts from switching before the tail clears (*Tail Clearance*), and verifies whether a train physically fits into a passing loop siding before diverting.
- **Station Display & Dwell Logic:**
  - 0.96'' OLED display (128x64 pixels via I2C, SSD1306) showing: Station Name, Expected Train, ETA Countdown, Headway/Signal Aspect, and Track Switch Position.
  - Autonomous execution of scheduled station stops (*Dwell Time*) with visual on-screen countdown and automated departure clearance.
- **Serial Ecosystem Diagnostics & Event Logging (115200 Baud):**
  - **TX Logs:** Periodic telemetry reporting (`[Track TX -> Bonded Master] Telemetry: Switch=..., Beam=..., BeaconCode=#...`), traffic regulation commands (`[Track TX -> Loco] MSG_TRAFFIC_REGULATION: Loco=..., TrimSpd=...`), train length logs (`[Track Sensor] Measured Train Length: 48cm (dt=1200ms, v=40cm/s)`), and dwell completion notifications.
  - **RX Logs:** Incoming switch commands (`[Track RX <- Master] MSG_TRACK_COMMAND: Target=..., SwitchIndex=..., Switch=..., DwellTime=...`), station ETA broadcasts with signal aspect, and pairing confirmations.
  - **Event Logs:** Real-time logging of physical switch servo transitions, anti-collision headway adjustments, and IR beam-break occupancy state changes.

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

### 3.5. Dynamic Over-The-Air Locomotive Removal & Unpairing (Without Reflashing)
To maximize operational convenience during exhibition setups and layout reconfigurations, **locomotives can be dynamically unpaired and removed over the air directly from the Master Web UI, returning to an unpaired state ready to be paired again without needing to be re-flashed**:

1. **Trigger via Master Web UI:**
   - Operators can remove/unpair any locomotive from:
     - The **Fleet & Discovery Table** (`ESP-NOW Network & Fleet Devices`),
     - The **Locomotive Control Card** in the manual dispatch tab,
     - The **Advanced Configuration Studio** (`Settings -> Locomotives`).
   - Clicking **"🗑️ Unpair / Remove"** displays a safety confirmation prompt.
   - Upon confirmation, the Master Gateway transmits an over-the-air `MSG_UNPAIR (0x06)` command to the locomotive via unicast and broadcast.

2. **Locomotive Autonomous Unpairing & Bond Reset:**
   - When the locomotive receives `MSG_UNPAIR` validated against its bonded Master MAC:
     - **Emergency Motor Stop:** Immediately stops traction (`g_motor.emergencyStop()`).
     - **EEPROM Bond Clearance:** Clears `g_bond.isPaired = 0` and zeroes `masterMac` in EEPROM, committing the change immediately to Flash.
     - **Internal State Reset:** Resets operational state machine to `LOCO_STATE_MANUAL`, clears learned loop calibration, and resets dwell and traffic hold states.
     - **Visual Feedback:** Emits 4 rapid LED flashes and transitions to the gentle alternating pulse indicator signalling unpaired standby.
     - **Presence Announcement:** Immediately broadcasts `MSG_DISCOVERY_ANNOUNCE` with `isPaired = 0`.
     - **Immediate Re-pairing Readiness:** The locomotive appears instantly in the Web UI as an **Unpaired Device** with the **"🔗 Pair"** button, ready to be paired to the same or a new Master layout without requiring USB cables or firmware re-flashing!

3. **Master Gateway Registry Cleanup:**
   - The Master marks the node as unpaired (`isPaired = false`), removes its saved kinetic parameters from LittleFS (`/config/config.json`), halts periodic keepalive refresh packets for that locomotive, and broadcasts updated WebSocket telemetry to all connected clients.

4. **Hardware Flash Erase (Optional Fallback):**
   - Reflashing with flash erase (`pio run -e loco_c3 -t erase` or the Web Flasher "Erase all flash" checkbox) remains available as a hardware fallback for units that are powered off or out of radio range.

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
│   ├── master/main.cpp         # Master Gateway & Supervisor entry point
│   ├── master/TopologyManager.h # Circuit Graph, Auto-Layout & Multi-Node Config
│   ├── master/TopologyManager.cpp
│   ├── loco/main.cpp           # Mobile Autonomous Locomotive firmware
│   └── track/main.cpp          # Track Switch & Smart Station Platform firmware
├── data/                       # LittleFS Web UI assets (served by Master)
│   ├── index.html              # Responsive dispatch dashboard, CTC Map & Config Studio
│   ├── style.css               # Modern dark-mode glassmorphism styling
│   ├── app.js                  # WebSocket client, SVG Map Renderer & real-time controls
│   └── config/                 # Dynamic system, node and topology JSON files
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

## 5. Advanced Configuration System & Multi-Beacon Specification (JSON)

Legacy CSV scenario scripts are completely deprecated. Instead, all system behavior, locomotive tuning, track station behaviors, and anti-collision rules are governed by a unified hierarchical JSON configuration schema stored in LittleFS (`/config/config.json`) and edited via an intuitive **Multi-Tab Web Interface**.

### 5.1. Multi-Tab Configuration Architecture
The Web UI provides three dedicated configuration panels:
1. **Global / System Tab:**
   - Layout name and venue identifier.
   - Wi-Fi network settings (SSID, WPA2 password, channel 1-13, AP vs STA mode).
   - Global Anti-Collision Headway thresholds:
     - `headwaySafeMinSec`: Minimum green-aspect headway time (default: 12s).
     - `headwayCautionSec`: Caution / speed-trimming headway time (default: 6s).
     - `headwaySpeedTrimPct`: Percentage throttle reduction in caution zone (default: 40%).
   - Global emergency stop policy & telemetry update rate.
2. **Per-Locomotive Tabs (Dedicated Card/Tab per Discovered Train):**
   - Node ID and user-customizable Friendly Name (e.g. `LOCO_4B5C` -> *"High-Speed Intercity"*).
   - Maximum Cruise Speed (-100% to +100%) and Calibration/Learning Speed (default: 35%).
   - Kinematic ramping: Acceleration rate (%/s) and Deceleration rate (%/s).
   - Braking curve offset (ms) for precision station stopping.
   - Default station dwell duration (seconds).
   - Lighting default mode (Auto-Directional, Manual presets for Front, Rear, Cab).
   - Battery low-voltage cut-off threshold (mV).
   - Actions: **Start Learning Lap**, **View Learned Circuit Map**, and **Reset Calibration**.
3. **Per-Station Tabs (Dedicated Card/Tab per Track Node):**
   - Node ID and Friendly Station Name (e.g. `TRACK_1A2B` -> *"Grand Central Platform 1"*).
   - Platform Dwell Time (seconds) with automated departure clearance.
   - Turnout Routing Policy (e.g. Auto-divert to passing siding if platform occupied, main line priority).
   - Passing Siding Physical Capacity (cm): restricts automatic diversion of trains that exceed siding length.
   - **Multi-Turnout Switch Management:**
     - Dynamic table supporting multiple turnout switches per station node ($N_{\text{switches}} \ge 1$).
     - **Switch ID:** Explicit identifier (1..N) matching hardware routing indices.
     - **Servo GPIO:** Dedicated PWM GPIO pin assigned to that turnout servo (e.g. GPIO 18, 25, 26...).
     - **Default Position:** Priority route (`STRAIGHT` vs `TURNOUT`).
     - **Angular Calibration:** Independent `servoStraightAngle` and `servoTurnoutAngle` (0° to 180°).
     - **Description:** Friendly tag (e.g. *"North Entry Switch"*, *"Siding Turnout"*).
     - **Interactive Testing & Actions:** Real-time test buttons (`[➡️ Straight]` and `[🔀 Turnout]`), plus `+ Add Switch` and removal controls.
   - **Multi-Beacon Allocation with Explicit GPIO Mapping:**
     - Dynamic table supporting configurable beacons per station sector ($N_{\text{beacons}} \ge 1$).
     - **Beacon ID:** Explicit numeric ID broadcast over 38kHz IR.
     - **Associated GPIO:** Explicit GPIO pin assigned to the IR modulation emitter or optical beam-break detector (e.g. GPIO 19, 23...).
     - **Role / Function:**
       - **Station Arrival (`ROLE_STATION_ARRIVAL`):** Exactly 1 primary beacon located at the station platform itself to detect train arrival and trigger passenger dwell.
       - **Localizer (`ROLE_LOCATOR`):** Waypoint beacons placed along open track, passing loop, or sector boundaries solely to track train position and measure train length.
       - **Approach / Siding (`ROLE_APPROACH`, `ROLE_SIDING`):** Advance signaling and siding entry verification.
     - **Train Length Measurement Feature (`measureTrainLength = true`):**
       - The beacon sensor measures the time between train entry ($t_{\text{block}}$) and tail clearance ($t_{\text{unblock}}$):
         $$\Delta t = t_{\text{unblock}} - t_{\text{block}}$$
       - Using the locomotive's reported velocity $v$:
         $$L_{\text{train}} = v \times \Delta t$$
       - Automatically computes composition length (locomotive + wagons) to verify siding capacity and clear turnouts.

### 5.2. JSON Configuration Schema (`/config/config.json`)
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
          "action": "STOP_AND_DWELL", 
          "dwellSec": 10 
        }
      ]
    }
  ]
}
```

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
- **Bottom Navigation Bar:** One-handed switching between 🚂 Locomotives, 🔀 Switches, 🗺️ Circuit Map, ⚙️ Parameters (Global/Loco/Station), 🛰️ Fleet, and 📋 Logs.
- **Embedded PROGMEM Fallback Controller:** If flashed without the LittleFS web assets, the Master serves an autonomous embedded web controller from flash memory with full traction, turnout, pairing, multi-tab parameter tuning, and AP settings capabilities.

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
| Function | ESP32 DevKit | ESP8266 (D1 Mini) | Notes |
| :--- | :--- | :--- | :--- |
| Primary Servo Switch (SW #1) | GPIO 18 | D4 (GPIO 2) | Default primary switch pin; configurable in UI |
| Auxiliary Servo Switches (SW #2..N) | GPIO 25, 26, 27 | D7 (GPIO 13), D8 | Additional turnouts mapped via UI GPIO field |
| 38kHz IR Beacon (TX) | GPIO 19 | D5 (GPIO 14) | Default beacon emitter pin; configurable per beacon |
| Beam-Break Sensor (RX)| GPIO 23 | D6 (GPIO 12) | Occupancy & train length timing sensor |
| OLED Display (SDA) | GPIO 21 | D2 (GPIO 4) | I2C SSD1306 Display |
| OLED Display (SCL) | GPIO 22 | D1 (GPIO 5) | I2C SSD1306 Display |

---

## 9. Approved Core Libraries
- `bblanchon/ArduinoJson` (v7.x)
- `ESPAsyncWebServer` & `AsyncTCP`
- `z3t0/IRremote` (ESP32) & `crankyoldgit/IRremoteESP8266` (ESP8266)
- `adafruit/Adafruit_SSD1306` & `adafruit/Adafruit_GFX`
- `madhephaestus/ESP32Servo` (ESP32) & `Servo.h` (ESP8266)

---

## 10. Autonomous Locomotive Operations & Smart Station Traffic Control

```
                      ┌──────────────────────────────────────┐
                      │    CENTRAL SUPERVISOR (MASTER)       │
                      │  - Global Monitoring & Web Dashboard │
                      │  - Scenario Upload & Emergency Halt  │
                      └──────────────────┬───────────────────┘
                                         │ Telemetry & Global Commands
                 ┌───────────────────────┴───────────────────────┐
                 │                                               │
                 ▼                                               ▼
   ┌───────────────────────────┐  Direct ESP-NOW Traffic Reg.  ┌───────────────────────────┐
   │    AUTONOMOUS LOCOMOTIVE  │◄─────────────────────────────►│    SMART TRACK / STATION  │
   │  - Self-Guided Learning   │  - Headway Distance & Speed   │  - Auto Turnout Routing   │
   │  - Inter-Beacon Map       │  - Anti-Collision Trimming    │  - Multi-Train Headway    │
   │  - Dynamic Speed Profiles │  - Platform Hold & Release    │  - Passing Loop Decider   │
   │  - Precision Station Stop │                               │  - Signal Aspect (OLED)   │
   └───────────────────────────┘                               └───────────────────────────┘
```

### 10.1. Locomotive Circuit Learning Lap
- **Objective:** Enable locomotives to operate autonomously on any track layout without manual geometric coding or pre-programmed block maps.
- **Calibration Workflow & State Machine:**
  1. **Calibration Trigger:** Initiated via the Master Web UI (*"Start Learning Lap"*), REST API, or an initial boot calibration flag.
  2. **Reconnaissance Run:** The locomotive accelerates smoothly to a standardized calibration cruise speed (default: **35% throttle**).
  3. **Beacon Sequence & Timing Acquisition:**
     - Upon crossing each 38kHz IR track beacon ($B_0, B_1, B_2, \dots, B_n$), the locomotive registers the unique 16-bit Beacon ID and records high-precision millisecond timestamps ($t_{B_i}$).
     - Computes the transit interval for each segment:  
       $$\Delta t_i = t_{B_i} - t_{B_{i-1}}$$
  4. **Loop Closure Detection:**
     - When the locomotive detects the initial beacon $B_0$ again with a matching subsequent beacon sequence, circuit closure is confirmed.
     - Calculates total lap duration $T_{\text{circuit}} = \sum \Delta t_i$ and verifies repeatability.
  5. **Non-Volatile Storage (EEPROM / LittleFS):**
     - Stores the calibrated circuit map in persistent storage:
       ```cpp
       struct CircuitSegment {
           uint16_t fromBeaconId;
           uint16_t toBeaconId;
           uint32_t transitTimeMs;      // Nominal transit time at calibration speed
           uint8_t  recommendedCruise;  // Optimal cruising power percentage
           bool     isStationStop;      // Flag indicating platform dwell requirement
       };
       ```
  6. **Visual & Auditory Feedback:** Provides confirmation (e.g. 3 rapid headlight pulses) and enters `CIRCUIT_CALIBRATED` ready state.

### 10.2. Dynamic Speed Management & Precision Station Braking
- **Autonomous Kinetic Profile:**
  - Once calibrated, the locomotive runs autonomously without requiring step-by-step throttle commands from the Master.
  - Dynamically calculates its acceleration ramp after departing any block or station stop.
- **Precision Station Braking Curve:**
  - Using the learned transit duration ($\Delta t_i$) and current cruising speed, the locomotive calculates the exact moment to initiate braking (*Top of Descent*):
    $$t_{\text{brake\_start}} = t_{\text{expected\_station}} - t_{\text{decel\_ramp}}$$
  - Applies a smooth decelerating S-curve profile, bringing the train to a gentle halt centered on the station beacon.
- **Decentralized ETA Estimation:**
  - The locomotive computes its real-time ETA to the next station stop using its instantaneous speed and remaining segment time:
    $$\text{ETA}_{\text{next}} = \frac{\Delta t_{\text{learned}} \times v_{\text{calib}}}{v_{\text{current}}}$$
  - The locomotive includes this computed `etaSeconds` directly inside its periodic `MsgLocoTelemetry` packet, eliminating mathematical computation overhead on the Master Gateway.

### 10.3. Smart Station Autonomous Turnout Management & Interlocking
- **Autonomous Route Switching & Siding Capacity Verification:**
  - Track stations monitor approaching trains via telemetry, localizer beacons, and optical beam-break sensors.
  - Servos actuate automatically to direct traffic based on real-time track availability:
    - **Passing Loop Routing:** If the main station platform line is occupied by another train, the station automatically switches the turnout to `SWITCH_TURNOUT` to route the incoming train into the passing siding.
    - **Train Length vs. Siding Capacity Safety:** The station cross-checks the approaching train's measured composition length ($L_{\text{train}}$) against the physical length of the siding ($L_{\text{siding}}$). If the train is too long to fit completely within the passing siding, turnout diversion is inhibited to prevent the train's tail from fouling the main line points.
    - **Main Line Priority:** When the line is clear, the turnout defaults to `SWITCH_STRAIGHT`.
- **Local Fail-Safe Interlocking & Tail Clearance:**
  - **Mechanical Protection Window:** Turnout servo movement is strictly locked while the IR beam-break sensor (`IR_BEAM_RX`) indicates train occupancy over the points.
  - **Tail Clearance Guarantee:** A track block or junction is considered cleared only after the train unblocks the sensor ($t_{\text{unblock}}$), guaranteeing that the full physical composition (locomotive + all attached wagons) has completely cleared the points before any switch movement is permitted.

### 10.4. Multi-Train Headway Monitoring & Anti-Collision Speed Regulation
- **Decentralized Headway Measurement:**
  - Localizer beacons and stations track the passage timestamps between consecutive trains:
    $$T_{\text{headway}} = t_{\text{passage}}(\text{Train } B) - t_{\text{passage}}(\text{Train } A)$$
- **Dynamic 3-Zone Anti-Collision Regulation:**
  1. **Clear Zone ($T_{\text{headway}} \ge T_{\text{safe\_min}}$, e.g. $\ge 12\text{s}$):**
     - Ample safe buffer. Station displays **GREEN** aspect.
     - Locomotives run at their nominal autonomous cruising speed.
  2. **Caution Zone ($T_{\text{caution}} \le T_{\text{headway}} < T_{\text{safe\_min}}$, e.g. $5\text{s} \text{ to } 12\text{s}$):**
     - Trains are closing up. Station displays **YELLOW** aspect.
     - The station transmits a direct ESP-NOW speed regulation packet (`MSG_TRAFFIC_REGULATION`) to the trailing locomotive, trimming its speed by 30% to 50% to restore safe spacing.
  3. **Danger Zone ($T_{\text{headway}} < T_{\text{caution}}$, e.g. $< 5\text{s}$ or Block Occupied):**
     - Imminent risk of collision. Station displays **RED** aspect.
     - The station transmits an immediate stop command (`targetSpeed = 0`, active electrical braking) to the approaching train, holding it outside the block until the preceding train clears the sector.
- **Fail-Safe Emergency Override:**
  - If a trailing train fails to acknowledge or fails to decelerate within 500ms after entering the Danger Zone, the station broadcasts an immediate sector emergency halt (`MSG_EMERGENCY_STOP`).

### 10.5. Extended Protocol Message Structures (ESP-NOW)
To support distributed autonomous operation, the protocol includes dedicated packet types:
- **`MSG_LEARNING_CMD` (Master/Station -> Loco):** Commands locomotive to start, abort, or reset the circuit learning lap.
- **`MSG_TRAFFIC_REGULATION` (Station -> Loco):** Transmits speed trimming commands, target speed caps, hold orders, and signal aspect states.
- **`MSG_CIRCUIT_STATUS` (Loco -> Master/Station):** Reports learning progress, number of identified beacons, total loop travel duration, and calibration status.

---

## 11. Circuit Topology Auto-Discovery & Interactive Map Generation (CTC Synoptic)

### 11.1. Feasibility Analysis & Core Principles
Auto-generating a visual track schematic from sensor telemetry is **technically feasible and highly efficient**, leveraging the combined observations of locomotives and stationary track controllers without requiring manual layout design.

```
  Locomotive Learning Run        Station Configurations           Track Sensors
   [ B1 -> B2 -> B3 -> B1 ]   +  [ Station A: {B1, B2} ]     +  [ Beam Break & Switch ]
   [ Transit Times: Δt1... ]     [ Station B: {B3}     ]        [ STRAIGHT vs TURNOUT ]
                │                           │                              │
                └───────────────────────────┼──────────────────────────────┘
                                            ▼
                             ┌─────────────────────────────┐
                             │ Topological Graph G = (V,E) │
                             │  - Nodes: Beacons & Switches│
                             │  - Edges: Track Blocks (Δt) │
                             └──────────────┬──────────────┘
                                            ▼
                             ┌─────────────────────────────┐
                             │ Interactive Web SVG Map     │
                             │  - Real-time Train Tracking │
                             │  - Live Switch Alignment    │
                             │  - Block Occupancy Colors   │
                             └─────────────────────────────┘
```

### 11.2. Topological Input Fusion
The layout discovery engine fuses three sources of truth:
1. **Beacon Sequence & Segment Lengths (Locomotive Learning Lap):**
   - The sequence of detected beacon IDs establishes the connectivity order:  
     $$B_1 \xrightarrow{\Delta t_1} B_2 \xrightarrow{\Delta t_2} \dots \xrightarrow{\Delta t_n} B_1$$
   - Because the learning lap is conducted at constant known velocity $v_{\text{calib}}$, the transit time $\Delta t_i$ directly correlates with the physical distance $D_i = v_{\text{calib}} \times \Delta t_i$, providing accurate edge weights for the schematic layout.
2. **Station Multi-Beacon Role Mapping (`/config/config.json`):**
   - Connects abstract beacons to physical station infrastructures:
     - Example: Station A owns Beacon 10 (`ROLE_APPROACH`) and Beacon 11 (`ROLE_PLATFORM`).
     - The engine clusters these beacons as a Station Zone, rendering a passenger platform between them.
3. **Turnout Divergence & Siding Detection:**
   - When a station turnout is actuated (`SWITCH_TURNOUT` vs `SWITCH_STRAIGHT`), the locomotive traversing the junction observes divergent sequences:
     - Main Route: $B_{\text{approach}} \rightarrow B_{\text{platform\_main}} \rightarrow B_{\text{exit}}$
     - Siding Route: $B_{\text{approach}} \rightarrow B_{\text{siding}} \rightarrow B_{\text{exit}}$
   - The graph algorithm automatically identifies $B_{\text{approach}}$ as a **Branch Fork (Junction)** and $B_{\text{exit}}$ as a **Converging Joint**, reconstructing passing loops and sidings automatically.

### 11.3. Graph Data Model (`/config/topology.json`)
The learned circuit is stored as a weighted directed multigraph $G = (V, E)$:
- **Vertices ($V$):**
  - **Beacon Nodes ($B_i$):** Physical IR transmitter locations with role attributes.
  - **Station Nodes ($S_k$):** Identified station platforms with dwell properties.
  - **Junction Nodes ($J_m$):** Servo turnouts with active branch states (`STRAIGHT` or `TURNOUT`).
- **Edges ($E$):**
  - Directed track segments connecting vertices.
  - Weight $w(e) = \Delta t_i$ (baseline transit duration).
  - Dynamic status: `CLEAR`, `OCCUPIED` (via beam-break/train tracking), or `LOCKED`.

### 11.4. Web UI Vector Schematic Map Rendering (SVG Synoptic)
- **Automatic Layout Synthesis (JavaScript Client in `app.js`):**
  - Uses an orthogonal grid alignment algorithm (similar to Centralized Traffic Control - CTC railroad dispatch boards):
    - Main lines are rendered as continuous horizontal or rounded loop lines.
    - Siding tracks and passing loops are drawn as parallel tracks branching off at clean 45-degree angles.
    - Stations are represented by stylized platform outlines labeled with their friendly names.
    - Signal aspect circles (Green, Yellow, Red) display live states next to station approach blocks.
- **Real-Time Interactive Dispatching:**
  - **Live Train Position Interpolation & Proportional Length:** As locomotives transmit telemetry (`currentBlockId`, `currentSpeed`, `batteryMv`), train icons smoothly traverse the SVG tracks between beacons. The train icon automatically scales its visual pixel length to reflect its actual measured physical composition length ($L_{\text{train}}$), visually showing locomotives with attached wagons.
  - **Live Track Turnout State:** Clicking a switch junction on the map sends an immediate toggle command over WebSockets/ESP-NOW, and the schematic route updates visually in real time.
  - **Occupancy Illumination:** Track blocks illuminate in **Red** when occupied by a train or interrupted by an IR beam-break sensor, and return to **Subtle Neon Blue** when clear.
