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

struct ScenarioStep {
    uint16_t stepId = 0;
    String   triggerType;   // "START", "IR_BEACON", "TRACK_OCCUPIED", "TRACK_CLEARED", "TIMER"
    String   triggerValue;  // e.g. "5", "TRACK_1A2B", "3000"
    String   targetNode;    // e.g. "LOCO_4B5C", "TRACK_1A2B", "ALL"
    String   action;        // "SET_SPEED", "SET_SWITCH", "SET_LIGHTS", "DWELL_WAIT", "EMERGENCY_STOP", "GOTO_STEP"
    String   parameter;     // e.g. "30", "TURNOUT", "STRAIGHT", "CAB_ON", "5000", "1"
};

struct SystemSettings {
    String  wifiSsid = "LegoTrain_Master";
    String  wifiPassword = ""; // Open or WPA2
    bool    apMode = true;     // true = Access Point, false = Station
    uint8_t wifiChannel = 1;
    String  activeScenario = "default.csv";
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

    // Friendly Node Names Mapping
    String getFriendlyName(const String& nodeId);
    void setFriendlyName(const String& nodeId, const String& name);
    bool saveNodeMappings();
    bool loadNodeMappings();
    const std::map<String, String>& getAllNodeMappings() const { return _nodeNames; }

    // Scenario CSV Manager
    std::vector<String> listScenarios();
    bool loadScenario(const String& filename, std::vector<ScenarioStep>& steps);
    bool saveScenario(const String& filename, const String& csvContent);
    bool saveScenarioSteps(const String& filename, const std::vector<ScenarioStep>& steps);
    bool deleteScenario(const String& filename);
    String readScenarioRaw(const String& filename);

    // CSV Parsing Helpers
    static bool parseCsvLine(const String& line, ScenarioStep& step);
    static String stepToCsvLine(const ScenarioStep& step);
    static String getCsvHeader();

    // Default Scenarios
    void createDefaultScenarioIfNotExists();

private:
    ConfigStore();
    ~ConfigStore() = default;

    bool _fsMounted = false;
    SystemSettings _settings;
    std::map<String, String> _nodeNames;

    const char* SETTINGS_PATH = "/config/settings.json";
    const char* NODES_PATH    = "/config/nodes.json";
    const char* SCENARIOS_DIR = "/scenarios";
};
