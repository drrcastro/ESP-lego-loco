#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include "ConfigStore.h"
#include "ESPNowManager.h"
#include "LocoWebServer.h"
#include "ScenarioEngine.h"

static DNSServer g_dnsServer;

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
            doc["nodeId"]       = t->nodeId;
            doc["speed"]        = t->currentSpeed;
            doc["currentBlock"] = t->currentBlockId;
            doc["batteryMv"]    = t->batteryMv;
            LocoWebServer::instance().broadcastTelemetry("loco_telemetry", doc);

            // If train crossed a new beacon, hook into Scenario Engine
            if (blockChanged && t->currentBlockId > 0) {
                ScenarioEngine::instance().onBeaconDetected(t->nodeId, t->currentBlockId);
                // Also broadcast ETA to track stations
                ScenarioEngine::instance().updateStationEta("ALL", t->nodeId, t->currentBlockId, t->currentSpeed);
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
            doc["nodeId"]       = t->nodeId;
            doc["switchState"]  = (t->switchPosition == SWITCH_STRAIGHT) ? "STRAIGHT" : "TURNOUT";
            doc["beamOccupied"] = (bool)t->beamOccupied;
            LocoWebServer::instance().broadcastTelemetry("track_telemetry", doc);

            if (occChanged) {
                ScenarioEngine::instance().onTrackOccupancyChanged(t->nodeId, (bool)t->beamOccupied);
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F("  LEGO TRAIN MASTER GATEWAY & BRAIN (V2.0)        "));
    Serial.println(F("=================================================="));

    // 1. Initialize Configuration & LittleFS
    ConfigStore::instance().begin();
    const SystemSettings& settings = ConfigStore::instance().getSettings();
    Serial.printf("[Config] Loaded Settings: SSID='%s', apMode=%d, Ch=%d\n",
                  settings.wifiSsid.c_str(), (int)settings.apMode, (int)settings.wifiChannel);

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
    });

    LocoWebServer::instance().onTrackControl([](const String& target, uint8_t switchPos, uint16_t dwell) {
        MsgTrackCommand cmd = {};
        cmd.msgType = MSG_TRACK_COMMAND;
        strncpy(cmd.targetNodeId, target.c_str(), sizeof(cmd.targetNodeId) - 1);
        cmd.switchIndex = 0;
        cmd.switchPosition = switchPos;
        cmd.dwellTimeSec = dwell;
        ESPNowManager::instance().sendToNode(target.c_str(), &cmd, sizeof(cmd));
    });

    LocoWebServer::instance().onEmergencyStop([]() {
        Serial.println(F("[Master] EMERGENCY STOP ALL! Broadcast sent."));
        MsgEmergencyStop em = {};
        em.msgType = MSG_EMERGENCY_STOP;
        em.reasonCode = 0; // Manual Web UI button
        strncpy(em.sourceNodeId, "MASTER", sizeof(em.sourceNodeId) - 1);
        ESPNowManager::instance().sendBroadcast(&em, sizeof(em));
        ScenarioEngine::instance().stopScenario();
    });

    LocoWebServer::instance().onScenarioRun([](const String& scenarioName, bool run) {
        if (run) {
            LocoWebServer::instance().setOperatingMode(MODE_AUTOMATIC);
            ScenarioEngine::instance().startScenario(scenarioName);
        } else {
            ScenarioEngine::instance().stopScenario();
            LocoWebServer::instance().setOperatingMode(MODE_MANUAL);
        }
    });

    LocoWebServer::instance().onModeChange([](OperatingMode mode) {
        if (mode == MODE_MANUAL) {
            ScenarioEngine::instance().stopScenario();
        }
        Serial.printf("[Master] Mode changed to: %s\n", (mode == MODE_AUTOMATIC) ? "AUTOMATIC" : "MANUAL");
    });

    LocoWebServer::instance().begin(80);

    // 5. Initialize Scenario Engine
    ScenarioEngine::instance().begin();

    Serial.println(F("[Master] Startup complete. System is ready!"));
}

void loop() {
    g_dnsServer.processNextRequest();
    ESPNowManager::instance().update();
    ScenarioEngine::instance().update();
    delay(2);
}
