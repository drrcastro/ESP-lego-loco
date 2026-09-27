#include "ScenarioEngine.h"
#include "ESPNowManager.h"
#include "LocoWebServer.h"

ScenarioEngine& ScenarioEngine::instance() {
    static ScenarioEngine inst;
    return inst;
}

ScenarioEngine::ScenarioEngine() {
}

void ScenarioEngine::begin() {
    Serial.println(F("[Scenario] Engine ready."));
}

bool ScenarioEngine::startScenario(const String& scenarioName) {
    if (!ConfigStore::instance().loadScenario(scenarioName, _steps)) {
        Serial.printf("[Scenario] Failed to load scenario: %s\n", scenarioName.c_str());
        return false;
    }

    _activeScenarioName = scenarioName;
    _isRunning = true;
    _isPaused = false;
    _currentStepIdx = 0;
    _timerActive = false;

    Serial.printf("[Scenario] Started scenario '%s' (%u steps loaded)\n",
                  scenarioName.c_str(), _steps.size());

    LocoWebServer::instance().setScenarioRunning(true, scenarioName);

    // Execute immediate START steps
    for (size_t i = 0; i < _steps.size(); i++) {
        if (_steps[i].triggerType.equalsIgnoreCase("START")) {
            executeStep(_steps[i]);
        }
    }

    return true;
}

void ScenarioEngine::stopScenario() {
    _isRunning = false;
    _isPaused = false;
    _timerActive = false;
    _steps.clear();

    Serial.println(F("[Scenario] Scenario stopped."));
    LocoWebServer::instance().setScenarioRunning(false, "");
}

void ScenarioEngine::pauseScenario() {
    _isPaused = true;
    Serial.println(F("[Scenario] Scenario paused."));
}

void ScenarioEngine::resumeScenario() {
    _isPaused = false;
    Serial.println(F("[Scenario] Scenario resumed."));
}

void ScenarioEngine::executeStep(const ScenarioStep& step) {
    Serial.printf("[Scenario] Executing Step #%u: Action=%s, Target=%s, Param=%s\n",
                  step.stepId, step.action.c_str(), step.targetNode.c_str(), step.parameter.c_str());

    LocoWebServer::instance().notifyScenarioStep(step.stepId, step.action, step.targetNode, "EXECUTING");

    if (step.action.equalsIgnoreCase("SET_SPEED")) {
        int8_t speed = (int8_t)step.parameter.toInt();
        sendLocoSpeed(step.targetNode.c_str(), speed);
    } else if (step.action.equalsIgnoreCase("SET_SWITCH")) {
        SwitchState pos = step.parameter.equalsIgnoreCase("TURNOUT") ? SWITCH_TURNOUT : SWITCH_STRAIGHT;
        sendTrackSwitch(step.targetNode.c_str(), pos, 0);
    } else if (step.action.equalsIgnoreCase("SET_LIGHTS")) {
        MsgLocoCommand cmd = {};
        cmd.msgType = MSG_LOCO_COMMAND;
        strncpy(cmd.targetNodeId, step.targetNode.c_str(), sizeof(cmd.targetNodeId) - 1);
        if (step.parameter.equalsIgnoreCase("AUTO")) {
            cmd.lightingMode = LIGHT_MODE_AUTO_DIRECTION;
        } else if (step.parameter.equalsIgnoreCase("FRONT_ON")) {
            cmd.lightingMode = LIGHT_MODE_MANUAL;
            cmd.lightsFront = 255;
        } else if (step.parameter.equalsIgnoreCase("ALL_OFF")) {
            cmd.lightingMode = LIGHT_MODE_MANUAL;
            cmd.lightsFront = 0;
            cmd.lightsRear = 0;
            cmd.lightsCab = 0;
        } else {
            cmd.lightingMode = LIGHT_MODE_AUTO_DIRECTION;
        }
        ESPNowManager::instance().sendToNode(step.targetNode.c_str(), &cmd, sizeof(cmd));
    } else if (step.action.equalsIgnoreCase("DWELL_WAIT")) {
        uint16_t dwellSec = (uint16_t)step.parameter.toInt();
        sendTrackSwitch(step.targetNode.c_str(), SWITCH_STRAIGHT, dwellSec);
    } else if (step.action.equalsIgnoreCase("EMERGENCY_STOP")) {
        MsgEmergencyStop eMsg = {};
        eMsg.msgType = MSG_EMERGENCY_STOP;
        eMsg.reasonCode = 0;
        ESPNowManager::instance().sendBroadcast(&eMsg, sizeof(eMsg));
    } else if (step.action.equalsIgnoreCase("GOTO_STEP")) {
        uint16_t targetStepId = (uint16_t)step.parameter.toInt();
        for (size_t i = 0; i < _steps.size(); i++) {
            if (_steps[i].stepId == targetStepId) {
                _currentStepIdx = i;
                Serial.printf("[Scenario] Branching to Step #%u\n", targetStepId);
                executeStep(_steps[i]);
                return;
            }
        }
    }
}

void ScenarioEngine::checkMatchingTriggers(const String& type, const String& value, const String& node) {
    if (!_isRunning || _isPaused) return;

    for (const auto& step : _steps) {
        if (step.triggerType.equalsIgnoreCase(type)) {
            bool valueMatch = (step.triggerValue.length() == 0 || step.triggerValue == value || step.triggerValue == "*");
            bool nodeMatch  = (step.targetNode.equalsIgnoreCase("ALL") || step.targetNode == node || step.targetNode.length() == 0);

            if (valueMatch) {
                executeStep(step);
            }
        }
    }
}

void ScenarioEngine::onBeaconDetected(const char* locoId, uint16_t blockId) {
    Serial.printf("[Scenario] Hook: Loco %s crossed Beacon #%u\n", locoId, blockId);
    checkMatchingTriggers("IR_BEACON", String(blockId), String(locoId));
}

void ScenarioEngine::onTrackOccupancyChanged(const char* trackId, bool occupied) {
    Serial.printf("[Scenario] Hook: Track %s Occupancy = %s\n", trackId, occupied ? "OCCUPIED" : "CLEARED");
    if (occupied) {
        checkMatchingTriggers("TRACK_OCCUPIED", String(trackId), String(trackId));
    } else {
        checkMatchingTriggers("TRACK_CLEARED", String(trackId), String(trackId));
    }
}

void ScenarioEngine::onDwellCompleted(const char* trackId) {
    Serial.printf("[Scenario] Hook: Track %s Dwell Complete\n", trackId);
    checkMatchingTriggers("DWELL_COMPLETE", String(trackId), String(trackId));
}

void ScenarioEngine::sendLocoSpeed(const char* target, int8_t speed) {
    MsgLocoCommand cmd = {};
    cmd.msgType = MSG_LOCO_COMMAND;
    strncpy(cmd.targetNodeId, target, sizeof(cmd.targetNodeId) - 1);
    cmd.targetSpeed = speed;
    cmd.brake = (speed == 0) ? 1 : 0;
    cmd.lightingMode = LIGHT_MODE_AUTO_DIRECTION;
    ESPNowManager::instance().sendToNode(target, &cmd, sizeof(cmd));
}

void ScenarioEngine::sendTrackSwitch(const char* target, SwitchState state, uint16_t dwell) {
    MsgTrackCommand cmd = {};
    cmd.msgType = MSG_TRACK_COMMAND;
    strncpy(cmd.targetNodeId, target, sizeof(cmd.targetNodeId) - 1);
    cmd.switchPosition = (uint8_t)state;
    cmd.dwellTimeSec = dwell;
    ESPNowManager::instance().sendToNode(target, &cmd, sizeof(cmd));
}

void ScenarioEngine::updateStationEta(const char* stationId, const char* trainName, uint16_t blockId, int8_t currentSpeed) {
    uint16_t etaSec = 0;
    SignalAspect aspect = SIGNAL_GREEN;

    if (currentSpeed == 0) {
        etaSec = 0;
        aspect = SIGNAL_RED;
    } else {
        // Approximate calculation based on remaining distance (e.g. 200 units / speed)
        float absSpd = fabs((float)currentSpeed);
        if (absSpd > 0) {
            etaSec = (uint16_t)(1500.0f / absSpd); // e.g. 50% -> 30s
        }
        aspect = (etaSec < 10) ? SIGNAL_YELLOW : SIGNAL_GREEN;
    }

    MsgStationEtaBroadcast etaMsg = {};
    etaMsg.msgType = MSG_STATION_ETA_BROADCAST;
    strncpy(etaMsg.targetStationId, stationId, sizeof(etaMsg.targetStationId) - 1);
    strncpy(etaMsg.trainName, trainName, sizeof(etaMsg.trainName) - 1);
    etaMsg.etaSeconds = etaSec;
    etaMsg.signalAspect = (uint8_t)aspect;
    strncpy(etaMsg.destination, "PLATFORM 1", sizeof(etaMsg.destination) - 1);

    ESPNowManager::instance().sendToNode(stationId, &etaMsg, sizeof(etaMsg));
}

void ScenarioEngine::update() {
    if (!_isRunning || _isPaused) return;

    // Check timer triggers
    if (_timerActive && (millis() >= _timerTargetMs)) {
        _timerActive = false;
        checkMatchingTriggers("TIMER", String(_timerTriggerMs), "");
    }
}
