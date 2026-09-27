#include <Arduino.h>
#include "ESPNowManager.h"
#include "MotorController.h"
#include "LightingSystem.h"
#include "IRTelemetry.h"

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

    if (msgType == MSG_LOCO_COMMAND && len >= (int)sizeof(MsgLocoCommand)) {
        const MsgLocoCommand* cmd = (const MsgLocoCommand*)data;
        // Check if command is for this locomotive or broadcast ALL
        if (strcmp(cmd->targetNodeId, myId) == 0 || strcmp(cmd->targetNodeId, "ALL") == 0) {
            // Speed & Brake
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
        g_motor.emergencyStop();
        g_lights.setMode(LIGHT_MODE_EMERGENCY_FLASH);
        Serial.println(F("[Loco] Emergency stop command received!"));
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
        Serial.printf("[Loco] Entered Track Block #%u\n", blockId);
        // Immediate telemetry notification to Master
        sendTelemetry();
    });

    // 4. Initialize ESP-NOW
    ESPNowManager::instance().begin(NODE_TYPE_LOCO, 1);
    ESPNowManager::instance().onReceive(handleIncomingEspNow);

    Serial.printf("[Loco] Node ID: %s ready!\n", ESPNowManager::instance().getNodeId());
}

void loop() {
    g_motor.update();
    g_lights.updateTrainMovement(g_motor.getCurrentSpeed(), (uint8_t)g_motor.getDirection());
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
