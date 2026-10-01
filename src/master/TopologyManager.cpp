#include "TopologyManager.h"
#include "ESPNowManager.h"
#include "LocoWebServer.h"
#include <ArduinoJson.h>

TopologyManager& TopologyManager::instance() {
    static TopologyManager inst;
    return inst;
}

TopologyManager::TopologyManager() {
}

void TopologyManager::begin() {
    loadTopology();
    Serial.println(F("[TopologyManager] Circuit graph manager initialized."));
}

void TopologyManager::update() {
    // Background topology decay or sanity checks if needed
}

void TopologyManager::registerOrUpdateBeacon(uint16_t beaconId, uint8_t role, const char* stationId) {
    if (beaconId == 0) return;
    for (auto& v : _vertices) {
        if (v.beaconId == beaconId) {
            v.lastSeenMs = millis();
            if (role != 0) v.role = role;
            if (stationId && strlen(stationId) > 0) {
                strncpy(v.stationId, stationId, sizeof(v.stationId) - 1);
            }
            return;
        }
    }
    TrackNodeVertex nv = {};
    nv.beaconId = beaconId;
    nv.role = role;
    if (stationId) strncpy(nv.stationId, stationId, sizeof(nv.stationId) - 1);
    snprintf(nv.description, sizeof(nv.description), "Beacon #%u", beaconId);
    nv.lastSeenMs = millis();
    _vertices.push_back(nv);
}

void TopologyManager::registerOrUpdateEdge(uint16_t fromB, uint16_t toB, uint32_t dtMs, uint8_t routeType) {
    if (fromB == 0 || toB == 0 || fromB == toB) return;
    for (auto& e : _edges) {
        if (e.fromBeaconId == fromB && e.toBeaconId == toB && e.routeType == routeType) {
            // Smoothly average transit time
            if (dtMs > 0) {
                e.transitTimeMs = (e.transitTimeMs == 0) ? dtMs : (uint32_t)(e.transitTimeMs * 0.7f + dtMs * 0.3f);
            }
            return;
        }
    }
    TrackSegmentEdge ne;
    ne.fromBeaconId = fromB;
    ne.toBeaconId = toB;
    ne.transitTimeMs = dtMs;
    ne.routeType = routeType;
    _edges.push_back(ne);
    saveTopology();
}

void TopologyManager::onBeaconDetected(const char* locoId, uint16_t blockId, int8_t currentSpeed) {
    if (!locoId || blockId == 0) return;

    registerOrUpdateBeacon(blockId);

    uint32_t now = millis();
    String lId = String(locoId);

    if (_locoLastBeacon.find(lId) != _locoLastBeacon.end()) {
        uint16_t prevB = _locoLastBeacon[lId];
        uint32_t prevT = _locoLastBeaconTime[lId];
        if (prevB != blockId && prevB > 0) {
            uint32_t dt = now - prevT;
            // Record edge transition
            registerOrUpdateEdge(prevB, blockId, dt, 0);
            Serial.printf("[Topology] Edge Transition: %s crossed B#%u -> B#%u (dt=%ums, speed=%d%%)\n",
                          locoId, prevB, blockId, dt, currentSpeed);
        }
    }

    _locoLastBeacon[lId] = blockId;
    _locoLastBeaconTime[lId] = now;
}

void TopologyManager::onTrackOccupancyChanged(const char* trackId, bool occupied) {
    Serial.printf("[Topology] Track node %s occupancy = %s\n", trackId, occupied ? "OCCUPIED" : "CLEAR");
}

void TopologyManager::onLocoCircuitStatus(const MsgCircuitStatus& status) {
    Serial.printf("[Topology] Circuit status from %s: state=%u, beacons=%u, lapTime=%ums, calibrated=%u\n",
                  status.locoId, status.state, status.beaconCount, status.totalLapTimeMs, status.isCalibrated);

    // Notify Web UI
    JsonDocument doc;
    doc["locoId"]         = status.locoId;
    doc["state"]          = status.state;
    doc["beaconCount"]    = status.beaconCount;
    doc["totalLapTimeMs"] = status.totalLapTimeMs;
    doc["isCalibrated"]   = (bool)status.isCalibrated;
    LocoWebServer::instance().broadcastTelemetry("circuit_status", doc);
}

void TopologyManager::onTrainLengthReport(const MsgTrainLengthReport& report) {
    Serial.printf("[Topology] Length Report: Loco %s at B#%u = %ucm (dt=%ums) reported by %s\n",
                  report.locoId, report.beaconId, report.measuredLengthCm, report.transitTimeMs, report.sensorNodeId);

    // Update Loco config with measured length
    if (strlen(report.locoId) > 0) {
        LocoParamConfig cfg = ConfigStore::instance().getLocoConfig(report.locoId);
        cfg.measuredLengthCm = report.measuredLengthCm;
        ConfigStore::instance().setLocoConfig(cfg);
    }

    // Broadcast event to Web UI
    JsonDocument doc;
    doc["locoId"]           = report.locoId;
    doc["sensorNodeId"]     = report.sensorNodeId;
    doc["beaconId"]         = report.beaconId;
    doc["measuredLengthCm"] = report.measuredLengthCm;
    doc["transitTimeMs"]    = report.transitTimeMs;
    LocoWebServer::instance().broadcastTelemetry("train_length_report", doc);
}

bool TopologyManager::startLearningLap(const char* locoId, uint8_t calibrationSpeed) {
    Serial.printf("[Topology] Triggering Learning Lap for Loco '%s' at speed %u%%\n", locoId, calibrationSpeed);

    MsgLearningCmd cmd = {};
    cmd.msgType = MSG_LEARNING_CMD;
    strncpy(cmd.targetLocoId, locoId, sizeof(cmd.targetLocoId) - 1);
    cmd.command = 1; // START_LEARNING
    cmd.calibrationSpeed = calibrationSpeed;

    if (strcmp(locoId, "ALL") == 0) {
        return ESPNowManager::instance().sendBroadcast(&cmd, sizeof(cmd));
    } else {
        return ESPNowManager::instance().sendToNode(locoId, &cmd, sizeof(cmd));
    }
}

bool TopologyManager::stopLearningLap(const char* locoId) {
    Serial.printf("[Topology] Stopping Learning Lap for Loco '%s'\n", locoId);

    MsgLearningCmd cmd = {};
    cmd.msgType = MSG_LEARNING_CMD;
    strncpy(cmd.targetLocoId, locoId, sizeof(cmd.targetLocoId) - 1);
    cmd.command = 0; // STOP_LEARNING
    cmd.calibrationSpeed = 0;

    if (strcmp(locoId, "ALL") == 0) {
        return ESPNowManager::instance().sendBroadcast(&cmd, sizeof(cmd));
    } else {
        return ESPNowManager::instance().sendToNode(locoId, &cmd, sizeof(cmd));
    }
}

String TopologyManager::getTopologyJson() {
    JsonDocument doc;

    // Synchronize station and beacon associations from ConfigStore
    const auto& stations = ConfigStore::instance().getAllStations();
    for (const auto& s : stations) {
        for (const auto& b : s.beacons) {
            registerOrUpdateBeacon(b.beaconId, b.role, s.nodeId.c_str());
        }
    }

    JsonArray nodesArr = doc["nodes"].to<JsonArray>();
    for (const auto& v : _vertices) {
        JsonObject no = nodesArr.add<JsonObject>();
        no["beaconId"]    = v.beaconId;
        no["role"]        = v.role;
        no["stationId"]   = v.stationId;
        no["description"] = v.description;
    }

    JsonArray edgesArr = doc["edges"].to<JsonArray>();
    for (const auto& e : _edges) {
        JsonObject eo = edgesArr.add<JsonObject>();
        eo["from"]          = e.fromBeaconId;
        eo["to"]            = e.toBeaconId;
        eo["transitTimeMs"] = e.transitTimeMs;
        eo["routeType"]     = e.routeType;
    }

    String res;
    serializeJson(doc, res);
    return res;
}

bool TopologyManager::saveTopology() {
    String jsonStr = getTopologyJson();
    return ConfigStore::instance().saveTopologyJson(jsonStr);
}

bool TopologyManager::loadTopology() {
    String jsonStr = ConfigStore::instance().loadTopologyJson();
    if (jsonStr.length() < 10) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    if (err) return false;

    if (doc["nodes"].is<JsonArray>()) {
        _vertices.clear();
        for (JsonObject no : doc["nodes"].as<JsonArray>()) {
            TrackNodeVertex v = {};
            v.beaconId = no["beaconId"] | 0;
            v.role     = no["role"] | 0;
            const char* st = no["stationId"] | "";
            strncpy(v.stationId, st, sizeof(v.stationId) - 1);
            const char* desc = no["description"] | "";
            strncpy(v.description, desc, sizeof(v.description) - 1);
            v.lastSeenMs = millis();
            if (v.beaconId > 0) _vertices.push_back(v);
        }
    }

    if (doc["edges"].is<JsonArray>()) {
        _edges.clear();
        for (JsonObject eo : doc["edges"].as<JsonArray>()) {
            TrackSegmentEdge e;
            e.fromBeaconId   = eo["from"] | 0;
            e.toBeaconId     = eo["to"] | 0;
            e.transitTimeMs  = eo["transitTimeMs"] | 0;
            e.routeType      = eo["routeType"] | 0;
            if (e.fromBeaconId > 0 && e.toBeaconId > 0) _edges.push_back(e);
        }
    }

    return true;
}
