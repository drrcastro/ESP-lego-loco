#pragma once
#include <Arduino.h>
#include <functional>
#include "ProtocolMessages.h"

#if defined(ESP32)
  #include <ESP32Servo.h>
#elif defined(ESP8266)
  #include <Servo.h>
#endif

class TrackManager {
public:
    using DwellCompleteCallback = std::function<void()>;
    using SwitchChangeCallback = std::function<void(SwitchState state)>;

    TrackManager();

    // Initialize track switch servo pin
    bool beginSwitch(uint8_t servoPin, uint8_t straightAngle = 75, uint8_t turnoutAngle = 105);

    // Initialize auxiliary beam break digital pin (if not using IR receiver)
    bool beginAuxOccupancyPin(uint8_t digitalPin, bool activeLow = true);

    // Command switch position
    void setSwitchPosition(SwitchState state, bool immediate = false);
    SwitchState getSwitchPosition() const { return _currentState; }

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
    Servo   _servo;
    uint8_t _servoPin = 255;
    uint8_t _straightAngle = 75;
    uint8_t _turnoutAngle = 105;

    SwitchState _currentState = SWITCH_STRAIGHT;
    SwitchState _targetState = SWITCH_STRAIGHT;
    float   _currentAngle = 75.0f;
    float   _targetAngle = 75.0f;
    bool    _isMoving = false;
    uint32_t _motionStartMs = 0;

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
