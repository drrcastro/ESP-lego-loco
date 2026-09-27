#include "MotorController.h"

MotorController::MotorController() {
    _lastFeedMs = millis();
    _lastUpdateMs = millis();
}

bool MotorController::begin(uint8_t pinIA, uint8_t pinIB, uint32_t pwmFreq, uint8_t pwmResolution) {
    _pinIA = pinIA;
    _pinIB = pinIB;
    _pwmFreq = pwmFreq;
    _pwmResolution = pwmResolution;
    _maxDuty = (1 << _pwmResolution) - 1;

    pinMode(_pinIA, OUTPUT);
    pinMode(_pinIB, OUTPUT);

#if defined(ESP32)
  #if defined(ESP_ARDUINO_VERSION) && defined(ESP_ARDUINO_VERSION_VAL)
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      ledcAttach(_pinIA, _pwmFreq, _pwmResolution);
      ledcAttach(_pinIB, _pwmFreq, _pwmResolution);
    #else
      _pwmChannelA = 0;
      _pwmChannelB = 1;
      ledcSetup(_pwmChannelA, _pwmFreq, _pwmResolution);
      ledcAttachPin(_pinIA, _pwmChannelA);
      ledcSetup(_pwmChannelB, _pwmFreq, _pwmResolution);
      ledcAttachPin(_pinIB, _pwmChannelB);
    #endif
  #else
    _pwmChannelA = 0;
    _pwmChannelB = 1;
    ledcSetup(_pwmChannelA, _pwmFreq, _pwmResolution);
    ledcAttachPin(_pinIA, _pwmChannelA);
    ledcSetup(_pwmChannelB, _pwmFreq, _pwmResolution);
    ledcAttachPin(_pinIB, _pwmChannelB);
  #endif
#elif defined(ESP8266)
    analogWriteRange(_maxDuty);
    analogWriteFreq(_pwmFreq);
#endif

    coast();
    _lastFeedMs = millis();
    _lastUpdateMs = millis();

    Serial.printf("[Motor] Initialized on pins IA=%d, IB=%d (Freq=%u Hz, Res=%d bits)\n",
                  _pinIA, _pinIB, _pwmFreq, _pwmResolution);
    return true;
}

void MotorController::writePwmDuty(uint8_t pin, int channel, uint32_t duty) {
#if defined(ESP32)
  #if defined(ESP_ARDUINO_VERSION) && defined(ESP_ARDUINO_VERSION_VAL)
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      ledcWrite(pin, duty);
    #else
      ledcWrite(channel, duty);
    #endif
  #else
    ledcWrite(channel, duty);
  #endif
#elif defined(ESP8266)
    analogWrite(pin, duty);
#endif
}

void MotorController::setTargetSpeed(int8_t targetSpeed) {
    if (targetSpeed > 100) targetSpeed = 100;
    if (targetSpeed < -100) targetSpeed = -100;
    _targetSpeed = targetSpeed;
    feedWatchdog();
}

void MotorController::emergencyStop() {
    _targetSpeed = 0;
    _currentSpeed = 0.0f;
    _currentDir = DIR_BRAKE;
    applyHardwareOutputs(0.0f, true);
    Serial.println(F("[Motor] EMERGENCY STOP triggered!"));
}

void MotorController::brake() {
    _targetSpeed = 0;
    _currentSpeed = 0.0f;
    _currentDir = DIR_BRAKE;
    applyHardwareOutputs(0.0f, true);
}

void MotorController::coast() {
    _targetSpeed = 0;
    _currentSpeed = 0.0f;
    _currentDir = DIR_STOP;
    applyHardwareOutputs(0.0f, false);
}

void MotorController::feedWatchdog() {
    _lastFeedMs = millis();
    _watchdogTriggered = false;
}

void MotorController::update() {
    uint32_t now = millis();
    float dt = (now - _lastUpdateMs) / 1000.0f;
    if (dt <= 0.0f) return;
    _lastUpdateMs = now;

    // Failsafe watchdog check
    if (_watchdogTimeoutMs > 0 && (now - _lastFeedMs > _watchdogTimeoutMs)) {
        if (!_watchdogTriggered && (_targetSpeed != 0 || _currentSpeed != 0.0f)) {
            Serial.println(F("[Motor] WATCHDOG TIMEOUT! Failsafe stopping motor..."));
            _watchdogTriggered = true;
            _targetSpeed = 0;
        }
    }

    // Smooth speed ramping
    if (_currentSpeed < _targetSpeed) {
        float step = _accelRate * dt;
        _currentSpeed += step;
        if (_currentSpeed > _targetSpeed) {
            _currentSpeed = (float)_targetSpeed;
        }
    } else if (_currentSpeed > _targetSpeed) {
        float step = _decelRate * dt;
        _currentSpeed -= step;
        if (_currentSpeed < _targetSpeed) {
            _currentSpeed = (float)_targetSpeed;
        }
    }

    // Apply output if running or coasting
    if (_currentDir != DIR_BRAKE || _targetSpeed != 0) {
        applyHardwareOutputs(_currentSpeed, false);
    }
}

void MotorController::applyHardwareOutputs(float speedPercent, bool isBraking) {
    if (_pinIA == 255 || _pinIB == 255) return;

    if (isBraking) {
        // L9110 Active Brake: IA = HIGH, IB = HIGH
        writePwmDuty(_pinIA, _pwmChannelA, _maxDuty);
        writePwmDuty(_pinIB, _pwmChannelB, _maxDuty);
        _currentDir = DIR_BRAKE;
        return;
    }

    if (fabs(speedPercent) < 1.0f) {
        // Coast / Stop: IA = 0, IB = 0
        writePwmDuty(_pinIA, _pwmChannelA, 0);
        writePwmDuty(_pinIB, _pwmChannelB, 0);
        _currentDir = DIR_STOP;
        return;
    }

    float absSpeed = fabs(speedPercent);
    // Scale speed to account for minimum motor startup torque
    float scaledDutyPercent = _minStartPower + ((100.0f - _minStartPower) * (absSpeed / 100.0f));
    if (scaledDutyPercent > 100.0f) scaledDutyPercent = 100.0f;
    uint32_t duty = (uint32_t)((scaledDutyPercent / 100.0f) * _maxDuty);

    if (speedPercent > 0.0f) {
        // Forward: IA = PWM, IB = LOW
        writePwmDuty(_pinIA, _pwmChannelA, duty);
        writePwmDuty(_pinIB, _pwmChannelB, 0);
        _currentDir = DIR_FORWARD;
    } else {
        // Reverse: IA = LOW, IB = PWM
        writePwmDuty(_pinIA, _pwmChannelA, 0);
        writePwmDuty(_pinIB, _pwmChannelB, duty);
        _currentDir = DIR_REVERSE;
    }
}
