#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include "ConfigStore.h"
#include "ESPNowManager.h"
#include "LocoWebServer.h"
#include "TopologyManager.h"

static DNSServer g_dnsServer;

struct ActiveLocoCmd {
    char targetNodeId[16];
    int8_t targetSpeed;
    uint8_t brake;
    uint8_t lightsFront;
    uint8_t lightsRear;
    uint8_t lightsCab;
    uint8_t lightingMode;
};
static std::vector<ActiveLocoCmd> g_activeLocoCmds;

static void handleIncomingEspNow(const uint8_t* mac, const uint8_t* data, int len) {
    if (len <= 0 || !data) return;
    uint8_t msgType = data[0];

    if (msgType == MSG_LOCO_TELEMETRY && len >= (int)sizeof(MsgLocoTelemetry)) {
        const MsgLocoTelemetry* t = (const MsgLocoTelemetry*)data;
        DiscoveredNode* node = ESPNowManager::instance().findNode(t->nodeId);
        if (node) {
            bool blockChanged = (node->currentBlock != t->currentBlockId);
            node->speed = t->currentSpeed;
            node->currentBlock = t->currentBlockId;
            node->batteryMv = t->batteryMv;
            node->lastSeenMs = millis();
            node->isOnline = true;

            // Broadcast telemetry to connected Web UI clients
            JsonDocument doc;
            doc["nodeId"]           = t->nodeId;
            doc["speed"]            = t->currentSpeed;
            doc["currentBlock"]     = t->currentBlockId;
            doc["batteryMv"]        = t->batteryMv;
            doc["locoState"]        = t->locoState;
            doc["etaSeconds"]       = t->etaSeconds;
            doc["measuredLengthCm"] = t->measuredLengthCm;
            doc["lapCount"]         = t->lapCount;
            LocoWebServer::instance().broadcastTelemetry("loco_telemetry", doc);

            // Hook into Topology Manager
            if (blockChanged && t->currentBlockId > 0) {
                TopologyManager::instance().onBeaconDetected(t->nodeId, t->currentBlockId, t->currentSpeed);
            }
        }
    } else if (msgType == MSG_TRACK_TELEMETRY && len >= (int)sizeof(MsgTrackTelemetry)) {
        const MsgTrackTelemetry* t = (const MsgTrackTelemetry*)data;
        DiscoveredNode* node = ESPNowManager::instance().findNode(t->nodeId);
        if (node) {
            bool occChanged = (node->beamOccupied != t->beamOccupied);
            node->switchPosition = t->switchPosition;
            node->beamOccupied   = t->beamOccupied;
            node->lastSeenMs     = millis();
            node->isOnline       = true;

            // Broadcast track state to Web UI
            JsonDocument doc;
            doc["nodeId"]               = t->nodeId;
            doc["switchState"]          = (t->switchPosition == SWITCH_STRAIGHT) ? "STRAIGHT" : "TURNOUT";
            doc["beamOccupied"]         = (bool)t->beamOccupied;
            doc["currentSignalAspect"]  = t->currentSignalAspect;
            doc["lastMeasuredLengthCm"] = t->lastMeasuredLengthCm;
            doc["dwellRemainingSec"]   = t->dwellRemainingSec;
            LocoWebServer::instance().broadcastTelemetry("track_telemetry", doc);

            if (occChanged) {
                TopologyManager::instance().onTrackOccupancyChanged(t->nodeId, (bool)t->beamOccupied);
            }
        }
    } else if (msgType == MSG_CIRCUIT_STATUS && len >= (int)sizeof(MsgCircuitStatus)) {
        const MsgCircuitStatus* s = (const MsgCircuitStatus*)data;
        TopologyManager::instance().onLocoCircuitStatus(*s);
    } else if (msgType == MSG_TRAIN_LENGTH_REPORT && len >= (int)sizeof(MsgTrainLengthReport)) {
        const MsgTrainLengthReport* r = (const MsgTrainLengthReport*)data;
        TopologyManager::instance().onTrainLengthReport(*r);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F("  LEGO TRAIN MASTER GATEWAY & SUPERVISOR          "));
    Serial.println(F("=================================================="));

    // 1. Initialize Configuration & LittleFS
    ConfigStore::instance().begin();
    const SystemSettings& settings = ConfigStore::instance().getSettings();
    Serial.printf("[Config] Loaded Settings: Layout='%s', SSID='%s', Ch=%d\n",
                  settings.layoutName.c_str(), settings.wifiSsid.c_str(), (int)settings.wifiChannel);

    // 2. Wi-Fi Configuration (AP or STA)
    WiFi.persistent(false);
    WiFi.disconnect(true, true);
    delay(50);

    IPAddress apIP(192, 168, 4, 1);
    IPAddress netMsk(255, 255, 255, 0);

    if (settings.apMode) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(apIP, apIP, netMsk);

        const char* pass = (settings.wifiPassword.length() >= 8) ? settings.wifiPassword.c_str() : nullptr;
        uint8_t ch = (settings.wifiChannel >= 1 && settings.wifiChannel <= 13) ? settings.wifiChannel : 1;
        String ssid = (settings.wifiSsid.length() > 0) ? settings.wifiSsid : String("LegoTrain_Master");

        bool apOk = WiFi.softAP(ssid.c_str(), pass, ch);
        if (!apOk) {
            Serial.println(F("[Wi-Fi] ERROR: softAP failed with custom params! Retrying with open 'LegoTrain_Master' on Ch 1..."));
            WiFi.softAP("LegoTrain_Master", nullptr, 1);
        }

        Serial.printf("[Wi-Fi] SoftAP Started: %s (IP: %s, Ch: %d)\n",
                      WiFi.softAPSSID().c_str(),
                      WiFi.softAPIP().toString().c_str(),
                      ch);

        // Start Captive Portal DNS to auto-redirect mobile devices
        g_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        g_dnsServer.start(53, "*", apIP);
        Serial.printf("[DNS] Captive Portal server started on %s:53\n", apIP.toString().c_str());
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());
        Serial.printf("[Wi-Fi] Connecting to %s...\n", settings.wifiSsid.c_str());
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
            delay(200);
            Serial.print('.');
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[Wi-Fi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println(F("[Wi-Fi] Failed to connect, falling back to SoftAP"));
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAPConfig(apIP, apIP, netMsk);
            WiFi.softAP("LegoTrain_Master_Fallback", nullptr, 1);
            g_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
            g_dnsServer.start(53, "*", apIP);
        }
    }

    // Start mDNS Responder so users can access http://legoloco.local
    if (MDNS.begin("legoloco")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println(F("[mDNS] Responder started: http://legoloco.local"));
    }

    // 3. Initialize ESP-NOW Mesh/Star Network
    uint8_t ch = WiFi.channel();
    if (ch == 0) ch = settings.wifiChannel;
    ESPNowManager::instance().begin(NODE_TYPE_MASTER, ch);
    ESPNowManager::instance().onReceive(handleIncomingEspNow);

    // Notify Web UI on node discovery/state changes
    ESPNowManager::instance().onNodeEvent([](const DiscoveredNode& node, bool isNew) {
        JsonDocument doc;
        doc["nodeId"]        = node.nodeId;
        doc["friendlyName"]  = ConfigStore::instance().getFriendlyName(node.nodeId);
        doc["nodeType"]      = (node.nodeType == NODE_TYPE_LOCO) ? "LOCO" : "TRACK";
        doc["isOnline"]      = node.isOnline;
        doc["isPaired"]      = node.isPaired;
        doc["isBondedOther"] = node.isBondedOther;
        doc["isNew"]         = isNew;
        LocoWebServer::instance().broadcastTelemetry("node_update", doc);
    });

    // 4. Initialize WebServer & WebSocket Handlers
    LocoWebServer::instance().onLocoControl([](const String& target, int8_t speed, uint8_t brake,
                                              uint8_t lf, uint8_t lr, uint8_t lc, uint8_t lm) {
        MsgLocoCommand cmd = {};
        cmd.msgType = MSG_LOCO_COMMAND;
        strncpy(cmd.targetNodeId, target.c_str(), sizeof(cmd.targetNodeId) - 1);
        cmd.targetSpeed = speed;
        cmd.brake = brake;
        cmd.lightsFront = lf;
        cmd.lightsRear  = lr;
        cmd.lightsCab   = lc;
        cmd.lightingMode = lm;

        if (target == "ALL") {
            ESPNowManager::instance().sendBroadcast(&cmd, sizeof(cmd));
        } else {
            ESPNowManager::instance().sendToNode(target.c_str(), &cmd, sizeof(cmd));
        }

        // Maintain active command state for periodic failsafe refresh
        if (speed == 0 && brake == 2) {
            for (auto it = g_activeLocoCmds.begin(); it != g_activeLocoCmds.end(); ) {
                if (strcmp(it->targetNodeId, target.c_str()) == 0) {
                    it = g_activeLocoCmds.erase(it);
                } else {
                    ++it;
                }
            }
            return;
        }

        bool found = false;
        for (auto& c : g_activeLocoCmds) {
            if (strcmp(c.targetNodeId, target.c_str()) == 0) {
                c.targetSpeed = speed;
                c.brake = brake;
                c.lightsFront = lf;
                c.lightsRear = lr;
                c.lightsCab = lc;
                c.lightingMode = lm;
                found = true;
                break;
            }
        }
        if (!found) {
            ActiveLocoCmd c = {};
            strncpy(c.targetNodeId, target.c_str(), sizeof(c.targetNodeId) - 1);
            c.targetSpeed = speed;
            c.brake = brake;
            c.lightsFront = lf;
            c.lightsRear = lr;
            c.lightsCab = lc;
            c.lightingMode = lm;
            g_activeLocoCmds.push_back(c);
        }
    });

    LocoWebServer::instance().onTrackControl([](const String& target, uint8_t switchPos, uint16_t dwell, uint8_t switchIndex) {
        MsgTrackCommand cmd = {};
        cmd.msgType = MSG_TRACK_COMMAND;
        strncpy(cmd.targetNodeId, target.c_str(), sizeof(cmd.targetNodeId) - 1);
        cmd.switchIndex = switchIndex;
        cmd.switchPosition = switchPos;
        cmd.dwellTimeSec = dwell;
        if (target == "ALL") {
            ESPNowManager::instance().sendBroadcast(&cmd, sizeof(cmd));
        } else {
            ESPNowManager::instance().sendToNode(target.c_str(), &cmd, sizeof(cmd));
        }
    });

    LocoWebServer::instance().onEmergencyStop([]() {
        Serial.println(F("[Master] EMERGENCY STOP ALL! Broadcast sent."));
        MsgEmergencyStop em = {};
        em.msgType = MSG_EMERGENCY_STOP;
        em.reasonCode = 0; // Manual Web UI button
        strncpy(em.sourceNodeId, "MASTER", sizeof(em.sourceNodeId) - 1);
        ESPNowManager::instance().sendBroadcast(&em, sizeof(em));

        // Clear active running speeds
        for (auto& c : g_activeLocoCmds) {
            c.targetSpeed = 0;
            c.brake = 2;
        }
    });

    LocoWebServer::instance().onLearningLap([](const String& locoId, bool start, uint8_t speed) {
        if (start) {
            TopologyManager::instance().startLearningLap(locoId.c_str(), speed);
        } else {
            TopologyManager::instance().stopLearningLap(locoId.c_str());
        }
    });

    LocoWebServer::instance().onModeChange([](OperatingMode mode) {
        Serial.printf("[Master] Mode changed to: %s\n", (mode == MODE_AUTONOMOUS) ? "AUTONOMOUS" : "MANUAL");
        MsgLearningCmd cmd = {};
        cmd.msgType = MSG_LEARNING_CMD;
        strncpy(cmd.targetLocoId, "ALL", sizeof(cmd.targetLocoId) - 1);
        if (mode == MODE_AUTONOMOUS) {
            cmd.command = 3; // START_AUTONOMOUS
            cmd.calibrationSpeed = 35;
        } else {
            cmd.command = 0; // STOP_AUTONOMOUS -> return to manual stop
        }
        ESPNowManager::instance().sendBroadcast(&cmd, sizeof(cmd));
    });

    LocoWebServer::instance().begin(80);

    // 5. Initialize Topology Manager
    TopologyManager::instance().begin();

    Serial.println(F("[Master] Startup complete. System is ready!"));
}

void loop() {
    g_dnsServer.processNextRequest();
    ESPNowManager::instance().update();
    TopologyManager::instance().update();

    // Periodic refresh of active running locomotives (every 1500ms) to ensure continuous cruising in manual mode
    static uint32_t lastLocoRefreshMs = 0;
    uint32_t now = millis();
    if (now - lastLocoRefreshMs >= 1500) {
        lastLocoRefreshMs = now;
        for (const auto& c : g_activeLocoCmds) {
            if (c.targetSpeed != 0) {
                MsgLocoCommand cmd = {};
                cmd.msgType = MSG_LOCO_COMMAND;
                strncpy(cmd.targetNodeId, c.targetNodeId, sizeof(cmd.targetNodeId) - 1);
                cmd.targetSpeed = c.targetSpeed;
                cmd.brake = c.brake;
                cmd.lightsFront = c.lightsFront;
                cmd.lightsRear = c.lightsRear;
                cmd.lightsCab = c.lightsCab;
                cmd.lightingMode = c.lightingMode;
                if (strcmp(c.targetNodeId, "ALL") == 0) {
                    ESPNowManager::instance().sendBroadcast(&cmd, sizeof(cmd));
                } else {
                    ESPNowManager::instance().sendToNode(c.targetNodeId, &cmd, sizeof(cmd));
                }
            }
        }
    }

    delay(2);
}
