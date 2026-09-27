#pragma once
#include <Arduino.h>
#include <vector>
#include "ConfigStore.h"
#include "ProtocolMessages.h"

class ScenarioEngine {
public:
    static ScenarioEngine& instance();

    void begin();
    void update();

    // Execution control
    bool startScenario(const String& scenarioName);
    void stopScenario();
    void pauseScenario();
    void resumeScenario();
    bool isRunning() const { return _isRunning; }
    const String& getActiveScenarioName() const { return _activeScenarioName; }

    // Event hooks called on incoming telemetry
    void onBeaconDetected(const char* locoId, uint16_t blockId);
    void onTrackOccupancyChanged(const char* trackId, bool occupied);
    void onDwellCompleted(const char* trackId);

    // ETA calculation & Station broadcasting
    void updateStationEta(const char* stationId, const char* trainName, uint16_t blockId, int8_t currentSpeed);

private:
    ScenarioEngine();
    ~ScenarioEngine() = default;

    bool _isRunning = false;
    bool _isPaused = false;
    String _activeScenarioName;
    std::vector<ScenarioStep> _steps;
    size_t _currentStepIdx = 0;

    uint32_t _timerTriggerMs = 0;
    uint32_t _timerTargetMs = 0;
    bool     _timerActive = false;

    void executeStep(const ScenarioStep& step);
    void advanceNextStep();
    void checkMatchingTriggers(const String& type, const String& value, const String& node);
    void sendLocoSpeed(const char* target, int8_t speed);
    void sendTrackSwitch(const char* target, SwitchState state, uint16_t dwell = 0);
};
