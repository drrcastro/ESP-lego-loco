#pragma once
#include <Arduino.h>

enum MotorDirection : uint8_t {
    DIR_STOP    = 0,
    DIR_FORWARD = 1,
    DIR_REVERSE = 2,
    DIR_BRAKE   = 3
};

class MotorController {
public:
    MotorController();

    // Initialize with IA and IB GPIO pins, PWM frequency, and resolution
    bool begin(uint8_t pinIA, uint8_t pinIB, uint32_t pwmFreq = 10000, uint8_t pwmResolution = 8);

    // Speed target: -100 (100% Reverse) to +100 (100% Forward), 0 = Stop
    void setTargetSpeed(int8_t targetSpeed);
    
    // Immediate emergency stop (no ramping)
    void emergencyStop();

    // Active electrical brake (IA=HIGH, IB=HIGH)
    void brake();

    // Coast to stop
    void coast();

    // Ramping configuration (% change per second)
    void setAccelerationRate(float percentPerSec) { _accelRate = percentPerSec; }
    void setDecelerationRate(float percentPerSec) { _decelRate = percentPerSec; }
    void setMinStartPower(uint8_t minPercent) { _minStartPower = minPercent; }
    
    // Safety watchdog: timeout in ms after which train auto-stops if no refresh
    void setWatchdogTimeout(uint32_t timeoutMs) { _watchdogTimeoutMs = timeoutMs; }
    void feedWatchdog();

    // Update function to be called in main loop
    void update();

    // Status queries
    int8_t getTargetSpeed() const { return _targetSpeed; }
    int8_t getCurrentSpeed() const { return (int8_t)_currentSpeed; }
    MotorDirection getDirection() const { return _currentDir; }
    bool isStopped() const { return _currentDir == DIR_STOP || _currentDir == DIR_BRAKE; }
    bool isWatchdogTriggered() const { return _watchdogTriggered; }

private:
    uint8_t _pinIA = 255;
    uint8_t _pinIB = 255;
    uint32_t _pwmFreq = 10000;
    uint8_t _pwmResolution = 8;
    uint32_t _maxDuty = 255;

    int _pwmChannelA = 0;
    int _pwmChannelB = 1;


    int8_t  _targetSpeed = 0;      // -100 to 100
    float   _currentSpeed = 0.0f;  // -100.0 to 100.0
    MotorDirection _currentDir = DIR_STOP;

    float   _accelRate = 45.0f;    // % per second
    float   _decelRate = 60.0f;    // % per second
    uint8_t _minStartPower = 18;   // Minimum % to overcome friction

    uint32_t _lastUpdateMs = 0;
    uint32_t _lastFeedMs = 0;
    uint32_t _watchdogTimeoutMs = 4000;
    bool     _watchdogTriggered = false;

    void applyHardwareOutputs(float speedPercent, bool isBraking = false);
    void writePwmDuty(uint8_t pin, int channel, uint32_t duty);
};
