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
    MSG_LOCO_COMMAND            = 0x10,
    MSG_LOCO_TELEMETRY          = 0x11,
    MSG_TRACK_COMMAND           = 0x20,
    MSG_TRACK_TELEMETRY         = 0x21,
    MSG_STATION_ETA_BROADCAST   = 0x30,
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

// =================================================================
// Message Structures (Payload <= 250 bytes)
// =================================================================

struct MsgDiscoveryAnnounce {
    uint8_t  msgType;             // MSG_DISCOVERY_ANNOUNCE
    uint8_t  nodeType;            // NodeType
    char     nodeId[16];          // "LOCO_4B5C", "TRACK_1A2B"
    uint8_t  mac[6];              // Sender MAC
    uint16_t firmwareVersion;     // e.g. 0x0200 = v2.0
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
};

struct MsgStationEtaBroadcast {
    uint8_t  msgType;             // MSG_STATION_ETA_BROADCAST
    char     targetStationId[16]; // e.g. "TRACK_1A2B" or "ALL"
    char     trainName[16];       // Friendly name or ID (e.g. "Cargo 102")
    uint16_t etaSeconds;          // Seconds until arrival
    uint8_t  signalAspect;        // SignalAspect (RED, YELLOW, GREEN)
    char     destination[16];     // Destination label
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
    MsgLocoCommand          locoCommand;
    MsgLocoTelemetry        locoTelemetry;
    MsgTrackCommand         trackCommand;
    MsgTrackTelemetry       trackTelemetry;
    MsgStationEtaBroadcast  stationEta;
    MsgEmergencyStop        emergencyStop;
};

#pragma pack(pop)
