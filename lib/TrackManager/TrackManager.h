#pragma once
#include <Arduino.h>
#include <functional>
#include "ProtocolMessages.h"

#if defined(ESP32)
  #include <ESP32Servo.h>
#elif defined(ESP8266)
  #include <Servo.h>
#endif

#define MAX_TRACK_SWITCHES 4

struct TrackSwitchDevice {
    uint8_t id = 1;              // Switch ID (e.g. 1, 2...)
    uint8_t gpioPin = 255;       // GPIO pin connected to servo
    uint8_t straightAngle = 75;  // Straight position angle
    uint8_t turnoutAngle = 105;  // Turnout position angle
    SwitchState currentState = SWITCH_STRAIGHT;
    SwitchState targetState = SWITCH_STRAIGHT;
    float currentAngle = 75.0f;
    float targetAngle = 75.0f;
    bool isMoving = false;
    uint32_t motionStartMs = 0;
    Servo servo;
};

class TrackManager {
public:
    using DwellCompleteCallback = std::function<void()>;
    using SwitchChangeCallback = std::function<void(SwitchState state)>;

    TrackManager();

    // Initialize track switch servo pin (default primary switch)
    bool beginSwitch(uint8_t servoPin, uint8_t straightAngle = 75, uint8_t turnoutAngle = 105);

    // Add additional switch identified by ID and GPIO
    bool addSwitch(uint8_t id, uint8_t servoPin, uint8_t straightAngle = 75, uint8_t turnoutAngle = 105);

    // Initialize auxiliary beam break digital pin (if not using IR receiver)
    bool beginAuxOccupancyPin(uint8_t digitalPin, bool activeLow = true);

    // Command switch position (default switch 0)
    void setSwitchPosition(SwitchState state, bool immediate = false);

    // Command specific switch position by index
    void setSwitchPosition(uint8_t switchIndex, SwitchState state, bool immediate = false);

    SwitchState getSwitchPosition(uint8_t switchIndex = 0) const;
    size_t getSwitchCount() const { return _switchCount; }
    const TrackSwitchDevice* getSwitch(uint8_t switchIndex) const {
        return (switchIndex < _switchCount) ? &_switches[switchIndex] : nullptr;
    }

    // Station Dwell Countdown
    void startDwellCountdown(uint16_t dwellSeconds);
    void cancelDwell();
    bool isDwellActive() const { return _dwellActive; }
    uint16_t getRemainingDwellSeconds() const;

    // Track Occupancy
    void setOccupied(bool occupied);
    bool isOccupied() const { return _isOccupied; }

    // Callbacks
    void onDwellComplete(DwellCompleteCallback cb) { _onDwellCompleteCb = cb; }
    void onSwitchChanged(SwitchChangeCallback cb) { _onSwitchChangedCb = cb; }

    // Update in loop
    void update();

private:
    TrackSwitchDevice _switches[MAX_TRACK_SWITCHES];
    uint8_t _switchCount = 0;

    uint8_t _auxPin = 255;
    bool    _auxActiveLow = true;
    bool    _isOccupied = false;

    bool     _dwellActive = false;
    uint32_t _dwellStartMs = 0;
    uint32_t _dwellDurationMs = 0;

    DwellCompleteCallback _onDwellCompleteCb = nullptr;
    SwitchChangeCallback  _onSwitchChangedCb = nullptr;

    uint32_t _lastUpdateMs = 0;
};
