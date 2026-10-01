#include "StationDisplay.h"

StationDisplay::StationDisplay(uint8_t screenWidth, uint8_t screenHeight)
    : _width(screenWidth), _height(screenHeight) {
}

bool StationDisplay::begin(uint8_t sdaPin, uint8_t sclPin, uint8_t i2cAddr) {
#if defined(ESP32)
    Wire.begin(sdaPin, sclPin);
#elif defined(ESP8266)
    Wire.begin(sdaPin, sclPin);
#endif

    _oled = new Adafruit_SSD1306(_width, _height, &Wire, -1);
    if (!_oled->begin(SSD1306_SWITCHCAPVCC, i2cAddr)) {
        Serial.println(F("[OLED] SSD1306 allocation failed!"));
        return false;
    }

    _oled->clearDisplay();
    _oled->setTextWrap(false);
    _oled->setTextColor(SSD1306_WHITE);

    // Boot splash screen
    _oled->fillRect(0, 0, _width, 16, SSD1306_WHITE);
    _oled->setTextColor(SSD1306_BLACK);
    _oled->setCursor(8, 4);
    _oled->setTextSize(1);
    _oled->print(F("LEGO LOCO"));

    _oled->setTextColor(SSD1306_WHITE);
    _oled->setCursor(10, 28);
    _oled->print(F("Station Node"));
    _oled->setCursor(10, 44);
    _oled->print(F("Initializing..."));
    _oled->display();
    delay(500);

    Serial.printf("[OLED] 128x64 display initialized (SDA: %d, SCL: %d, Addr: 0x%02X)\n",
                  sdaPin, sclPin, i2cAddr);
    return true;
}

void StationDisplay::setStationName(const char* name) {
    strncpy(_stationName, name, sizeof(_stationName) - 1);
}

void StationDisplay::updateIncomingTrain(const char* trainName, uint16_t etaSeconds, SignalAspect aspect) {
    strncpy(_nextTrainName, trainName, sizeof(_nextTrainName) - 1);
    _etaSeconds = etaSeconds;
    _etaReceivedMs = millis();
    _signalAspect = aspect;
}

void StationDisplay::updateTrackStatus(SwitchState swState, bool occupied) {
    _switchState = swState;
    _trackOccupied = occupied;
}

void StationDisplay::updateSystemInfo(const char* nodeId, const char* ipOrStatus, int8_t rssi) {
    strncpy(_nodeId, nodeId, sizeof(_nodeId) - 1);
    strncpy(_sysStatus, ipOrStatus, sizeof(_sysStatus) - 1);
    _rssi = rssi;
}

void StationDisplay::setScreenMode(DisplayScreenMode mode) {
    _screenMode = mode;
}

void StationDisplay::cycleScreen() {
    _screenMode = (DisplayScreenMode)((_screenMode + 1) % 3);
}

void StationDisplay::drawSignalIcon(int16_t x, int16_t y, SignalAspect aspect) {
    if (!_oled) return;
    // Signal housing box
    _oled->drawRoundRect(x, y, 12, 30, 2, SSD1306_WHITE);

    // 3 lights: Red (top), Yellow (middle), Green (bottom)
    int16_t cx = x + 6;
    int16_t cyRed = y + 5;
    int16_t cyYel = y + 15;
    int16_t cyGrn = y + 25;

    // Red light
    if (aspect == SIGNAL_RED) _oled->fillCircle(cx, cyRed, 3, SSD1306_WHITE);
    else _oled->drawCircle(cx, cyRed, 3, SSD1306_WHITE);

    // Yellow light
    if (aspect == SIGNAL_YELLOW) _oled->fillCircle(cx, cyYel, 3, SSD1306_WHITE);
    else _oled->drawCircle(cx, cyYel, 3, SSD1306_WHITE);

    // Green light
    if (aspect == SIGNAL_GREEN) _oled->fillCircle(cx, cyGrn, 3, SSD1306_WHITE);
    else _oled->drawCircle(cx, cyGrn, 3, SSD1306_WHITE);
}

void StationDisplay::renderTimetableScreen() {
    // Header Bar
    _oled->fillRect(0, 0, _width, 14, SSD1306_WHITE);
    _oled->setTextColor(SSD1306_BLACK);
    _oled->setTextSize(1);
    _oled->setCursor(4, 3);
    _oled->print(_stationName);

    _oled->setTextColor(SSD1306_WHITE);

    // Incoming train info
    _oled->setCursor(2, 18);
    _oled->print(F("Train: "));
    _oled->print(_nextTrainName);

    // Calculate dynamic ETA countdown
    uint32_t elapsedSec = (millis() - _etaReceivedMs) / 1000UL;
    int remainingSec = (int)_etaSeconds - (int)elapsedSec;
    if (remainingSec < 0) remainingSec = 0;

    _oled->setCursor(2, 30);
    _oled->print(F("ETA: "));
    if (remainingSec == 0 && _etaSeconds > 0) {
        _oled->print(F("ARRIVED"));
    } else if (_etaSeconds == 0) {
        _oled->print(F("--:--"));
    } else {
        int minutes = remainingSec / 60;
        int seconds = remainingSec % 60;
        char buf[10];
        snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
        _oled->print(buf);
    }

    // Platform occupancy badge
    _oled->setCursor(2, 44);
    _oled->print(F("Track: "));
    if (_trackOccupied) {
        _oled->fillRect(42, 42, 60, 12, SSD1306_WHITE);
        _oled->setTextColor(SSD1306_BLACK);
        _oled->setCursor(45, 44);
        _oled->print(F("OCCUPIED"));
        _oled->setTextColor(SSD1306_WHITE);
    } else {
        _oled->drawRect(42, 42, 42, 12, SSD1306_WHITE);
        _oled->setCursor(46, 44);
        _oled->print(F("CLEAR"));
    }

    // Draw Signal Light on right
    drawSignalIcon(112, 18, _signalAspect);
}

void StationDisplay::renderSwitchScreen() {
    _oled->fillRect(0, 0, _width, 14, SSD1306_WHITE);
    _oled->setTextColor(SSD1306_BLACK);
    _oled->setTextSize(1);
    _oled->setCursor(4, 3);
    _oled->print(F("TRACK SWITCH"));

    _oled->setTextColor(SSD1306_WHITE);
    _oled->setCursor(2, 20);
    _oled->print(F("Position: "));
    _oled->setTextSize(1);
    if (_switchState == SWITCH_STRAIGHT) {
        _oled->print(F("MAIN (STR)"));
    } else {
        _oled->print(F("TURNOUT"));
    }

    // Visual branch diagram
    _oled->drawLine(10, 52, 110, 52, SSD1306_WHITE); // Main straight line
    if (_switchState == SWITCH_TURNOUT) {
        _oled->drawLine(50, 52, 90, 36, SSD1306_WHITE); // Diverging path
        _oled->fillCircle(50, 52, 3, SSD1306_WHITE);
    } else {
        _oled->fillCircle(50, 52, 3, SSD1306_WHITE);
    }
}

void StationDisplay::renderSystemScreen() {
    _oled->fillRect(0, 0, _width, 14, SSD1306_WHITE);
    _oled->setTextColor(SSD1306_BLACK);
    _oled->setTextSize(1);
    _oled->setCursor(4, 3);
    _oled->print(F("SYSTEM DIAG"));

    _oled->setTextColor(SSD1306_WHITE);
    _oled->setCursor(2, 18);
    _oled->print(F("ID: "));
    _oled->print(_nodeId);

    _oled->setCursor(2, 32);
    _oled->print(F("Status: "));
    _oled->print(_sysStatus);

    _oled->setCursor(2, 46);
    _oled->print(F("Signal: "));
    _oled->print(_rssi);
    _oled->print(F(" dBm"));
}

void StationDisplay::update() {
    if (!_oled) return;

    uint32_t now = millis();
    if (now - _lastRenderMs < 200) return; // ~5 fps refresh for OLED
    _lastRenderMs = now;

    _oled->clearDisplay();

    switch (_screenMode) {
        case SCREEN_STATION_TIMETABLE:
            renderTimetableScreen();
            break;
        case SCREEN_TRACK_SWITCH:
            renderSwitchScreen();
            break;
        case SCREEN_SYSTEM_INFO:
            renderSystemScreen();
            break;
    }

    _oled->display();
}
