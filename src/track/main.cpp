#include <Arduino.h>
#include <EEPROM.h>
#include "ESPNowManager.h"
#include "TrackManager.h"
#include "IRTelemetry.h"
#include "StationDisplay.h"

#define EEPROM_SIZE 64
#define TRACK_BOND_MAGIC 0x54524B32 // "TRK2"

struct TrackBondConfig {
    uint32_t magic;         // TRACK_BOND_MAGIC
    uint8_t  isPaired;      // 1 = bonded to master, 0 = unpaired
    uint8_t  masterMac[6];  // Bonded Master MAC
    uint8_t  wifiChannel;   // Wi-Fi Channel
    uint8_t  reserved[20];
};

static TrackBondConfig g_bond = {};

// =================================================================
// Pin Assignments (ESP32 vs ESP8266)
// =================================================================
#if defined(ESP32)
  #define PIN_SERVO_SWITCH 18
  #define PIN_IR_BEACON_TX 19
  #define PIN_IR_BEAM_RX   23
  #define PIN_I2C_SDA      21
  #define PIN_I2C_SCL      22
#elif defined(ESP8266)
  #define PIN_SERVO_SWITCH 2  // D4
  #define PIN_IR_BEACON_TX 14 // D5
  #define PIN_IR_BEAM_RX   12 // D6
  #define PIN_I2C_SDA      4  // D2
  #define PIN_I2C_SCL      5  // D1
#endif

// Hardware Controller Singletons
static TrackManager    g_track;
static IRTelemetry     g_ir;
static StationDisplay  g_display(128, 64);

static uint16_t g_blockBeaconId = 5; // Configurable block ID
static uint32_t g_lastTelemetryMs = 0;

static void sendTrackTelemetry() {
    MsgTrackTelemetry telem = {};
    telem.msgType = MSG_TRACK_TELEMETRY;
    strncpy(telem.nodeId, ESPNowManager::instance().getNodeId(), sizeof(telem.nodeId) - 1);
    telem.switchPosition = (uint8_t)g_track.getSwitchPosition();
    telem.beamOccupied = g_track.isOccupied() ? 1 : 0;
    telem.beaconCodeEmitted = g_blockBeaconId;
    telem.timestampMs = millis();

    if (g_bond.isPaired == 1) {
        ESPNowManager::instance().sendUnicast(g_bond.masterMac, &telem, sizeof(telem));
    } else if (ESPNowManager::instance().hasMasterMac()) {
        ESPNowManager::instance().sendUnicast(ESPNowManager::instance().getMasterMac(), &telem, sizeof(telem));
    } else {
        ESPNowManager::instance().sendBroadcast(&telem, sizeof(telem));
    }
}

static void handleIncomingEspNow(const uint8_t* mac, const uint8_t* data, int len) {
    if (len <= 0 || !data) return;
    uint8_t msgType = data[0];

    const char* myId = ESPNowManager::instance().getNodeId();

    // 1. Handle Pairing Confirmation from Master
    if (msgType == MSG_PAIR_CONFIRM && len >= (int)sizeof(MsgPairConfirm)) {
        const MsgPairConfirm* pairCmd = (const MsgPairConfirm*)data;
        if (strcmp(pairCmd->targetNodeId, myId) == 0 || strcmp(pairCmd->targetNodeId, "ALL") == 0) {
            if (g_bond.isPaired == 1 && memcmp(g_bond.masterMac, pairCmd->masterMac, 6) != 0) {
                Serial.println(F("[Track] Pairing REJECTED: Station is already bonded to another Master!"));
                return;
            }

            g_bond.magic = TRACK_BOND_MAGIC;
            g_bond.isPaired = 1;
            memcpy(g_bond.masterMac, pairCmd->masterMac, 6);
            g_bond.wifiChannel = pairCmd->wifiChannel;
            EEPROM.put(0, g_bond);
            EEPROM.commit();

            ESPNowManager::instance().setPairedState(true, g_bond.masterMac);
            Serial.printf("[Track] PAIRED & BONDED to Master: %02X:%02X:%02X:%02X:%02X:%02X! Stored in EEPROM.\n",
                          g_bond.masterMac[0], g_bond.masterMac[1], g_bond.masterMac[2],
                          g_bond.masterMac[3], g_bond.masterMac[4], g_bond.masterMac[5]);

            g_display.updateSystemInfo(ESPNowManager::instance().getNodeId(), "BONDED", -50);
            g_display.updateIncomingTrain("MASTER BONDED", 0, SIGNAL_GREEN);

            ESPNowManager::instance().announcePresence();
            sendTrackTelemetry();
        }
        return;
    }

    // 2. Security Check: If station is paired, accept commands ONLY from its bonded Master!
    if (g_bond.isPaired == 1) {
        if (memcmp(mac, g_bond.masterMac, 6) != 0) {
            // Command is from an unauthorized transmitter or adjacent layout. Discard!
            return;
        }
    } else {
        // If UNPAIRED, station ignores switch commands until paired with Master
        if (msgType == MSG_TRACK_COMMAND) {
            Serial.println(F("[Track] Switch command ignored: Station is UNPAIRED. Please pair with Master via Web UI."));
            return;
        }
    }

    // 3. Process Valid Track Commands & ETA Broadcasts
    if (msgType == MSG_TRACK_COMMAND && len >= (int)sizeof(MsgTrackCommand)) {
        const MsgTrackCommand* cmd = (const MsgTrackCommand*)data;
        if (strcmp(cmd->targetNodeId, myId) == 0 || strcmp(cmd->targetNodeId, "ALL") == 0) {
            g_track.setSwitchPosition((SwitchState)cmd->switchPosition);
            if (cmd->dwellTimeSec > 0) {
                g_track.startDwellCountdown(cmd->dwellTimeSec);
            }
            sendTrackTelemetry();
        }
    } else if (msgType == MSG_STATION_ETA_BROADCAST && len >= (int)sizeof(MsgStationEtaBroadcast)) {
        const MsgStationEtaBroadcast* eta = (const MsgStationEtaBroadcast*)data;
        if (strcmp(eta->targetStationId, myId) == 0 || strcmp(eta->targetStationId, "ALL") == 0) {
            g_display.updateIncomingTrain(eta->trainName, eta->etaSeconds, (SignalAspect)eta->signalAspect);
        }
    } else if (msgType == MSG_EMERGENCY_STOP) {
        g_display.updateIncomingTrain("EMERGENCY STOP", 0, SIGNAL_RED);
        Serial.println(F("[Track] Emergency stop broadcast received."));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F("  LEGO TRAIN TRACK & STATION FIRMWARE (V2.0)      "));
    Serial.println(F("=================================================="));

    // 1. Initialize Track Switch Servo
    g_track.beginSwitch(PIN_SERVO_SWITCH, 75, 105);
    g_track.onSwitchChanged([](SwitchState state) {
        g_display.updateTrackStatus(state, g_track.isOccupied());
        sendTrackTelemetry();
    });
    g_track.onDwellComplete([]() {
        Serial.println(F("[Station] Dwell finished. Notifying Master..."));
        sendTrackTelemetry();
    });

    // 2. Initialize IR Localization Beacon & Beam Break
    g_ir.beginTransmitter(PIN_IR_BEACON_TX, g_blockBeaconId, 100);
    g_ir.beginReceiver(PIN_IR_BEAM_RX);
    g_ir.enableBeamBreakDetection(300); // 300ms timeout triggers beam break

    g_ir.onOccupancyChanged([](bool occupied) {
        g_track.setOccupied(occupied);
        g_display.updateTrackStatus(g_track.getSwitchPosition(), occupied);
        sendTrackTelemetry();
    });

    // 3. Initialize OLED Station Display
    g_display.begin(PIN_I2C_SDA, PIN_I2C_SCL, 0x3C);
    g_display.setStationName("CENTRAL STATION");

    // 4. Initialize EEPROM and check Master Bonding status
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, g_bond);
    if (g_bond.magic != TRACK_BOND_MAGIC || g_bond.isPaired != 1) {
        g_bond.magic = TRACK_BOND_MAGIC;
        g_bond.isPaired = 0;
        memset(g_bond.masterMac, 0, 6);
        g_bond.wifiChannel = 1;
        Serial.println(F("[Track] Status: UNPAIRED (Ready to pair with Master via Web UI)."));
        g_display.updateSystemInfo(ESPNowManager::instance().getNodeId(), "UNPAIRED", -50);
        g_display.updateIncomingTrain("AWAITING PAIRING", 0, SIGNAL_YELLOW);
    } else {
        Serial.printf("[Track] Status: BONDED to Master MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      g_bond.masterMac[0], g_bond.masterMac[1], g_bond.masterMac[2],
                      g_bond.masterMac[3], g_bond.masterMac[4], g_bond.masterMac[5]);
        ESPNowManager::instance().setPairedState(true, g_bond.masterMac);
        g_display.updateSystemInfo(ESPNowManager::instance().getNodeId(), "BONDED", -50);
    }

    // 5. Initialize ESP-NOW
    uint8_t initialChannel = (g_bond.wifiChannel >= 1 && g_bond.wifiChannel <= 13) ? g_bond.wifiChannel : 1;
    ESPNowManager::instance().begin(NODE_TYPE_TRACK, initialChannel);
    ESPNowManager::instance().onReceive(handleIncomingEspNow);

    Serial.printf("[Track] Node ID: %s ready!\n", ESPNowManager::instance().getNodeId());
}

void loop() {
    g_track.update();
    g_ir.update();
    g_display.update();
    ESPNowManager::instance().update();

    // Periodic telemetry ping (every 1500ms)
    uint32_t now = millis();
    if (now - g_lastTelemetryMs >= 1500) {
        g_lastTelemetryMs = now;
        sendTrackTelemetry();
    }

    delay(2);
}
