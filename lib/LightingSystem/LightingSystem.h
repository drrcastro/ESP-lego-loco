#pragma once
#include <Arduino.h>
#include "ProtocolMessages.h"

enum LightZone : uint8_t {
    ZONE_FRONT = 0,
    ZONE_REAR  = 1,
    ZONE_CAB   = 2,
    ZONE_COUNT = 3
};

class LightingSystem {
public:
    LightingSystem();

    // Initialize with GPIO pins for Front, Rear, and Cab LED zones
    bool begin(uint8_t pinFront, uint8_t pinRear, uint8_t pinCab);

    // Set individual brightness (0-255)
    void setBrightness(LightZone zone, uint8_t targetBrightness);

    // Set operating mode
    void setMode(LightingMode mode);
    LightingMode getMode() const { return _mode; }

    // Notify train movement for auto-directional mode
    void updateTrainMovement(int8_t speed, uint8_t direction);

    // Update LED PWM and effects in main loop
    void update();

    uint8_t getBrightness(LightZone zone) const { return (uint8_t)_currentBrightness[zone]; }

private:
    uint8_t _pins[ZONE_COUNT] = {255, 255, 255};
    float   _currentBrightness[ZONE_COUNT] = {0.0f, 0.0f, 0.0f};
    uint8_t _targetBrightness[ZONE_COUNT] = {0, 0, 0};
    float   _fadeSpeed = 350.0f; // Units per second

    LightingMode _mode = LIGHT_MODE_AUTO_DIRECTION;
    int8_t  _lastSpeed = 0;
    uint8_t _lastDirection = 0; // 0=Stop, 1=Fwd, 2=Rev

    uint32_t _lastUpdateMs = 0;
    uint32_t _flashTimerMs = 0;
    bool     _flashToggle = false;

    void writeOutput(LightZone zone, uint8_t value);
    void applyModeLogic();
};
