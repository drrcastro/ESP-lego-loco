#pragma once
#include <Arduino.h>
#include <vector>
#include <map>
#include <ArduinoJson.h>

#if defined(ESP32)
  #include <LittleFS.h>
#elif defined(ESP8266)
  #include <LittleFS.h>
#endif

// =================================================================
// Configuration Structures
// =================================================================

struct SystemSettings {
    String  layoutName = "Lego Central Layout";
    String  wifiSsid = "LegoTrain_Master";
    String  wifiPassword = ""; // Open or WPA2
    bool    apMode = true;     // true = Access Point, false = Station
    uint8_t wifiChannel = 1;
    uint16_t headwaySafeSec = 12;
    uint16_t headwayCautionSec = 6;
    uint8_t  headwaySpeedTrimPct = 40;
};

struct LocoParamConfig {
    String  nodeId;
    String  name;
    int8_t  maxSpeed = 70;
    uint8_t learningSpeed = 35;
    float   accelRate = 40.0f;
    float   decelRate = 60.0f;
    uint16_t brakeOffsetMs = 450;
    uint16_t dwellTimeSec = 10;
    String  lightMode = "AUTO";
    uint16_t measuredLengthCm = 0;
};

struct StationBeaconParam {
    uint16_t beaconId = 0;
    uint8_t  gpioPin = 19; // GPIO associated with beacon (IR emitter or optical sensor)
    uint8_t  role = 0; // 0=LOCATOR, 1=STATION_ARRIVAL, 2=APPROACH, 3=DEPARTURE, 4=SIDING
    String   description;
    bool     measureTrainLength = true;
    uint16_t dwellSec = 10;
};

struct StationSwitchParam {
    uint8_t  switchId = 1;        // Switch ID (e.g. 1, 2...)
    uint8_t  gpioPin = 18;        // GPIO pin connected to servo motor (e.g. 18, 25...)
    uint8_t  servoStraightAngle = 75;  // Servo straight angle
    uint8_t  servoTurnoutAngle = 105;  // Servo turnout angle
    String   defaultPosition = "STRAIGHT"; // "STRAIGHT" or "TURNOUT"
    String   description;         // Friendly description (e.g. "North Entry Turnout")
};

struct StationParamConfig {
    String  nodeId;
    String  name;
    String  defaultSwitch = "STRAIGHT"; // "STRAIGHT" or "TURNOUT"
    uint8_t servoStraightAngle = 75;
    uint8_t servoTurnoutAngle = 105;
    uint8_t switchGpioPin = 18; // Primary switch GPIO
    uint16_t dwellTimeSec = 10;
    bool    autoDivertOnOccupied = true;
    std::vector<StationBeaconParam> beacons;
    std::vector<StationSwitchParam> switches;
};

class ConfigStore {
public:
    static ConfigStore& instance();

    bool begin();

    // System Settings
    const SystemSettings& getSettings() const { return _settings; }
    void setSettings(const SystemSettings& settings);
    bool saveSettings();
    bool loadSettings();

    // Node Friendly Names
    String getFriendlyName(const String& nodeId);
    void setFriendlyName(const String& nodeId, const String& name);
    bool saveNodeMappings();
    bool loadNodeMappings();
    const std::map<String, String>& getAllNodeMappings() const { return _nodeNames; }

    // Locomotive Configurations
    bool hasLocoConfig(const String& nodeId) const;
    LocoParamConfig getLocoConfig(const String& nodeId);
    void setLocoConfig(const LocoParamConfig& cfg);
    bool removeLoco(const String& nodeId);
    const std::vector<LocoParamConfig>& getAllLocos() const { return _locos; }

    // Station Configurations
    bool hasStationConfig(const String& nodeId) const;
    StationParamConfig getStationConfig(const String& nodeId);
    void setStationConfig(const StationParamConfig& cfg);
    const std::vector<StationParamConfig>& getAllStations() const { return _stations; }

    // Unified Master Configuration JSON (/config/config.json)
    bool loadUnifiedConfig();
    bool saveUnifiedConfig();
    String serializeUnifiedConfigJson();
    bool deserializeUnifiedConfigJson(const String& jsonStr);

    // Topology Map JSON (/config/topology.json)
    bool saveTopologyJson(const String& jsonStr);
    String loadTopologyJson();

private:
    ConfigStore();
    ~ConfigStore() = default;

    bool _fsMounted = false;
    SystemSettings _settings;
    std::map<String, String> _nodeNames;
    std::vector<LocoParamConfig> _locos;
    std::vector<StationParamConfig> _stations;

    const char* CONFIG_PATH    = "/config/config.json";
    const char* SETTINGS_PATH  = "/config/settings.json";
    const char* NODES_PATH     = "/config/nodes.json";
    const char* TOPOLOGY_PATH  = "/config/topology.json";

    void initDefaultConfig();
};
