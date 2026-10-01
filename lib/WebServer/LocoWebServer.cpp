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

        if (strcmp(cmd, "loco_throttle") == 0 || strcmp(cmd, "set_loco_speed") == 0) {
            String target = doc["target"] | (doc["nodeId"] | "ALL");
            int8_t speed = doc["speed"] | (doc["targetSpeed"] | 0);
            uint8_t brake = doc["brake"] | 0;
            uint8_t lf = doc["lightsFront"] | 255;
            uint8_t lr = doc["lightsRear"] | 255;
            uint8_t lc = doc["lightsCab"] | 100;
            uint8_t lm = doc["lightMode"] | 1;
            if (_onLocoControlCb) _onLocoControlCb(target, speed, brake, lf, lr, lc, lm);
        } else if (strcmp(cmd, "track_switch") == 0 || strcmp(cmd, "set_switch") == 0) {
            String target = doc["target"] | (doc["nodeId"] | "");
            uint8_t pos = 0;
            if (doc["switchPosition"].is<const char*>()) {
                const char* pStr = doc["switchPosition"];
                pos = (strcmp(pStr, "TURNOUT") == 0) ? 1 : 0;
            } else {
                pos = doc["position"] | (doc["switchPosition"] | 0);
            }
            uint16_t dwell = doc["dwell"] | 0;
            uint8_t swIdx = doc["switchIndex"] | (doc["switchId"] | 0);
            if (_onTrackControlCb) _onTrackControlCb(target, pos, dwell, swIdx);
        } else if (strcmp(cmd, "set_loco_lights") == 0) {
            String target = doc["nodeId"] | "ALL";
            const char* zone = doc["zone"] | "front";
            uint8_t lf = (strcmp(zone, "front") == 0) ? 255 : 100;
            uint8_t lr = (strcmp(zone, "rear") == 0) ? 255 : 0;
            uint8_t lc = (strcmp(zone, "cab") == 0) ? 255 : 50;
            if (_onLocoControlCb) _onLocoControlCb(target, -128, 0, lf, lr, lc, 0); // -128 = keep speed
        } else if (strcmp(cmd, "emergency_stop") == 0) {
            if (_onEmergencyStopCb) _onEmergencyStopCb();
        } else if (strcmp(cmd, "set_mode") == 0) {
            String mStr = doc["mode"] | "MANUAL";
            _currentMode = (mStr == "AUTONOMOUS" || mStr == "AUTOMATIC") ? MODE_AUTONOMOUS : MODE_MANUAL;
            if (_onModeChangeCb) _onModeChangeCb(_currentMode);
        } else if (strcmp(cmd, "start_learning") == 0) {
            String target = doc["target"] | (doc["targetLocoId"] | "ALL");
            uint8_t speed = doc["speed"] | (doc["calibrationSpeed"] | 35);
            if (_onLearningLapCb) _onLearningLapCb(target, true, speed);
        } else if (strcmp(cmd, "stop_learning") == 0) {
            String target = doc["target"] | (doc["targetLocoId"] | "ALL");
            if (_onLearningLapCb) _onLearningLapCb(target, false, 0);
        } else if (strcmp(cmd, "scan_locos") == 0) {
            ESPNowManager::instance().sendDiscoveryScan();
        } else if (strcmp(cmd, "pair_loco") == 0 || strcmp(cmd, "pair_node") == 0) {
            String target = doc["target"] | (doc["nodeId"] | "");
            if (target.length() > 0) {
                ESPNowManager::instance().pairNode(target.c_str());
            }
        } else if (strcmp(cmd, "unpair_loco") == 0 || strcmp(cmd, "unpair_node") == 0 || strcmp(cmd, "remove_loco") == 0) {
            String target = doc["target"] | (doc["nodeId"] | "");
            if (target.length() > 0) {
                ESPNowManager::instance().unpairNode(target.c_str());
                ConfigStore::instance().removeLoco(target);
                if (_onLocoControlCb) {
                    _onLocoControlCb(target, 0, 2, 0, 0, 0, 0); // E-stop & clear from active loops
                }
            }
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

        // Complete mobile-friendly embedded controller with 3-tab Advanced Configuration Studio
        const char* fallbackHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="pt">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <meta name="theme-color" content="#090d16">
  <title>Lego Loco Controller</title>
  <style>
    :root {
      --bg: #151821; --card: #202532; --card-alt: #181D27; --input: #12141C;
      --primary: #FED100; --success: #00852B; --danger: #D11013; --warning: #FED100; --accent: #0055BF;
      --text: #ffffff; --text-dim: #A3AFBF; --border: rgba(255,255,255,0.12);
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 12px; min-height: 100vh; padding-bottom: 50px; }
    
    /* Header - Sharp LEGO Tile */
    .header { display: flex; justify-content: space-between; align-items: center; background: var(--card); padding: 12px 16px; border-radius: 0; margin-bottom: 12px; border: 2px solid var(--border); box-shadow: 0 4px 14px rgba(0,0,0,0.4); }
    .header h1 { font-size: 1.15rem; font-weight: 800; display: flex; align-items: center; gap: 6px; }
    .header h1 span { color: var(--primary); }
    .header-right { display: flex; align-items: center; gap: 8px; }
    .status-badge { font-size: 0.72rem; padding: 4px 10px; border-radius: 0; background: rgba(0,133,43,0.25); color: #8EE6A8; font-weight: 800; border: 1px solid var(--success); }
    .status-badge.offline { background: rgba(209,16,19,0.25); color: #FFA3A5; border-color: var(--danger); }
    
    /* Emergency Bar - LEGO Brick Red Button */
    .estop-bar { margin-bottom: 12px; }
    .btn-estop { width: 100%; background: linear-gradient(180deg, #E3000B 0%, #D11013 100%); border: 2px solid #FFA3A5; outline: 2px solid #8A0B0E; color: #fff; padding: 13px; font-size: 1.1rem; font-weight: 800; border-radius: 0; cursor: pointer; box-shadow: 0 4px 0 #8A0B0E, 0 6px 16px rgba(209,16,19,0.4); }
    .btn-estop:active { transform: translateY(3px); box-shadow: 0 1px 0 #8A0B0E; }
    
    /* Nav Tabs - Sharp Brick Plates */
    .nav-tabs { display: flex; gap: 4px; background: var(--card); padding: 4px; border-radius: 0; margin-bottom: 14px; border: 2px solid var(--border); overflow-x: auto; }
    .nav-btn { flex: 1; min-width: 80px; background: transparent; border: none; color: var(--text-dim); padding: 9px 12px; font-size: 0.85rem; font-weight: 700; border-radius: 0; cursor: pointer; transition: all 0.2s; white-space: nowrap; text-align: center; }
    .nav-btn.active { background: var(--accent); color: #fff; box-shadow: 0 2px 0 #003B85; font-weight: 800; }
    .tab-content { display: none; }
    .tab-content.active { display: block; animation: fadeIn 0.2s ease-out; }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }

    /* Cards - Sharp Rectangular Bricks */
    .card { background: var(--card); border: 2px solid var(--border); border-radius: 0; padding: 16px; margin-bottom: 14px; box-shadow: 0 6px 18px rgba(0,0,0,0.35); }
    .card-title { font-size: 1rem; font-weight: 700; margin-bottom: 12px; display: flex; justify-content: space-between; align-items: center; }
    .card-desc { font-size: 0.8rem; color: var(--text-dim); margin-bottom: 12px; line-height: 1.4; }
    
    /* Forms - Sharp Inputs */
    .form-group { margin-bottom: 12px; }
    .form-group label { display: block; font-size: 0.8rem; color: var(--text-dim); margin-bottom: 4px; font-weight: 600; }
    .form-control { width: 100%; background: var(--input); color: var(--text); border: 1.5px solid var(--border); padding: 9px 12px; border-radius: 0; font-size: 0.9rem; }
    .form-control:focus { outline: none; border-color: var(--primary); box-shadow: 0 0 0 2px rgba(254,209,0,0.3); }
    .field-hint { display: block; font-size: 0.72rem; color: var(--text-dim); margin-top: 4px; }
    
    /* Grid */
    .grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 12px; }
    
    /* Sliders & Buttons */
    .throttle-box { margin: 14px 0; }
    .speed-label { display: flex; justify-content: space-between; font-size: 0.85rem; color: var(--text-dim); margin-bottom: 6px; }
    .speed-val { font-size: 1.3rem; font-weight: 800; color: var(--primary); }
    .slider { width: 100%; height: 16px; border-radius: 0; background: var(--input); -webkit-appearance: none; outline: none; border: 1.5px solid var(--border); }
    .slider::-webkit-slider-thumb { -webkit-appearance: none; width: 30px; height: 30px; border-radius: 50%; background: radial-gradient(circle at 35% 30%, #FFF3A8 0%, #FED100 70%, #B89200 100%); border: 2px solid #fff; cursor: pointer; box-shadow: 0 3px 8px rgba(0,0,0,0.5); }
    .btn-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; margin: 12px 0; }
    .btn { background: #374151; border: 1px solid rgba(255,255,255,0.2); color: #fff; padding: 10px 14px; font-weight: 700; font-size: 0.85rem; border-radius: 0; cursor: pointer; text-align: center; box-shadow: 0 3px 0 rgba(0,0,0,0.4); }
    .btn:active { transform: translateY(2px); box-shadow: 0 1px 0 rgba(0,0,0,0.4); }
    .btn.primary { background: var(--accent); color: #fff; font-weight: 800; border: none; box-shadow: 0 3px 0 #003B85; }
    .btn.stop { background: var(--danger); color: #fff; border-color: rgba(255,255,255,0.2); box-shadow: 0 3px 0 #8A0B0E; }
    .btn.success { background: var(--success); color: #fff; border-color: rgba(255,255,255,0.2); box-shadow: 0 3px 0 #005C1E; }
    .btn-sm { padding: 6px 10px; font-size: 0.75rem; }
    
    /* Config Subtabs */
    .subtabs-bar { display: flex; gap: 8px; border-bottom: 2px solid var(--border); margin-bottom: 14px; padding-bottom: 8px; }
    .subtab-btn { background: transparent; border: 1px solid transparent; color: var(--text-dim); font-size: 0.85rem; font-weight: 700; padding: 7px 14px; border-radius: 0; cursor: pointer; }
    .subtab-btn.active { background: var(--accent); color: #fff; box-shadow: 0 2px 0 #003B85; }
    .subtab-pane { display: none; }
    .subtab-pane.active { display: block; }
    
    /* Tables */
    .table-box { width: 100%; overflow-x: auto; margin-top: 10px; border: 1.5px solid var(--border); border-radius: 0; }
    table { width: 100%; border-collapse: collapse; font-size: 0.8rem; }
    th { background: rgba(255,255,255,0.06); color: var(--primary); padding: 8px 10px; text-align: left; border-bottom: 1.5px solid var(--border); font-weight: 800; }
    td { padding: 8px 10px; border-bottom: 1px solid rgba(255,255,255,0.05); vertical-align: middle; }
    .input-sm { background: var(--input); color: var(--text); border: 1px solid var(--border); padding: 5px 8px; border-radius: 0; font-size: 0.8rem; width: 100%; }
    
    .mode-toggle-group { display: flex; background: var(--input); padding: 3px; border-radius: 0; border: 1.5px solid var(--border); }
    .mode-btn-top { background: transparent; border: none; color: var(--text-dim); padding: 6px 12px; font-size: 0.78rem; font-weight: 700; border-radius: 0; cursor: pointer; transition: all 0.2s; }
    .mode-btn-top.active { background: var(--primary); color: #000; font-weight: 800; box-shadow: 0 2px 0 #B89200; }
    .notice { font-size: 0.75rem; color: var(--text-dim); line-height: 1.4; }
    .notice-warn { background: rgba(254,209,0,0.12); color: var(--warning); border: 1.5px dashed var(--warning); padding: 8px 10px; border-radius: 0; font-size: 0.78rem; margin: 8px 0; }
  </style>
</head>
<body>
  <!-- HEADER -->
  <div class="header">
    <h1>🚂 LEGO LOCO</h1>
    <div class="header-right">
      <div class="mode-toggle-group">
        <button class="mode-btn-top active" id="btnModeMan" onclick="setSystemMode('MANUAL')">🕹️ MANUAL</button>
        <button class="mode-btn-top" id="btnModeAuto" onclick="setSystemMode('AUTONOMOUS')">🤖 AUTONOMOUS</button>
      </div>
      <span class="status-badge" id="wsBadge">LIVE WS</span>
    </div>
  </div>

  <!-- MODE EXPLANATION BANNER -->
  <div id="modeBanner" style="background:rgba(0,85,191,0.1);border:1.5px solid rgba(0,85,191,0.35);border-radius:0;padding:10px 14px;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between;font-size:0.82rem;">
    <div id="modeBannerText">
      🕹️ <b>Manual Mode Active:</b> Direct throttle control via sliders. Master sends commands and failsafe stops motor on signal loss.
    </div>
  </div>

  <!-- EMERGENCY STOP -->
  <div class="estop-bar">
    <button class="btn-estop" onclick="eStop()">🛑 EMERGENCY STOP</button>
  </div>

  <!-- NAVIGATION TABS -->
  <div class="nav-tabs">
    <button class="nav-btn active" onclick="switchNav('tabTraction')">🚂 Locos</button>
    <button class="nav-btn" onclick="switchNav('tabTrack')">🔀 Switches</button>
    <button class="nav-btn" onclick="switchNav('tabAuto')">🤖 Autonomous</button>
    <button class="nav-btn" onclick="switchNav('tabConfig')">⚙️ Settings</button>
    <button class="nav-btn" onclick="switchNav('tabFleet')">🛰️ Fleet</button>
  </div>

  <!-- TAB 1: TRACTION CONTROL -->
  <div id="tabTraction" class="tab-content active">
    <!-- Unpaired Discovery Banner -->
    <div class="card" style="border-left: 4px solid var(--warning);">
      <div class="card-title">
        <span>🔍 Device Discovery</span>
        <button class="btn primary btn-sm" id="btnScan" onclick="scanForDevices()">🔍 Scan</button>
      </div>
      <p class="notice">Pair new locomotives and stations to associate them with this Master.</p>
      <div id="unpairedList" style="margin-top:8px;"></div>
    </div>

    <!-- Throttle Card -->
    <div class="card">
      <div class="card-title">
        <span>Traction Control</span>
        <span class="status-badge" id="locoStateBadge" style="font-size:0.75rem;background:rgba(0,210,255,0.2);color:var(--primary);">🕹️ MANUAL MODE</span>
        <span class="speed-val" id="spdText">0%</span>
      </div>

      <div class="form-group">
        <div style="display:flex;justify-content:space-between;align-items:center;">
          <label style="margin-bottom:0;">ACTIVE LOCOMOTIVE:</label>
          <button id="btnUnpairActiveLoco" class="btn danger btn-sm" style="display:none;padding:2px 8px;font-size:0.75rem;" onclick="unpairActiveLoco()">🗑️ Unpair</button>
        </div>
        <select id="targetSelect" class="form-control" onchange="onTargetChanged()" style="margin-top:6px;">
          <option value="ALL">📢 All Locomotives (Broadcast)</option>
        </select>
      </div>

      <div class="throttle-box">
        <div class="speed-label">
          <span>◀ Reverse (-100%)</span>
          <span>Forward (+100%) ▶</span>
        </div>
        <input type="range" class="slider" id="spdSlider" min="-100" max="100" value="0" oninput="setSpeed(this.value)">
      </div>

      <div class="btn-grid">
        <button class="btn" onclick="quickSpeed(-50)">◀ REV 50%</button>
        <button class="btn stop" onclick="quickSpeed(0)">⏹ STOP</button>
        <button class="btn" onclick="quickSpeed(50)">FWD 50% ▶</button>
      </div>

      <div style="display:flex;gap:8px;margin-top:10px;">
        <button class="btn" style="flex:1;" onclick="toggleLight(1)">💡 Auto Lights</button>
        <button class="btn" style="flex:1;" onclick="toggleLight(0)">Lights Off</button>
      </div>
    </div>
  </div>

  <!-- TAB 2: TRACK SWITCHES -->
  <div id="tabTrack" class="tab-content">
    <div class="card">
      <div class="card-title"><span>🔀 Station Track Switches &amp; Turnouts</span></div>
      <p class="notice" style="margin-bottom:10px;">
        Individual control of track switches and servo turnout motors, identified by their <b>ID</b> and <b>GPIO pin</b> on each station.
      </p>
      <div id="manualSwitchesContainer"></div>
      <div style="margin-top:14px;border-top:1px solid var(--border);padding-top:10px;">
        <span style="font-size:0.8rem;color:#8a99b5;">Broadcast All Turnouts Control:</span>
        <div class="grid-2" style="margin-top:6px;">
          <button class="btn primary" onclick="setSwitch('ALL', 0, 0)">➡️ ALL STRAIGHT</button>
          <button class="btn primary" onclick="setSwitch('ALL', 0, 1)">🔀 ALL TURNOUT</button>
        </div>
      </div>
      <div id="trackStateNotice" class="notice" style="margin-top:12px;"></div>
    </div>
  </div>

  <!-- TAB 3: AUTONOMOUS & LEARNING -->
  <div id="tabAuto" class="tab-content">
    <div class="card" style="border-left: 4px solid var(--success);">
      <div class="card-title">
        <span>🤖 Autonomous Operations &amp; Learning</span>
      </div>
      <p class="notice">
        <b>Learning Lap:</b> Locomotive drives around track at 35% calibration speed, learns inter-beacon transit times, and auto-calibrates station stop deceleration.
      </p>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:14px;">
        <button class="btn success" onclick="startLearningLap()">🚀 Start Learning Lap</button>
        <button class="btn" onclick="resetLearningLap()">🔄 Reset Calibration</button>
      </div>
      <div id="learningStatus" class="notice" style="margin-top:10px;font-weight:700;"></div>
    </div>
  </div>

  <!-- TAB 4: ADVANCED CONFIGURATION STUDIO (3 SEPARATORS) -->
  <div id="tabConfig" class="tab-content">
    <div class="card">
      <div class="card-title">
        <span>⚙️ Advanced Configuration Studio</span>
      </div>

      <!-- 3 Separators (Subtabs) -->
      <div class="subtabs-bar">
        <button class="subtab-btn active" id="btnSubGlobal" onclick="switchConfigSub('subGlobal')">🌐 1. Global</button>
        <button class="subtab-btn" id="btnSubLocos" onclick="switchConfigSub('subLocos')">🚂 2. Locomotives</button>
        <button class="subtab-btn" id="btnSubStations" onclick="switchConfigSub('subStations')">🚉 3. Stations &amp; Beacons</button>
      </div>

      <!-- SUBTAB 1: GLOBAL -->
      <div id="subGlobal" class="subtab-pane active">
        <div class="card-desc">Global parameters for Wi-Fi network, radio channel isolation, and anti-collision safety headways.</div>
        <div class="grid-2">
          <div class="form-group">
            <label>Track / Layout Name:</label>
            <input type="text" id="cfgLayout" class="form-control" placeholder="Lego Central">
          </div>
          <div class="form-group">
            <label>Wi-Fi SSID (Access Point Name):</label>
            <input type="text" id="cfgSsid" class="form-control" placeholder="LegoTrain_Master">
          </div>
          <div class="form-group">
            <label>Wi-Fi Password (or empty for open network):</label>
            <input type="text" id="cfgPass" class="form-control" placeholder="Min 8 characters or empty">
          </div>
          <div class="form-group">
            <label>Wi-Fi / ESP-NOW Radio Channel (1 to 13):</label>
            <input type="number" id="cfgChannel" min="1" max="13" class="form-control" value="1">
            <span class="field-hint">Allows isolating multiple layouts (e.g. Layout 1 on Channel 1, Layout 2 on Channel 6).</span>
          </div>
          <div class="form-group">
            <label>Anti-Collision Safe Headway (seconds):</label>
            <input type="number" id="cfgSafeSec" min="4" max="60" class="form-control" value="12">
            <span class="field-hint">Green Aspect: safe headway between locomotives.</span>
          </div>
          <div class="form-group">
            <label>Proximity Warning Headway (seconds):</label>
            <input type="number" id="cfgCautionSec" min="2" max="30" class="form-control" value="6">
            <span class="field-hint">Yellow Aspect: triggers preventive deceleration.</span>
          </div>
          <div class="form-group">
            <label>Warning Speed Trim (% reduction):</label>
            <input type="number" id="cfgTrimPct" min="10" max="80" class="form-control" value="40">
            <span class="field-hint">Speed reduction when approaching preceding locomotive.</span>
          </div>
        </div>
        <button class="btn primary" style="margin-top:10px;width:100%;" onclick="saveGlobalConfig()">💾 Save Global Settings</button>
      </div>

      <!-- SUBTAB 2: LOCOMOTIVES -->
      <div id="subLocos" class="subtab-pane">
        <div class="card-desc">Dynamic kinetic parameters, acceleration, deceleration, station dwell times, and length for each locomotive.</div>
        <div id="locoCardsList"></div>
      </div>

      <!-- SUBTAB 3: STATIONS & BEACONS -->
      <div id="subStations" class="subtab-pane">
        <div class="card-desc">
          Station configuration, turnout switch control, and beacon management. Supports multiple beacons per station configured as <b>Locators</b> or <b>Station Arrival</b>, with automatic train length measurement.
        </div>
        <div id="stationCardsList"></div>
      </div>
    </div>
  </div>

  <!-- TAB 5: FLEET & DISCOVERY -->
  <div id="tabFleet" class="tab-content">
    <div class="card">
      <div class="card-title">
        <span>🛰️ ESP-NOW Network &amp; Fleet Devices</span>
        <button class="btn primary btn-sm" onclick="scanForDevices()">🔍 Scan</button>
      </div>
      <div class="table-box">
        <table>
          <thead>
            <tr><th>Node</th><th>Name</th><th>Type</th><th>Signal</th><th>Status</th><th>Action</th></tr>
          </thead>
          <tbody id="nodesTableBody"></tbody>
        </table>
      </div>
    </div>
  </div>

  <!-- LOGIC SCRIPT -->
  <script>
    let ws = null;
    let timer = null;
    let currentMode = 'MANUAL';
    let systemConfig = { system: {}, locomotives: [], stations: [] };
    let discoveredNodes = [];

    // Initialize
    window.addEventListener('DOMContentLoaded', () => {
      initWs();
      loadNodes();
      loadConfig();
      updateModeUI();
      setInterval(loadNodes, 3000);
    });

    function setSystemMode(mode) {
      currentMode = mode;
      updateModeUI();
      sendCmd({ cmd: 'set_mode', mode: mode });
    }

    function updateModeUI() {
      const isAuto = (currentMode === 'AUTONOMOUS' || currentMode === 'AUTOMATIC');
      const btnMan = document.getElementById('btnModeMan');
      const btnAut = document.getElementById('btnModeAuto');
      if (btnMan) btnMan.classList.toggle('active', !isAuto);
      if (btnAut) btnAut.classList.toggle('active', isAuto);

      const bText = document.getElementById('modeBannerText');
      const bDiv = document.getElementById('modeBanner');
      const lBadge = document.getElementById('locoStateBadge');

      if (isAuto) {
        if (bDiv) { bDiv.style.background = 'rgba(0,242,155,0.08)'; bDiv.style.borderColor = 'rgba(0,242,155,0.3)'; }
        if (bText) bText.innerHTML = '🤖 <b>Autonomous Mode Active:</b> Locomotive manages speed and station stops independently. Stations coordinate turnouts and anti-collision via direct ESP-NOW.';
        if (lBadge) {
          lBadge.textContent = '🤖 AUTONOMOUS';
          lBadge.style.background = 'rgba(0,242,155,0.2)';
          lBadge.style.color = 'var(--success)';
        }
      } else {
        if (bDiv) { bDiv.style.background = 'rgba(0,210,255,0.08)'; bDiv.style.borderColor = 'rgba(0,210,255,0.25)'; }
        if (bText) bText.innerHTML = '🕹️ <b>Manual Mode Active:</b> Direct throttle control via sliders. Master sends commands and failsafe stops motor on signal loss.';
        if (lBadge) {
          lBadge.textContent = '🕹️ MANUAL';
          lBadge.style.background = 'rgba(0,210,255,0.2)';
          lBadge.style.color = 'var(--primary)';
        }
      }
    }

    // Navigation
    function switchNav(tabId) {
      document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
      const activeBtn = Array.from(document.querySelectorAll('.nav-btn')).find(b => b.getAttribute('onclick').includes(tabId));
      if (activeBtn) activeBtn.classList.add('active');
      const target = document.getElementById(tabId);
      if (target) target.classList.add('active');
      if (tabId === 'tabConfig') loadConfig();
    }

    function switchConfigSub(subId) {
      document.querySelectorAll('.subtab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.subtab-pane').forEach(p => p.classList.remove('active'));
      const btn = document.getElementById('btn' + subId.charAt(0).toUpperCase() + subId.slice(1));
      if (btn) btn.classList.add('active');
      const pane = document.getElementById(subId);
      if (pane) pane.classList.add('active');
    }

    // WebSocket
    function initWs() {
      const url = (location.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + location.host + '/ws';
      ws = new WebSocket(url);
      ws.onopen = () => {
        document.getElementById('wsBadge').textContent = 'ONLINE';
        document.getElementById('wsBadge').classList.remove('offline');
      };
      ws.onclose = () => {
        document.getElementById('wsBadge').textContent = 'OFFLINE';
        document.getElementById('wsBadge').classList.add('offline');
        setTimeout(initWs, 3000);
      };
      ws.onmessage = (e) => {
        try {
          const msg = JSON.parse(e.data);
          if (msg.event === 'node_update') loadNodes();
          if (msg.event === 'loco_telemetry') {
            const d = msg.data;
            const lBadge = document.getElementById('locoStateBadge');
            if (lBadge) {
              if (d.locoState === 1) {
                lBadge.textContent = '🚀 LEARNING';
                lBadge.style.background = 'rgba(255,183,3,0.2)';
                lBadge.style.color = 'var(--warning)';
              } else if (d.locoState === 2) {
                lBadge.textContent = '🤖 AUTONOMOUS';
                lBadge.style.background = 'rgba(0,242,155,0.2)';
                lBadge.style.color = 'var(--success)';
              } else {
                lBadge.textContent = '🕹️ MANUAL';
                lBadge.style.background = 'rgba(0,210,255,0.2)';
                lBadge.style.color = 'var(--primary)';
              }
            }
          }
        } catch(err) {}
      };
    }

    function sendCmd(obj) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(obj));
      } else {
        fetch('/api/control/' + (obj.cmd && obj.cmd.includes('loco') ? 'loco' : 'track'), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(obj)
        }).catch(() => {});
      }
    }

    // Traction
    function getSelectedTarget() {
      const el = document.getElementById('targetSelect');
      return el ? el.value : 'ALL';
    }
    function onTargetChanged() {
      document.getElementById('spdSlider').value = 0;
      document.getElementById('spdText').textContent = '0%';
      const tgt = getSelectedTarget();
      const btn = document.getElementById('btnUnpairActiveLoco');
      if (btn) btn.style.display = (tgt !== 'ALL') ? 'inline-block' : 'none';
    }
    function unpairActiveLoco() {
      const tgt = getSelectedTarget();
      if (tgt !== 'ALL') unpairLoco(tgt);
    }
    function setSpeed(v) {
      if (currentMode === 'AUTONOMOUS') {
        currentMode = 'MANUAL';
        updateModeUI();
      }
      document.getElementById('spdText').textContent = v + '%';
      clearTimeout(timer);
      timer = setTimeout(() => {
        sendCmd({ cmd: 'loco_throttle', target: getSelectedTarget(), speed: parseInt(v), brake: (parseInt(v) === 0 ? 1 : 0) });
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
      sendCmd({ cmd: 'loco_throttle', target: getSelectedTarget(), lightMode: m });
    }
    function setSwitch(target, swIdx, p) {
      if (typeof target === 'number') {
        p = target;
        target = 'ALL';
        swIdx = 0;
      }
      sendCmd({ cmd: 'track_switch', target: target || 'ALL', switchIndex: (swIdx !== undefined ? swIdx : 0), position: p });
      const notice = document.getElementById('trackStateNotice');
      if (notice) notice.textContent = `Command sent to switch #${(swIdx || 0) + 1} (${target}): ${p === 0 ? 'STRAIGHT' : 'TURNOUT'}`;
    }

    function renderManualTrackSwitches() {
      const container = document.getElementById('manualSwitchesContainer');
      if (!container) return;
      const stations = systemConfig.stations || [];
      if (stations.length === 0) {
        container.innerHTML = '<span class="notice">No stations detected. Turn on a station node or configure in the Settings tab.</span>';
        return;
      }
      container.innerHTML = stations.map(st => {
        const sws = (st.switches && st.switches.length > 0) ? st.switches : [{ switchId: 1, gpioPin: 18, defaultPosition: 'STRAIGHT', description: 'Main Turnout' }];
        return `
          <div style="background:var(--card-alt);border:2px solid var(--border);border-radius:0;padding:10px 12px;margin-top:8px;">
            <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;">
              <b>🚉 ${st.name || st.nodeId}</b> <code style="font-size:0.75rem;color:var(--primary);">${st.nodeId}</code>
            </div>
            ${sws.map((sw, swIdx) => `
              <div style="background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);border-radius:0;padding:8px 10px;margin-top:6px;display:flex;justify-content:space-between;align-items:center;">
                <div>
                  <b style="color:var(--text);">Switch #${sw.switchId}</b>
                  <span class="status-badge" style="margin-left:6px;font-size:0.7rem;">GPIO ${sw.gpioPin}</span>
                  <span style="font-size:0.8rem;color:#8a99b5;margin-left:8px;">${sw.description || ''}</span>
                </div>
                <div style="display:flex;gap:6px;">
                  <button class="btn btn-sm" onclick="setSwitch('${st.nodeId}', ${swIdx}, 0)">➡️ Straight</button>
                  <button class="btn primary btn-sm" onclick="setSwitch('${st.nodeId}', ${swIdx}, 1)">🔀 Turnout</button>
                </div>
              </div>
            `).join('')}
          </div>
        `;
      }).join('');
    }

    // Scan & Pair
    async function scanForDevices() {
      const btn = document.getElementById('btnScan');
      if (btn) { btn.disabled = true; btn.textContent = '⏳ Scanning...'; }
      sendCmd({ cmd: 'scan_locos' });
      try { await fetch('/api/locos/scan', { method: 'POST' }); } catch(e){}
      setTimeout(loadNodes, 500);
      setTimeout(loadNodes, 1500);
      setTimeout(() => {
        if (btn) { btn.disabled = false; btn.textContent = '🔍 Scan'; }
      }, 3000);
    }

    async function pairNode(id) {
      try {
        const res = await fetch('/api/locos/pair', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ nodeId: id })
        });
        if (res.ok) {
          alert('Node ' + id + ' paired successfully!');
          loadNodes();
        }
      } catch(e) {
        alert('Failed to pair node: ' + e.message);
      }
    }

    async function unpairLoco(id) {
      if (!confirm('Are you sure you want to remove and unpair locomotive ' + id + '?\n\nThe locomotive will become unbonded and ready for re-pairing immediately without reflashing.')) return;
      try {
        const res = await fetch('/api/locos/unpair', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ nodeId: id })
        });
        if (res.ok) {
          alert('Locomotive ' + id + ' unpaired successfully!');
          loadNodes();
          loadConfig();
        } else {
          alert('Failed to unpair locomotive.');
        }
      } catch(e) {
        alert('Error unpairing: ' + e.message);
      }
    }

    // Autonomous
    async function startLearningLap(targetLoco) {
      const tgt = targetLoco || getSelectedTarget();
      const st = document.getElementById('learningStatus');
      if (st) st.textContent = '🚀 Starting learning lap for ' + tgt + '...';
      try {
        await fetch('/api/learning/start', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ targetLocoId: tgt, calibrationSpeed: 35 })
        });
        if (st) st.textContent = '🏁 Learning lap in progress (calibration at 35%)...';
      } catch(e) {
        if (st) st.textContent = '❌ Error: ' + e.message;
      }
    }

    async function resetLearningLap(targetLoco) {
      const tgt = targetLoco || getSelectedTarget();
      const st = document.getElementById('learningStatus');
      try {
        await fetch('/api/learning/reset', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ targetLocoId: tgt })
        });
        if (st) st.textContent = '🔄 Calibration reset for ' + tgt + '.';
      } catch(e) {}
    }

    // Nodes List
    async function loadNodes() {
      try {
        const res = await fetch('/api/nodes');
        const nodes = await res.json();
        discoveredNodes = nodes;
        
        // Select options
        const sel = document.getElementById('targetSelect');
        const curr = sel.value;
        sel.innerHTML = '<option value="ALL">📢 All Locomotives (Broadcast)</option>';
        nodes.filter(n => n.nodeType === 'LOCO' && n.isPaired).forEach(l => {
          const opt = document.createElement('option');
          opt.value = l.nodeId;
          opt.textContent = '🚂 ' + (l.friendlyName || l.nodeId) + ' [🔒 Bonded]' + (l.isOnline ? ' [Online]' : ' [Offline]');
          sel.appendChild(opt);
        });
        sel.value = curr;

        // Unpaired banner
        const unpList = document.getElementById('unpairedList');
        const unpaired = nodes.filter(n => !n.isPaired && !n.isBondedOther);
        if (unpaired.length > 0) {
          unpList.innerHTML = unpaired.map(u => `
            <div style="background:rgba(254,209,0,0.1);border:1.5px solid rgba(254,209,0,0.3);padding:8px 12px;border-radius:0;margin-top:6px;display:flex;justify-content:space-between;align-items:center;">
              <div><b>${u.nodeType === 'LOCO' ? '🚂' : '🚉'} ${u.nodeId}</b> <span style="font-size:0.75rem;color:#8a99b5;">(${u.friendlyName || 'Ready Device'})</span></div>
              <button class="btn primary btn-sm" onclick="pairNode('${u.nodeId}')">🔗 Pair</button>
            </div>
          `).join('');
        } else {
          unpList.innerHTML = '<span class="notice">All detected nodes are currently paired.</span>';
        }

        // Fleet table
        const tbody = document.getElementById('nodesTableBody');
        if (tbody) {
          tbody.innerHTML = nodes.map(n => `
            <tr>
              <td><code>${n.nodeId}</code></td>
              <td><b>${n.friendlyName || '--'}</b></td>
              <td><span class="status-badge">${n.nodeType}</span></td>
              <td>${n.rssi || -50} dBm</td>
              <td><span class="status-badge ${n.isOnline ? '' : 'offline'}">${n.isOnline ? 'ONLINE' : 'OFFLINE'}</span></td>
              <td>${n.isPaired ? `<span>🔒 Paired</span> <button class="btn danger btn-sm" style="margin-left:6px;padding:2px 8px;font-size:0.75rem;" onclick="unpairLoco('${n.nodeId}')">🗑️ Unpair</button>` : `<button class="btn primary btn-sm" onclick="pairNode('${n.nodeId}')">Pair</button>`}</td>
            </tr>
          `).join('');
        }
      } catch(e) {}
    }

    // Config Management (Unified API)
    async function loadConfig() {
      try {
        const res = await fetch('/api/config');
        if (res.ok) {
          systemConfig = await res.json();
        }
      } catch(e) {}

      // 1. Render Global
      const sys = systemConfig.system || {};
      document.getElementById('cfgLayout').value = sys.layoutName || 'Lego Central Layout';
      document.getElementById('cfgSsid').value = sys.wifiSsid || 'LegoTrain_Master';
      document.getElementById('cfgPass').value = sys.wifiPassword || '';
      document.getElementById('cfgChannel').value = sys.wifiChannel || 1;
      document.getElementById('cfgSafeSec').value = sys.headwaySafeSec || 12;
      document.getElementById('cfgCautionSec').value = sys.headwayCautionSec || 6;
      document.getElementById('cfgTrimPct').value = sys.headwaySpeedTrimPct || 40;

      // 2. Render Locomotives
      renderLocosConfig();

      // 3. Render Stations & Beacons
      renderStationsConfig();
    }

    function renderLocosConfig() {
      const container = document.getElementById('locoCardsList');
      if (!container) return;

      const locos = [...(systemConfig.locomotives || [])];
      discoveredNodes.filter(n => n.nodeType === 'LOCO').forEach(d => {
        if (!locos.some(l => l.nodeId === d.nodeId)) {
          locos.push({
            nodeId: d.nodeId,
            name: d.friendlyName || d.nodeId,
            maxSpeed: 70,
            learningSpeed: 35,
            accelRate: 40.0,
            decelRate: 60.0,
            brakeOffsetMs: 450,
            dwellTimeSec: 12,
            measuredLengthCm: 28
          });
        }
      });

      if (locos.length === 0) {
        container.innerHTML = '<p class="notice" style="padding:10px;">No locomotives configured or detected. Power on locomotive and click Scan.</p>';
        return;
      }

      container.innerHTML = locos.map((l, idx) => `
        <div class="card" style="margin-top:10px;background:var(--card-alt);">
          <div class="card-title">
            <span>🚂 ${l.name || l.nodeId} <code style="font-size:0.75rem;color:var(--primary);">${l.nodeId}</code></span>
            <div>
              <button class="btn success btn-sm" onclick="startLearningLap('${l.nodeId}')">🚀 Learning Lap</button>
              <button class="btn danger btn-sm" style="margin-left:6px;" onclick="unpairLoco('${l.nodeId}')">🗑️ Remove / Unpair</button>
            </div>
          </div>
          <div class="grid-2">
            <div class="form-group">
              <label>Locomotive Name:</label>
              <input type="text" class="form-control" id="locoName_${idx}" value="${l.name || ''}">
            </div>
            <div class="form-group">
              <label>Maximum Speed (%):</label>
              <input type="number" class="form-control" id="locoMax_${idx}" min="20" max="100" value="${l.maxSpeed || 70}">
            </div>
            <div class="form-group">
              <label>Learning Speed (%):</label>
              <input type="number" class="form-control" id="locoLearn_${idx}" min="20" max="60" value="${l.learningSpeed || 35}">
              <span class="field-hint">Standardized speed for track discovery and length measurement.</span>
            </div>
            <div class="form-group">
              <label>Acceleration Rate (%/s):</label>
              <input type="number" class="form-control" id="locoAcc_${idx}" min="10" max="100" value="${l.accelRate || 40}">
            </div>
            <div class="form-group">
              <label>Deceleration Rate (%/s):</label>
              <input type="number" class="form-control" id="locoDec_${idx}" min="10" max="150" value="${l.decelRate || 60}">
            </div>
            <div class="form-group">
              <label>Smooth Braking Offset (ms):</label>
              <input type="number" class="form-control" id="locoBrake_${idx}" min="0" max="2000" value="${l.brakeOffsetMs || 450}">
            </div>
            <div class="form-group">
              <label>Station Dwell Time (s):</label>
              <input type="number" class="form-control" id="locoDwell_${idx}" min="2" max="60" value="${l.dwellTimeSec || 12}">
            </div>
            <div class="form-group">
              <label>Measured Train Length (cm):</label>
              <input type="number" class="form-control" id="locoLen_${idx}" min="10" max="250" value="${l.measuredLengthCm || 28}">
              <span class="field-hint">Automatically measured by optical sensors or adjusted manually.</span>
            </div>
          </div>
          <button class="btn primary btn-sm" style="margin-top:8px;width:100%;" onclick="saveLocoConfig(${idx}, '${l.nodeId}')">💾 Save Locomotive Settings</button>
        </div>
      `).join('');
    }

    function renderStationsConfig() {
      const container = document.getElementById('stationCardsList');
      if (!container) return;

      const stations = [...(systemConfig.stations || [])];
      discoveredNodes.filter(n => n.nodeType === 'TRACK').forEach(d => {
        if (!stations.some(s => s.nodeId === d.nodeId)) {
          stations.push({
            nodeId: d.nodeId,
            name: d.friendlyName || d.nodeId,
            dwellTimeSec: 10,
            autoDivertOnOccupied: true,
            sidingCapacityCm: 65,
            switches: [
              { switchId: 1, gpioPin: 18, servoStraightAngle: 75, servoTurnoutAngle: 105, defaultPosition: 'STRAIGHT', description: 'Main Turnout' }
            ],
            beacons: [
              { beaconId: 10, gpioPin: 19, role: 'ROLE_LOCATOR', description: 'Entry / Locator', measureTrainLength: true },
              { beaconId: 11, gpioPin: 19, role: 'ROLE_STATION_ARRIVAL', description: 'Platform (Stop)', measureTrainLength: true }
            ]
          });
        }
      });

      if (stations.length === 0) {
        container.innerHTML = '<p class="notice" style="padding:10px;">No stations configured or detected. Power on track/station module and click Scan.</p>';
        return;
      }

      container.innerHTML = stations.map((s, sIdx) => {
        const swList = (s.switches && s.switches.length > 0) ? s.switches : [
          { switchId: 1, gpioPin: s.switchGpioPin || 18, servoStraightAngle: s.servoStraightAngle || 75, servoTurnoutAngle: s.servoTurnoutAngle || 105, defaultPosition: s.defaultSwitch || 'STRAIGHT', description: 'Main Turnout' }
        ];
        const bList = s.beacons || [];
        return `
          <div class="card" style="margin-top:10px;background:var(--card-alt);">
            <div class="card-title">
              <span>🚉 ${s.name || s.nodeId} <code style="font-size:0.75rem;color:var(--primary);">${s.nodeId}</code></span>
            </div>
            <div class="grid-2">
              <div class="form-group">
                <label>Station Name:</label>
                <input type="text" class="form-control" id="stName_${sIdx}" value="${s.name || ''}">
              </div>
              <div class="form-group">
                <label>Station Dwell Time (s):</label>
                <input type="number" class="form-control" id="stDwell_${sIdx}" min="0" max="120" value="${s.dwellTimeSec || 10}">
              </div>
              <div class="form-group">
                <label>Siding Capacity (cm):</label>
                <input type="number" class="form-control" id="stSiding_${sIdx}" min="20" max="250" value="${s.sidingCapacityCm || 65}">
                <span class="field-hint">Trains longer than this limit will never be routed into the siding.</span>
              </div>
              <div class="form-group" style="display:flex;align-items:flex-end;">
                <label style="display:flex;align-items:center;gap:8px;cursor:pointer;padding-bottom:10px;">
                  <input type="checkbox" id="stAutoDivert_${sIdx}" ${s.autoDivertOnOccupied ? 'checked' : ''}>
                  <span><b>Auto Divert on Occupied Platform:</b> Routes train to siding if main platform is occupied.</span>
                </label>
              </div>
            </div>

            <!-- Multi-Switches Manager -->
            <div style="margin-top:14px;border-top:1px solid var(--border);padding-top:12px;">
              <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
                <span style="font-weight:700;font-size:0.9rem;">🔀 Station Turnout Switches (${swList.length})</span>
                <button class="btn btn-sm" onclick="addSwitch(${sIdx})">+ Add Switch</button>
              </div>
              <p class="field-hint" style="margin-bottom:8px;">
                Each switch is driven by a servo motor connected to a <b>dedicated GPIO</b> with individual angle calibration.
              </p>

              <div class="table-box">
                <table>
                  <thead>
                    <tr>
                      <th style="width:55px;">ID</th>
                      <th style="width:65px;">GPIO</th>
                      <th style="width:110px;">Default Pos</th>
                      <th style="width:75px;">Str. Angle</th>
                      <th style="width:75px;">Turn. Angle</th>
                      <th>Description</th>
                      <th style="width:105px;text-align:center;">Test</th>
                      <th style="width:40px;"></th>
                    </tr>
                  </thead>
                  <tbody>
                    ${swList.map((sw, swIdx) => `
                      <tr>
                        <td>
                          <input type="number" class="input-sm" id="swId_${sIdx}_${swIdx}" value="${sw.switchId || (swIdx+1)}" min="1" max="16" style="width:50px;">
                        </td>
                        <td>
                          <input type="number" class="input-sm" id="swGpio_${sIdx}_${swIdx}" value="${sw.gpioPin !== undefined ? sw.gpioPin : 18}" min="0" max="48" style="width:58px;" title="Servo GPIO">
                        </td>
                        <td>
                          <select class="input-sm" id="swDefPos_${sIdx}_${swIdx}">
                            <option value="STRAIGHT" ${sw.defaultPosition === 'STRAIGHT' ? 'selected' : ''}>STRAIGHT</option>
                            <option value="TURNOUT" ${sw.defaultPosition === 'TURNOUT' ? 'selected' : ''}>TURNOUT</option>
                          </select>
                        </td>
                        <td>
                          <input type="number" class="input-sm" id="swAngStr_${sIdx}_${swIdx}" value="${sw.servoStraightAngle || 75}" min="0" max="180" style="width:65px;" title="Straight Angle (°)">
                        </td>
                        <td>
                          <input type="number" class="input-sm" id="swAngTur_${sIdx}_${swIdx}" value="${sw.servoTurnoutAngle || 105}" min="0" max="180" style="width:65px;" title="Turnout Angle (°)">
                        </td>
                        <td>
                          <input type="text" class="input-sm" id="swDesc_${sIdx}_${swIdx}" value="${sw.description || ''}" placeholder="e.g. Entry Turnout">
                        </td>
                        <td style="text-align:center;white-space:nowrap;">
                          <button class="btn btn-sm" style="padding:2px 6px;" title="Test Straight" onclick="setSwitch('${s.nodeId}', ${swIdx}, 0)">➡️</button>
                          <button class="btn primary btn-sm" style="padding:2px 6px;margin-left:4px;" title="Test Turnout" onclick="setSwitch('${s.nodeId}', ${swIdx}, 1)">🔀</button>
                        </td>
                        <td>
                          <button class="btn stop btn-sm" style="padding:2px 6px;" onclick="removeSwitch(${sIdx}, ${swIdx})">✕</button>
                        </td>
                      </tr>
                    `).join('')}
                  </tbody>
                </table>
              </div>
            </div>

            <!-- Multi-Beacons Manager -->
            <div style="margin-top:14px;border-top:1px solid var(--border);padding-top:12px;">
              <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
                <span style="font-weight:700;font-size:0.9rem;">📍 Station Beacons &amp; Locators (${bList.length})</span>
                <button class="btn btn-sm" onclick="addBeacon(${sIdx})">+ Add Beacon</button>
              </div>
              <p class="field-hint" style="margin-bottom:8px;">
                Rule: Exactly <b>1 Station Arrival beacon</b> (triggers station stop and dwell). Remaining beacons should be configured as <b>Locators</b> for tracking and length calculation.
              </p>

              <div class="table-box">
                <table>
                  <thead>
                    <tr>
                      <th style="width:60px;">ID</th>
                      <th style="width:65px;">GPIO</th>
                      <th style="width:180px;">Role / Function</th>
                      <th>Description / Sector</th>
                      <th style="width:80px;text-align:center;">Measure Len</th>
                      <th style="width:40px;"></th>
                    </tr>
                  </thead>
                  <tbody>
                    ${bList.map((b, bIdx) => `
                      <tr>
                        <td>
                          <input type="number" class="input-sm" id="bId_${sIdx}_${bIdx}" value="${b.beaconId}" style="width:55px;">
                        </td>
                        <td>
                          <input type="number" class="input-sm" id="bGpio_${sIdx}_${bIdx}" value="${b.gpioPin !== undefined ? b.gpioPin : 19}" min="0" max="48" style="width:58px;" title="IR Sensor/Emitter GPIO">
                        </td>
                        <td>
                          <select class="input-sm" id="bRole_${sIdx}_${bIdx}">
                            <option value="ROLE_STATION_ARRIVAL" ${b.role === 'ROLE_STATION_ARRIVAL' || b.role === 1 ? 'selected' : ''}>🚉 Station Arrival (Stop)</option>
                            <option value="ROLE_LOCATOR" ${b.role === 'ROLE_LOCATOR' || b.role === 0 ? 'selected' : ''}>📍 Locator (Position only)</option>
                            <option value="ROLE_APPROACH" ${b.role === 'ROLE_APPROACH' || b.role === 2 ? 'selected' : ''}>⚠️ Approach Warning</option>
                            <option value="ROLE_SIDING" ${b.role === 'ROLE_SIDING' || b.role === 4 ? 'selected' : ''}>🔀 Siding / Refuge</option>
                          </select>
                        </td>
                        <td>
                          <input type="text" class="input-sm" id="bDesc_${sIdx}_${bIdx}" value="${b.description || ''}" placeholder="e.g. Entry Sector 1">
                        </td>
                        <td style="text-align:center;">
                          <input type="checkbox" id="bLen_${sIdx}_${bIdx}" ${b.measureTrainLength !== false ? 'checked' : ''} title="Measure train length with optical sensor">
                        </td>
                        <td>
                          <button class="btn stop btn-sm" style="padding:2px 6px;" onclick="removeBeacon(${sIdx}, ${bIdx})">✕</button>
                        </td>
                      </tr>
                    `).join('')}
                  </tbody>
                </table>
              </div>
            </div>

            <button class="btn primary btn-sm" style="margin-top:10px;width:100%;" onclick="saveStationConfig(${sIdx}, '${s.nodeId}')">💾 Save Station Settings</button>
          </div>
        `;
      }).join('');
    }

    function addSwitch(sIdx) {
      if (!systemConfig.stations) systemConfig.stations = [];
      if (!systemConfig.stations[sIdx]) return;
      if (!systemConfig.stations[sIdx].switches) systemConfig.stations[sIdx].switches = [];
      const swList = systemConfig.stations[sIdx].switches;
      const nextId = swList.length > 0 ? Math.max(...swList.map(s => s.switchId)) + 1 : 1;
      swList.push({
        switchId: nextId,
        gpioPin: (nextId === 1 ? 18 : 18 + nextId),
        servoStraightAngle: 75,
        servoTurnoutAngle: 105,
        defaultPosition: 'STRAIGHT',
        description: 'Switch #' + nextId
      });
      renderStationsConfig();
      renderManualTrackSwitches();
    }

    function removeSwitch(sIdx, swIdx) {
      if (!systemConfig.stations || !systemConfig.stations[sIdx] || !systemConfig.stations[sIdx].switches) return;
      systemConfig.stations[sIdx].switches.splice(swIdx, 1);
      renderStationsConfig();
      renderManualTrackSwitches();
    }

    function addBeacon(sIdx) {
      if (!systemConfig.stations) systemConfig.stations = [];
      if (!systemConfig.stations[sIdx]) return;
      if (!systemConfig.stations[sIdx].beacons) systemConfig.stations[sIdx].beacons = [];
      const bList = systemConfig.stations[sIdx].beacons;
      const nextId = bList.length > 0 ? Math.max(...bList.map(b => b.beaconId)) + 1 : 10;
      bList.push({
        beaconId: nextId,
        gpioPin: 19,
        role: 'ROLE_LOCATOR',
        description: 'Sector Locator #' + nextId,
        measureTrainLength: true
      });
      renderStationsConfig();
    }

    function removeBeacon(sIdx, bIdx) {
      if (!systemConfig.stations || !systemConfig.stations[sIdx] || !systemConfig.stations[sIdx].beacons) return;
      systemConfig.stations[sIdx].beacons.splice(bIdx, 1);
      renderStationsConfig();
    }

    // Save Handlers
    async function saveGlobalConfig() {
      if (!systemConfig.system) systemConfig.system = {};
      systemConfig.system.layoutName = document.getElementById('cfgLayout').value.trim();
      systemConfig.system.wifiSsid = document.getElementById('cfgSsid').value.trim();
      systemConfig.system.wifiPassword = document.getElementById('cfgPass').value;
      systemConfig.system.wifiChannel = parseInt(document.getElementById('cfgChannel').value) || 1;
      systemConfig.system.headwaySafeSec = parseInt(document.getElementById('cfgSafeSec').value) || 12;
      systemConfig.system.headwayCautionSec = parseInt(document.getElementById('cfgCautionSec').value) || 6;
      systemConfig.system.headwaySpeedTrimPct = parseInt(document.getElementById('cfgTrimPct').value) || 40;

      await postConfig();
      alert('Global configuration saved to Master Gateway!');
    }

    async function saveLocoConfig(idx, nodeId) {
      if (!systemConfig.locomotives) systemConfig.locomotives = [];
      let item = systemConfig.locomotives.find(l => l.nodeId === nodeId);
      if (!item) { item = { nodeId: nodeId }; systemConfig.locomotives.push(item); }

      item.name = document.getElementById('locoName_' + idx).value.trim();
      item.maxSpeed = parseInt(document.getElementById('locoMax_' + idx).value) || 70;
      item.learningSpeed = parseInt(document.getElementById('locoLearn_' + idx).value) || 35;
      item.accelRate = parseFloat(document.getElementById('locoAcc_' + idx).value) || 40.0;
      item.decelRate = parseFloat(document.getElementById('locoDec_' + idx).value) || 60.0;
      item.brakeOffsetMs = parseInt(document.getElementById('locoBrake_' + idx).value) || 450;
      item.dwellTimeSec = parseInt(document.getElementById('locoDwell_' + idx).value) || 12;
      item.measuredLengthCm = parseInt(document.getElementById('locoLen_' + idx).value) || 28;

      await postConfig();
      alert('Locomotive parameters for ' + nodeId + ' saved!');
    }

    async function saveStationConfig(sIdx, nodeId) {
      if (!systemConfig.stations) systemConfig.stations = [];
      let item = systemConfig.stations.find(s => s.nodeId === nodeId);
      if (!item) { item = { nodeId: nodeId }; systemConfig.stations.push(item); }

      item.name = document.getElementById('stName_' + sIdx).value.trim();
      item.dwellTimeSec = parseInt(document.getElementById('stDwell_' + sIdx).value) || 10;
      item.sidingCapacityCm = parseInt(document.getElementById('stSiding_' + sIdx).value) || 65;
      item.autoDivertOnOccupied = document.getElementById('stAutoDivert_' + sIdx).checked;

      // Read switches
      const swRows = document.querySelectorAll(`[id^="swId_${sIdx}_"]`);
      const updatedSwitches = [];
      swRows.forEach((r, swIdx) => {
        const idVal = parseInt(document.getElementById(`swId_${sIdx}_${swIdx}`).value) || (swIdx + 1);
        const gpioVal = parseInt(document.getElementById(`swGpio_${sIdx}_${swIdx}`).value) || 18;
        const defPos = document.getElementById(`swDefPos_${sIdx}_${swIdx}`).value || 'STRAIGHT';
        const angStr = parseInt(document.getElementById(`swAngStr_${sIdx}_${swIdx}`).value) || 75;
        const angTur = parseInt(document.getElementById(`swAngTur_${sIdx}_${swIdx}`).value) || 105;
        const descEl = document.getElementById(`swDesc_${sIdx}_${swIdx}`);
        const descVal = descEl ? descEl.value.trim() : '';
        updatedSwitches.push({
          switchId: idVal,
          gpioPin: gpioVal,
          servoStraightAngle: angStr,
          servoTurnoutAngle: angTur,
          defaultPosition: defPos,
          description: descVal
        });
      });
      item.switches = updatedSwitches;
      if (updatedSwitches.length > 0) {
        item.defaultSwitch = updatedSwitches[0].defaultPosition;
        item.switchGpioPin = updatedSwitches[0].gpioPin;
        item.servoStraightAngle = updatedSwitches[0].servoStraightAngle;
        item.servoTurnoutAngle = updatedSwitches[0].servoTurnoutAngle;
      }

      // Read beacons
      const rows = document.querySelectorAll(`[id^="bId_${sIdx}_"]`);
      const updatedBeacons = [];
      let arrivalCount = 0;
      rows.forEach((r, bIdx) => {
        const role = document.getElementById(`bRole_${sIdx}_${bIdx}`).value;
        if (role === 'ROLE_STATION_ARRIVAL') arrivalCount++;
        const bGpio = parseInt(document.getElementById(`bGpio_${sIdx}_${bIdx}`).value) || 19;
        updatedBeacons.push({
          beaconId: parseInt(document.getElementById(`bId_${sIdx}_${bIdx}`).value) || (10 + bIdx),
          gpioPin: bGpio,
          role: role,
          description: document.getElementById(`bDesc_${sIdx}_${bIdx}`).value.trim(),
          measureTrainLength: document.getElementById(`bLen_${sIdx}_${bIdx}`).checked
        });
      });

      if (arrivalCount > 1) {
        alert('Warning: Exactly ONE beacon per station should be ROLE_STATION_ARRIVAL (Arrival/Stop). Remaining beacons should be Locators.');
      }

      item.beacons = updatedBeacons;
      item.beaconCount = updatedBeacons.length;

      await postConfig();
      renderManualTrackSwitches();
      alert('Settings saved: ' + updatedSwitches.length + ' switch(es) and ' + updatedBeacons.length + ' beacon(s) saved for station!');
    }

    async function postConfig() {
      try {
        await fetch('/api/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(systemConfig)
        });
      } catch(e) {
        alert('Error sending configuration: ' + e.message);
      }
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
        doc["mode"] = (_currentMode == MODE_AUTONOMOUS) ? "AUTONOMOUS" : "MANUAL";
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
            o["nodeId"]        = n.nodeId;
            o["friendlyName"]  = ConfigStore::instance().getFriendlyName(n.nodeId);
            o["nodeType"]      = (n.nodeType == NODE_TYPE_LOCO) ? "LOCO" : (n.nodeType == NODE_TYPE_TRACK ? "TRACK" : "MASTER");
            o["isOnline"]      = n.isOnline;
            o["isPaired"]      = n.isPaired;
            o["isBondedOther"] = n.isBondedOther;
            o["rssi"]          = n.rssi;
            o["speed"]         = n.speed;
            o["currentBlock"]  = n.currentBlock;
            o["switchState"]   = (n.switchPosition == SWITCH_STRAIGHT) ? "STRAIGHT" : "TURNOUT";
            o["beamOccupied"]  = (bool)n.beamOccupied;
            o["batteryMv"]     = n.batteryMv;
            o["lastSeenSec"]   = (millis() - n.lastSeenMs) / 1000;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Scan for Locomotives (Broadcast Discovery Scan)
    _server.on("/api/locos/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
        ESPNowManager::instance().sendDiscoveryScan();
        request->send(200, "application/json", "{\"status\":\"scanning\"}");
    });

    // API: Pair Locomotive to this Master
    _server.on("/api/locos/pair", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeJson(doc, (char*)data)) {
            String nodeId = doc["nodeId"] | "";
            if (nodeId.length() > 0) {
                bool ok = ESPNowManager::instance().pairNode(nodeId.c_str());
                if (ok) {
                    if (doc["friendlyName"].is<const char*>()) {
                        String name = doc["friendlyName"].as<String>();
                        if (name.length() > 0) {
                            ConfigStore::instance().setFriendlyName(nodeId, name);
                        }
                    }
                    request->send(200, "application/json", "{\"status\":\"paired\",\"nodeId\":\"" + nodeId + "\"}");
                    return;
                }
            }
        }
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Failed to pair node\"}");
    });

    // API: Unpair / Remove Locomotive from this Master
    _server.on("/api/locos/unpair", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeJson(doc, (char*)data)) {
            String nodeId = doc["nodeId"] | "";
            if (nodeId.length() > 0) {
                bool ok = ESPNowManager::instance().unpairNode(nodeId.c_str());
                ConfigStore::instance().removeLoco(nodeId);
                if (_onLocoControlCb) {
                    _onLocoControlCb(nodeId, 0, 2, 0, 0, 0, 0); // Halt & clear
                }
                request->send(200, "application/json", "{\"status\":\"unpaired\",\"nodeId\":\"" + nodeId + "\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Failed to unpair node\"}");
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

    // API: Unified Configuration (GET / POST)
    _server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = ConfigStore::instance().serializeUnifiedConfigJson();
        request->send(200, "application/json", json);
    });

    _server.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        String json = String((char*)data).substring(0, len);
        if (ConfigStore::instance().deserializeUnifiedConfigJson(json)) {
            if (_onConfigUpdatedCb) _onConfigUpdatedCb();
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
    });

    // API: Circuit Topology (GET / POST)
    _server.on("/api/topology", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = ConfigStore::instance().loadTopologyJson();
        request->send(200, "application/json", json);
    });

    _server.on("/api/topology", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        String json = String((char*)data).substring(0, len);
        ConfigStore::instance().saveTopologyJson(json);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // API: Learning Lap Start / Stop / Reset
    _server.on("/api/learning/start", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String loco = doc["target"] | (doc["targetLocoId"] | "ALL");
        uint8_t spd = doc["speed"] | (doc["calibrationSpeed"] | 35);
        if (_onLearningLapCb) _onLearningLapCb(loco, true, spd);
        request->send(200, "application/json", "{\"status\":\"started\"}");
    });

    _server.on("/api/learning/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String loco = doc["target"] | (doc["targetLocoId"] | "ALL");
        if (_onLearningLapCb) _onLearningLapCb(loco, false, 0);
        request->send(200, "application/json", "{\"status\":\"stopped\"}");
    });

    _server.on("/api/learning/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String loco = doc["target"] | (doc["targetLocoId"] | "ALL");
        if (_onLearningLapCb) _onLearningLapCb(loco, false, 0);
        request->send(200, "application/json", "{\"status\":\"reset\"}");
    });

    // API: Change Mode
    _server.on("/api/mode", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String mStr = doc["mode"] | "MANUAL";
        _currentMode = (mStr == "AUTONOMOUS" || mStr == "AUTOMATIC") ? MODE_AUTONOMOUS : MODE_MANUAL;
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
        uint8_t swIdx = doc["switchIndex"] | (doc["switchId"] | 0);
        if (_onTrackControlCb) _onTrackControlCb(target, pos, dwell, swIdx);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // API: Settings (Wi-Fi AP & Multi-layout configuration)
    _server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        const SystemSettings& s = ConfigStore::instance().getSettings();
        JsonDocument doc;
        doc["layoutName"]   = s.layoutName;
        doc["wifiSsid"]     = s.wifiSsid;
        doc["wifiPassword"] = s.wifiPassword;
        doc["apMode"]       = s.apMode;
        doc["wifiChannel"]  = s.wifiChannel;
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    _server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeJson(doc, (char*)data)) {
            SystemSettings s = ConfigStore::instance().getSettings();
            if (doc["wifiSsid"].is<const char*>()) s.wifiSsid = doc["wifiSsid"].as<String>();
            if (doc["wifiPassword"].is<const char*>()) s.wifiPassword = doc["wifiPassword"].as<String>();
            if (doc["apMode"].is<bool>()) s.apMode = doc["apMode"].as<bool>();
            if (doc["wifiChannel"].is<uint8_t>()) s.wifiChannel = doc["wifiChannel"].as<uint8_t>();

            if (s.wifiSsid.length() == 0) s.wifiSsid = "LegoTrain_Master";
            if (s.wifiChannel < 1 || s.wifiChannel > 13) s.wifiChannel = 1;

            ConfigStore::instance().setSettings(s);
            request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Settings saved\"}");
            return;
        }
        request->send(400, "application/json", "{\"status\":\"error\"}");
    });

    // API: System Restart
    _server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(400);
        ESP.restart();
    });

    // API: Emergency Stop
    _server.on("/api/emergency_stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (_onEmergencyStopCb) _onEmergencyStopCb();
        request->send(200, "application/json", "{\"status\":\"emergency_stop_triggered\"}");
    });

    // Captive Portal Probes & Redirections (Android, iOS, Windows, Mac)
    auto redirectToRoot = [](AsyncWebServerRequest *request) {
        request->redirect("http://192.168.4.1/");
    };
    _server.on("/generate_204", HTTP_GET, redirectToRoot);
    _server.on("/gen_204", HTTP_GET, redirectToRoot);
    _server.on("/hotspot-detect.html", HTTP_GET, redirectToRoot);
    _server.on("/canonical.html", HTTP_GET, redirectToRoot);
    _server.on("/connecttest.txt", HTTP_GET, redirectToRoot);
    _server.on("/ncsi.txt", HTTP_GET, redirectToRoot);

    _server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
            return;
        }
        request->redirect("http://192.168.4.1/");
    });

    _server.onNotFound([](AsyncWebServerRequest *request) {
        String host = request->host();
        if (host != "192.168.4.1" && host != "legoloco.local") {
            request->redirect("http://192.168.4.1/");
            return;
        }
        request->send(404, "text/plain", "Not Found");
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
