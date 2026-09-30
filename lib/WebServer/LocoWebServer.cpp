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
        } else if (strcmp(cmd, "scan_locos") == 0) {
            ESPNowManager::instance().sendDiscoveryScan();
        } else if (strcmp(cmd, "pair_loco") == 0) {
            String target = doc["target"] | "";
            if (target.length() > 0) {
                ESPNowManager::instance().pairNode(target.c_str());
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

        // Complete mobile-friendly embedded controller fallback
        const char* fallbackHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="pt">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <meta name="theme-color" content="#090d16">
  <title>Lego Loco Controller V2.0</title>
  <style>
    :root {
      --bg: #090d16; --card: #121826; --input: #0e1320;
      --primary: #00d2ff; --success: #00f29b; --danger: #ff3366; --warning: #ffb703; --text: #f0f4fc;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 12px; min-height: 100vh; padding-bottom: 40px; }
    .header { display: flex; justify-content: space-between; align-items: center; background: var(--card); padding: 12px; border-radius: 12px; margin-bottom: 12px; border: 1px solid rgba(255,255,255,0.08); }
    .header h1 { font-size: 1.1rem; font-weight: 800; }
    .header h1 span { color: var(--primary); }
    .status-badge { font-size: 0.72rem; padding: 4px 8px; border-radius: 12px; background: rgba(0,242,155,0.15); color: var(--success); font-weight: 700; }
    .status-badge.offline { background: rgba(255,51,102,0.15); color: var(--danger); }
    .estop-bar { margin-bottom: 12px; }
    .btn-estop { width: 100%; background: linear-gradient(135deg, #ff3366, #c9184a); border: none; color: #fff; padding: 14px; font-size: 1.1rem; font-weight: 800; border-radius: 12px; cursor: pointer; box-shadow: 0 4px 15px rgba(255,51,102,0.4); }
    .card { background: var(--card); border: 1px solid rgba(255,255,255,0.08); border-radius: 14px; padding: 16px; margin-bottom: 12px; }
    .card-title { font-size: 1rem; font-weight: 700; margin-bottom: 10px; display: flex; justify-content: space-between; align-items: center; }
    .throttle-box { margin: 14px 0; }
    .speed-label { display: flex; justify-content: space-between; font-size: 0.85rem; color: #8a99b5; margin-bottom: 6px; }
    .speed-val { font-size: 1.3rem; font-weight: 800; color: var(--primary); }
    .slider { width: 100%; height: 16px; border-radius: 8px; background: var(--input); -webkit-appearance: none; outline: none; }
    .slider::-webkit-slider-thumb { -webkit-appearance: none; width: 34px; height: 34px; border-radius: 50%; background: var(--primary); cursor: pointer; }
    .btn-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; margin: 12px 0; }
    .btn { background: #161e31; border: 1px solid rgba(255,255,255,0.1); color: #fff; padding: 12px 6px; font-weight: 700; font-size: 0.85rem; border-radius: 8px; cursor: pointer; text-align: center; }
    .btn.stop { background: rgba(255,51,102,0.15); color: var(--danger); border-color: rgba(255,51,102,0.4); }
    .btn.primary { background: linear-gradient(135deg, #00d2ff, #0077b6); color: #000; font-weight: 800; }
    .form-group { margin-bottom: 10px; }
    .form-group label { display: block; font-size: 0.8rem; color: #8a99b5; margin-bottom: 4px; font-weight: 600; }
    .form-control { width: 100%; background: var(--input); color: var(--text); border: 1px solid rgba(255,255,255,0.12); padding: 9px 12px; border-radius: 8px; font-size: 0.9rem; }
    .notice { font-size: 0.75rem; color: #8a99b5; margin-top: 5px; line-height: 1.4; }
    .notice-warn { background: rgba(255,183,3,0.12); color: var(--warning); border: 1px solid rgba(255,183,3,0.3); padding: 8px 10px; border-radius: 8px; font-size: 0.78rem; margin: 8px 0; }
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

  <!-- PAIRING & DISCOVERY FOR LOCOS & STATIONS -->
  <div class="card" style="border-left: 4px solid var(--primary);">
    <div class="card-title">
      <span>🚂 / 🚉 Pair Devices</span>
      <button class="btn primary" id="btnScan" style="padding:6px 12px;font-size:0.8rem;" onclick="scanForLocos()">🔍 Scan for Devices</button>
    </div>
    <p class="notice">
      <b>Setup Procedure:</b> Turn on locomotive or station (LED pulses or OLED displays <i>UNPAIRED</i>) &bull; Click <b>Scan for Devices</b> &bull; Click <b>Pair to this Master</b>.<br>
      🔒 <i>Once bonded, the node responds exclusively to this Master. To reset or move to another Master, re-flash firmware with Erase Flash.</i>
    </p>
    <div id="pairingStatus" class="notice" style="margin-top:6px;font-weight:600;"></div>
    <div id="unpairedLocosList"></div>
  </div>

  <!-- LOCOMOTIVE CONTROLLER -->
  <div class="card">
    <div class="card-title">
      <span>Traction Control</span>
      <span class="speed-val" id="spdText">0%</span>
    </div>

    <div class="form-group">
      <label>LOCOMOTIVE IN CONTROL:</label>
      <select id="targetSelect" class="form-control" onchange="onTargetChanged()">
        <option value="ALL">📢 ALL (Broadcast - All Locomotives)</option>
      </select>
      <div id="noLocoNotice" class="notice-warn">
        ⚠️ No individual locomotive bonded to this Master. Click <b>Scan for Devices</b> above to pair your locomotive.
      </div>
    </div>

    <div class="throttle-box">
      <div class="speed-label">
        <span>◀ Reverse (-100%)</span>
        <span>Forward (+100%) ▶</span>
      </div>
      <input type="range" class="slider" id="spdSlider" min="-100" max="100" value="0" oninput="setSpeed(this.value)">
    </div>

    <!-- Buttons in natural order: Left (REV), Center (STOP), Right (FWD) -->
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

  <!-- TRACK SWITCHES -->
  <div class="card">
    <div class="card-title"><span>Track Switches</span></div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;">
      <button class="btn" onclick="setSwitch(0)">STRAIGHT</button>
      <button class="btn" onclick="setSwitch(1)">TURNOUT</button>
    </div>
  </div>

  <!-- SCENARIO CSV MANAGER (IMPORT / EXPORT) -->
  <div class="card">
    <div class="card-title">
      <span>Automated Scenarios (CSV)</span>
      <span id="scRunningText" style="font-size:0.75rem;color:var(--primary);font-weight:700;">Manual</span>
    </div>
    <div style="display:flex;gap:8px;margin-bottom:10px;">
      <select id="scSelect" class="form-control" style="flex:1;"></select>
      <button id="btnRunSc" class="btn primary" style="padding:0 16px;" onclick="toggleScenarioRun()">▶ Start</button>
    </div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;">
      <button class="btn" onclick="exportScenarioCsv()">⬇️ Export .CSV</button>
      <button class="btn" onclick="document.getElementById('csvFileInput').click()">⬆️ Import .CSV</button>
      <input type="file" id="csvFileInput" accept=".csv" style="display:none;" onchange="importScenarioCsv(this)">
    </div>
    <div id="scMsg" class="notice"></div>
  </div>

  <!-- AP & NETWORK SETTINGS (MULTI-INSTALLATION ISOLATION) -->
  <div class="card">
    <div class="card-title">
      <span>⚙️ Access Point (AP) Settings</span>
      <button class="btn" style="padding:4px 10px;font-size:0.75rem;" onclick="toggleSettingsView()">Toggle Settings</button>
    </div>
    <div id="settingsPanel" style="display:none;margin-top:10px;">
      <p class="notice" style="margin-bottom:12px;">
        Configure Wi-Fi SSID and radio channel to isolate layouts and prevent interference when operating near other model train layouts.
      </p>

      <div class="form-group">
        <label>Wi-Fi SSID (Access Point Name):</label>
        <input type="text" id="cfgSsid" class="form-control" placeholder="LegoTrain_Master">
      </div>

      <div class="form-group">
        <label>Wi-Fi Password (Leave empty for open network):</label>
        <input type="text" id="cfgPass" class="form-control" placeholder="Min 8 characters or empty">
      </div>

      <div class="form-group">
        <label>Wi-Fi / ESP-NOW Channel (1 to 13):</label>
        <input type="number" id="cfgChannel" min="1" max="13" class="form-control" value="1">
        <span class="notice">Tip: Layout 1 on Channel 1, Layout 2 on Channel 6 or 11.</span>
      </div>

      <button class="btn primary" style="width:100%;margin-top:10px;padding:12px;" onclick="saveSettings()">
        💾 Save Settings & Restart AP
      </button>
      <div id="cfgMsg" class="notice" style="margin-top:8px;"></div>
    </div>
  </div>

  <div class="notice" style="text-align:center;">
    Lego Loco Gateway V2.0 &bull; IP: 192.168.4.1 &bull; http://legoloco.local
  </div>

  <script>
    let ws = new WebSocket((location.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + location.host + '/ws');
    let timer = null;
    let isRunningScenario = false;

    ws.onopen = () => { document.getElementById('wsBadge').textContent = 'ONLINE'; document.getElementById('wsBadge').classList.remove('offline'); };
    ws.onclose = () => { document.getElementById('wsBadge').textContent = 'OFFLINE'; document.getElementById('wsBadge').classList.add('offline'); setTimeout(() => location.reload(), 3000); };
    ws.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        if (msg.event === 'node_update') { refreshNodes(); }
      } catch(err){}
    };

    function sendCmd(obj) {
      if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
    }
    function getSelectedTarget() {
      const el = document.getElementById('targetSelect');
      return el ? el.value : 'ALL';
    }
    function onTargetChanged() {
      const t = getSelectedTarget();
      document.getElementById('spdSlider').value = 0;
      document.getElementById('spdText').textContent = '0%';
    }
    function setSpeed(v) {
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
    function setSwitch(p) {
      sendCmd({ cmd: 'track_switch', target: 'ALL', position: p });
    }

    async function scanForLocos() {
      const btn = document.getElementById('btnScan');
      const st = document.getElementById('pairingStatus');
      btn.disabled = true;
      btn.textContent = '⏳ Scanning...';
      st.textContent = '📡 Broadcasting ESP-NOW discovery scan... Power on devices.';
      try {
        await fetch('/api/locos/scan', { method: 'POST' });
        sendCmd({ cmd: 'scan_locos' });
        setTimeout(refreshNodes, 400);
        setTimeout(refreshNodes, 1200);
        setTimeout(refreshNodes, 2500);
        setTimeout(() => {
          btn.disabled = false;
          btn.textContent = '🔍 Scan for Devices';
          st.textContent = 'Scan complete.';
        }, 3000);
      } catch(e) {
        btn.disabled = false;
        btn.textContent = '🔍 Scan for Devices';
        st.textContent = '❌ Error sending discovery scan.';
      }
    }

    async function pairLoco(nodeId) {
      const st = document.getElementById('pairingStatus');
      st.textContent = '🔗 Pairing & bonding ' + nodeId + ' to this Master...';
      try {
        const res = await fetch('/api/locos/pair', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ nodeId: nodeId })
        });
        if (res.ok) {
          st.textContent = '✅ Success! ' + nodeId + ' is now bonded to this Master (persisted in EEPROM).';
          refreshNodes();
        } else {
          st.textContent = '❌ Failed to pair ' + nodeId + '.';
        }
      } catch(e) {
        st.textContent = '❌ Communication error: ' + e.message;
      }
    }

    async function refreshNodes() {
      try {
        const res = await fetch('/api/nodes');
        const nodes = await res.json();
        const sel = document.getElementById('targetSelect');
        const curr = sel.value;
        sel.innerHTML = '<option value="ALL">📢 ALL (Broadcast - All Locomotives)</option>';
        let pairedCount = 0;
        let unpairedList = [];

        nodes.forEach(n => {
          if (n.isPaired) {
            if (n.nodeType === 'LOCO' || n.nodeId.startsWith('LOCO')) {
              pairedCount++;
              const opt = document.createElement('option');
              opt.value = n.nodeId;
              opt.textContent = '🚂 ' + (n.friendlyName || n.nodeId) + ' [🔒 Bonded]' + (n.isOnline ? ' [Online]' : ' [Offline]');
              sel.appendChild(opt);
            }
          } else if (!n.isBondedOther) {
            unpairedList.push(n);
          }
        });

        const unpDiv = document.getElementById('unpairedLocosList');
        if (unpairedList.length > 0) {
          let html = '<div style="margin-top:8px;font-size:0.8rem;font-weight:700;color:var(--warning);">Devices Ready to Pair:</div>';
          unpairedList.forEach(u => {
            const isLoco = (u.nodeType === 'LOCO' || u.nodeId.startsWith('LOCO'));
            const icon = isLoco ? '🚂' : '🚉';
            const typeStr = isLoco ? 'Unpaired Locomotive' : 'Unpaired Station';
            html += '<div style="background:rgba(255,183,3,0.1);border:1px solid rgba(255,183,3,0.3);padding:8px 10px;border-radius:8px;margin-top:6px;display:flex;justify-content:space-between;align-items:center;">';
            html += '<div><b>' + icon + ' ' + u.nodeId + '</b> <span style="font-size:0.75rem;color:#8a99b5;">(' + typeStr + ')</span></div>';
            html += '<button class="btn primary" style="padding:5px 10px;font-size:0.75rem;" onclick="pairLoco(\'' + u.nodeId + '\')">🔗 Pair to this Master</button>';
            html += '</div>';
          });
          unpDiv.innerHTML = html;
        } else {
          unpDiv.innerHTML = '';
        }

        if (pairedCount > 0) {
          document.getElementById('noLocoNotice').style.display = 'none';
        } else {
          document.getElementById('noLocoNotice').style.display = 'block';
        }
        sel.value = curr;
      } catch(e){}
    }

    async function loadScenarios() {
      try {
        const res = await fetch('/api/scenarios');
        const list = await res.json();
        const sel = document.getElementById('scSelect');
        sel.innerHTML = '';
        list.forEach(f => {
          const opt = document.createElement('option');
          opt.value = f;
          opt.textContent = f;
          sel.appendChild(opt);
        });
      } catch(e){}
    }

    function toggleScenarioRun() {
      const sc = document.getElementById('scSelect').value || 'default.csv';
      isRunningScenario = !isRunningScenario;
      sendCmd({ cmd: 'run_scenario', scenario: sc, run: isRunningScenario });
      document.getElementById('btnRunSc').textContent = isRunningScenario ? '⏹ Stop' : '▶ Start';
      document.getElementById('scRunningText').textContent = isRunningScenario ? 'AUTOMATIC (' + sc + ')' : 'Manual';
    }

    async function exportScenarioCsv() {
      const sc = document.getElementById('scSelect').value || 'default.csv';
      try {
        const res = await fetch('/api/scenario?name=' + encodeURIComponent(sc));
        if (!res.ok) throw new Error('Scenario not found');
        const text = await res.text();
        const blob = new Blob([text], { type: 'text/csv' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = sc;
        a.click();
        document.getElementById('scMsg').textContent = '✅ File ' + sc + ' exported successfully.';
      } catch(err) {
        document.getElementById('scMsg').textContent = '❌ Export error: ' + err.message;
      }
    }

    async function importScenarioCsv(input) {
      if (!input.files || input.files.length === 0) return;
      const file = input.files[0];
      try {
        const text = await file.text();
        const res = await fetch('/api/scenario?name=' + encodeURIComponent(file.name), {
          method: 'POST',
          body: text
        });
        if (res.ok) {
          document.getElementById('scMsg').textContent = '✅ Scenario ' + file.name + ' imported successfully!';
          loadScenarios();
        } else {
          document.getElementById('scMsg').textContent = '❌ Failed to save imported scenario.';
        }
      } catch(err) {
        document.getElementById('scMsg').textContent = '❌ Error reading file: ' + err.message;
      }
      input.value = '';
    }

    function toggleSettingsView() {
      const p = document.getElementById('settingsPanel');
      p.style.display = (p.style.display === 'none' ? 'block' : 'none');
      if (p.style.display === 'block') loadSettings();
    }

    async function loadSettings() {
      try {
        const res = await fetch('/api/settings');
        const data = await res.json();
        document.getElementById('cfgSsid').value = data.wifiSsid || 'LegoTrain_Master';
        document.getElementById('cfgPass').value = data.wifiPassword || '';
        document.getElementById('cfgChannel').value = data.wifiChannel || 1;
      } catch(e){}
    }

    async function saveSettings() {
      const ssid = document.getElementById('cfgSsid').value.trim();
      const pass = document.getElementById('cfgPass').value;
      const chan = parseInt(document.getElementById('cfgChannel').value) || 1;
      if (!ssid) { alert('SSID cannot be empty!'); return; }
      if (pass.length > 0 && pass.length < 8) { alert('Password must be at least 8 characters (or leave empty for open network)!'); return; }

      document.getElementById('cfgMsg').textContent = 'Saving settings...';
      try {
        const res = await fetch('/api/settings', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ wifiSsid: ssid, wifiPassword: pass, wifiChannel: chan, apMode: true })
        });
        if (res.ok) {
          document.getElementById('cfgMsg').textContent = '✅ Saved! Restarting Master... Reconnect to Wi-Fi in 10 seconds.';
          fetch('/api/restart', { method: 'POST' });
        }
      } catch(e) {
        document.getElementById('cfgMsg').textContent = '❌ Error saving settings.';
      }
    }

    // Initial load
    refreshNodes();
    loadScenarios();
    setInterval(refreshNodes, 3000);
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

    // API: Save / Import Scenario (POST)
    _server.on("/api/scenario", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        String filename = "";
        if (request->hasParam("name")) {
            filename = request->getParam("name")->value();
        } else if (request->hasParam("name", true)) {
            filename = request->getParam("name", true)->value();
        }
        if (filename.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"Missing filename\"}");
            return;
        }
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

    // API: Settings (Wi-Fi AP & Multi-layout configuration)
    _server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        const SystemSettings& s = ConfigStore::instance().getSettings();
        JsonDocument doc;
        doc["wifiSsid"]     = s.wifiSsid;
        doc["wifiPassword"] = s.wifiPassword;
        doc["apMode"]       = s.apMode;
        doc["wifiChannel"]  = s.wifiChannel;
        doc["activeScenario"] = s.activeScenario;
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
