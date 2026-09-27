#include "LocoWebServer.h"
#include "ESPNowManager.h"
#include <LittleFS.h>

LocoWebServer& LocoWebServer::instance() {
    static LocoWebServer inst;
    return inst;
}

LocoWebServer::LocoWebServer() {
}

bool LocoWebServer::begin(uint16_t port) {
    setupWebSocket();
    setupRoutes();
    _server.begin();
    Serial.printf("[WebServer] HTTP & WebSocket Server started on port %u\n", port);
    return true;
}

void LocoWebServer::setupWebSocket() {
    _ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType type, void *arg, uint8_t *data, size_t len) {
        switch (type) {
            case WS_EVT_CONNECT:
                Serial.printf("[WebSocket] Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
                break;
            case WS_EVT_DISCONNECT:
                Serial.printf("[WebSocket] Client #%u disconnected\n", client->id());
                break;
            case WS_EVT_DATA:
                handleWebSocketMessage(arg, data, len);
                break;
            case WS_EVT_PONG:
            case WS_EVT_ERROR:
                break;
        }
    });

    _server.addHandler(&_ws);
}

void LocoWebServer::handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0; // null-terminate
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, (char*)data);
        if (err) return;

        const char* cmd = doc["cmd"] | "";

        if (strcmp(cmd, "loco_throttle") == 0) {
            String target = doc["target"] | "ALL";
            int8_t speed = doc["speed"] | 0;
            uint8_t brake = doc["brake"] | 0;
            uint8_t lf = doc["lightsFront"] | 255;
            uint8_t lr = doc["lightsRear"] | 255;
            uint8_t lc = doc["lightsCab"] | 100;
            uint8_t lm = doc["lightMode"] | 1;
            if (_onLocoControlCb) _onLocoControlCb(target, speed, brake, lf, lr, lc, lm);
        } else if (strcmp(cmd, "track_switch") == 0) {
            String target = doc["target"] | "";
            uint8_t pos = doc["position"] | 0;
            uint16_t dwell = doc["dwell"] | 0;
            if (_onTrackControlCb) _onTrackControlCb(target, pos, dwell);
        } else if (strcmp(cmd, "emergency_stop") == 0) {
            if (_onEmergencyStopCb) _onEmergencyStopCb();
        } else if (strcmp(cmd, "set_mode") == 0) {
            String mStr = doc["mode"] | "MANUAL";
            _currentMode = (mStr == "AUTOMATIC") ? MODE_AUTOMATIC : MODE_MANUAL;
            if (_onModeChangeCb) _onModeChangeCb(_currentMode);
        } else if (strcmp(cmd, "run_scenario") == 0) {
            String scName = doc["scenario"] | "default.csv";
            bool run = doc["run"] | true;
            if (_onScenarioRunCb) _onScenarioRunCb(scName, run);
        }
    }
}

void LocoWebServer::setupRoutes() {
    // LittleFS static files
    _server.serveStatic("/style.css", LittleFS, "/style.css");
    _server.serveStatic("/app.js", LittleFS, "/app.js");
    _server.serveStatic("/favicon.ico", LittleFS, "/favicon.ico");

    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
            return;
        }

        // Complete mobile-friendly embedded controller fallback
        const char* fallbackHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <meta name="theme-color" content="#090d16">
  <title>Lego Loco Mobile Controller</title>
  <style>
    :root {
      --bg: #090d16; --card: #121826; --input: #0e1320;
      --primary: #00d2ff; --success: #00f29b; --danger: #ff3366; --text: #f0f4fc;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 12px; min-height: 100vh; }
    .header { display: flex; justify-content: space-between; align-items: center; background: var(--card); padding: 12px; border-radius: 12px; margin-bottom: 12px; border: 1px solid rgba(255,255,255,0.08); }
    .header h1 { font-size: 1.1rem; font-weight: 800; }
    .header h1 span { color: var(--primary); }
    .status-badge { font-size: 0.72rem; padding: 4px 8px; border-radius: 12px; background: rgba(0,242,155,0.15); color: var(--success); font-weight: 700; }
    .estop-bar { margin-bottom: 12px; }
    .btn-estop { width: 100%; background: linear-gradient(135deg, #ff3366, #c9184a); border: none; color: #fff; padding: 14px; font-size: 1.1rem; font-weight: 800; border-radius: 12px; cursor: pointer; box-shadow: 0 4px 15px rgba(255,51,102,0.4); }
    .card { background: var(--card); border: 1px solid rgba(255,255,255,0.08); border-radius: 14px; padding: 16px; margin-bottom: 12px; }
    .card-title { font-size: 1rem; font-weight: 700; margin-bottom: 10px; display: flex; justify-content: space-between; }
    .throttle-box { margin: 14px 0; }
    .speed-label { display: flex; justify-content: space-between; font-size: 0.85rem; color: #8a99b5; margin-bottom: 6px; }
    .speed-val { font-size: 1.3rem; font-weight: 800; color: var(--primary); }
    .slider { width: 100%; height: 14px; border-radius: 7px; background: var(--input); -webkit-appearance: none; outline: none; }
    .slider::-webkit-slider-thumb { -webkit-appearance: none; width: 34px; height: 34px; border-radius: 50%; background: var(--primary); cursor: pointer; }
    .btn-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; margin: 12px 0; }
    .btn { background: #161e31; border: 1px solid rgba(255,255,255,0.1); color: #fff; padding: 12px 6px; font-weight: 700; font-size: 0.85rem; border-radius: 8px; cursor: pointer; }
    .btn.stop { background: rgba(255,51,102,0.15); color: var(--danger); border-color: rgba(255,51,102,0.4); }
    .notice { font-size: 0.72rem; color: #5c6a85; text-align: center; margin-top: 16px; line-height: 1.4; }
  </style>
</head>
<body>
  <div class="header">
    <h1>LEGO LOCO <span>V2.0</span></h1>
    <span class="status-badge" id="wsBadge">LIVE WS</span>
  </div>

  <div class="estop-bar">
    <button class="btn-estop" onclick="eStop()">🛑 EMERGENCY STOP</button>
  </div>

  <div class="card">
    <div class="card-title">
      <span>Locomotive Throttle</span>
      <span class="speed-val" id="spdText">0%</span>
    </div>
    <div class="throttle-box">
      <div class="speed-label">
        <span>Reverse (-100%)</span>
        <span>Forward (+100%)</span>
      </div>
      <input type="range" class="slider" id="spdSlider" min="-100" max="100" value="0" oninput="setSpeed(this.value)">
    </div>
    <div class="btn-grid">
      <button class="btn" onclick="quickSpeed(50)">FWD 50%</button>
      <button class="btn stop" onclick="quickSpeed(0)">STOP</button>
      <button class="btn" onclick="quickSpeed(-50)">REV 50%</button>
    </div>
    <div style="display:flex;gap:8px;margin-top:10px;">
      <button class="btn" style="flex:1;" onclick="toggleLight(1)">Lights Auto</button>
      <button class="btn" style="flex:1;" onclick="toggleLight(0)">Lights Off</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title"><span>Track Switch</span></div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;">
      <button class="btn" onclick="setSwitch(0)">STRAIGHT</button>
      <button class="btn" onclick="setSwitch(1)">TURNOUT</button>
    </div>
  </div>

  <div class="notice">
    Lego Loco Master Controller active at http://legoloco.local / 192.168.4.1<br>
    To enable full CSV Scenario Studio, upload data files via PlatformIO uploadfs.
  </div>

  <script>
    let ws = new WebSocket((location.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + location.host + '/ws');
    let timer = null;

    ws.onopen = () => { document.getElementById('wsBadge').textContent = 'CONNECTED'; };
    ws.onclose = () => { document.getElementById('wsBadge').textContent = 'OFFLINE'; setTimeout(() => location.reload(), 3000); };

    function sendCmd(obj) {
      if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
    }
    function setSpeed(v) {
      document.getElementById('spdText').textContent = v + '%';
      clearTimeout(timer);
      timer = setTimeout(() => {
        sendCmd({ cmd: 'loco_throttle', target: 'ALL', speed: parseInt(v), brake: (parseInt(v) === 0 ? 1 : 0) });
      }, 40);
    }
    function quickSpeed(v) {
      document.getElementById('spdSlider').value = v;
      setSpeed(v);
    }
    function eStop() {
      quickSpeed(0);
      sendCmd({ cmd: 'emergency_stop' });
    }
    function toggleLight(m) {
      sendCmd({ cmd: 'loco_throttle', target: 'ALL', lightMode: m });
    }
    function setSwitch(p) {
      sendCmd({ cmd: 'track_switch', target: 'ALL', position: p });
    }
  </script>
</body>
</html>
)rawliteral";
        request->send(200, "text/html", fallbackHtml);
    });


    // API: System Status
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["mode"] = (_currentMode == MODE_AUTOMATIC) ? "AUTOMATIC" : "MANUAL";
        doc["scenarioRunning"] = _scenarioRunning;
        doc["activeScenario"] = _activeScenarioName;
        doc["uptimeSec"] = millis() / 1000;
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["nodeCount"] = ESPNowManager::instance().getDiscoveredNodes().size();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Discovered Nodes
    _server.on("/api/nodes", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        const auto& nodes = ESPNowManager::instance().getDiscoveredNodes();
        for (const auto& n : nodes) {
            JsonObject o = arr.add<JsonObject>();
            o["nodeId"]       = n.nodeId;
            o["friendlyName"] = ConfigStore::instance().getFriendlyName(n.nodeId);
            o["nodeType"]     = (n.nodeType == NODE_TYPE_LOCO) ? "LOCO" : (n.nodeType == NODE_TYPE_TRACK ? "TRACK" : "MASTER");
            o["isOnline"]     = n.isOnline;
            o["rssi"]         = n.rssi;
            o["speed"]        = n.speed;
            o["currentBlock"] = n.currentBlock;
            o["switchState"]  = (n.switchPosition == SWITCH_STRAIGHT) ? "STRAIGHT" : "TURNOUT";
            o["beamOccupied"] = (bool)n.beamOccupied;
            o["batteryMv"]    = n.batteryMv;
            o["lastSeenSec"]  = (millis() - n.lastSeenMs) / 1000;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Rename Node
    _server.on("/api/nodes/rename", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeJson(doc, (char*)data)) {
            String nodeId = doc["nodeId"] | "";
            String name   = doc["friendlyName"] | "";
            if (nodeId.length() > 0 && name.length() > 0) {
                ConfigStore::instance().setFriendlyName(nodeId, name);
                request->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"status\":\"error\"}");
    });

    // API: List Scenarios
    _server.on("/api/scenarios", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        std::vector<String> list = ConfigStore::instance().listScenarios();
        for (const auto& f : list) {
            arr.add(f);
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Get Scenario Content
    _server.on("/api/scenario", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("name")) {
            request->send(400, "application/json", "{\"error\":\"Missing name param\"}");
            return;
        }
        String filename = request->getParam("name")->value();
        String content = ConfigStore::instance().readScenarioRaw(filename);
        if (content.length() == 0) {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
            return;
        }
        request->send(200, "text/csv", content);
    });

    // API: Save Scenario (POST)
    _server.on("/api/scenario", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!request->hasParam("name", true)) {
            request->send(400, "application/json", "{\"error\":\"Missing filename\"}");
            return;
        }
        String filename = request->getParam("name", true)->value();
        String content = String((char*)data).substring(0, len);
        if (ConfigStore::instance().saveScenario(filename, content)) {
            request->send(200, "application/json", "{\"status\":\"saved\"}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save\"}");
        }
    });

    // API: Delete Scenario
    _server.on("/api/scenario", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("name")) {
            request->send(400, "application/json", "{\"error\":\"Missing name param\"}");
            return;
        }
        String filename = request->getParam("name")->value();
        if (ConfigStore::instance().deleteScenario(filename)) {
            request->send(200, "application/json", "{\"status\":\"deleted\"}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to delete\"}");
        }
    });

    // API: Start / Stop Scenario
    _server.on("/api/scenario/run", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String name = doc["scenario"] | "default.csv";
        if (_onScenarioRunCb) _onScenarioRunCb(name, true);
        request->send(200, "application/json", "{\"status\":\"running\"}");
    });

    _server.on("/api/scenario/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (_onScenarioRunCb) _onScenarioRunCb("", false);
        request->send(200, "application/json", "{\"status\":\"stopped\"}");
    });

    // API: Change Mode
    _server.on("/api/mode", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String mStr = doc["mode"] | "MANUAL";
        _currentMode = (mStr == "AUTOMATIC") ? MODE_AUTOMATIC : MODE_MANUAL;
        if (_onModeChangeCb) _onModeChangeCb(_currentMode);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // API: Direct Control Endpoints
    _server.on("/api/control/loco", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String target = doc["target"] | "ALL";
        int8_t speed = doc["speed"] | 0;
        uint8_t brake = doc["brake"] | 0;
        uint8_t lf = doc["lightsFront"] | 255;
        uint8_t lr = doc["lightsRear"] | 255;
        uint8_t lc = doc["lightsCab"] | 100;
        uint8_t lm = doc["lightMode"] | 1;
        if (_onLocoControlCb) _onLocoControlCb(target, speed, brake, lf, lr, lc, lm);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    _server.on("/api/control/track", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String target = doc["target"] | "";
        uint8_t pos = doc["position"] | 0;
        uint16_t dwell = doc["dwell"] | 0;
        if (_onTrackControlCb) _onTrackControlCb(target, pos, dwell);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // API: Emergency Stop
    _server.on("/api/emergency_stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (_onEmergencyStopCb) _onEmergencyStopCb();
        request->send(200, "application/json", "{\"status\":\"emergency_stop_triggered\"}");
    });
}

void LocoWebServer::broadcastWs(const String& jsonPayload) {
    if (_ws.count() > 0) {
        _ws.textAll(jsonPayload);
    }
}

void LocoWebServer::broadcastTelemetry(const String& type, const JsonDocument& data) {
    if (_ws.count() == 0) return;
    JsonDocument doc;
    doc["event"] = type;
    doc["data"]  = data;
    String str;
    serializeJson(doc, str);
    _ws.textAll(str);
}

void LocoWebServer::notifyScenarioStep(uint16_t stepId, const String& action, const String& target, const String& status) {
    JsonDocument doc;
    doc["event"] = "scenario_step";
    JsonObject d = doc["data"].to<JsonObject>();
    d["stepId"] = stepId;
    d["action"] = action;
    d["target"] = target;
    d["status"] = status;
    String str;
    serializeJson(doc, str);
    broadcastWs(str);
}
