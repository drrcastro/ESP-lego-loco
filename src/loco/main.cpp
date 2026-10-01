#include <Arduino.h>
#include <EEPROM.h>
#include "ESPNowManager.h"
#include "MotorController.h"
#include "LightingSystem.h"
#include "IRTelemetry.h"

#define EEPROM_SIZE 64
#define LOCO_BOND_MAGIC 0x4C4F4332 // "LOC2"

struct LocoBondConfig {
    uint32_t magic;         // LOCO_BOND_MAGIC
    uint8_t  isPaired;      // 1 = bonded to master, 0 = unpaired
    uint8_t  masterMac[6];  // Bonded Master MAC
    uint8_t  wifiChannel;   // Wi-Fi Channel
    uint8_t  reserved[20];
};

static LocoBondConfig g_bond = {};
static uint32_t g_lastUnpairedBlinkMs = 0;
static bool g_unpairedLedState = false;

// =================================================================
// Pin Assignments (ESP32-C3 vs ESP8266)
// =================================================================
#if defined(ESP32)
  #define PIN_MOTOR_IA    4
  #define PIN_MOTOR_IB    5
  #define PIN_LED_FRONT   6
  #define PIN_LED_REAR    7
  #define PIN_LED_CAB     8
  #define PIN_IR_RX       3
  #define PIN_BATTERY_ADC 0
#elif defined(ESP8266)
  #define PIN_MOTOR_IA    5  // D1
  #define PIN_MOTOR_IB    4  // D2
  #define PIN_LED_FRONT   14 // D5
  #define PIN_LED_REAR    12 // D6
  #define PIN_LED_CAB     13 // D7
  #define PIN_IR_RX       0  // D3
  #define PIN_BATTERY_ADC A0
#endif

// Hardware Controller Singletons
static MotorController g_motor;
static LightingSystem  g_lights;
static IRTelemetry     g_ir;

static uint16_t g_currentBlockId = 0;
static uint32_t g_lastTelemetryMs = 0;

// Autonomous Learning & Speed Management
#define MAX_LEARNED_BEACONS 16

struct LearnedSegment {
    uint16_t fromBeaconId;
    uint16_t toBeaconId;
    uint32_t transitTimeMs;
};

static LearnedSegment g_learnedMap[MAX_LEARNED_BEACONS] = {};
static uint8_t  g_learnedSegmentCount = 0;
static uint16_t g_discoveredBeacons[MAX_LEARNED_BEACONS] = {};
static uint8_t  g_discoveredBeaconCount = 0;
static uint32_t g_learningStartMs = 0;
static uint32_t g_lastBeaconMs = 0;
static uint32_t g_totalLapTimeMs = 0;
static bool     g_isCalibrated = false;

static LocoState g_locoState = LOCO_STATE_MANUAL;
static int8_t   g_cruiseSpeed = 50;
static uint8_t  g_calibrationSpeed = 35;
static uint16_t g_etaSeconds = 0;
static uint16_t g_measuredLengthCm = 0;
static uint8_t  g_lapCount = 0;

static uint32_t g_dwellUntilMs = 0;
static bool     g_isDwelling = false;
static int8_t   g_trafficSpeedTrimPct = 0;
static bool     g_trafficHold = false;

static uint16_t readBatteryMv() {
#if defined(PIN_BATTERY_ADC)
    int raw = analogRead(PIN_BATTERY_ADC);
    // Assumes 1/2 resistor divider for 1S LiPo (3.0V - 4.2V)
    #if defined(ESP32)
      return (uint16_t)((raw / 4095.0f) * 3300.0f * 2.0f);
    #elif defined(ESP8266)
      return (uint16_t)((raw / 1023.0f) * 1000.0f * 4.2f);
    #endif
#else
    return 0;
#endif
}

static void sendTelemetry() {
    MsgLocoTelemetry telem = {};
    telem.msgType = MSG_LOCO_TELEMETRY;
    strncpy(telem.nodeId, ESPNowManager::instance().getNodeId(), sizeof(telem.nodeId) - 1);
    telem.currentSpeed = g_motor.getCurrentSpeed();
    telem.direction = (uint8_t)g_motor.getDirection();
    telem.currentBlockId = g_currentBlockId;
    telem.batteryMv = readBatteryMv();
    telem.timestampMs = millis();
    telem.locoState = (uint8_t)g_locoState;
    telem.etaSeconds = g_etaSeconds;
    telem.measuredLengthCm = g_measuredLengthCm;
    telem.lapCount = g_lapCount;

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

    const char* dirStr = (telem.direction == 1 ? "FWD" : (telem.direction == 2 ? "REV" : "STOP"));
    Serial.printf("[Loco TX -> %s] Telem: Spd=%d%%, Dir=%s, B#%u, State=%u, Bat=%umV, ETA=%us, Lap=%u\n",
                  destDesc, telem.currentSpeed, dirStr, telem.currentBlockId,
                  (unsigned int)telem.locoState, telem.batteryMv, telem.etaSeconds, telem.lapCount);
}

static void handleIncomingEspNow(const uint8_t* mac, const uint8_t* data, int len) {
    if (len <= 0 || !data) return;
    uint8_t msgType = data[0];

    const char* myId = ESPNowManager::instance().getNodeId();

    // 1. Handle Pairing Confirmation from Master
    if (msgType == MSG_PAIR_CONFIRM && len >= (int)sizeof(MsgPairConfirm)) {
        const MsgPairConfirm* pairCmd = (const MsgPairConfirm*)data;
        Serial.printf("[Loco RX <- %02X:%02X:%02X:%02X:%02X:%02X] MSG_PAIR_CONFIRM: Target='%s', Ch=%u\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      pairCmd->targetNodeId, pairCmd->wifiChannel);

        if (strcmp(pairCmd->targetNodeId, myId) == 0 || strcmp(pairCmd->targetNodeId, "ALL") == 0) {
            if (g_bond.isPaired == 1 && memcmp(g_bond.masterMac, pairCmd->masterMac, 6) != 0) {
                Serial.printf("[Loco RX] Pairing REJECTED: Already bonded to Master!\n");
                return;
            }

            g_bond.magic = LOCO_BOND_MAGIC;
            g_bond.isPaired = 1;
            memcpy(g_bond.masterMac, pairCmd->masterMac, 6);
            g_bond.wifiChannel = pairCmd->wifiChannel;
            EEPROM.put(0, g_bond);
            EEPROM.commit();

            ESPNowManager::instance().setPairedState(true, g_bond.masterMac);
            Serial.printf("[Loco] PAIRED & BONDED to Master saved in EEPROM.\n");

            // Visual confirmation blink (3 flashes)
            for (int i = 0; i < 3; i++) {
                g_lights.setBrightness(ZONE_FRONT, 255);
                g_lights.setBrightness(ZONE_CAB, 255);
                delay(120);
                g_lights.setBrightness(ZONE_FRONT, 0);
                g_lights.setBrightness(ZONE_CAB, 0);
                delay(100);
            }
            g_lights.setMode(LIGHT_MODE_AUTO_DIRECTION);

            ESPNowManager::instance().announcePresence();
            sendTelemetry();
        }
        return;
    }

    // 2. Handle Unpair / Removal from Master
    if (msgType == MSG_UNPAIR && len >= (int)sizeof(MsgUnpair)) {
        const MsgUnpair* unpCmd = (const MsgUnpair*)data;
        Serial.printf("[Loco RX <- %02X:%02X:%02X:%02X:%02X:%02X] MSG_UNPAIR: Target='%s'\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      unpCmd->targetNodeId);

        if (strcmp(unpCmd->targetNodeId, myId) == 0 || strcmp(unpCmd->targetNodeId, "ALL") == 0) {
            if (g_bond.isPaired == 1 && memcmp(g_bond.masterMac, unpCmd->masterMac, 6) != 0 && memcmp(mac, g_bond.masterMac, 6) != 0) {
                Serial.println(F("[Loco RX] UNPAIR REJECTED: Sender is not our bonded Master!"));
                return;
            }

            Serial.println(F("[Loco] UNPAIR ACCEPTED. Clearing EEPROM bond and resetting to unpaired mode..."));

            // 1. Halt motor safely
            g_motor.emergencyStop();

            // 2. Clear EEPROM bonding
            g_bond.isPaired = 0;
            memset(g_bond.masterMac, 0, 6);
            g_bond.magic = LOCO_BOND_MAGIC;
            EEPROM.put(0, g_bond);
            EEPROM.commit();

            // 3. Clear ESPNowManager paired status
            ESPNowManager::instance().setPairedState(false, nullptr);

            // 4. Reset internal state
            g_locoState = LOCO_STATE_MANUAL;
            g_isCalibrated = false;
            g_discoveredBeaconCount = 0;
            g_learnedSegmentCount = 0;
            g_isDwelling = false;
            g_trafficHold = false;
            g_currentBlockId = 0;
            g_lapCount = 0;

            // 5. Visual unpair confirmation: 4 rapid flashes
            for (int i = 0; i < 4; i++) {
                g_lights.setBrightness(ZONE_FRONT, 255);
                g_lights.setBrightness(ZONE_CAB, 255);
                delay(80);
                g_lights.setBrightness(ZONE_FRONT, 0);
                g_lights.setBrightness(ZONE_CAB, 0);
                delay(80);
            }
            g_lights.setMode(LIGHT_MODE_MANUAL);

            // 6. Broadcast discovery announcement so Master sees us as unpaired immediately
            ESPNowManager::instance().announcePresence();
            sendTelemetry();
            Serial.println(F("[Loco] Successfully UNPAIRED! Ready to pair again without reflashing."));
        }
        return;
    }

    // 3. Security Check: If paired, accept only from bonded Master or valid Smart Station
    if (g_bond.isPaired == 1) {
        // Accept MSG_TRAFFIC_REGULATION or commands
        if (memcmp(mac, g_bond.masterMac, 6) != 0 && msgType != MSG_TRAFFIC_REGULATION && msgType != MSG_EMERGENCY_STOP) {
            return;
        }
    } else {
        if (msgType == MSG_LOCO_COMMAND || msgType == MSG_LEARNING_CMD) {
            Serial.printf("[Loco RX] REJECTED command: Locomotive is UNPAIRED.\n");
            return;
        }
    }

    // Radio Link Vitality: ANY packet received from our bonded Master proves the link is alive
    if (g_bond.isPaired == 1 && memcmp(mac, g_bond.masterMac, 6) == 0) {
        g_motor.feedWatchdog();
    }

    // 3. Process Valid Locomotive Commands
    if (msgType == MSG_LOCO_COMMAND && len >= (int)sizeof(MsgLocoCommand)) {
        const MsgLocoCommand* cmd = (const MsgLocoCommand*)data;
        if (strcmp(cmd->targetNodeId, myId) == 0 || strcmp(cmd->targetNodeId, "ALL") == 0) {
            // Log command only when setpoint or state changes to avoid log flooding on periodic keepalives
            if (cmd->targetSpeed != g_motor.getTargetSpeed() || cmd->brake != (uint8_t)g_motor.getDirection() || g_locoState != LOCO_STATE_MANUAL) {
                Serial.printf("[Loco RX <- Master] MSG_LOCO_COMMAND: Target=%s, Spd=%d%%, Brake=%u, Lights=%u (F:%u R:%u Cab:%u)\n",
                              cmd->targetNodeId, cmd->targetSpeed, cmd->brake, cmd->lightingMode,
                              cmd->lightsFront, cmd->lightsRear, cmd->lightsCab);
            }

            // Manual command switches out of autonomous or learning mode
            g_locoState = LOCO_STATE_MANUAL;
            g_isDwelling = false;
            g_trafficHold = false;

            if (cmd->brake == 2) {
                g_motor.emergencyStop();
            } else if (cmd->brake == 1) {
                g_motor.brake();
            } else {
                g_motor.setTargetSpeed(cmd->targetSpeed);
            }

            // Lighting
            g_lights.setMode((LightingMode)cmd->lightingMode);
            if (cmd->lightingMode == LIGHT_MODE_MANUAL) {
                g_lights.setBrightness(ZONE_FRONT, cmd->lightsFront);
                g_lights.setBrightness(ZONE_REAR, cmd->lightsRear);
                g_lights.setBrightness(ZONE_CAB, cmd->lightsCab);
            }
            g_motor.feedWatchdog();
        }
    } else if (msgType == MSG_LEARNING_CMD && len >= (int)sizeof(MsgLearningCmd)) {
        const MsgLearningCmd* lCmd = (const MsgLearningCmd*)data;
        if (strcmp(lCmd->targetLocoId, myId) == 0 || strcmp(lCmd->targetLocoId, "ALL") == 0) {
            if (lCmd->command == 1) {
                // START_LEARNING
                g_locoState = LOCO_STATE_LEARNING;
                g_calibrationSpeed = (lCmd->calibrationSpeed >= 20 && lCmd->calibrationSpeed <= 80) ? lCmd->calibrationSpeed : 35;
                g_discoveredBeaconCount = 0;
                g_learnedSegmentCount = 0;
                g_isCalibrated = false;
                g_learningStartMs = millis();
                g_lastBeaconMs = millis();
                g_isDwelling = false;
                g_trafficHold = false;

                Serial.printf("[Loco Learning] STARTING reconnaissance lap at %u%% speed\n", g_calibrationSpeed);
                g_lights.setMode(LIGHT_MODE_AUTO_DIRECTION);
                g_motor.setTargetSpeed(g_calibrationSpeed);
                g_motor.feedWatchdog();
            } else if (lCmd->command == 0) {
                // STOP_LEARNING
                Serial.println(F("[Loco Learning] ABORTED. Returning to manual stop."));
                g_locoState = LOCO_STATE_MANUAL;
                g_motor.setTargetSpeed(0);
            } else if (lCmd->command == 2) {
                // RESET_CALIBRATION
                Serial.println(F("[Loco Learning] Calibration cleared."));
                g_isCalibrated = false;
                g_discoveredBeaconCount = 0;
                g_learnedSegmentCount = 0;
                g_locoState = LOCO_STATE_MANUAL;
            } else if (lCmd->command == 3) {
                // START_AUTONOMOUS
                g_locoState = LOCO_STATE_AUTONOMOUS;
                g_isDwelling = false;
                g_trafficHold = false;
                g_motor.setTargetSpeed(g_cruiseSpeed);
                g_motor.feedWatchdog();
                Serial.printf("[Loco] Switched to AUTONOMOUS mode at cruise speed %d%%\n", g_cruiseSpeed);
            }
        }
    } else if (msgType == MSG_TRAFFIC_REGULATION && len >= (int)sizeof(MsgTrafficRegulation)) {
        const MsgTrafficRegulation* reg = (const MsgTrafficRegulation*)data;
        if (strcmp(reg->targetLocoId, myId) == 0 || strcmp(reg->targetLocoId, "ALL") == 0) {
            Serial.printf("[Loco RX <- Station] Traffic Reg: SpeedCap=%d, Trim=%d%%, Signal=%u, Hold=%u\n",
                          reg->speedCap, reg->speedTrimPct, reg->signalAspect, reg->holdTrain);

            g_trafficSpeedTrimPct = reg->speedTrimPct;
            g_trafficHold = (reg->holdTrain == 1 || reg->signalAspect == SIGNAL_RED);

            if (g_trafficHold) {
                g_motor.brake();
            } else if (g_locoState == LOCO_STATE_AUTONOMOUS) {
                int8_t effectiveSpd = g_cruiseSpeed;
                if (reg->speedCap > 0 && effectiveSpd > reg->speedCap) {
                    effectiveSpd = reg->speedCap;
                }
                if (g_trafficSpeedTrimPct > 0) {
                    effectiveSpd = (int8_t)(effectiveSpd * (100 - g_trafficSpeedTrimPct) / 100);
                }
                g_motor.setTargetSpeed(effectiveSpd);
            }
            g_motor.feedWatchdog();
        }
    } else if (msgType == MSG_TRAIN_LENGTH_REPORT && len >= (int)sizeof(MsgTrainLengthReport)) {
        const MsgTrainLengthReport* rep = (const MsgTrainLengthReport*)data;
        if (strcmp(rep->locoId, myId) == 0 || strcmp(rep->locoId, "ALL") == 0) {
            g_measuredLengthCm = rep->measuredLengthCm;
            Serial.printf("[Loco] Updated Measured Length = %ucm\n", g_measuredLengthCm);
        }
    } else if (msgType == MSG_EMERGENCY_STOP) {
        Serial.println(F("[Loco RX] EMERGENCY STOP Activated!"));
        g_motor.emergencyStop();
        g_lights.setMode(LIGHT_MODE_EMERGENCY_FLASH);
        g_locoState = LOCO_STATE_MANUAL;
        g_isDwelling = false;
        g_trafficHold = false;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F("  LEGO TRAIN AUTONOMOUS LOCOMOTIVE FIRMWARE       "));
    Serial.println(F("=================================================="));

    // 1. Initialize Motors
    g_motor.begin(PIN_MOTOR_IA, PIN_MOTOR_IB);
    g_motor.setAccelerationRate(40.0f); // 40%/s acceleration
    g_motor.setDecelerationRate(60.0f); // 60%/s deceleration
    g_motor.setWatchdogTimeout(4000);   // 4-second safety watchdog

    // 2. Initialize Lighting System
    g_lights.begin(PIN_LED_FRONT, PIN_LED_REAR, PIN_LED_CAB);
    g_lights.setMode(LIGHT_MODE_AUTO_DIRECTION);

    // 3. Initialize IR Receiver for Track Beacon Localization
    g_ir.beginReceiver(PIN_IR_RX);
    g_ir.onBeaconDetected([](uint16_t blockId) {
        g_currentBlockId = blockId;
        uint32_t now = millis();
        Serial.printf("[Loco RX-IR] Beacon Detected: Block #%u\n", blockId);

        if (g_locoState == LOCO_STATE_LEARNING) {
            if (g_discoveredBeaconCount == 0) {
                // First beacon in reconnaissance lap
                g_discoveredBeacons[g_discoveredBeaconCount++] = blockId;
                g_lastBeaconMs = now;
                Serial.printf("[Loco Learning] First Beacon #%u marked as Loop Origin\n", blockId);
            } else {
                // Check if loop closure detected (re-visited initial beacon after at least 2 beacons)
                if (blockId == g_discoveredBeacons[0] && g_discoveredBeaconCount >= 2) {
                    uint32_t dt = now - g_lastBeaconMs;
                    if (g_learnedSegmentCount < MAX_LEARNED_BEACONS) {
                        g_learnedMap[g_learnedSegmentCount].fromBeaconId = g_discoveredBeacons[g_discoveredBeaconCount - 1];
                        g_learnedMap[g_learnedSegmentCount].toBeaconId = blockId;
                        g_learnedMap[g_learnedSegmentCount].transitTimeMs = dt;
                        g_learnedSegmentCount++;
                    }
                    g_totalLapTimeMs = now - g_learningStartMs;
                    g_isCalibrated = true;
                    g_locoState = LOCO_STATE_AUTONOMOUS;
                    g_lapCount = 1;

                    Serial.printf("[Loco Learning] LOOP CLOSURE! Calibrated %u beacons, Lap=%ums. Entering AUTONOMOUS mode!\n",
                                  g_discoveredBeaconCount, g_totalLapTimeMs);

                    // Notify Master Gateway
                    MsgCircuitStatus cs = {};
                    cs.msgType = MSG_CIRCUIT_STATUS;
                    strncpy(cs.locoId, ESPNowManager::instance().getNodeId(), sizeof(cs.locoId) - 1);
                    cs.state = (uint8_t)LOCO_STATE_AUTONOMOUS;
                    cs.beaconCount = g_discoveredBeaconCount;
                    cs.totalLapTimeMs = g_totalLapTimeMs;
                    cs.isCalibrated = 1;
                    if (g_bond.isPaired == 1) {
                        ESPNowManager::instance().sendUnicast(g_bond.masterMac, &cs, sizeof(cs));
                    } else {
                        ESPNowManager::instance().sendBroadcast(&cs, sizeof(cs));
                    }

                    // Rapid celebratory headlight flashes
                    for (int i = 0; i < 3; i++) {
                        g_lights.setBrightness(ZONE_FRONT, 255);
                        delay(120);
                        g_lights.setBrightness(ZONE_FRONT, 0);
                        delay(100);
                    }
                    g_lights.setMode(LIGHT_MODE_AUTO_DIRECTION);

                    // Resume cruising
                    g_motor.setTargetSpeed(g_cruiseSpeed);
                } else {
                    // Record intermediate segment
                    bool alreadyVisited = false;
                    for (uint8_t i = 0; i < g_discoveredBeaconCount; i++) {
                        if (g_discoveredBeacons[i] == blockId) { alreadyVisited = true; break; }
                    }
                    if (!alreadyVisited && g_discoveredBeaconCount < MAX_LEARNED_BEACONS) {
                        uint32_t dt = now - g_lastBeaconMs;
                        if (g_learnedSegmentCount < MAX_LEARNED_BEACONS) {
                            g_learnedMap[g_learnedSegmentCount].fromBeaconId = g_discoveredBeacons[g_discoveredBeaconCount - 1];
                            g_learnedMap[g_learnedSegmentCount].toBeaconId = blockId;
                            g_learnedMap[g_learnedSegmentCount].transitTimeMs = dt;
                            g_learnedSegmentCount++;
                        }
                        g_discoveredBeacons[g_discoveredBeaconCount++] = blockId;
                        g_lastBeaconMs = now;
                        Serial.printf("[Loco Learning] Added Beacon #%u (dt=%ums)\n", blockId, dt);
                    }
                }
            }
        } else if (g_locoState == LOCO_STATE_AUTONOMOUS) {
            // Check ETA to next expected segment
            uint32_t expectedDt = 5000;
            for (uint8_t i = 0; i < g_learnedSegmentCount; i++) {
                if (g_learnedMap[i].fromBeaconId == blockId) {
                    expectedDt = g_learnedMap[i].transitTimeMs;
                    break;
                }
            }
            float curSpd = fabs((float)g_motor.getCurrentSpeed());
            if (curSpd > 0 && g_calibrationSpeed > 0) {
                g_etaSeconds = (uint16_t)((expectedDt * g_calibrationSpeed / curSpd) / 1000.0f);
            }
            if (g_discoveredBeaconCount > 0 && blockId == g_discoveredBeacons[0]) {
                g_lapCount++;
            }
        }

        sendTelemetry();
    });

    // 4. Initialize EEPROM and check Master Bonding status
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, g_bond);
    if (g_bond.magic != LOCO_BOND_MAGIC || g_bond.isPaired != 1) {
        g_bond.magic = LOCO_BOND_MAGIC;
        g_bond.isPaired = 0;
        memset(g_bond.masterMac, 0, 6);
        g_bond.wifiChannel = 1;
        Serial.println(F("[Loco] Status: UNPAIRED (Ready to pair with Master via Web UI)."));
    } else {
        Serial.printf("[Loco] Status: BONDED to Master MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      g_bond.masterMac[0], g_bond.masterMac[1], g_bond.masterMac[2],
                      g_bond.masterMac[3], g_bond.masterMac[4], g_bond.masterMac[5]);
        ESPNowManager::instance().setPairedState(true, g_bond.masterMac);
    }

    // 5. Initialize ESP-NOW
    uint8_t initialChannel = (g_bond.wifiChannel >= 1 && g_bond.wifiChannel <= 13) ? g_bond.wifiChannel : 1;
    ESPNowManager::instance().begin(NODE_TYPE_LOCO, initialChannel);
    ESPNowManager::instance().onReceive(handleIncomingEspNow);
    ESPNowManager::instance().onSend([](const uint8_t* mac, bool success) {
        // Hardware 802.11 ACK from Master confirms link health and feeds watchdog
        if (success && g_bond.isPaired == 1 && memcmp(mac, g_bond.masterMac, 6) == 0) {
            g_motor.feedWatchdog();
        }
    });

    Serial.printf("[Loco] Node ID: %s ready!\n", ESPNowManager::instance().getNodeId());
}

void loop() {
    g_motor.update();

    if (g_bond.isPaired == 1) {
        g_lights.updateTrainMovement(g_motor.getCurrentSpeed(), (uint8_t)g_motor.getDirection());
    } else {
        // Unpaired indicator: gentle alternating pulse
        uint32_t nowMs = millis();
        if (nowMs - g_lastUnpairedBlinkMs >= 600) {
            g_lastUnpairedBlinkMs = nowMs;
            g_unpairedLedState = !g_unpairedLedState;
            g_lights.setMode(LIGHT_MODE_MANUAL);
            g_lights.setBrightness(ZONE_CAB, g_unpairedLedState ? 160 : 0);
            g_lights.setBrightness(ZONE_FRONT, g_unpairedLedState ? 0 : 160);
        }
    }

    // In Autonomous / Learning modes, feed the watchdog
    if (g_locoState == LOCO_STATE_LEARNING || g_locoState == LOCO_STATE_AUTONOMOUS) {
        g_motor.feedWatchdog();
    }

    // Dwell handling
    if (g_isDwelling && (millis() >= g_dwellUntilMs)) {
        g_isDwelling = false;
        Serial.println(F("[Loco] Dwell complete. Resuming cruise."));
        if (!g_trafficHold) {
            g_motor.setTargetSpeed(g_cruiseSpeed);
        }
    }

    g_lights.update();
    g_ir.update();
    ESPNowManager::instance().update();

    // Periodic telemetry (every 1000ms)
    uint32_t now = millis();
    if (now - g_lastTelemetryMs >= 1000) {
        g_lastTelemetryMs = now;
        sendTelemetry();
    }

    delay(2);
}
