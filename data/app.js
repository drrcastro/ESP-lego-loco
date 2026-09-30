// =================================================================
// Lego Loco Control Center V2.0 - Core Application Logic
// =================================================================

let ws = null;
let currentMode = "MANUAL";
let discoveredNodes = [];
let activeScenarioFile = "default.csv";
let isRawCsvMode = false;
let throttleSendTimers = {};

// Default scenario table data
let scenarioSteps = [
  { stepId: 1, triggerType: "START", triggerValue: "0", targetNode: "ALL", action: "SET_SWITCH", parameter: "STRAIGHT" },
  { stepId: 2, triggerType: "START", triggerValue: "0", targetNode: "ALL", action: "SET_LIGHTS", parameter: "AUTO" },
  { stepId: 3, triggerType: "START", triggerValue: "0", targetNode: "ALL", action: "SET_SPEED", parameter: "45" },
  { stepId: 4, triggerType: "IR_BEACON", triggerValue: "5", targetNode: "ALL", action: "SET_SPEED", parameter: "25" },
  { stepId: 5, triggerType: "IR_BEACON", triggerValue: "5", targetNode: "ALL", action: "SET_SWITCH", parameter: "TURNOUT" },
  { stepId: 6, triggerType: "TRACK_OCCUPIED", triggerValue: "*", targetNode: "ALL", action: "SET_SPEED", parameter: "0" },
  { stepId: 7, triggerType: "TRACK_OCCUPIED", triggerValue: "*", targetNode: "ALL", action: "DWELL_WAIT", parameter: "8" },
  { stepId: 8, triggerType: "TRACK_CLEARED", triggerValue: "*", targetNode: "ALL", action: "SET_SPEED", parameter: "40" }
];

document.addEventListener("DOMContentLoaded", () => {
  initWebSocket();
  fetchStatus();
  fetchNodes();
  fetchScenarios();
  loadScenarioContent(activeScenarioFile);
  loadNetworkSettings();

  // Periodic polling every 3 seconds for node discovery/heartbeats
  setInterval(() => {
    fetchStatus();
    fetchNodes();
  }, 3000);
});

// =================================================================
// Tab Navigation
// =================================================================
function switchTab(tabId) {
  document.querySelectorAll(".nav-tab").forEach(t => t.classList.remove("active"));
  document.querySelectorAll(".bottom-nav-item").forEach(t => t.classList.remove("active"));
  document.querySelectorAll(".tab-content").forEach(c => c.classList.remove("active"));

  const selectedTab = document.querySelector(`.nav-tab[data-tab="${tabId}"]`);
  const selectedBottom = document.querySelector(`.bottom-nav-item[data-tab="${tabId}"]`);
  const selectedContent = document.getElementById(tabId);
  if (selectedTab) selectedTab.classList.add("active");
  if (selectedBottom) selectedBottom.classList.add("active");
  if (selectedContent) selectedContent.classList.add("active");
}

// =================================================================
// WebSocket Connection
// =================================================================
function initWebSocket() {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const wsUrl = `${protocol}//${window.location.host}/ws`;

  logMessage("system", `Connecting to WebSocket: ${wsUrl}`);
  try {
    ws = new WebSocket(wsUrl);
  } catch (e) {
    console.warn("WebSocket fallback:", e);
  }

  if (!ws) return;

  ws.onopen = () => {
    document.getElementById("wsIndicator").className = "status-indicator pulse-online";
    document.getElementById("wsStatusText").textContent = "LIVE";
    logMessage("system", "WebSocket connected. Real-time telemetry linked.");
  };

  ws.onclose = () => {
    document.getElementById("wsIndicator").className = "status-indicator pulse-offline";
    document.getElementById("wsStatusText").textContent = "OFFLINE";
    logMessage("system", "WebSocket disconnected. Reconnecting in 3s...");
    setTimeout(initWebSocket, 3000);
  };

  ws.onerror = (err) => {
    console.error("WebSocket error:", err);
  };

  ws.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data);
      handleIncomingTelemetry(msg);
    } catch (e) {
      console.warn("Non-JSON WebSocket message:", event.data);
    }
  };
}

function sendWsCommand(payload) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
  } else {
    // Fallback to HTTP API
    fetch("/api/control/" + (payload.cmd.includes("loco") ? "loco" : "track"), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    }).catch(e => console.error("API command error:", e));
  }
}

// =================================================================
// Telemetry Handler
// =================================================================
function handleIncomingTelemetry(msg) {
  const evt = msg.event;
  const d = msg.data;

  if (evt === "loco_telemetry") {
    logMessage("telemetry", `[LOCO] ${d.nodeId}: Spd=${d.speed}%, Blk=#${d.currentBlock}, Bat=${d.batteryMv}mV`);
    updateLocoCardTelemetry(d);
  } else if (evt === "track_telemetry") {
    logMessage("telemetry", `[TRACK] ${d.nodeId}: Switch=${d.switchState}, Occupied=${d.beamOccupied}`);
    updateTrackCardTelemetry(d);
  } else if (evt === "node_update") {
    logMessage("system", `[DISCOVERY] Node ${d.nodeId} (${d.friendlyName}) - Online: ${d.isOnline}`);
    fetchNodes();
  } else if (evt === "scenario_step") {
    logMessage("step", `[SCENARIO] Step #${d.stepId}: ${d.action} -> ${d.target} [${d.status}]`);
  }
}

// =================================================================
// REST API Helpers
// =================================================================
async function fetchStatus() {
  try {
    const res = await fetch("/api/status");
    if (!res.ok) return;
    const data = await res.json();

    currentMode = data.mode || "MANUAL";
    updateModeButtons();
    document.getElementById("statActiveScenario").textContent = data.scenarioRunning ? data.activeScenario : "None";

    // Format uptime
    const sec = data.uptimeSec || 0;
    const h = String(Math.floor(sec / 3600)).padStart(2, "0");
    const m = String(Math.floor((sec % 3600) / 60)).padStart(2, "0");
    const s = String(sec % 60).padStart(2, "0");
    document.getElementById("statUptime").textContent = `${h}:${m}:${s}`;
  } catch (e) {
    // Demo mode fallback if running detached
  }
}

async function fetchNodes() {
  try {
    const res = await fetch("/api/nodes");
    if (!res.ok) return;
    const nodes = await res.json();
    discoveredNodes = nodes;
    renderLocoCards(nodes.filter(n => n.nodeType === "LOCO"));
    renderTrackCards(nodes.filter(n => n.nodeType === "TRACK"));
    renderNodeTable(nodes);

    document.getElementById("statLocoCount").textContent = nodes.filter(n => n.nodeType === "LOCO").length;
    document.getElementById("statTrackCount").textContent = nodes.filter(n => n.nodeType === "TRACK").length;
  } catch (e) {
    // If running in preview mode without ESP hardware
    if (discoveredNodes.length === 0) {
      loadMockPreviewNodes();
    }
  }
}

function loadMockPreviewNodes() {
  discoveredNodes = [
    { nodeId: "LOCO_4B5C", friendlyName: "Cargo Hauler 101", nodeType: "LOCO", isOnline: true, speed: 0, currentBlock: 1, batteryMv: 3850, rssi: -45, lastSeenSec: 1 },
    { nodeId: "TRACK_1A2B", friendlyName: "North Junction & Station", nodeType: "TRACK", isOnline: true, switchState: "STRAIGHT", beamOccupied: false, rssi: -52, lastSeenSec: 1 }
  ];
  renderLocoCards(discoveredNodes.filter(n => n.nodeType === "LOCO"));
  renderTrackCards(discoveredNodes.filter(n => n.nodeType === "TRACK"));
  renderNodeTable(discoveredNodes);
}

// =================================================================
// Mode & Emergency Stop
// =================================================================
function setSystemMode(mode) {
  currentMode = mode;
  updateModeButtons();
  sendWsCommand({ cmd: "set_mode", mode: mode });
  logMessage("system", `System mode switched to: ${mode}`);
}

function updateModeButtons() {
  document.getElementById("btnModeManual").classList.toggle("active", currentMode === "MANUAL");
  document.getElementById("btnModeAuto").classList.toggle("active", currentMode === "AUTOMATIC");
  const mMan = document.getElementById("mBtnManual");
  const mAut = document.getElementById("mBtnAuto");
  if (mMan) mMan.classList.toggle("active", currentMode === "MANUAL");
  if (mAut) mAut.classList.toggle("active", currentMode === "AUTOMATIC");
}

function triggerEmergencyStop() {
  sendWsCommand({ cmd: "emergency_stop" });
  logMessage("emergency", "⚠️ EMERGENCY BRAKE ACTIVATED! Stopping all locomotives.");
  // Visual feedback
  document.querySelectorAll(".throttle-slider").forEach(sl => {
    sl.value = 0;
    sl.dispatchEvent(new Event("input"));
  });
}

// =================================================================
// Fleet Scanning & Pairing
// =================================================================
async function scanFleet() {
  const btn = document.getElementById("btnScanFleetTop");
  if (btn) {
    btn.disabled = true;
    btn.textContent = "⏳ Scanning...";
  }
  logMessage("system", "🔍 Broadcasting ESP-NOW discovery scan to discover devices...");
  try {
    await fetch("/api/locos/scan", { method: "POST" });
    sendWsCommand({ cmd: "scan_locos" });
    setTimeout(fetchNodes, 400);
    setTimeout(fetchNodes, 1200);
    setTimeout(fetchNodes, 2500);
    setTimeout(() => {
      if (btn) {
        btn.disabled = false;
        btn.textContent = "🔍 Scan for Locomotives";
      }
      logMessage("system", "Device discovery scan complete.");
    }, 3000);
  } catch (e) {
    if (btn) {
      btn.disabled = false;
      btn.textContent = "🔍 Scan for Locomotives";
    }
    logMessage("system", `Error initiating discovery scan: ${e.message}`);
  }
}

async function pairNode(nodeId) {
  logMessage("system", `🔗 Pairing node ${nodeId} to this Master...`);
  try {
    const res = await fetch("/api/locos/pair", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ nodeId: nodeId })
    });
    if (res.ok) {
      logMessage("system", `✅ Success! Node ${nodeId} permanently bonded to this Master.`);
      fetchNodes();
    } else {
      logMessage("system", `❌ Failed to pair node ${nodeId}.`);
    }
  } catch (e) {
    logMessage("system", `Error pairing node: ${e.message}`);
  }
}

function pairLocomotive(nodeId) {
  return pairNode(nodeId);
}

// =================================================================
// Locomotive Controller Cards
// =================================================================
function renderLocoCards(locos) {
  const grid = document.getElementById("locoGrid");
  const empty = document.getElementById("locoEmptyState");
  const banner = document.getElementById("unpairedFleetBanner");
  const unpList = document.getElementById("unpairedFleetList");

  const pairedLocos = (locos || []).filter(l => l.isPaired);
  const unpairedLocos = (locos || []).filter(l => !l.isPaired && !l.isBondedOther);

  if (unpairedLocos.length > 0) {
    if (banner) banner.style.display = "block";
    if (unpList) {
      unpList.innerHTML = unpairedLocos.map(u => `
        <div style="background: rgba(255,255,255,0.04); border: 1px solid rgba(255,183,3,0.3); border-radius: 8px; padding: 10px 14px; margin-top: 8px; display: flex; justify-content: space-between; align-items: center;">
          <div>
            <strong style="color:var(--text); font-size: 0.95rem;">🚂 ${u.nodeId}</strong>
            <span style="font-size: 0.8rem; color: var(--text-dim); margin-left: 8px;">(${u.friendlyName || 'New Locomotive'}) &bull; RSSI: ${u.rssi || -50}dBm</span>
          </div>
          <button class="btn btn-primary btn-sm" onclick="pairLocomotive('${u.nodeId}')">
            🔗 Pair to this Master
          </button>
        </div>
      `).join("");
    }
  } else {
    if (banner) banner.style.display = "none";
  }

  if (pairedLocos.length === 0) {
    if (empty) empty.style.display = "block";
    document.querySelectorAll(".loco-card").forEach(c => c.remove());
    return;
  }
  if (empty) empty.style.display = "none";

  pairedLocos.forEach(loco => {
    let card = document.getElementById(`loco_card_${loco.nodeId}`);
    if (!card) {
      card = document.createElement("div");
      card.id = `loco_card_${loco.nodeId}`;
      card.className = "loco-card";
      grid.appendChild(card);
    }

    card.innerHTML = `
      <div class="card-top">
        <div>
          <span class="loco-id">${loco.nodeId}</span>
          <h3 class="loco-name" onclick="renameNode('${loco.nodeId}', '${loco.friendlyName}')">${loco.friendlyName} ✎</h3>
        </div>
        <div style="display:flex; gap:6px; align-items:center;">
          <span class="badge" style="background:rgba(0,242,155,0.15); color:var(--success);">🔒 CASADA</span>
          <span class="pill ${loco.isOnline ? 'highlight' : ''}">${loco.isOnline ? 'ONLINE' : 'OFFLINE'}</span>
        </div>
      </div>

      <div class="loco-telemetry-pills">
        <span class="pill" id="blk_${loco.nodeId}">Block: #${loco.currentBlock || '--'}</span>
        <span class="pill" id="bat_${loco.nodeId}">Batt: ${(loco.batteryMv / 1000).toFixed(2)}V</span>
        <span class="pill">RSSI: ${loco.rssi || -50}dBm</span>
      </div>

      <div class="throttle-control">
        <div class="throttle-header">
          <span>THROTTLE POWER</span>
          <span class="speed-display" id="spd_val_${loco.nodeId}">${loco.speed || 0}%</span>
        </div>
        <div class="slider-container">
          <input type="range" class="throttle-slider" id="spd_slider_${loco.nodeId}"
                 min="-100" max="100" value="${loco.speed || 0}"
                 oninput="handleThrottleInput('${loco.nodeId}', this.value)">
        </div>
      </div>

      <div class="loco-actions">
        <button class="btn-dir ${loco.speed < 0 ? 'active' : ''}" onclick="setLocoPresetSpeed('${loco.nodeId}', -50)">◀ REV 50%</button>
        <button class="btn-brake" onclick="brakeLoco('${loco.nodeId}')">⏹ STOP</button>
        <button class="btn-dir ${loco.speed > 0 ? 'active' : ''}" onclick="setLocoPresetSpeed('${loco.nodeId}', 50)">FWD 50% ▶</button>
      </div>

      <div class="lighting-panel">
        <div class="lighting-header">Lighting Controls</div>
        <div class="light-toggles">
          <button class="btn-light active" id="btn_light_auto_${loco.nodeId}" onclick="setLocoLightMode('${loco.nodeId}', 1)">Auto Direction</button>
          <button class="btn-light" id="btn_light_front_${loco.nodeId}" onclick="toggleLocoZone('${loco.nodeId}', 'front')">Headlights</button>
          <button class="btn-light" id="btn_light_cab_${loco.nodeId}" onclick="toggleLocoZone('${loco.nodeId}', 'cab')">Cab</button>
        </div>
      </div>
    `;
  });
}

function handleThrottleInput(nodeId, value) {
  document.getElementById(`spd_val_${nodeId}`).textContent = `${value}%`;

  // Debounced sending
  clearTimeout(throttleSendTimers[nodeId]);
  throttleSendTimers[nodeId] = setTimeout(() => {
    sendWsCommand({
      cmd: "loco_throttle",
      target: nodeId,
      speed: parseInt(value),
      brake: (parseInt(value) === 0 ? 1 : 0),
      lightsFront: 255,
      lightsRear: 255,
      lightsCab: 100,
      lightMode: 1
    });
  }, 40);
}

function setLocoPresetSpeed(nodeId, speed) {
  const slider = document.getElementById(`spd_slider_${nodeId}`);
  if (slider) {
    slider.value = speed;
    handleThrottleInput(nodeId, speed);
  }
}

function brakeLoco(nodeId) {
  setLocoPresetSpeed(nodeId, 0);
  sendWsCommand({
    cmd: "loco_throttle",
    target: nodeId,
    speed: 0,
    brake: 1
  });
}

function setLocoLightMode(nodeId, mode) {
  sendWsCommand({
    cmd: "loco_throttle",
    target: nodeId,
    lightMode: mode
  });
  logMessage("telemetry", `Loco ${nodeId}: lighting mode set to ${mode === 1 ? 'Auto' : 'Manual'}`);
}

function toggleLocoZone(nodeId, zone) {
  sendWsCommand({
    cmd: "loco_throttle",
    target: nodeId,
    lightMode: 0, // Manual
    lightsFront: zone === 'front' ? 255 : 0,
    lightsCab: zone === 'cab' ? 255 : 0
  });
}

function updateLocoCardTelemetry(d) {
  const spdVal = document.getElementById(`spd_val_${d.nodeId}`);
  const blk = document.getElementById(`blk_${d.nodeId}`);
  const bat = document.getElementById(`bat_${d.nodeId}`);

  if (spdVal) spdVal.textContent = `${d.speed}%`;
  if (blk) blk.textContent = `Block: #${d.currentBlock || '--'}`;
  if (bat) bat.textContent = `Batt: ${(d.batteryMv / 1000).toFixed(2)}V`;
}

// =================================================================
// Track Switch & Station Cards
// =================================================================
function renderTrackCards(tracks) {
  const grid = document.getElementById("trackGrid");
  const empty = document.getElementById("trackEmptyState");
  const banner = document.getElementById("unpairedTrackBanner");
  const unpList = document.getElementById("unpairedTrackList");

  const pairedTracks = (tracks || []).filter(t => t.isPaired);
  const unpairedTracks = (tracks || []).filter(t => !t.isPaired && !t.isBondedOther);

  if (unpairedTracks.length > 0) {
    if (banner) banner.style.display = "block";
    if (unpList) {
      unpList.innerHTML = unpairedTracks.map(u => `
        <div style="background: rgba(255,255,255,0.04); border: 1px solid rgba(255,183,3,0.3); border-radius: 8px; padding: 10px 14px; margin-top: 8px; display: flex; justify-content: space-between; align-items: center;">
          <div>
            <strong style="color:var(--text); font-size: 0.95rem;">🚉 ${u.nodeId}</strong>
            <span style="font-size: 0.8rem; color: var(--text-dim); margin-left: 8px;">(${u.friendlyName || 'New Station'}) &bull; RSSI: ${u.rssi || -50}dBm</span>
          </div>
          <button class="btn btn-primary btn-sm" onclick="pairNode('${u.nodeId}')">
            🔗 Pair to this Master
          </button>
        </div>
      `).join("");
    }
  } else {
    if (banner) banner.style.display = "none";
  }

  if (pairedTracks.length === 0) {
    if (empty) empty.style.display = "block";
    document.querySelectorAll(".track-card").forEach(c => c.remove());
    return;
  }
  if (empty) empty.style.display = "none";

  pairedTracks.forEach(tr => {
    let card = document.getElementById(`track_card_${tr.nodeId}`);
    if (!card) {
      card = document.createElement("div");
      card.id = `track_card_${tr.nodeId}`;
      card.className = "track-card";
      grid.appendChild(card);
    }

    const isStraight = tr.switchState !== "TURNOUT";
    const isOccupied = tr.beamOccupied;

    card.innerHTML = `
      <div class="card-top">
        <div>
          <span class="loco-id">${tr.nodeId}</span>
          <h3 class="loco-name" onclick="renameNode('${tr.nodeId}', '${tr.friendlyName}')">${tr.friendlyName} ✎</h3>
        </div>
        <div style="display:flex; gap:6px; align-items:center;">
          <span class="badge" style="background:rgba(0,242,155,0.15); color:var(--success);">🔒 CASADA</span>
          <span class="pill ${isOccupied ? 'highlight' : ''}">${isOccupied ? '🔴 OCCUPIED' : '🟢 CLEAR'}</span>
        </div>
      </div>

      <div class="track-diagram">
        <svg class="railway-svg" viewBox="0 0 200 60">
          <!-- Main straight track -->
          <line x1="10" y1="30" x2="190" y2="30" stroke="${isStraight ? '#00f29b' : '#3a4a6b'}" stroke-width="4" stroke-linecap="round"></line>
          <!-- Turnout branching line -->
          <line x1="80" y1="30" x2="160" y2="12" stroke="${!isStraight ? '#00f29b' : '#3a4a6b'}" stroke-width="4" stroke-linecap="round"></line>
          <!-- Switch junction point indicator -->
          <circle cx="80" cy="30" r="5" fill="#00d2ff"></circle>
        </svg>
      </div>

      <div class="switch-toggle-buttons">
        <button class="btn-switch ${isStraight ? 'active' : ''}" onclick="setSwitchPosition('${tr.nodeId}', 0)">
          STRAIGHT
        </button>
        <button class="btn-switch ${!isStraight ? 'active' : ''}" onclick="setSwitchPosition('${tr.nodeId}', 1)">
          TURNOUT
        </button>
      </div>
    `;
  });
}

function setSwitchPosition(nodeId, position) {
  sendWsCommand({
    cmd: "track_switch",
    target: nodeId,
    position: position,
    dwell: 0
  });
  logMessage("telemetry", `Track ${nodeId}: Switch set to ${position === 0 ? 'STRAIGHT' : 'TURNOUT'}`);
}

function updateTrackCardTelemetry(d) {
  fetchNodes(); // Refresh track state
}

// =================================================================
// Scenario Manager (CSV Studio)
// =================================================================
async function fetchScenarios() {
  try {
    const res = await fetch("/api/scenarios");
    if (!res.ok) return;
    const list = await res.json();
    renderScenarioFileList(list);
  } catch (e) {
    renderScenarioFileList(["default.csv", "cargo_loop.csv"]);
  }
}

function renderScenarioFileList(files) {
  const ul = document.getElementById("scenarioFileList");
  ul.innerHTML = "";
  files.forEach(f => {
    const li = document.createElement("li");
    li.className = `file-item ${f === activeScenarioFile ? 'active' : ''}`;
    li.textContent = f;
    li.onclick = () => loadScenarioContent(f);
    ul.appendChild(li);
  });
}

async function loadScenarioContent(filename) {
  activeScenarioFile = filename;
  document.getElementById("scenarioFileName").value = filename;

  try {
    const res = await fetch(`/api/scenario?name=${encodeURIComponent(filename)}`);
    if (res.ok) {
      const csv = await res.text();
      parseCsvToSteps(csv);
      document.getElementById("scenarioRawText").value = csv;
    }
  } catch (e) {
    renderScenarioTable();
  }
  fetchScenarios();
}

function parseCsvToSteps(csvText) {
  const lines = csvText.split("\n");
  scenarioSteps = [];

  lines.forEach(line => {
    const clean = line.trim();
    if (!clean || clean.startsWith("#") || clean.includes("TRIGGER_TYPE")) return;

    const parts = clean.split(",").map(p => p.trim());
    if (parts.length >= 6) {
      scenarioSteps.push({
        stepId: parseInt(parts[0]) || scenarioSteps.length + 1,
        triggerType: parts[1],
        triggerValue: parts[2],
        targetNode: parts[3],
        action: parts[4],
        parameter: parts[5]
      });
    }
  });

  renderScenarioTable();
}

function exportStepsToCsv() {
  let csv = "STEP_ID, TRIGGER_TYPE, TRIGGER_VALUE, TARGET_NODE, ACTION, PARAMETER\n";
  scenarioSteps.forEach(s => {
    csv += `${s.stepId}, ${s.triggerType}, ${s.triggerValue}, ${s.targetNode}, ${s.action}, ${s.parameter}\n`;
  });
  return csv;
}

function renderScenarioTable() {
  const tbody = document.getElementById("scenarioTableBody");
  tbody.innerHTML = "";

  const triggerTypes = ["START", "IR_BEACON", "TRACK_OCCUPIED", "TRACK_CLEARED", "TIMER"];
  const actionTypes = ["SET_SPEED", "SET_SWITCH", "SET_LIGHTS", "DWELL_WAIT", "EMERGENCY_STOP", "GOTO_STEP"];

  scenarioSteps.forEach((step, idx) => {
    const tr = document.createElement("tr");

    tr.innerHTML = `
      <td><strong>${step.stepId}</strong></td>
      <td>
        <select onchange="updateStepField(${idx}, 'triggerType', this.value)">
          ${triggerTypes.map(t => `<option value="${t}" ${step.triggerType === t ? 'selected' : ''}>${t}</option>`).join("")}
        </select>
      </td>
      <td>
        <input type="text" value="${step.triggerValue}" onchange="updateStepField(${idx}, 'triggerValue', this.value)">
      </td>
      <td>
        <input type="text" value="${step.targetNode}" onchange="updateStepField(${idx}, 'targetNode', this.value)">
      </td>
      <td>
        <select onchange="updateStepField(${idx}, 'action', this.value)">
          ${actionTypes.map(a => `<option value="${a}" ${step.action === a ? 'selected' : ''}>${a}</option>`).join("")}
        </select>
      </td>
      <td>
        <input type="text" value="${step.parameter}" onchange="updateStepField(${idx}, 'parameter', this.value)">
      </td>
      <td>
        <button class="btn btn-sm btn-danger" onclick="deleteScenarioStep(${idx})">✕</button>
      </td>
    `;
    tbody.appendChild(tr);
  });

  document.getElementById("scenarioRawText").value = exportStepsToCsv();
}

function updateStepField(idx, field, value) {
  scenarioSteps[idx][field] = value;
  document.getElementById("scenarioRawText").value = exportStepsToCsv();
}

function addScenarioStep() {
  const nextId = scenarioSteps.length > 0 ? scenarioSteps[scenarioSteps.length - 1].stepId + 1 : 1;
  scenarioSteps.push({
    stepId: nextId,
    triggerType: "IR_BEACON",
    triggerValue: "5",
    targetNode: "ALL",
    action: "SET_SPEED",
    parameter: "30"
  });
  renderScenarioTable();
}

function deleteScenarioStep(idx) {
  scenarioSteps.splice(idx, 1);
  renderScenarioTable();
}

function toggleCsvViewMode() {
  isRawCsvMode = !isRawCsvMode;
  document.getElementById("scenarioTableView").style.display = isRawCsvMode ? "none" : "block";
  document.getElementById("scenarioRawView").style.display = isRawCsvMode ? "block" : "none";
  document.getElementById("btnToggleCsvMode").textContent = isRawCsvMode ? "Switch to Visual Table" : "Switch to Raw CSV";

  if (!isRawCsvMode) {
    parseCsvToSteps(document.getElementById("scenarioRawText").value);
  }
}

async function saveScenarioToServer() {
  const filename = document.getElementById("scenarioFileName").value.trim();
  const csvContent = isRawCsvMode ? document.getElementById("scenarioRawText").value : exportStepsToCsv();

  try {
    const res = await fetch(`/api/scenario?name=${encodeURIComponent(filename)}`, {
      method: "POST",
      headers: { "Content-Type": "text/csv" },
      body: csvContent
    });
    if (res.ok) {
      logMessage("step", `Scenario '${filename}' saved to LittleFS.`);
      fetchScenarios();
    }
  } catch (e) {
    logMessage("system", "Demo Mode: Scenario saved locally in memory.");
  }
}

function createNewScenario() {
  const name = prompt("Enter scenario file name (e.g. route_a.csv):", "new_scenario.csv");
  if (name) {
    activeScenarioFile = name;
    document.getElementById("scenarioFileName").value = name;
    scenarioSteps = [
      { stepId: 1, triggerType: "START", triggerValue: "0", targetNode: "ALL", action: "SET_SPEED", parameter: "40" }
    ];
    renderScenarioTable();
    saveScenarioToServer();
  }
}

function exportScenarioCsv() {
  const csvContent = isRawCsvMode ? document.getElementById("scenarioRawText").value : exportStepsToCsv();
  const blob = new Blob([csvContent], { type: "text/csv" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = document.getElementById("scenarioFileName").value || "scenario.csv";
  a.click();
  URL.revokeObjectURL(url);
}

async function importScenarioCsvFile(input) {
  if (!input.files || input.files.length === 0) return;
  const file = input.files[0];
  try {
    const text = await file.text();
    const res = await fetch("/api/scenario?name=" + encodeURIComponent(file.name), {
      method: "POST",
      body: text
    });
    if (res.ok) {
      logMessage("system", `Scenario '${file.name}' imported successfully.`);
      fetchScenarios();
      loadScenarioContent(file.name);
    } else {
      logMessage("system", `Failed to save imported scenario '${file.name}'.`);
    }
  } catch (e) {
    logMessage("system", `Error importing CSV: ${e.message}`);
  }
  input.value = "";
}

function runCurrentScenario() {
  const filename = document.getElementById("scenarioFileName").value.trim();
  sendWsCommand({ cmd: "run_scenario", scenario: filename, run: true });
  logMessage("step", `Started running automatic scenario '${filename}'`);
  fetchStatus();
}

function stopCurrentScenario() {
  sendWsCommand({ cmd: "run_scenario", run: false });
  logMessage("step", "Stopped automatic scenario execution.");
  fetchStatus();
}

// =================================================================
// Fleet Topology Table
// =================================================================
function renderNodeTable(nodes) {
  const tbody = document.getElementById("nodeTableBody");
  tbody.innerHTML = "";

  if (!nodes || nodes.length === 0) {
    tbody.innerHTML = `<tr><td colspan="8" style="text-align:center;color:var(--text-dim);">No nodes registered</td></tr>`;
    return;
  }

  nodes.forEach(n => {
    let pairBadge = `<span style="color:var(--text-dim);">-</span>`;
    if (n.nodeType === "LOCO" || n.nodeType === "TRACK") {
      if (n.isPaired) {
        pairBadge = `<span class="badge" style="background:rgba(0,242,155,0.15);color:var(--success);">🔒 Bonded</span>`;
      } else if (n.isBondedOther) {
        pairBadge = `<span class="badge" style="background:rgba(255,51,102,0.15);color:var(--danger);">Other Master</span>`;
      } else {
        pairBadge = `<span class="badge" style="background:rgba(255,183,3,0.15);color:var(--warning);">Unpaired</span> <button class="btn btn-sm btn-primary" style="padding:2px 8px;font-size:0.75rem;margin-left:4px;" onclick="pairNode('${n.nodeId}')">🔗 Pair</button>`;
      }
    }

    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td><strong>${n.nodeId}</strong></td>
      <td>
        <span style="cursor:pointer;" onclick="renameNode('${n.nodeId}', '${n.friendlyName}')">${n.friendlyName} ✎</span>
      </td>
      <td><span class="badge">${n.nodeType}</span></td>
      <td>${n.rssi || -50} dBm</td>
      <td>${pairBadge}</td>
      <td>${n.nodeType === 'LOCO' ? `Spd: ${n.speed}%, Blk: #${n.currentBlock}` : `Sw: ${n.switchState}, Occ: ${n.beamOccupied}`}</td>
      <td>${n.lastSeenSec || 0}s ago</td>
      <td>
        <span class="pill ${n.isOnline ? 'highlight' : ''}">${n.isOnline ? 'ONLINE' : 'OFFLINE'}</span>
      </td>
    `;
    tbody.appendChild(tr);
  });
}

async function renameNode(nodeId, currentName) {
  const newName = prompt(`Enter new friendly name for ${nodeId}:`, currentName);
  if (newName && newName !== currentName) {
    try {
      await fetch("/api/nodes/rename", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ nodeId: nodeId, friendlyName: newName })
      });
      fetchNodes();
    } catch (e) {
      console.warn("Could not save rename to master:", e);
    }
  }
}

function refreshNodesList() {
  fetchNodes();
  logMessage("system", "Fleet topology refreshed.");
}

// =================================================================
// Event Log Console
// =================================================================
function logMessage(type, text) {
  const box = document.getElementById("eventLogConsole");
  if (!box) return;

  const now = new Date();
  const timeStr = `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}`;

  const line = document.createElement("div");
  line.className = `log-line ${type}`;
  line.textContent = `[${timeStr}] ${text}`;

  box.appendChild(line);
  box.scrollTop = box.scrollHeight;
}

function clearLogs() {
  const box = document.getElementById("eventLogConsole");
  if (box) box.innerHTML = "";
}

// =================================================================
// Network Settings (AP Configuration)
// =================================================================
async function loadNetworkSettings() {
  try {
    const res = await fetch("/api/settings");
    if (!res.ok) return;
    const s = await res.json();
    const elSsid = document.getElementById("settingWifiSsid");
    const elPass = document.getElementById("settingWifiPassword");
    const elChan = document.getElementById("settingWifiChannel");
    if (elSsid) elSsid.value = s.wifiSsid || "LegoTrain_Master";
    if (elPass) elPass.value = s.wifiPassword || "";
    if (elChan) elChan.value = s.wifiChannel || 1;
  } catch (e) {
    console.warn("Could not load network settings:", e);
  }
}

async function saveNetworkSettings() {
  const ssid = document.getElementById("settingWifiSsid").value.trim();
  const pass = document.getElementById("settingWifiPassword").value;
  const chan = parseInt(document.getElementById("settingWifiChannel").value) || 1;
  const fb = document.getElementById("settingsFeedback");

  if (!ssid) {
    alert("SSID cannot be empty!");
    return;
  }
  if (pass.length > 0 && pass.length < 8) {
    alert("Password must be at least 8 characters or left empty for open network!");
    return;
  }

  if (fb) fb.innerHTML = `<span style="color:var(--primary);">Saving configuration to Master...</span>`;

  try {
    const res = await fetch("/api/settings", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ wifiSsid: ssid, wifiPassword: pass, wifiChannel: chan, apMode: true })
    });

    if (res.ok) {
      if (fb) fb.innerHTML = `<span style="color:var(--success);font-weight:700;">✅ Settings saved! Rebooting Master Gateway... Reconnect to Wi-Fi '${ssid}' in 10s.</span>`;
      logMessage("system", `Settings saved. Rebooting Master with SSID '${ssid}' on channel ${chan}.`);
      await fetch("/api/restart", { method: "POST" });
    } else {
      if (fb) fb.innerHTML = `<span style="color:var(--danger);">❌ Failed to save settings to Master.</span>`;
    }
  } catch (e) {
    if (fb) fb.innerHTML = `<span style="color:var(--danger);">❌ Error communicating with Master: ${e.message}</span>`;
  }
}
