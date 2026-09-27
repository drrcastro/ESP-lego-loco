#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ProtocolMessages.h"

enum DisplayScreenMode : uint8_t {
    SCREEN_STATION_TIMETABLE = 0,
    SCREEN_TRACK_SWITCH      = 1,
    SCREEN_SYSTEM_INFO       = 2
};

class StationDisplay {
public:
    StationDisplay(uint8_t screenWidth = 128, uint8_t screenHeight = 64);

    bool begin(uint8_t sdaPin = 21, uint8_t sclPin = 22, uint8_t i2cAddr = 0x3C);

    void setStationName(const char* name);
    void updateIncomingTrain(const char* trainName, uint16_t etaSeconds, SignalAspect aspect);
    void updateTrackStatus(SwitchState swState, bool occupied);
    void updateSystemInfo(const char* nodeId, const char* ipOrStatus, int8_t rssi);

    void setScreenMode(DisplayScreenMode mode);
    void cycleScreen();

    // Renders the OLED frame
    void update();

private:
    uint8_t _width;
    uint8_t _height;
    Adafruit_SSD1306* _oled = nullptr;

    DisplayScreenMode _screenMode = SCREEN_STATION_TIMETABLE;
    char _stationName[24] = "CENTRAL STATION";
    char _nextTrainName[16] = "--";
    uint16_t _etaSeconds = 0;
    uint32_t _etaReceivedMs = 0;
    SignalAspect _signalAspect = SIGNAL_GREEN;

    SwitchState _switchState = SWITCH_STRAIGHT;
    bool _trackOccupied = false;

    char _nodeId[16] = "TRACK_0000";
    char _sysStatus[24] = "Connecting...";
    int8_t _rssi = -50;

    uint32_t _lastRenderMs = 0;

    void renderTimetableScreen();
    void renderSwitchScreen();
    void renderSystemScreen();
    void drawSignalIcon(int16_t x, int16_t y, SignalAspect aspect);
};
