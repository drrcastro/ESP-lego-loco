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
    Serial.printf("[Loco TX -> %s] Telemetry: Spd=%d%%, Dir=%s, Block=#%u, Bat=%umV\n",
                  destDesc, telem.currentSpeed, dirStr, telem.currentBlockId, telem.batteryMv);
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
                Serial.printf("[Loco RX] Pairing REJECTED: Already bonded to Master %02X:%02X:%02X:%02X:%02X:%02X!\n",
                              g_bond.masterMac[0], g_bond.masterMac[1], g_bond.masterMac[2],
                              g_bond.masterMac[3], g_bond.masterMac[4], g_bond.masterMac[5]);
                return;
            }

            g_bond.magic = LOCO_BOND_MAGIC;
            g_bond.isPaired = 1;
            memcpy(g_bond.masterMac, pairCmd->masterMac, 6);
            g_bond.wifiChannel = pairCmd->wifiChannel;
            EEPROM.put(0, g_bond);
            EEPROM.commit();

            ESPNowManager::instance().setPairedState(true, g_bond.masterMac);
            Serial.printf("[Loco] PAIRED & BONDED to Master: %02X:%02X:%02X:%02X:%02X:%02X! Saved in EEPROM.\n",
                          g_bond.masterMac[0], g_bond.masterMac[1], g_bond.masterMac[2],
                          g_bond.masterMac[3], g_bond.masterMac[4], g_bond.masterMac[5]);

            // Rapid visual confirmation blink (3 flashes)
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

    // 2. Security Check: If locomotive is paired, accept commands ONLY from its bonded Master!
    if (g_bond.isPaired == 1) {
        if (memcmp(mac, g_bond.masterMac, 6) != 0) {
            // Command is from an unauthorized transmitter or adjacent layout. Discard!
            Serial.printf("[Loco RX] REJECTED packet from unauthorized MAC %02X:%02X:%02X:%02X:%02X:%02X (bonded to %02X:%02X:%02X:%02X:%02X:%02X)\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                          g_bond.masterMac[0], g_bond.masterMac[1], g_bond.masterMac[2],
                          g_bond.masterMac[3], g_bond.masterMac[4], g_bond.masterMac[5]);
            return;
        }
    } else {
        // If UNPAIRED, locomotive does not drive until paired with Master
        if (msgType == MSG_LOCO_COMMAND) {
            Serial.printf("[Loco RX] REJECTED MSG_LOCO_COMMAND from %02X:%02X:%02X:%02X:%02X:%02X: Locomotive is UNPAIRED. Pair via Web UI.\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return;
        }
    }

    // 3. Process Valid Locomotive Commands
    if (msgType == MSG_LOCO_COMMAND && len >= (int)sizeof(MsgLocoCommand)) {
        const MsgLocoCommand* cmd = (const MsgLocoCommand*)data;
        if (strcmp(cmd->targetNodeId, myId) == 0 || strcmp(cmd->targetNodeId, "ALL") == 0) {
            Serial.printf("[Loco RX <- Master] MSG_LOCO_COMMAND: Target=%s, Spd=%d%%, Brake=%u, Lights=%u (F:%u R:%u Cab:%u)\n",
                          cmd->targetNodeId, cmd->targetSpeed, cmd->brake, cmd->lightingMode,
                          cmd->lightsFront, cmd->lightsRear, cmd->lightsCab);

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
    } else if (msgType == MSG_EMERGENCY_STOP) {
        Serial.printf("[Loco RX <- Master] MSG_EMERGENCY_STOP from %02X:%02X:%02X:%02X:%02X:%02X -> Emergency Stop Activated!\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        g_motor.emergencyStop();
        g_lights.setMode(LIGHT_MODE_EMERGENCY_FLASH);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F("  LEGO TRAIN LOCOMOTIVE FIRMWARE (V2.0)           "));
    Serial.println(F("=================================================="));

    // 1. Initialize Motors
    g_motor.begin(PIN_MOTOR_IA, PIN_MOTOR_IB);
    g_motor.setAccelerationRate(40.0f); // Smooth 40%/s acceleration
    g_motor.setDecelerationRate(60.0f); // 60%/s deceleration
    g_motor.setWatchdogTimeout(4000);   // 4-second safety stop

    // 2. Initialize Lighting System
    g_lights.begin(PIN_LED_FRONT, PIN_LED_REAR, PIN_LED_CAB);
    g_lights.setMode(LIGHT_MODE_AUTO_DIRECTION);

    // 3. Initialize IR Receiver for Track Beacon Localization
    g_ir.beginReceiver(PIN_IR_RX);
    g_ir.onBeaconDetected([](uint16_t blockId) {
        g_currentBlockId = blockId;
        Serial.printf("[Loco RX-IR] Track Beacon Detected: Block #%u -> Triggering immediate telemetry\n", blockId);
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

    Serial.printf("[Loco] Node ID: %s ready!\n", ESPNowManager::instance().getNodeId());
}

void loop() {
    g_motor.update();

    if (g_bond.isPaired == 1) {
        g_lights.updateTrainMovement(g_motor.getCurrentSpeed(), (uint8_t)g_motor.getDirection());
    } else {
        // Unpaired indicator: gentle alternating pulse between cab & headlights
        uint32_t nowMs = millis();
        if (nowMs - g_lastUnpairedBlinkMs >= 600) {
            g_lastUnpairedBlinkMs = nowMs;
            g_unpairedLedState = !g_unpairedLedState;
            g_lights.setMode(LIGHT_MODE_MANUAL);
            g_lights.setBrightness(ZONE_CAB, g_unpairedLedState ? 160 : 0);
            g_lights.setBrightness(ZONE_FRONT, g_unpairedLedState ? 0 : 160);
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
