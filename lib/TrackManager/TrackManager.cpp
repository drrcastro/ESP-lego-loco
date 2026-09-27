#include "TrackManager.h"

TrackManager::TrackManager() {
    _lastUpdateMs = millis();
}

bool TrackManager::beginSwitch(uint8_t servoPin, uint8_t straightAngle, uint8_t turnoutAngle) {
    _servoPin = servoPin;
    _straightAngle = straightAngle;
    _turnoutAngle = turnoutAngle;
    _currentAngle = _straightAngle;
    _targetAngle = _straightAngle;
    _currentState = SWITCH_STRAIGHT;
    _targetState = SWITCH_STRAIGHT;

#if defined(ESP32)
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    _servo.setPeriodHertz(50);
    _servo.attach(_servoPin, 500, 2400);
#elif defined(ESP8266)
    _servo.attach(_servoPin);
#endif

    _servo.write((int)_currentAngle);
    delay(200);
    _servo.detach(); // Detach to prevent idle hum/jitter

    Serial.printf("[Track] Switch initialized on pin %d (Straight: %d°, Turnout: %d°)\n",
                  _servoPin, _straightAngle, _turnoutAngle);
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
    _targetState = state;
    _targetAngle = (state == SWITCH_STRAIGHT) ? _straightAngle : _turnoutAngle;

    if (immediate) {
        _currentAngle = _targetAngle;
        _currentState = _targetState;
        if (!_servo.attached()) {
            _servo.attach(_servoPin);
        }
        _servo.write((int)_currentAngle);
        delay(150);
        _servo.detach();
        _isMoving = false;
        if (_onSwitchChangedCb) _onSwitchChangedCb(_currentState);
        return;
    }

    if (!_servo.attached()) {
        _servo.attach(_servoPin);
    }
    _isMoving = true;
    _motionStartMs = millis();
    Serial.printf("[Track] Moving switch to %s (%d°)\n",
                  (state == SWITCH_STRAIGHT ? "STRAIGHT" : "TURNOUT"), (int)_targetAngle);
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

    // Smooth servo angle movement
    if (_isMoving) {
        float angleSpeed = 90.0f; // degrees per second
        float step = angleSpeed * dt;

        if (_currentAngle < _targetAngle) {
            _currentAngle += step;
            if (_currentAngle >= _targetAngle) {
                _currentAngle = _targetAngle;
                _isMoving = false;
            }
        } else if (_currentAngle > _targetAngle) {
            _currentAngle -= step;
            if (_currentAngle <= _targetAngle) {
                _currentAngle = _targetAngle;
                _isMoving = false;
            }
        }

        _servo.write((int)_currentAngle);

        if (!_isMoving) {
            _currentState = _targetState;
            // Detach servo shortly after reaching target to save power & avoid hum
            delay(100);
            _servo.detach();
            Serial.printf("[Track] Switch reached position: %s\n",
                          (_currentState == SWITCH_STRAIGHT ? "STRAIGHT" : "TURNOUT"));
            if (_onSwitchChangedCb) _onSwitchChangedCb(_currentState);
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
