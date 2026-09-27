#include <Arduino.h>
#include "ESPNowManager.h"
#include "TrackManager.h"
#include "IRTelemetry.h"
#include "StationDisplay.h"

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

    if (ESPNowManager::instance().hasMasterMac()) {
        ESPNowManager::instance().sendUnicast(ESPNowManager::instance().getMasterMac(), &telem, sizeof(telem));
    } else {
        ESPNowManager::instance().sendBroadcast(&telem, sizeof(telem));
    }
}

static void handleIncomingEspNow(const uint8_t* mac, const uint8_t* data, int len) {
    if (len <= 0 || !data) return;
    uint8_t msgType = data[0];

    const char* myId = ESPNowManager::instance().getNodeId();

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
    g_display.updateSystemInfo(ESPNowManager::instance().getNodeId(), "Online", -50);

    // 4. Initialize ESP-NOW
    ESPNowManager::instance().begin(NODE_TYPE_TRACK, 1);
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
