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

    if (!loadUnifiedConfig()) {
        initDefaultConfig();
        saveUnifiedConfig();
    }

    if (!loadNodeMappings()) {
        saveNodeMappings();
    }

    // Ensure default topology exists if missing
    if (!LittleFS.exists(TOPOLOGY_PATH)) {
        const char* defTopo = R"({"isCalibrated":false,"totalLapTimeMs":0,"nodes":[{"id":10,"type":"BEACON","role":"ROLE_APPROACH","label":"West Approach"},{"id":11,"type":"STATION","role":"ROLE_STATION_ARRIVAL","label":"Platform 1"},{"id":12,"type":"BEACON","role":"ROLE_DEPARTURE","label":"East Exit"},{"id":15,"type":"BEACON","role":"ROLE_SIDING","label":"Passing Siding"},{"id":20,"type":"BEACON","role":"ROLE_LOCATOR","label":"South Sector"}],"edges":[{"from":10,"to":11,"transitTimeMs":4200,"isTurnoutBranch":false},{"from":10,"to":15,"transitTimeMs":4600,"isTurnoutBranch":true},{"from":11,"to":12,"transitTimeMs":3800,"isTurnoutBranch":false},{"from":15,"to":12,"transitTimeMs":4100,"isTurnoutBranch":false},{"from":12,"to":20,"transitTimeMs":6200,"isTurnoutBranch":false},{"from":20,"to":10,"transitTimeMs":5900,"isTurnoutBranch":false}]})";
        saveTopologyJson(defTopo);
    }

    return true;
}

void ConfigStore::initDefaultConfig() {
    _settings = SystemSettings();

    // Default Station 1
    StationParamConfig st1;
    st1.nodeId = "TRACK_1A2B";
    st1.name = "Central Station";
    st1.defaultSwitch = "STRAIGHT";
    st1.servoStraightAngle = 75;
    st1.servoTurnoutAngle = 105;
    st1.dwellTimeSec = 10;
    st1.autoDivertOnOccupied = true;

    StationBeaconParam b1;
    b1.beaconId = 10;
    b1.role = 0; // ROLE_LOCATOR
    b1.description = "Sector Approach Tracker";
    b1.measureTrainLength = true;
    b1.dwellSec = 0;
    st1.beacons.push_back(b1);

    StationBeaconParam b2;
    b2.beaconId = 11;
    b2.role = 1; // ROLE_STATION_ARRIVAL
    b2.description = "Platform 1 Arrival Stop";
    b2.measureTrainLength = false;
    b2.dwellSec = 10;
    st1.beacons.push_back(b2);

    _stations.push_back(st1);

    // Default Loco 1
    LocoParamConfig l1;
    l1.nodeId = "LOCO_4B5C";
    l1.name = "Express Loco";
    l1.maxSpeed = 70;
    l1.learningSpeed = 35;
    l1.accelRate = 40.0f;
    l1.decelRate = 60.0f;
    l1.brakeOffsetMs = 450;
    l1.dwellTimeSec = 10;
    l1.lightMode = "AUTO";
    l1.measuredLengthCm = 0;
    _locos.push_back(l1);
}

void ConfigStore::setSettings(const SystemSettings& settings) {
    _settings = settings;
    saveUnifiedConfig();
}

bool ConfigStore::saveSettings() {
    return saveUnifiedConfig();
}

bool ConfigStore::loadSettings() {
    return loadUnifiedConfig();
}

String ConfigStore::getFriendlyName(const String& nodeId) {
    auto it = _nodeNames.find(nodeId);
    if (it != _nodeNames.end()) {
        return it->second;
    }
    for (const auto& l : _locos) {
        if (l.nodeId == nodeId && l.name.length() > 0) return l.name;
    }
    for (const auto& s : _stations) {
        if (s.nodeId == nodeId && s.name.length() > 0) return s.name;
    }
    return nodeId;
}

void ConfigStore::setFriendlyName(const String& nodeId, const String& name) {
    _nodeNames[nodeId] = name;
    for (auto& l : _locos) {
        if (l.nodeId == nodeId) { l.name = name; break; }
    }
    for (auto& s : _stations) {
        if (s.nodeId == nodeId) { s.name = name; break; }
    }
    saveNodeMappings();
    saveUnifiedConfig();
}

bool ConfigStore::saveNodeMappings() {
    if (!_fsMounted) return false;
    File f = LittleFS.open(NODES_PATH, "w");
    if (!f) return false;

    JsonDocument doc;
    for (const auto& pair : _nodeNames) {
        doc[pair.first] = pair.second;
    }
    serializeJson(doc, f);
    f.close();
    return true;
}

bool ConfigStore::loadNodeMappings() {
    if (!_fsMounted || !LittleFS.exists(NODES_PATH)) return false;
    File f = LittleFS.open(NODES_PATH, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    _nodeNames.clear();
    JsonObject obj = doc.as<JsonObject>();
    for (JsonPair p : obj) {
        _nodeNames[p.key().c_str()] = p.value().as<String>();
    }
    return true;
}

bool ConfigStore::hasLocoConfig(const String& nodeId) const {
    for (const auto& l : _locos) {
        if (l.nodeId == nodeId) return true;
    }
    return false;
}

LocoParamConfig ConfigStore::getLocoConfig(const String& nodeId) {
    for (const auto& l : _locos) {
        if (l.nodeId == nodeId) return l;
    }
    LocoParamConfig def;
    def.nodeId = nodeId;
    def.name = getFriendlyName(nodeId);
    return def;
}

void ConfigStore::setLocoConfig(const LocoParamConfig& cfg) {
    for (auto& l : _locos) {
        if (l.nodeId == cfg.nodeId) {
            l = cfg;
            saveUnifiedConfig();
            return;
        }
    }
    _locos.push_back(cfg);
    saveUnifiedConfig();
}

bool ConfigStore::removeLoco(const String& nodeId) {
    bool removed = false;
    for (auto it = _locos.begin(); it != _locos.end(); ) {
        if (it->nodeId == nodeId) {
            it = _locos.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    _nodeNames.erase(nodeId);
    saveNodeMappings();
    saveUnifiedConfig();
    return removed;
}

bool ConfigStore::hasStationConfig(const String& nodeId) const {
    for (const auto& s : _stations) {
        if (s.nodeId == nodeId) return true;
    }
    return false;
}

StationParamConfig ConfigStore::getStationConfig(const String& nodeId) {
    for (const auto& s : _stations) {
        if (s.nodeId == nodeId) return s;
    }
    StationParamConfig def;
    def.nodeId = nodeId;
    def.name = getFriendlyName(nodeId);
    return def;
}

void ConfigStore::setStationConfig(const StationParamConfig& cfg) {
    for (auto& s : _stations) {
        if (s.nodeId == cfg.nodeId) {
            s = cfg;
            saveUnifiedConfig();
            return;
        }
    }
    _stations.push_back(cfg);
    saveUnifiedConfig();
}

String ConfigStore::serializeUnifiedConfigJson() {
    JsonDocument doc;

    // System Settings
    JsonObject sys = doc["system"].to<JsonObject>();
    sys["layoutName"]          = _settings.layoutName;
    sys["wifiSsid"]            = _settings.wifiSsid;
    sys["wifiPassword"]        = _settings.wifiPassword;
    sys["apMode"]              = _settings.apMode;
    sys["wifiChannel"]         = _settings.wifiChannel;
    sys["headwaySafeSec"]      = _settings.headwaySafeSec;
    sys["headwayCautionSec"]   = _settings.headwayCautionSec;
    sys["headwaySpeedTrimPct"] = _settings.headwaySpeedTrimPct;

    // Locomotives Array
    JsonArray locosArr = doc["locomotives"].to<JsonArray>();
    for (const auto& l : _locos) {
        JsonObject lo = locosArr.add<JsonObject>();
        lo["nodeId"]           = l.nodeId;
        lo["name"]             = l.name;
        lo["maxSpeed"]         = l.maxSpeed;
        lo["learningSpeed"]    = l.learningSpeed;
        lo["accelRate"]        = l.accelRate;
        lo["decelRate"]        = l.decelRate;
        lo["brakeOffsetMs"]    = l.brakeOffsetMs;
        lo["dwellTimeSec"]     = l.dwellTimeSec;
        lo["lightMode"]        = l.lightMode;
        lo["measuredLengthCm"] = l.measuredLengthCm;
    }

    // Stations Array
    JsonArray stationsArr = doc["stations"].to<JsonArray>();
    for (const auto& s : _stations) {
        JsonObject so = stationsArr.add<JsonObject>();
        so["nodeId"]               = s.nodeId;
        so["name"]                 = s.name;
        so["defaultSwitch"]         = s.defaultSwitch;
        so["servoStraightAngle"]   = s.servoStraightAngle;
        so["servoTurnoutAngle"]    = s.servoTurnoutAngle;
        so["dwellTimeSec"]         = s.dwellTimeSec;
        so["autoDivertOnOccupied"] = s.autoDivertOnOccupied;
        so["beaconCount"]          = s.beacons.size();

        JsonArray bArr = so["beacons"].to<JsonArray>();
        for (const auto& b : s.beacons) {
            JsonObject bo = bArr.add<JsonObject>();
            bo["beaconId"]           = b.beaconId;
            bo["gpioPin"]            = b.gpioPin;
            bo["role"]               = b.role;
            bo["description"]        = b.description;
            bo["measureTrainLength"] = b.measureTrainLength;
            bo["dwellSec"]           = b.dwellSec;
        }

        JsonArray swArr = so["switches"].to<JsonArray>();
        for (const auto& sw : s.switches) {
            JsonObject swo = swArr.add<JsonObject>();
            swo["switchId"]           = sw.switchId;
            swo["gpioPin"]            = sw.gpioPin;
            swo["servoStraightAngle"] = sw.servoStraightAngle;
            swo["servoTurnoutAngle"]  = sw.servoTurnoutAngle;
            swo["defaultPosition"]    = sw.defaultPosition;
            swo["description"]        = sw.description;
        }
    }

    String output;
    serializeJson(doc, output);
    return output;
}

bool ConfigStore::deserializeUnifiedConfigJson(const String& jsonStr) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    if (err) {
        Serial.printf("[ConfigStore] deserializeJson failed: %s\n", err.c_str());
        return false;
    }

    // System Settings
    if (doc["system"].is<JsonObject>()) {
        JsonObject sys = doc["system"].as<JsonObject>();
        if (sys["layoutName"].is<const char*>()) _settings.layoutName = sys["layoutName"].as<String>();
        if (sys["wifiSsid"].is<const char*>()) _settings.wifiSsid = sys["wifiSsid"].as<String>();
        if (sys["wifiPassword"].is<const char*>()) _settings.wifiPassword = sys["wifiPassword"].as<String>();
        if (sys["apMode"].is<bool>()) _settings.apMode = sys["apMode"].as<bool>();
        if (sys["wifiChannel"].is<uint8_t>()) _settings.wifiChannel = sys["wifiChannel"].as<uint8_t>();
        if (sys["headwaySafeSec"].is<uint16_t>()) _settings.headwaySafeSec = sys["headwaySafeSec"].as<uint16_t>();
        if (sys["headwayCautionSec"].is<uint16_t>()) _settings.headwayCautionSec = sys["headwayCautionSec"].as<uint16_t>();
        if (sys["headwaySpeedTrimPct"].is<uint8_t>()) _settings.headwaySpeedTrimPct = sys["headwaySpeedTrimPct"].as<uint8_t>();
    }

    // Locomotives
    if (doc["locomotives"].is<JsonArray>()) {
        _locos.clear();
        for (JsonObject lo : doc["locomotives"].as<JsonArray>()) {
            LocoParamConfig l;
            l.nodeId           = lo["nodeId"] | "";
            l.name             = lo["name"] | l.nodeId;
            l.maxSpeed         = lo["maxSpeed"] | 70;
            l.learningSpeed    = lo["learningSpeed"] | 35;
            l.accelRate        = lo["accelRate"] | 40.0f;
            l.decelRate        = lo["decelRate"] | 60.0f;
            l.brakeOffsetMs    = lo["brakeOffsetMs"] | 450;
            l.dwellTimeSec     = lo["dwellTimeSec"] | 10;
            l.lightMode        = lo["lightMode"] | "AUTO";
            l.measuredLengthCm = lo["measuredLengthCm"] | 0;
            if (l.nodeId.length() > 0) {
                _locos.push_back(l);
                _nodeNames[l.nodeId] = l.name;
            }
        }
    }

    // Stations
    if (doc["stations"].is<JsonArray>()) {
        _stations.clear();
        for (JsonObject so : doc["stations"].as<JsonArray>()) {
            StationParamConfig s;
            s.nodeId               = so["nodeId"] | "";
            s.name                 = so["name"] | s.nodeId;
            s.defaultSwitch        = so["defaultSwitch"] | "STRAIGHT";
            s.servoStraightAngle   = so["servoStraightAngle"] | 75;
            s.servoTurnoutAngle    = so["servoTurnoutAngle"] | 105;
            s.dwellTimeSec         = so["dwellTimeSec"] | 10;
            s.autoDivertOnOccupied = so["autoDivertOnOccupied"] | true;

            if (so["beacons"].is<JsonArray>()) {
                for (JsonObject bo : so["beacons"].as<JsonArray>()) {
                    StationBeaconParam b;
                    b.beaconId           = bo["beaconId"] | 0;
                    b.gpioPin            = bo["gpioPin"] | 19;
                    b.role               = bo["role"] | 0;
                    b.description        = bo["description"] | "";
                    b.measureTrainLength = bo["measureTrainLength"] | true;
                    b.dwellSec           = bo["dwellSec"] | 10;
                    s.beacons.push_back(b);
                }
            }

            if (so["switches"].is<JsonArray>()) {
                for (JsonObject swo : so["switches"].as<JsonArray>()) {
                    StationSwitchParam sw;
                    sw.switchId           = swo["switchId"] | 1;
                    sw.gpioPin            = swo["gpioPin"] | 18;
                    sw.servoStraightAngle = swo["servoStraightAngle"] | 75;
                    sw.servoTurnoutAngle  = swo["servoTurnoutAngle"] | 105;
                    sw.defaultPosition    = swo["defaultPosition"] | "STRAIGHT";
                    sw.description        = swo["description"] | "";
                    s.switches.push_back(sw);
                }
            } else {
                StationSwitchParam defSw;
                defSw.switchId = 1;
                defSw.gpioPin = s.switchGpioPin;
                defSw.servoStraightAngle = s.servoStraightAngle;
                defSw.servoTurnoutAngle = s.servoTurnoutAngle;
                defSw.defaultPosition = s.defaultSwitch;
                defSw.description = "Main Turnout";
                s.switches.push_back(defSw);
            }

            if (s.nodeId.length() > 0) {
                _stations.push_back(s);
                _nodeNames[s.nodeId] = s.name;
            }
        }
    }

    saveUnifiedConfig();
    return true;
}

bool ConfigStore::saveUnifiedConfig() {
    if (!_fsMounted) return false;
    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) {
        Serial.println(F("[ConfigStore] Error opening config.json for writing"));
        return false;
    }
    String jsonStr = serializeUnifiedConfigJson();
    f.print(jsonStr);
    f.close();
    Serial.println(F("[ConfigStore] Unified config saved successfully to LittleFS."));
    return true;
}

bool ConfigStore::loadUnifiedConfig() {
    if (!_fsMounted || !LittleFS.exists(CONFIG_PATH)) return false;
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) return false;

    String content = f.readString();
    f.close();
    return deserializeUnifiedConfigJson(content);
}

bool ConfigStore::saveTopologyJson(const String& jsonStr) {
    if (!_fsMounted) return false;
    File f = LittleFS.open(TOPOLOGY_PATH, "w");
    if (!f) return false;
    f.print(jsonStr);
    f.close();
    return true;
}

String ConfigStore::loadTopologyJson() {
    if (!_fsMounted || !LittleFS.exists(TOPOLOGY_PATH)) return "{}";
    File f = LittleFS.open(TOPOLOGY_PATH, "r");
    if (!f) return "{}";
    String content = f.readString();
    f.close();
    return content;
}
