#include "IRTelemetry.h"

// Define IRremote configuration macros before including library headers
#if defined(ESP32)
  #include <IRremote.hpp>
#elif defined(ESP8266)
  #include <IRremoteESP8266.h>
  #include <IRrecv.h>
  #include <IRsend.h>
  #include <IRutils.h>
  static IRsend* s_irSend = nullptr;
  static IRrecv* s_irRecv = nullptr;
#endif

IRTelemetry::IRTelemetry() {
    _lastTxMs = millis();
    _lastBeamSignalMs = millis();
}

IRTelemetry::~IRTelemetry() {
#if defined(ESP8266)
    if (s_irSend) { delete s_irSend; s_irSend = nullptr; }
    if (s_irRecv) { delete s_irRecv; s_irRecv = nullptr; }
#endif
}

bool IRTelemetry::beginReceiver(uint8_t rxPin) {
    _rxPin = rxPin;
    _rxEnabled = true;

#if defined(ESP32)
    IrReceiver.begin(_rxPin, ENABLE_LED_FEEDBACK);
#elif defined(ESP8266)
    if (s_irRecv) delete s_irRecv;
    s_irRecv = new IRrecv(_rxPin);
    s_irRecv->enableIRIn();
#endif

    _lastBeamSignalMs = millis();
    Serial.printf("[IR] Receiver initialized on pin %d\n", _rxPin);
    return true;
}

bool IRTelemetry::beginTransmitter(uint8_t txPin, uint16_t beaconBlockId, uint16_t intervalMs) {
    _txPin = txPin;
    _beaconBlockId = beaconBlockId;
    _txIntervalMs = intervalMs;
    _txEnabled = true;

#if defined(ESP32)
    IrSender.begin(_txPin);
#elif defined(ESP8266)
    if (s_irSend) delete s_irSend;
    s_irSend = new IRsend(_txPin);
    s_irSend->begin();
#endif

    Serial.printf("[IR] Transmitter initialized on pin %d (Beacon ID: %u, Interval: %ums)\n",
                  _txPin, _beaconBlockId, _txIntervalMs);
    return true;
}

void IRTelemetry::enableBeamBreakDetection(uint32_t beamBreakTimeoutMs) {
    _beamBreakEnabled = true;
    _beamBreakTimeoutMs = beamBreakTimeoutMs;
    _lastBeamSignalMs = millis();
    _beamOccupied = false;
}

void IRTelemetry::transmitBeacon() {
    if (!_txEnabled) return;

    // Encode Block ID: Address = 0x5452 ("TR"), Command = _beaconBlockId
    uint16_t address = 0x5452;
    uint8_t command = (uint8_t)(_beaconBlockId & 0xFF);

#if defined(ESP32)
    IrSender.sendNEC(address, command, 0);
#elif defined(ESP8266)
    if (s_irSend) {
        // Construct 32-bit NEC frame
        uint32_t rawData = ((uint32_t)address << 16) | ((uint32_t)command << 8) | (~command & 0xFF);
        s_irSend->sendNEC(rawData, 32);
    }
#endif
}

void IRTelemetry::processReceiver() {
    if (!_rxEnabled) return;

    uint32_t now = millis();
    bool receivedSignal = false;
    uint16_t decodedBlock = 0;

#if defined(ESP32)
    if (IrReceiver.decode()) {
        if (IrReceiver.decodedIRData.protocol == NEC) {
            uint16_t address = IrReceiver.decodedIRData.address;
            if (address == 0x5452) {
                decodedBlock = IrReceiver.decodedIRData.command;
                receivedSignal = true;
            }
        }
        IrReceiver.resume();
    }
#elif defined(ESP8266)
    if (s_irRecv) {
        decode_results results;
        if (s_irRecv->decode(&results)) {
            if (results.decode_type == decode_type_t::NEC) {
                uint16_t address = (results.value >> 16) & 0xFFFF;
                if (address == 0x5452) {
                    decodedBlock = (results.value >> 8) & 0xFF;
                    receivedSignal = true;
                }
            }
            s_irRecv->resume();
        }
    }
#endif

    if (receivedSignal) {
        _lastBeamSignalMs = now;

        // If beam was broken, mark it cleared
        if (_beamBreakEnabled && _beamOccupied) {
            _beamOccupied = false;
            Serial.println(F("[IR] Beam restored: Track block cleared"));
            if (_onOccupancyCb) _onOccupancyCb(false);
        }

        // Debounce block detections: don't re-trigger identical block within 1500ms
        if (decodedBlock != _lastBlockId || (now - _lastBlockDetectedMs > 1500)) {
            _lastBlockId = decodedBlock;
            _lastBlockDetectedMs = now;
            Serial.printf("[IR] Beacon detected: Block #%u\n", decodedBlock);
            if (_onBeaconCb) _onBeaconCb(decodedBlock);
        }
    } else if (_beamBreakEnabled) {
        // Check for beam break (train physical obstruction)
        if (!_beamOccupied && (now - _lastBeamSignalMs > _beamBreakTimeoutMs)) {
            _beamOccupied = true;
            Serial.println(F("[IR] Beam interrupted: Track block occupied!"));
            if (_onOccupancyCb) _onOccupancyCb(true);
        }
    }
}

void IRTelemetry::update() {
    uint32_t now = millis();

    // Periodic beacon transmission
    if (_txEnabled && (now - _lastTxMs >= _txIntervalMs)) {
        _lastTxMs = now;
        transmitBeacon();
    }

    // Process incoming IR signals
    if (_rxEnabled) {
        processReceiver();
    }
}
