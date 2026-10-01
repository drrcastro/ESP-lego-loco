#include "TrackManager.h"

TrackManager::TrackManager() {
    _lastUpdateMs = millis();
    _switchCount = 0;
}

bool TrackManager::beginSwitch(uint8_t servoPin, uint8_t straightAngle, uint8_t turnoutAngle) {
    _switchCount = 0;
    return addSwitch(1, servoPin, straightAngle, turnoutAngle);
}

bool TrackManager::addSwitch(uint8_t id, uint8_t servoPin, uint8_t straightAngle, uint8_t turnoutAngle) {
    if (_switchCount >= MAX_TRACK_SWITCHES) {
        Serial.println(F("[Track] Warning: Maximum number of switches reached!"));
        return false;
    }
    uint8_t idx = _switchCount++;
    auto& sw = _switches[idx];
    sw.id = id;
    sw.gpioPin = servoPin;
    sw.straightAngle = straightAngle;
    sw.turnoutAngle = turnoutAngle;
    sw.currentAngle = straightAngle;
    sw.targetAngle = straightAngle;
    sw.currentState = SWITCH_STRAIGHT;
    sw.targetState = SWITCH_STRAIGHT;
    sw.isMoving = false;

#if defined(ESP32)
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    sw.servo.setPeriodHertz(50);
    sw.servo.attach(sw.gpioPin, 500, 2400);
#elif defined(ESP8266)
    sw.servo.attach(sw.gpioPin);
#endif

    sw.servo.write((int)sw.currentAngle);
    delay(150);
    sw.servo.detach();

    Serial.printf("[Track] Switch #%u initialized on GPIO %d (Straight: %d°, Turnout: %d°)\n",
                  sw.id, sw.gpioPin, sw.straightAngle, sw.turnoutAngle);
    return true;
}

bool TrackManager::beginAuxOccupancyPin(uint8_t digitalPin, bool activeLow) {
    _auxPin = digitalPin;
    _auxActiveLow = activeLow;
    pinMode(_auxPin, _auxActiveLow ? INPUT_PULLUP : INPUT);
    Serial.printf("[Track] Auxiliary occupancy sensor pin %d configured\n", _auxPin);
    return true;
}

void TrackManager::setSwitchPosition(SwitchState state, bool immediate) {
    setSwitchPosition(0, state, immediate);
}

void TrackManager::setSwitchPosition(uint8_t switchIndex, SwitchState state, bool immediate) {
    if (_switchCount == 0) return;
    if (switchIndex >= _switchCount) switchIndex = 0;

    auto& sw = _switches[switchIndex];
    sw.targetState = state;
    sw.targetAngle = (state == SWITCH_STRAIGHT) ? sw.straightAngle : sw.turnoutAngle;

    if (immediate) {
        sw.currentAngle = sw.targetAngle;
        sw.currentState = sw.targetState;
        if (!sw.servo.attached()) {
            sw.servo.attach(sw.gpioPin);
        }
        sw.servo.write((int)sw.currentAngle);
        delay(150);
        sw.servo.detach();
        sw.isMoving = false;
        if (_onSwitchChangedCb) _onSwitchChangedCb(sw.currentState);
        return;
    }

    if (!sw.servo.attached()) {
        sw.servo.attach(sw.gpioPin);
    }
    sw.isMoving = true;
    sw.motionStartMs = millis();
    Serial.printf("[Track] Moving Switch #%u (GPIO %d) to %s (%d°)\n",
                  sw.id, sw.gpioPin, (state == SWITCH_STRAIGHT ? "STRAIGHT" : "TURNOUT"), (int)sw.targetAngle);
}

SwitchState TrackManager::getSwitchPosition(uint8_t switchIndex) const {
    if (_switchCount == 0) return SWITCH_STRAIGHT;
    if (switchIndex >= _switchCount) switchIndex = 0;
    return _switches[switchIndex].currentState;
}

void TrackManager::startDwellCountdown(uint16_t dwellSeconds) {
    if (dwellSeconds == 0) {
        cancelDwell();
        return;
    }
    _dwellActive = true;
    _dwellStartMs = millis();
    _dwellDurationMs = dwellSeconds * 1000UL;
    Serial.printf("[Track] Station Dwell started: %u seconds\n", dwellSeconds);
}

void TrackManager::cancelDwell() {
    _dwellActive = false;
    _dwellDurationMs = 0;
}

uint16_t TrackManager::getRemainingDwellSeconds() const {
    if (!_dwellActive) return 0;
    uint32_t elapsed = millis() - _dwellStartMs;
    if (elapsed >= _dwellDurationMs) return 0;
    return (uint16_t)((_dwellDurationMs - elapsed) / 1000UL);
}

void TrackManager::setOccupied(bool occupied) {
    if (_isOccupied != occupied) {
        _isOccupied = occupied;
        Serial.printf("[Track] Block Occupancy: %s\n", _isOccupied ? "OCCUPIED" : "CLEAR");
    }
}

void TrackManager::update() {
    uint32_t now = millis();
    float dt = (now - _lastUpdateMs) / 1000.0f;
    if (dt <= 0.0f) return;
    _lastUpdateMs = now;

    // Smooth movement for all configured switches
    for (uint8_t i = 0; i < _switchCount; i++) {
        auto& sw = _switches[i];
        if (sw.isMoving) {
            float angleSpeed = 90.0f; // degrees per second
            float step = angleSpeed * dt;

            if (sw.currentAngle < sw.targetAngle) {
                sw.currentAngle += step;
                if (sw.currentAngle >= sw.targetAngle) {
                    sw.currentAngle = sw.targetAngle;
                    sw.isMoving = false;
                }
            } else if (sw.currentAngle > sw.targetAngle) {
                sw.currentAngle -= step;
                if (sw.currentAngle <= sw.targetAngle) {
                    sw.currentAngle = sw.targetAngle;
                    sw.isMoving = false;
                }
            }

            sw.servo.write((int)sw.currentAngle);

            if (!sw.isMoving) {
                sw.currentState = sw.targetState;
                delay(100);
                sw.servo.detach();
                Serial.printf("[Track] Switch #%u (GPIO %d) reached position: %s\n",
                              sw.id, sw.gpioPin, (sw.currentState == SWITCH_STRAIGHT ? "STRAIGHT" : "TURNOUT"));
                if (_onSwitchChangedCb) _onSwitchChangedCb(sw.currentState);
            }
        }
    }

    // Check aux digital occupancy pin if configured
    if (_auxPin != 255) {
        int rawVal = digitalRead(_auxPin);
        bool occupied = _auxActiveLow ? (rawVal == LOW) : (rawVal == HIGH);
        setOccupied(occupied);
    }

    // Dwell timer check
    if (_dwellActive) {
        if (now - _dwellStartMs >= _dwellDurationMs) {
            _dwellActive = false;
            Serial.println(F("[Track] Station Dwell complete! Train cleared to depart."));
            if (_onDwellCompleteCb) _onDwellCompleteCb();
        }
    }
}
