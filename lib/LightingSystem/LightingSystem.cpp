#include "LightingSystem.h"

LightingSystem::LightingSystem() {
    _lastUpdateMs = millis();
    _flashTimerMs = millis();
}

bool LightingSystem::begin(uint8_t pinFront, uint8_t pinRear, uint8_t pinCab) {
    _pins[ZONE_FRONT] = pinFront;
    _pins[ZONE_REAR]  = pinRear;
    _pins[ZONE_CAB]   = pinCab;

    for (int i = 0; i < ZONE_COUNT; i++) {
        if (_pins[i] != 255) {
            pinMode(_pins[i], OUTPUT);
            writeOutput((LightZone)i, 0);
        }
    }

    _lastUpdateMs = millis();
    Serial.printf("[Lighting] Initialized (Front: %d, Rear: %d, Cab: %d)\n",
                  pinFront, pinRear, pinCab);
    return true;
}

void LightingSystem::writeOutput(LightZone zone, uint8_t value) {
    if (_pins[zone] == 255) return;
#if defined(ESP32)
    analogWrite(_pins[zone], value);
#elif defined(ESP8266)
    analogWrite(_pins[zone], value);
#endif
}

void LightingSystem::setBrightness(LightZone zone, uint8_t targetBrightness) {
    if (zone >= ZONE_COUNT) return;
    _targetBrightness[zone] = targetBrightness;
}

void LightingSystem::setMode(LightingMode mode) {
    _mode = mode;
    applyModeLogic();
}

void LightingSystem::updateTrainMovement(int8_t speed, uint8_t direction) {
    _lastSpeed = speed;
    _lastDirection = direction;
    if (_mode == LIGHT_MODE_AUTO_DIRECTION) {
        applyModeLogic();
    }
}

void LightingSystem::applyModeLogic() {
    switch (_mode) {
        case LIGHT_MODE_AUTO_DIRECTION:
            if (_lastDirection == 1) { // Forward
                _targetBrightness[ZONE_FRONT] = 255;
                _targetBrightness[ZONE_REAR]  = 0;
                _targetBrightness[ZONE_CAB]   = 40;
            } else if (_lastDirection == 2) { // Reverse
                _targetBrightness[ZONE_FRONT] = 0;
                _targetBrightness[ZONE_REAR]  = 255;
                _targetBrightness[ZONE_CAB]   = 40;
            } else { // Stopped / Idle at station
                _targetBrightness[ZONE_FRONT] = 70;
                _targetBrightness[ZONE_REAR]  = 70;
                _targetBrightness[ZONE_CAB]   = 200;
            }
            break;

        case LIGHT_MODE_SHUNTING:
            _targetBrightness[ZONE_FRONT] = 80;
            _targetBrightness[ZONE_REAR]  = 80;
            _targetBrightness[ZONE_CAB]   = 150;
            break;

        case LIGHT_MODE_EMERGENCY_FLASH:
            // Handled dynamically in update()
            break;

        case LIGHT_MODE_MANUAL:
        default:
            // Kept as manually assigned
            break;
    }
}

void LightingSystem::update() {
    uint32_t now = millis();
    float dt = (now - _lastUpdateMs) / 1000.0f;
    if (dt <= 0.0f) return;
    _lastUpdateMs = now;

    // Handle emergency flashing mode
    if (_mode == LIGHT_MODE_EMERGENCY_FLASH) {
        if (now - _flashTimerMs >= 350) {
            _flashTimerMs = now;
            _flashToggle = !_flashToggle;
            _targetBrightness[ZONE_FRONT] = _flashToggle ? 255 : 0;
            _targetBrightness[ZONE_REAR]  = _flashToggle ? 0 : 255;
            _targetBrightness[ZONE_CAB]   = 255;
        }
    }

    // Smooth fading step towards target brightness
    for (int i = 0; i < ZONE_COUNT; i++) {
        if (fabs(_currentBrightness[i] - _targetBrightness[i]) > 0.5f) {
            if (_currentBrightness[i] < _targetBrightness[i]) {
                _currentBrightness[i] += _fadeSpeed * dt;
                if (_currentBrightness[i] > _targetBrightness[i]) {
                    _currentBrightness[i] = (float)_targetBrightness[i];
                }
            } else {
                _currentBrightness[i] -= _fadeSpeed * dt;
                if (_currentBrightness[i] < _targetBrightness[i]) {
                    _currentBrightness[i] = (float)_targetBrightness[i];
                }
            }
            writeOutput((LightZone)i, (uint8_t)_currentBrightness[i]);
        }
    }
}
