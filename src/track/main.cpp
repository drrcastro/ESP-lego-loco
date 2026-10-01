#include <Arduino.h>
#include <EEPROM.h>
#include "ESPNowManager.h"
#include "TrackManager.h"
#include "IRTelemetry.h"
#include "StationDisplay.h"

#define EEPROM_SIZE 64
#define TRACK_BOND_MAGIC 0x54524B33 // "TRK3"

struct TrackBondConfig {
    uint32_t magic;         // TRACK_BOND_MAGIC
    uint8_t  isPaired;      // 1 = bonded to master, 0 = unpaired
    uint8_t  masterMac[6];  // Bonded Master MAC
    uint8_t  wifiChannel;   // Wi-Fi Channel
    uint8_t  beaconRole;    // BeaconRole (0 = ROLE_LOCATOR, 1 = ROLE_STATION_ARRIVAL)
    uint16_t beaconId;      // 16-bit Beacon ID
    uint16_t dwellTimeSec;  // Platform dwell time
    uint16_t sidingCapacityCm; // Length limit for passing siding
    uint8_t  reserved[14];
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

// Operational Configuration & State
static uint16_t g_blockBeaconId = 5;
static uint8_t  g_beaconRole = ROLE_STATION_ARRIVAL;
static uint16_t g_platformDwellSec = 10;
static uint16_t g_sidingCapacityCm = 65;
static bool     g_autoDivertOnOccupied = true;
static SignalAspect g_currentSignalAspect = SIGNAL_GREEN;

// Train Length Measurement & Headway Tracking
static uint32_t g_beamBreakStartMs = 0;
static uint32_t g_beamBreakEndMs = 0;
static uint16_t g_lastMeasuredLengthCm = 0;
static char     g_lastPassingLocoId[16] = "LOCO";
static int8_t   g_lastLocoSpeedPct = 35;
static uint32_t g_lastTrainPassageMs = 0;
static uint32_t g_lastTelemetryMs = 0;

static void sendTrackTelemetry() {
    MsgTrackTelemetry telem = {};
    telem.msgType = MSG_TRACK_TELEMETRY;
    strncpy(telem.nodeId, ESPNowManager::instance().getNodeId(), sizeof(telem.nodeId) - 1);
    telem.switchPosition = (uint8_t)g_track.getSwitchPosition();
    telem.beamOccupied = g_track.isOccupied() ? 1 : 0;
    telem.beaconCodeEmitted = g_blockBeaconId;
    telem.timestampMs = millis();
    telem.currentSignalAspect = (uint8_t)g_currentSignalAspect;
    telem.lastMeasuredLengthCm = g_lastMeasuredLengthCm;
    telem.dwellRemainingSec = g_track.getRemainingDwellSeconds();

    const char* destDesc = "Broadcast";
    if (g_bond.isPaired == 1) {
        ESPNowManager::instance().sendUnicast(g_bond.masterMac, &telem, sizeof(telem));
        destDesc = "Bonded Master";
    } else if (ESPNowManager::instance().hasMasterMac()) {
        ESPNowManager::instance().sendUnicast(ESPNowManager::instance().getMasterMac(), &telem, sizeof(telem));
        destDesc = "Master";
    } else {
        ESPNowManager::instance().sendBroadcast(&telem, sizeof(telem));
    }

    const char* sigStr = (g_currentSignalAspect == SIGNAL_GREEN ? "GREEN" : (g_currentSignalAspect == SIGNAL_YELLOW ? "YELLOW" : "RED"));
    Serial.printf("[Track TX -> %s] Telemetry: Switch=%s, Beam=%s, Beacon=#%u, Signal=%s, TrainLen=%ucm, Dwell=%us\n",
                  destDesc,
                  telem.switchPosition == 0 ? "STRAIGHT" : "TURNOUT",
                  telem.beamOccupied ? "OCCUPIED" : "CLEAR",
                  telem.beaconCodeEmitted,
                  sigStr,
                  telem.lastMeasuredLengthCm,
                  telem.dwellRemainingSec);
}

// Sends speed trim, hold or release command to an approaching locomotive
static void sendTrafficRegulation(const char* locoId, int8_t speedCap, int8_t speedTrimPct, SignalAspect aspect, bool hold) {
    MsgTrafficRegulation reg = {};
    reg.msgType = MSG_TRAFFIC_REGULATION;
    strncpy(reg.targetLocoId, locoId, sizeof(reg.targetLocoId) - 1);
    strncpy(reg.sourceStationId, ESPNowManager::instance().getNodeId(), sizeof(reg.sourceStationId) - 1);
    reg.speedCap = speedCap;
    reg.speedTrimPct = speedTrimPct;
    reg.signalAspect = (uint8_t)aspect;
    reg.holdTrain = hold ? 1 : 0;

    ESPNowManager::instance().sendBroadcast(&reg, sizeof(reg));
    Serial.printf("[Track Event] Traffic Regulation sent to %s: Aspect=%s, Cap=%d, Trim=%d%%, Hold=%u\n",
                  locoId,
                  aspect == SIGNAL_GREEN ? "GREEN" : (aspect == SIGNAL_YELLOW ? "YELLOW" : "RED"),
                  speedCap, speedTrimPct, hold ? 1 : 0);
}

static void evaluateHeadwayRegulation(const char* approachingLocoId) {
    uint32_t now = millis();
    bool blockBusy = g_track.isOccupied() || g_track.isDwellActive();

    if (blockBusy) {
        // Sector is busy! Check if we can safely divert approaching train into passing loop/siding
        if (g_autoDivertOnOccupied && g_track.getSwitchPosition() == SWITCH_STRAIGHT) {
            // Check siding capacity against train length
            if (g_lastMeasuredLengthCm > 0 && g_lastMeasuredLengthCm > g_sidingCapacityCm) {
                // Train is TOO LONG for passing siding! Diverting would foul the main switch points
                Serial.printf("[Track Safety] Inhibit Turnout: Train length (%u cm) exceeds siding capacity (%u cm)!\n",
                              g_lastMeasuredLengthCm, g_sidingCapacityCm);
                g_currentSignalAspect = SIGNAL_RED;
                sendTrafficRegulation(approachingLocoId, 0, 0, SIGNAL_RED, true);
            } else {
                // Safely divert train to siding
                Serial.printf("[Track Auto-Route] Platform occupied -> Diverting approaching train %s to siding\n", approachingLocoId);
                g_track.setSwitchPosition(SWITCH_TURNOUT);
                g_currentSignalAspect = SIGNAL_YELLOW;
                sendTrafficRegulation(approachingLocoId, 30, 40, SIGNAL_YELLOW, false);
            }
        } else {
            // Cannot divert: hold approaching train outside sector
            g_currentSignalAspect = SIGNAL_RED;
            sendTrafficRegulation(approachingLocoId, 0, 0, SIGNAL_RED, true);
        }
    } else {
        // Platform is clear; evaluate time headway from preceding train
        uint32_t headwaySec = (g_lastTrainPassageMs > 0) ? ((now - g_lastTrainPassageMs) / 1000) : 999;

        if (headwaySec < 5) {
            // Danger Zone (< 5s spacing)
            g_currentSignalAspect = SIGNAL_RED;
            sendTrafficRegulation(approachingLocoId, 0, 0, SIGNAL_RED, true);
        } else if (headwaySec < 12) {
            // Caution Zone (5s to 12s spacing): Trim throttle by 40%
            g_currentSignalAspect = SIGNAL_YELLOW;
            sendTrafficRegulation(approachingLocoId, 35, 40, SIGNAL_YELLOW, false);
        } else {
            // Clear Zone (>= 12s spacing): Full clear
            g_currentSignalAspect = SIGNAL_GREEN;
            sendTrafficRegulation(approachingLocoId, -1, 0, SIGNAL_GREEN, false);
        }
    }

    g_display.updateIncomingTrain(approachingLocoId, 0, g_currentSignalAspect);
}

static void handleIncomingEspNow(const uint8_t* mac, const uint8_t* data, int len) {
    if (len <= 0 || !data) return;
    uint8_t msgType = data[0];

    const char* myId = ESPNowManager::instance().getNodeId();

    // 1. Handle Pairing Confirmation from Master
    if (msgType == MSG_PAIR_CONFIRM && len >= (int)sizeof(MsgPairConfirm)) {
        const MsgPairConfirm* pairCmd = (const MsgPairConfirm*)data;
        Serial.printf("[Track RX <- %02X:%02X:%02X:%02X:%02X:%02X] MSG_PAIR_CONFIRM: Target='%s', Ch=%u\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      pairCmd->targetNodeId, pairCmd->wifiChannel);

        if (strcmp(pairCmd->targetNodeId, myId) == 0 || strcmp(pairCmd->targetNodeId, "ALL") == 0) {
            if (g_bond.isPaired == 1 && memcmp(g_bond.masterMac, pairCmd->masterMac, 6) != 0) {
                Serial.printf("[Track RX] Pairing REJECTED: Already bonded to Master %02X:%02X:%02X:%02X:%02X:%02X!\n",
                              g_bond.masterMac[0], g_bond.masterMac[1], g_bond.masterMac[2],
                              g_bond.masterMac[3], g_bond.masterMac[4], g_bond.masterMac[5]);
                return;
            }

            g_bond.magic = TRACK_BOND_MAGIC;
            g_bond.isPaired = 1;
            memcpy(g_bond.masterMac, pairCmd->masterMac, 6);
            g_bond.wifiChannel = pairCmd->wifiChannel;
            g_bond.beaconRole = g_beaconRole;
            g_bond.beaconId = g_blockBeaconId;
            g_bond.dwellTimeSec = g_platformDwellSec;
            g_bond.sidingCapacityCm = g_sidingCapacityCm;
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

    // 1b. Handle Unpair / Removal from Master
    if (msgType == MSG_UNPAIR && len >= (int)sizeof(MsgUnpair)) {
        const MsgUnpair* unpCmd = (const MsgUnpair*)data;
        if (strcmp(unpCmd->targetNodeId, myId) == 0 || strcmp(unpCmd->targetNodeId, "ALL") == 0) {
            if (g_bond.isPaired == 1 && memcmp(g_bond.masterMac, unpCmd->masterMac, 6) != 0 && memcmp(mac, g_bond.masterMac, 6) != 0) {
                return;
            }
            g_bond.isPaired = 0;
            memset(g_bond.masterMac, 0, 6);
            g_bond.magic = TRACK_BOND_MAGIC;
            EEPROM.put(0, g_bond);
            EEPROM.commit();
            ESPNowManager::instance().setPairedState(false, nullptr);
            g_display.updateSystemInfo(ESPNowManager::instance().getNodeId(), "UNPAIRED", -50);
            g_display.updateIncomingTrain("UNPAIRED READY", 0, SIGNAL_YELLOW);
            ESPNowManager::instance().announcePresence();
            sendTrackTelemetry();
            Serial.println(F("[Track] UNPAIRED and ready to pair again without reflashing."));
        }
        return;
    }

    // 2. Process Loco Telemetry for Live Tracking & Headway Regulation
    if (msgType == MSG_LOCO_TELEMETRY && len >= (int)sizeof(MsgLocoTelemetry)) {
        const MsgLocoTelemetry* locoTelem = (const MsgLocoTelemetry*)data;
        g_lastLocoSpeedPct = locoTelem->currentSpeed;

        // If loco is in autonomous or learning mode and approaching this block
        if (locoTelem->currentBlockId == g_blockBeaconId || (locoTelem->etaSeconds > 0 && locoTelem->etaSeconds <= 15)) {
            strncpy(g_lastPassingLocoId, locoTelem->nodeId, sizeof(g_lastPassingLocoId) - 1);
            evaluateHeadwayRegulation(locoTelem->nodeId);
        }
        return;
    }

    // 3. Security Check: If station is paired, accept commands ONLY from its bonded Master!
    if (g_bond.isPaired == 1) {
        if (memcmp(mac, g_bond.masterMac, 6) != 0) {
            return;
        }
    } else {
        // If UNPAIRED, station ignores switch commands until paired with Master
        if (msgType == MSG_TRACK_COMMAND) {
            Serial.printf("[Track RX] REJECTED MSG_TRACK_COMMAND: Station is UNPAIRED. Pair via Web UI.\n");
            return;
        }
    }

    // 4. Process Valid Track Commands & Interlocking Safety
    if (msgType == MSG_TRACK_COMMAND && len >= (int)sizeof(MsgTrackCommand)) {
        const MsgTrackCommand* cmd = (const MsgTrackCommand*)data;
        if (strcmp(cmd->targetNodeId, myId) == 0 || strcmp(cmd->targetNodeId, "ALL") == 0) {
            // Safety Interlock: Inhibit switch movement while a train occupies the sensor (Tail Clearance)
            if (g_track.isOccupied()) {
                Serial.printf("[Track Safety] INHIBITED switch command: Train actively traversing junction (Tail clearance lock)!\n");
                return;
            }

            Serial.printf("[Track RX <- Master] MSG_TRACK_COMMAND: Target=%s, SwitchIndex=%u, Switch=%s, DwellTime=%us\n",
                          cmd->targetNodeId,
                          cmd->switchIndex,
                          cmd->switchPosition == 0 ? "STRAIGHT" : "TURNOUT",
                          cmd->dwellTimeSec);
            g_track.setSwitchPosition(cmd->switchIndex, (SwitchState)cmd->switchPosition);
            if (cmd->dwellTimeSec > 0) {
                g_platformDwellSec = cmd->dwellTimeSec;
                g_track.startDwellCountdown(cmd->dwellTimeSec);
            }
            sendTrackTelemetry();
        }
    } else if (msgType == MSG_STATION_ETA_BROADCAST && len >= (int)sizeof(MsgStationEtaBroadcast)) {
        const MsgStationEtaBroadcast* eta = (const MsgStationEtaBroadcast*)data;
        if (strcmp(eta->targetStationId, myId) == 0 || strcmp(eta->targetStationId, "ALL") == 0) {
            const char* sigStr = (eta->signalAspect == SIGNAL_GREEN ? "GREEN" : (eta->signalAspect == SIGNAL_YELLOW ? "YELLOW" : "RED"));
            Serial.printf("[Track RX <- Master] MSG_STATION_ETA_BROADCAST: Target=%s, Train='%s', ETA=%us, Signal=%s\n",
                          eta->targetStationId, eta->trainName, eta->etaSeconds, sigStr);
            g_display.updateIncomingTrain(eta->trainName, eta->etaSeconds, (SignalAspect)eta->signalAspect);
        }
    } else if (msgType == MSG_EMERGENCY_STOP) {
        Serial.printf("[Track RX <- Master] MSG_EMERGENCY_STOP -> Signal RED!\n");
        g_currentSignalAspect = SIGNAL_RED;
        g_display.updateIncomingTrain("EMERGENCY STOP", 0, SIGNAL_RED);
        sendTrackTelemetry();
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F("  LEGO TRAIN SMART TRACK & STATION FIRMWARE       "));
    Serial.println(F("=================================================="));

    // 1. Initialize Track Switch Servo
    g_track.beginSwitch(PIN_SERVO_SWITCH, 75, 105);
    g_track.onSwitchChanged([](SwitchState state) {
        Serial.printf("[Track Event] Switch position updated: %s -> Sending telemetry\n",
                      state == SWITCH_STRAIGHT ? "STRAIGHT" : "TURNOUT");
        g_display.updateTrackStatus(state, g_track.isOccupied());
        sendTrackTelemetry();
    });

    g_track.onDwellComplete([]() {
        Serial.println(F("[Track Event] Station dwell countdown completed -> Releasing train"));
        // Release train held at platform
        sendTrafficRegulation(g_lastPassingLocoId, -1, 0, SIGNAL_GREEN, false);
        g_currentSignalAspect = SIGNAL_GREEN;
        g_display.updateIncomingTrain(g_lastPassingLocoId, 0, SIGNAL_GREEN);

        // Reset switch to straight default if diverted
        if (g_track.getSwitchPosition() != SWITCH_STRAIGHT) {
            g_track.setSwitchPosition(SWITCH_STRAIGHT);
        }
        sendTrackTelemetry();
    });

    // 2. Initialize EEPROM and check Master Bonding status
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, g_bond);
    if (g_bond.magic != TRACK_BOND_MAGIC || g_bond.isPaired != 1) {
        g_bond.magic = TRACK_BOND_MAGIC;
        g_bond.isPaired = 0;
        memset(g_bond.masterMac, 0, 6);
        g_bond.wifiChannel = 1;
        g_bond.beaconRole = ROLE_STATION_ARRIVAL;
        g_bond.beaconId = 5;
        g_bond.dwellTimeSec = 10;
        g_bond.sidingCapacityCm = 65;
        Serial.println(F("[Track] Status: UNPAIRED (Ready to pair with Master via Web UI)."));
        g_display.updateSystemInfo(ESPNowManager::instance().getNodeId(), "UNPAIRED", -50);
        g_display.updateIncomingTrain("AWAITING PAIRING", 0, SIGNAL_YELLOW);
    } else {
        g_beaconRole = g_bond.beaconRole;
        if (g_bond.beaconId > 0) g_blockBeaconId = g_bond.beaconId;
        if (g_bond.dwellTimeSec > 0) g_platformDwellSec = g_bond.dwellTimeSec;
        if (g_bond.sidingCapacityCm > 0) g_sidingCapacityCm = g_bond.sidingCapacityCm;

        Serial.printf("[Track] Status: BONDED to Master MAC: %02X:%02X:%02X:%02X:%02X:%02X (Role=%s, Beacon=#%u)\n",
                      g_bond.masterMac[0], g_bond.masterMac[1], g_bond.masterMac[2],
                      g_bond.masterMac[3], g_bond.masterMac[4], g_bond.masterMac[5],
                      g_beaconRole == ROLE_STATION_ARRIVAL ? "STATION_ARRIVAL" : "LOCATOR",
                      g_blockBeaconId);
        ESPNowManager::instance().setPairedState(true, g_bond.masterMac);
        g_display.updateSystemInfo(ESPNowManager::instance().getNodeId(), "BONDED", -50);
    }

    // 3. Initialize IR Localization Beacon & Beam Break
    g_ir.beginTransmitter(PIN_IR_BEACON_TX, g_blockBeaconId, 100);
    g_ir.beginReceiver(PIN_IR_BEAM_RX);
    g_ir.enableBeamBreakDetection(300); // 300ms timeout triggers beam break

    g_ir.onOccupancyChanged([](bool occupied) {
        uint32_t now = millis();
        g_track.setOccupied(occupied);
        g_display.updateTrackStatus(g_track.getSwitchPosition(), occupied);

        if (occupied) {
            // Train front nose has just interrupted the IR beam
            g_beamBreakStartMs = now;
            g_lastTrainPassageMs = now;
            Serial.printf("[Track Event] Train nose ENTERED beam -> Sensor OCCUPIED at %lu ms\n", now);

            // Red signal aspect while train is over junction/platform
            g_currentSignalAspect = SIGNAL_RED;

            // If this is a Station Arrival platform beacon, begin dwell stopping sequence
            if (g_beaconRole == ROLE_STATION_ARRIVAL) {
                Serial.printf("[Track Platform] Train arrived at platform! Starting %us dwell timer\n", g_platformDwellSec);
                g_track.startDwellCountdown(g_platformDwellSec);
                sendTrafficRegulation(g_lastPassingLocoId, 0, 0, SIGNAL_RED, true);
            }
        } else {
            // Train rear tail has completely cleared the IR beam (Tail Clearance)
            g_beamBreakEndMs = now;
            uint32_t transitTimeMs = (g_beamBreakEndMs >= g_beamBreakStartMs) ? (g_beamBreakEndMs - g_beamBreakStartMs) : 0;

            // Calculate Train Length: L = v * Delta_t
            // Nominal speed at 35% throttle is ~21 cm/s (speedCmPerSec ≈ throttle% * 0.6)
            int8_t speed = (abs(g_lastLocoSpeedPct) > 0) ? abs(g_lastLocoSpeedPct) : 35;
            float speedCmPerSec = speed * 0.6f;
            float measuredLength = (speedCmPerSec * transitTimeMs) / 1000.0f;
            if (measuredLength < 5.0f && transitTimeMs > 100) measuredLength = 5.0f; // Minimal train dimension
            g_lastMeasuredLengthCm = (uint16_t)round(measuredLength);

            Serial.printf("[Track Event] Train tail CLEARED beam -> Δt=%ums, Speed=%d%%, Length=%ucm\n",
                          transitTimeMs, speed, g_lastMeasuredLengthCm);

            // Broadcast Train Length Report
            MsgTrainLengthReport rep = {};
            rep.msgType = MSG_TRAIN_LENGTH_REPORT;
            strncpy(rep.locoId, g_lastPassingLocoId, sizeof(rep.locoId) - 1);
            strncpy(rep.sensorNodeId, ESPNowManager::instance().getNodeId(), sizeof(rep.sensorNodeId) - 1);
            rep.beaconId = g_blockBeaconId;
            rep.measuredLengthCm = g_lastMeasuredLengthCm;
            rep.transitTimeMs = transitTimeMs;
            ESPNowManager::instance().sendBroadcast(&rep, sizeof(rep));

            // If not dwelling at platform, update signal aspect
            if (!g_track.isDwellActive()) {
                g_currentSignalAspect = SIGNAL_GREEN;
            }
        }

        sendTrackTelemetry();
    });

    // 4. Initialize OLED Station Display
    g_display.begin(PIN_I2C_SDA, PIN_I2C_SCL, 0x3C);
    g_display.setStationName(g_beaconRole == ROLE_STATION_ARRIVAL ? "CENTRAL STATION" : "SECTOR TRACKER");

    // 5. Initialize ESP-NOW
    uint8_t initialChannel = (g_bond.wifiChannel >= 1 && g_bond.wifiChannel <= 13) ? g_bond.wifiChannel : 1;
    ESPNowManager::instance().begin(NODE_TYPE_TRACK, initialChannel);
    ESPNowManager::instance().onReceive(handleIncomingEspNow);

    Serial.printf("[Track] Node ID: %s ready! (Role=%s, Beacon=#%u)\n",
                  ESPNowManager::instance().getNodeId(),
                  g_beaconRole == ROLE_STATION_ARRIVAL ? "STATION_ARRIVAL" : "LOCATOR",
                  g_blockBeaconId);
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
