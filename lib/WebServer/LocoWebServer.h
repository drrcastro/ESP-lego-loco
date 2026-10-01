#pragma once
#include <Arduino.h>
#include <vector>
#include <functional>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "ConfigStore.h"

enum OperatingMode : uint8_t {
    MODE_MANUAL     = 0,
    MODE_AUTONOMOUS = 1
};

class LocoWebServer {
public:
    using LocoControlCallback = std::function<void(const String& target, int8_t speed, uint8_t brake, uint8_t front, uint8_t rear, uint8_t cab, uint8_t lightMode)>;
    using TrackControlCallback = std::function<void(const String& target, uint8_t switchPos, uint16_t dwell, uint8_t switchIndex)>;
    using EmergencyStopCallback = std::function<void()>;
    using LearningLapCallback = std::function<void(const String& locoId, bool start, uint8_t speed)>;
    using ModeChangeCallback = std::function<void(OperatingMode mode)>;
    using ConfigUpdatedCallback = std::function<void()>;

    static LocoWebServer& instance();

    bool begin(uint16_t port = 80);

    // Callbacks for hardware & autonomous orchestration
    void onLocoControl(LocoControlCallback cb) { _onLocoControlCb = cb; }
    void onTrackControl(TrackControlCallback cb) { _onTrackControlCb = cb; }
    void onEmergencyStop(EmergencyStopCallback cb) { _onEmergencyStopCb = cb; }
    void onLearningLap(LearningLapCallback cb) { _onLearningLapCb = cb; }
    void onModeChange(ModeChangeCallback cb) { _onModeChangeCb = cb; }
    void onConfigUpdated(ConfigUpdatedCallback cb) { _onConfigUpdatedCb = cb; }

    // WebSocket real-time push methods
    void broadcastWs(const String& jsonPayload);
    void broadcastTelemetry(const String& type, const JsonDocument& data);

    // Operating mode state
    OperatingMode getOperatingMode() const { return _currentMode; }
    void setOperatingMode(OperatingMode mode) { _currentMode = mode; }

private:
    LocoWebServer();
    ~LocoWebServer() = default;

    AsyncWebServer _server{80};
    AsyncWebSocket _ws{"/ws"};

    OperatingMode _currentMode = MODE_MANUAL;

    LocoControlCallback   _onLocoControlCb = nullptr;
    TrackControlCallback  _onTrackControlCb = nullptr;
    EmergencyStopCallback _onEmergencyStopCb = nullptr;
    LearningLapCallback   _onLearningLapCb = nullptr;
    ModeChangeCallback    _onModeChangeCb = nullptr;
    ConfigUpdatedCallback _onConfigUpdatedCb = nullptr;

    void setupRoutes();
    void setupWebSocket();
    void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
};
