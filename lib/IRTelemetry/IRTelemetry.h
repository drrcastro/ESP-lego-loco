#pragma once
#include <Arduino.h>
#include <functional>

class IRTelemetry {
public:
    using BeaconCallback = std::function<void(uint16_t blockId)>;
    using OccupancyCallback = std::function<void(bool occupied)>;

    IRTelemetry();
    ~IRTelemetry();

    // Initialize Receiver (used by Loco to read track beacons, or Track for beam break)
    bool beginReceiver(uint8_t rxPin);

    // Initialize Transmitter (used by Track nodes to broadcast block ID)
    bool beginTransmitter(uint8_t txPin, uint16_t beaconBlockId, uint16_t intervalMs = 100);

    // Enable beam break detection (Track node opposite transmitter)
    void enableBeamBreakDetection(uint32_t beamBreakTimeoutMs = 250);

    // Set callback when train detects a new track beacon
    void onBeaconDetected(BeaconCallback cb) { _onBeaconCb = cb; }

    // Set callback when track occupancy changes (beam break)
    void onOccupancyChanged(OccupancyCallback cb) { _onOccupancyCb = cb; }

    // Main loop update
    void update();

    uint16_t getLastDecodedBlock() const { return _lastBlockId; }
    bool isBeamBroken() const { return _beamOccupied; }
    void setBeaconBlockId(uint16_t blockId) { _beaconBlockId = blockId; }

private:
    uint8_t  _rxPin = 255;
    uint8_t  _txPin = 255;
    uint16_t _beaconBlockId = 0;
    uint16_t _txIntervalMs = 100;
    uint32_t _lastTxMs = 0;

    bool     _rxEnabled = false;
    bool     _txEnabled = false;
    bool     _beamBreakEnabled = false;
    uint32_t _beamBreakTimeoutMs = 300;

    uint16_t _lastBlockId = 0;
    uint32_t _lastBlockDetectedMs = 0;
    uint32_t _lastBeamSignalMs = 0;
    bool     _beamOccupied = false;

    BeaconCallback _onBeaconCb = nullptr;
    OccupancyCallback _onOccupancyCb = nullptr;

    void transmitBeacon();
    void processReceiver();
};
