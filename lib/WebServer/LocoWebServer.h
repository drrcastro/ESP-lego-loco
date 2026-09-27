#pragma once
#include <Arduino.h>
#include <vector>
#include <functional>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "ConfigStore.h"

enum OperatingMode : uint8_t {
    MODE_MANUAL    = 0,
    MODE_AUTOMATIC = 1
};

class LocoWebServer {
public:
    using LocoControlCallback = std::function<void(const String& target, int8_t speed, uint8_t brake, uint8_t front, uint8_t rear, uint8_t cab, uint8_t lightMode)>;
    using TrackControlCallback = std::function<void(const String& target, uint8_t switchPos, uint16_t dwell)>;
    using EmergencyStopCallback = std::function<void()>;
    using ScenarioRunCallback = std::function<void(const String& scenarioName, bool run)>;
    using ModeChangeCallback = std::function<void(OperatingMode mode)>;

    static LocoWebServer& instance();

    bool begin(uint16_t port = 80);

    // Callbacks for hardware & scenario orchestration
    void onLocoControl(LocoControlCallback cb) { _onLocoControlCb = cb; }
    void onTrackControl(TrackControlCallback cb) { _onTrackControlCb = cb; }
    void onEmergencyStop(EmergencyStopCallback cb) { _onEmergencyStopCb = cb; }
    void onScenarioRun(ScenarioRunCallback cb) { _onScenarioRunCb = cb; }
    void onModeChange(ModeChangeCallback cb) { _onModeChangeCb = cb; }

    // WebSocket real-time push methods
    void broadcastWs(const String& jsonPayload);
    void broadcastTelemetry(const String& type, const JsonDocument& data);
    void notifyScenarioStep(uint16_t stepId, const String& action, const String& target, const String& status);

    // Operating mode state
    OperatingMode getOperatingMode() const { return _currentMode; }
    void setOperatingMode(OperatingMode mode) { _currentMode = mode; }
    bool isScenarioRunning() const { return _scenarioRunning; }
    void setScenarioRunning(bool running, const String& scenarioName = "") {
        _scenarioRunning = running;
        _activeScenarioName = scenarioName;
    }

private:
    LocoWebServer();
    ~LocoWebServer() = default;

    AsyncWebServer _server{80};
    AsyncWebSocket _ws{"/ws"};

    OperatingMode _currentMode = MODE_MANUAL;
    bool _scenarioRunning = false;
    String _activeScenarioName = "default.csv";

    LocoControlCallback   _onLocoControlCb = nullptr;
    TrackControlCallback  _onTrackControlCb = nullptr;
    EmergencyStopCallback _onEmergencyStopCb = nullptr;
    ScenarioRunCallback   _onScenarioRunCb = nullptr;
    ModeChangeCallback    _onModeChangeCb = nullptr;

    void setupRoutes();
    void setupWebSocket();
    void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
};
