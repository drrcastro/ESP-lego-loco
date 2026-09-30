#include "ConfigStore.h"

ConfigStore& ConfigStore::instance() {
    static ConfigStore inst;
    return inst;
}

ConfigStore::ConfigStore() {
}

bool ConfigStore::begin() {
#if defined(ESP32)
    _fsMounted = LittleFS.begin(true); // formatOnFail = true
#elif defined(ESP8266)
    _fsMounted = LittleFS.begin();
#endif

    if (!_fsMounted) {
        Serial.println(F("[ConfigStore] LittleFS Mount Failed!"));
        return false;
    }
    Serial.println(F("[ConfigStore] LittleFS Mounted Successfully."));

    // Ensure folders exist
    if (!LittleFS.exists("/config")) {
        LittleFS.mkdir("/config");
    }
    if (!LittleFS.exists(SCENARIOS_DIR)) {
        LittleFS.mkdir(SCENARIOS_DIR);
    }

    if (!loadSettings()) {
        saveSettings();
    }
    if (!loadNodeMappings()) {
        saveNodeMappings();
    }
    createDefaultScenarioIfNotExists();

    return true;
}

String ConfigStore::getCsvHeader() {
    return String("STEP_ID, TRIGGER_TYPE, TRIGGER_VALUE, TARGET_NODE, ACTION, PARAMETER");
}

bool ConfigStore::parseCsvLine(const String& line, ScenarioStep& step) {
    String cleanLine = line;
    cleanLine.trim();
    if (cleanLine.length() == 0 || cleanLine.startsWith("#")) return false;

    // Check if line is the header
    if (cleanLine.indexOf("TRIGGER_TYPE") >= 0 || cleanLine.indexOf("STEP_ID") >= 0) {
        return false;
    }

    std::vector<String> tokens;
    int startIndex = 0;
    while (startIndex < cleanLine.length()) {
        int commaIndex = cleanLine.indexOf(',', startIndex);
        if (commaIndex == -1) {
            String token = cleanLine.substring(startIndex);
            token.trim();
            tokens.push_back(token);
            break;
        } else {
            String token = cleanLine.substring(startIndex, commaIndex);
            token.trim();
            tokens.push_back(token);
            startIndex = commaIndex + 1;
        }
    }

    if (tokens.size() < 6) return false;

    step.stepId       = tokens[0].toInt();
    step.triggerType  = tokens[1];
    step.triggerValue = tokens[2];
    step.targetNode   = tokens[3];
    step.action       = tokens[4];
    step.parameter    = tokens[5];

    return true;
}

String ConfigStore::stepToCsvLine(const ScenarioStep& step) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%u, %s, %s, %s, %s, %s",
             step.stepId,
             step.triggerType.c_str(),
             step.triggerValue.c_str(),
             step.targetNode.c_str(),
             step.action.c_str(),
             step.parameter.c_str());
    return String(buf);
}

void ConfigStore::setSettings(const SystemSettings& settings) {
    _settings = settings;
    saveSettings();
}

bool ConfigStore::saveSettings() {
    if (!_fsMounted) return false;

    JsonDocument doc;
    doc["wifiSsid"]       = _settings.wifiSsid;
    doc["wifiPassword"]   = _settings.wifiPassword;
    doc["apMode"]         = _settings.apMode;
    doc["wifiChannel"]    = _settings.wifiChannel;
    doc["activeScenario"] = _settings.activeScenario;

    File file = LittleFS.open(SETTINGS_PATH, "w");
    if (!file) return false;

    serializeJson(doc, file);
    file.close();
    return true;
}

bool ConfigStore::loadSettings() {
    if (!_fsMounted || !LittleFS.exists(SETTINGS_PATH)) return false;

    File file = LittleFS.open(SETTINGS_PATH, "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) return false;

    if (doc["wifiSsid"].is<const char*>()) _settings.wifiSsid = doc["wifiSsid"].as<String>();
    if (doc["wifiPassword"].is<const char*>()) _settings.wifiPassword = doc["wifiPassword"].as<String>();
    if (doc["apMode"].is<bool>()) _settings.apMode = doc["apMode"].as<bool>();
    if (doc["wifiChannel"].is<uint8_t>()) _settings.wifiChannel = doc["wifiChannel"].as<uint8_t>();
    if (doc["activeScenario"].is<const char*>()) _settings.activeScenario = doc["activeScenario"].as<String>();

    if (_settings.wifiSsid.length() == 0) _settings.wifiSsid = "LegoTrain_Master";
    if (_settings.wifiChannel < 1 || _settings.wifiChannel > 13) _settings.wifiChannel = 1;

    return true;
}

String ConfigStore::getFriendlyName(const String& nodeId) {
    auto it = _nodeNames.find(nodeId);
    if (it != _nodeNames.end()) {
        return it->second;
    }
    return nodeId;
}

void ConfigStore::setFriendlyName(const String& nodeId, const String& name) {
    _nodeNames[nodeId] = name;
    saveNodeMappings();
}

bool ConfigStore::saveNodeMappings() {
    if (!_fsMounted) return false;

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    for (const auto& pair : _nodeNames) {
        obj[pair.first] = pair.second;
    }

    File file = LittleFS.open(NODES_PATH, "w");
    if (!file) return false;

    serializeJson(doc, file);
    file.close();
    return true;
}

bool ConfigStore::loadNodeMappings() {
    if (!_fsMounted || !LittleFS.exists(NODES_PATH)) return false;

    File file = LittleFS.open(NODES_PATH, "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) return false;

    _nodeNames.clear();
    for (JsonPair kv : doc.as<JsonObject>()) {
        _nodeNames[kv.key().c_str()] = kv.value().as<String>();
    }
    return true;
}

std::vector<String> ConfigStore::listScenarios() {
    std::vector<String> list;
    if (!_fsMounted) return list;

#if defined(ESP32)
    File dir = LittleFS.open(SCENARIOS_DIR);
    if (!dir || !dir.isDirectory()) return list;

    File file = dir.openNextFile();
    while (file) {
        String fname = file.name();
        // Remove leading directory path if present
        int lastSlash = fname.lastIndexOf('/');
        if (lastSlash >= 0) fname = fname.substring(lastSlash + 1);

        if (fname.endsWith(".csv")) {
            list.push_back(fname);
        }
        file = dir.openNextFile();
    }
#elif defined(ESP8266)
    Dir dir = LittleFS.openDir(SCENARIOS_DIR);
    while (dir.next()) {
        String fname = dir.fileName();
        int lastSlash = fname.lastIndexOf('/');
        if (lastSlash >= 0) fname = fname.substring(lastSlash + 1);

        if (fname.endsWith(".csv")) {
            list.push_back(fname);
        }
    }
#endif
    return list;
}

String ConfigStore::readScenarioRaw(const String& filename) {
    String path = String(SCENARIOS_DIR) + "/" + filename;
    if (!_fsMounted || !LittleFS.exists(path)) return String();

    File file = LittleFS.open(path, "r");
    if (!file) return String();

    String content = file.readString();
    file.close();
    return content;
}

bool ConfigStore::loadScenario(const String& filename, std::vector<ScenarioStep>& steps) {
    String path = String(SCENARIOS_DIR) + "/" + filename;
    if (!_fsMounted || !LittleFS.exists(path)) return false;

    File file = LittleFS.open(path, "r");
    if (!file) return false;

    steps.clear();
    while (file.available()) {
        String line = file.readStringUntil('\n');
        ScenarioStep step;
        if (parseCsvLine(line, step)) {
            steps.push_back(step);
        }
    }
    file.close();
    return true;
}

bool ConfigStore::saveScenario(const String& filename, const String& csvContent) {
    if (!_fsMounted) return false;
    String cleanName = filename;
    if (!cleanName.endsWith(".csv")) cleanName += ".csv";
    String path = String(SCENARIOS_DIR) + "/" + cleanName;

    File file = LittleFS.open(path, "w");
    if (!file) return false;

    file.print(csvContent);
    file.close();
    return true;
}

bool ConfigStore::saveScenarioSteps(const String& filename, const std::vector<ScenarioStep>& steps) {
    String content = getCsvHeader() + "\n";
    for (const auto& step : steps) {
        content += stepToCsvLine(step) + "\n";
    }
    return saveScenario(filename, content);
}

bool ConfigStore::deleteScenario(const String& filename) {
    String path = String(SCENARIOS_DIR) + "/" + filename;
    if (!_fsMounted || !LittleFS.exists(path)) return false;
    return LittleFS.remove(path);
}

void ConfigStore::createDefaultScenarioIfNotExists() {
    String defaultPath = String(SCENARIOS_DIR) + "/default.csv";
    if (LittleFS.exists(defaultPath)) return;

    String demoCsv = 
        "STEP_ID, TRIGGER_TYPE, TRIGGER_VALUE, TARGET_NODE, ACTION, PARAMETER\n"
        "# Initial route setup on green signal\n"
        "1, START, 0, TRACK_1, SET_SWITCH, STRAIGHT\n"
        "2, START, 0, LOCO_1, SET_LIGHTS, AUTO\n"
        "3, START, 0, LOCO_1, SET_SPEED, 50\n"
        "# Approaching station approach beacon\n"
        "4, IR_BEACON, 5, LOCO_1, SET_SPEED, 30\n"
        "5, IR_BEACON, 5, TRACK_1, SET_SWITCH, TURNOUT\n"
        "# Train arrives at platform\n"
        "6, TRACK_OCCUPIED, TRACK_1, LOCO_1, SET_SPEED, 0\n"
        "7, TRACK_OCCUPIED, TRACK_1, TRACK_1, DWELL_WAIT, 10\n"
        "# Departure after platform dwell clearance\n"
        "8, TRACK_CLEARED, TRACK_1, TRACK_1, SET_SWITCH, STRAIGHT\n"
        "9, TRACK_CLEARED, TRACK_1, LOCO_1, SET_SPEED, 45\n";

    saveScenario("default.csv", demoCsv);
    Serial.println(F("[ConfigStore] Created default demonstration scenario 'default.csv'"));
}
