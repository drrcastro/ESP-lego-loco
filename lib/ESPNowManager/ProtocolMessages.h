#pragma once
#include <Arduino.h>

#pragma pack(push, 1)

// =================================================================
// Node Types & Role Identifiers
// =================================================================
enum NodeType : uint8_t {
    NODE_TYPE_UNKNOWN   = 0,
    NODE_TYPE_MASTER    = 1,
    NODE_TYPE_LOCO      = 2,
    NODE_TYPE_TRACK     = 3
};

// =================================================================
// Message Types
// =================================================================
enum MessageType : uint8_t {
    MSG_DISCOVERY_ANNOUNCE      = 0x01,
    MSG_DISCOVERY_ACK           = 0x02,
    MSG_HEARTBEAT               = 0x03,
    MSG_DISCOVERY_SCAN          = 0x04,
    MSG_PAIR_CONFIRM            = 0x05,
    MSG_UNPAIR                  = 0x06,
    MSG_LOCO_COMMAND            = 0x10,
    MSG_LOCO_TELEMETRY          = 0x11,
    MSG_TRACK_COMMAND           = 0x20,
    MSG_TRACK_TELEMETRY         = 0x21,
    MSG_STATION_ETA_BROADCAST   = 0x30,
    MSG_LEARNING_CMD            = 0x40,
    MSG_CIRCUIT_STATUS          = 0x41,
    MSG_TRAFFIC_REGULATION      = 0x42,
    MSG_TRAIN_LENGTH_REPORT     = 0x43,
    MSG_EMERGENCY_STOP          = 0xFF
};

// =================================================================
// Switch & Signal Positions
// =================================================================
enum SwitchState : uint8_t {
    SWITCH_STRAIGHT = 0,
    SWITCH_TURNOUT  = 1
};

enum SignalAspect : uint8_t {
    SIGNAL_RED    = 0,
    SIGNAL_YELLOW = 1,
    SIGNAL_GREEN  = 2
};

enum LightingMode : uint8_t {
    LIGHT_MODE_MANUAL          = 0,
    LIGHT_MODE_AUTO_DIRECTION  = 1,
    LIGHT_MODE_SHUNTING        = 2,
    LIGHT_MODE_EMERGENCY_FLASH = 3
};

enum BeaconRole : uint8_t {
    ROLE_LOCATOR          = 0, // General tracking waypoint (measures length, updates map, no stop)
    ROLE_STATION_ARRIVAL  = 1, // Single platform arrival beacon (triggers platform stop/dwell)
    ROLE_APPROACH         = 2, // Approach warning beacon
    ROLE_DEPARTURE        = 3, // Departure clearance beacon
    ROLE_SIDING           = 4  // Siding / passing loop marker
};

enum LocoState : uint8_t {
    LOCO_STATE_MANUAL       = 0,
    LOCO_STATE_LEARNING     = 1,
    LOCO_STATE_AUTONOMOUS   = 2
};

// =================================================================
// Message Structures (Payload <= 250 bytes)
// =================================================================

struct MsgDiscoveryAnnounce {
    uint8_t  msgType;             // MSG_DISCOVERY_ANNOUNCE
    uint8_t  nodeType;            // NodeType
    char     nodeId[16];          // "LOCO_4B5C", "TRACK_1A2B"
    uint8_t  mac[6];              // Sender MAC
    uint16_t firmwareVersion;     // Firmware version format: 0x0100
    uint8_t  capabilities;        // Bitmask: [0: Motor, 1: LEDs, 2: IR_RX, 3: IR_TX, 4: Servo, 5: OLED]
    uint8_t  isPaired;            // 0 = Unpaired (New), 1 = Bonded/Paired
    uint8_t  pairedMasterMac[6];  // Bonded Master MAC if isPaired == 1
};

struct MsgDiscoveryAck {
    uint8_t  msgType;             // MSG_DISCOVERY_ACK
    char     targetNodeId[16];    // Recipient node ID
    uint8_t  masterMac[6];        // Master MAC
    uint8_t  wifiChannel;         // ESP-NOW / Wi-Fi channel
    uint32_t serverEpochTime;     // Synced timestamp
};

struct MsgDiscoveryScan {
    uint8_t  msgType;             // MSG_DISCOVERY_SCAN
    uint8_t  masterMac[6];        // Master MAC
    uint8_t  wifiChannel;         // Master Wi-Fi channel
};

struct MsgPairConfirm {
    uint8_t  msgType;             // MSG_PAIR_CONFIRM
    char     targetNodeId[16];    // Recipient loco node ID
    uint8_t  masterMac[6];        // Master MAC to bond with
    uint8_t  wifiChannel;         // Wi-Fi channel
    uint32_t bondKey;             // Layout bonding token (0x50414952 "PAIR")
};

struct MsgUnpair {
    uint8_t  msgType;             // MSG_UNPAIR
    char     targetNodeId[16];    // Recipient node ID (e.g. "LOCO_4B5C" or "ALL")
    uint8_t  masterMac[6];        // Master MAC sending the unpair command
};

struct MsgHeartbeat {
    uint8_t  msgType;             // MSG_HEARTBEAT
    char     nodeId[16];
    uint8_t  nodeType;
    int8_t   rssi;
    uint32_t uptimeSec;
};

struct MsgLocoCommand {
    uint8_t  msgType;             // MSG_LOCO_COMMAND
    char     targetNodeId[16];    // Target or "ALL"
    int8_t   targetSpeed;         // -100 to +100 (-100 = 100% reverse, 0 = stop, 100 = 100% fwd)
    uint8_t  brake;               // 0 = coast/drive, 1 = active brake, 2 = e-stop
    uint8_t  lightsFront;         // 0-255 PWM
    uint8_t  lightsRear;          // 0-255 PWM
    uint8_t  lightsCab;           // 0-255 PWM
    uint8_t  lightingMode;        // LightingMode
};

struct MsgLocoTelemetry {
    uint8_t  msgType;             // MSG_LOCO_TELEMETRY
    char     nodeId[16];
    int8_t   currentSpeed;        // Current actual speed (-100 to 100)
    uint8_t  direction;           // 0 = stop, 1 = fwd, 2 = rev
    uint16_t currentBlockId;      // Last decoded IR beacon block ID
    uint16_t batteryMv;           // Millivolts (e.g. 3700)
    uint32_t timestampMs;
    uint8_t  locoState;           // LocoState (Manual=0, Learning=1, Autonomous=2)
    uint16_t etaSeconds;          // Self-calculated ETA to next station platform
    uint16_t measuredLengthCm;    // Measured train composition length
    uint8_t  lapCount;            // Laps completed
};

struct MsgTrackCommand {
    uint8_t  msgType;             // MSG_TRACK_COMMAND
    char     targetNodeId[16];    // Target track node ID
    uint8_t  switchIndex;         // 0-based index for track with multiple switches
    uint8_t  switchPosition;      // SwitchState (SWITCH_STRAIGHT / SWITCH_TURNOUT)
    uint16_t dwellTimeSec;        // Dwell countdown at platform (0 = none)
};

struct MsgTrackTelemetry {
    uint8_t  msgType;             // MSG_TRACK_TELEMETRY
    char     nodeId[16];
    uint8_t  switchPosition;      // SwitchState
    uint8_t  beamOccupied;        // 1 = beam broken (train present), 0 = free
    uint16_t beaconCodeEmitted;   // Active IR beacon ID emitted by this block
    uint32_t timestampMs;
    uint8_t  currentSignalAspect; // SignalAspect (RED, YELLOW, GREEN)
    uint16_t lastMeasuredLengthCm;// Computed train length in cm from occlusion timing
    uint16_t dwellRemainingSec;   // Active dwell countdown seconds
};

struct MsgStationEtaBroadcast {
    uint8_t  msgType;             // MSG_STATION_ETA_BROADCAST
    char     targetStationId[16]; // e.g. "TRACK_1A2B" or "ALL"
    char     trainName[16];       // Friendly name or ID (e.g. "Cargo 102")
    uint16_t etaSeconds;          // Seconds until arrival
    uint8_t  signalAspect;        // SignalAspect (RED, YELLOW, GREEN)
    char     destination[16];     // Destination label
};

struct MsgLearningCmd {
    uint8_t  msgType;             // MSG_LEARNING_CMD
    char     targetLocoId[16];    // Target or "ALL"
    uint8_t  command;             // 0 = STOP/ABORT, 1 = START_LEARNING, 2 = RESET_CALIBRATION, 3 = START_AUTONOMOUS
    uint8_t  calibrationSpeed;    // Default e.g. 35
};

struct MsgCircuitStatus {
    uint8_t  msgType;             // MSG_CIRCUIT_STATUS
    char     locoId[16];
    uint8_t  state;               // LocoState
    uint8_t  beaconCount;         // Number of discovered beacons
    uint32_t totalLapTimeMs;      // Full circuit lap duration
    uint8_t  isCalibrated;        // 1 = Calibrated, 0 = Not calibrated
};

struct MsgTrafficRegulation {
    uint8_t  msgType;             // MSG_TRAFFIC_REGULATION
    char     targetLocoId[16];    // Recipient loco
    char     sourceStationId[16]; // Station issuing regulation
    int8_t   speedCap;            // Maximum allowed speed (-1 = none)
    int8_t   speedTrimPct;        // Percent speed reduction (e.g. 30 = -30%)
    uint8_t  signalAspect;        // SignalAspect (RED, YELLOW, GREEN)
    uint8_t  holdTrain;           // 1 = stop and wait for signal
};

struct MsgTrainLengthReport {
    uint8_t  msgType;             // MSG_TRAIN_LENGTH_REPORT
    char     locoId[16];          // Target or detected loco
    char     sensorNodeId[16];    // Reporting station/sensor
    uint16_t beaconId;            // Beacon where length was measured
    uint16_t measuredLengthCm;    // Calculated length
    uint32_t transitTimeMs;       // Occlusion time delta
};

struct MsgEmergencyStop {
    uint8_t  msgType;             // MSG_EMERGENCY_STOP
    uint8_t  reasonCode;          // 0 = Manual E-Stop, 1 = Collision Hazard, 2 = Lost Comm
    char     sourceNodeId[16];
};

// Generic union container
union EspMessage {
    uint8_t                 raw[128];
    uint8_t                 msgType;
    MsgDiscoveryAnnounce    discoveryAnnounce;
    MsgDiscoveryAck         discoveryAck;
    MsgHeartbeat            heartbeat;
    MsgDiscoveryScan        discoveryScan;
    MsgPairConfirm          pairConfirm;
    MsgUnpair               unpair;
    MsgLocoCommand          locoCommand;
    MsgLocoTelemetry        locoTelemetry;
    MsgTrackCommand         trackCommand;
    MsgTrackTelemetry       trackTelemetry;
    MsgStationEtaBroadcast  stationEta;
    MsgLearningCmd          learningCmd;
    MsgCircuitStatus        circuitStatus;
    MsgTrafficRegulation    trafficRegulation;
    MsgTrainLengthReport    trainLengthReport;
    MsgEmergencyStop        emergencyStop;
};

#pragma pack(pop)
