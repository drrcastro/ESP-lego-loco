// =================================================================
// Lego Loco Control Center - Core Application Logic
// Autonomous Dispatch, CTC Synoptic Map & Advanced Multi-Beacon Config
// =================================================================

let ws = null;
let currentMode = "MANUAL";
let discoveredNodes = [];
let systemConfig = {
  system: {
    layoutName: "Lego Central Layout",
    wifiSsid: "LegoTrain_Master",
    wifiChannel: 1,
    headwaySafeSec: 12,
    headwayCautionSec: 6,
    headwaySpeedTrimPct: 40
  },
  locomotives: [],
  stations: []
};
let circuitTopology = { nodes: [], edges: [] };
let liveTrainTelemetry = {};
let liveTrackTelemetry = {};
let throttleSendTimers = {};

document.addEventListener("DOMContentLoaded", () => {
  initWebSocket();
  fetchStatus();
  fetchNodes();
  fetchConfig();
  fetchTopology();

  // Periodic polling every 3 seconds for node discovery/heartbeats
  setInterval(() => {
    fetchStatus();
    fetchNodes();
  }, 3000);

  // Redraw synoptic map periodically to smoothly update train positions
  setInterval(renderSynopticMap, 1000);
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

  if (tabId === "tabMap") {
    fetchTopology();
    renderSynopticMap();
  } else if (tabId === "tabConfig") {
    fetchConfig();
  }
}

function switchConfigSubtab(subTabId) {
  document.querySelectorAll(".config-subtab-btn").forEach(b => b.classList.remove("active"));
  document.querySelectorAll(".config-subtab-pane").forEach(p => p.classList.remove("active"));

  const btn = document.querySelector(`.config-subtab-btn[data-subtab="${subTabId}"]`);
  const pane = document.getElementById(subTabId);
  if (btn) btn.classList.add("active");
  if (pane) pane.classList.add("active");
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
    fetch("/api/control/" + (payload.cmd && payload.cmd.includes("loco") ? "loco" : "track"), {
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
    liveTrainTelemetry[d.nodeId] = d;
    logMessage("telemetry", `[LOCO] ${d.nodeId}: Spd=${d.speed}%, Blk=#${d.currentBlock}, Bat=${d.batteryMv}mV, ETA=${d.etaSeconds || 0}s, Len=${d.measuredLengthCm || 0}cm`);
    updateLocoCardTelemetry(d);
    renderSynopticMap();
  } else if (evt === "track_telemetry") {
    liveTrackTelemetry[d.nodeId] = d;
    logMessage("telemetry", `[TRACK] ${d.nodeId}: Switch=${d.switchState}, Occupied=${d.beamOccupied}, Sig=${d.signalAspect || 'GREEN'}, Len=${d.lastMeasuredLengthCm || 0}cm`);
    updateTrackCardTelemetry(d);
    renderSynopticMap();
  } else if (evt === "train_length_report") {
    logMessage("step", `📏 [LENGTH REPORT] Loco ${d.locoId} measured at Beacon #${d.beaconId}: ${d.measuredLengthCm} cm (${d.transitTimeMs} ms)`);
    if (liveTrainTelemetry[d.locoId]) {
      liveTrainTelemetry[d.locoId].measuredLengthCm = d.measuredLengthCm;
    }
    renderSynopticMap();
  } else if (evt === "node_update") {
    logMessage("system", `[DISCOVERY] Node ${d.nodeId} (${d.friendlyName}) - Online: ${d.isOnline}`);
    fetchNodes();
  } else if (evt === "circuit_status") {
    logMessage("step", `🗺️ [CIRCUIT] Loco ${d.locoId} lap complete! Beacons=${d.beaconCount}, LapTime=${(d.totalLapTimeMs/1000).toFixed(1)}s, Calibrated=${d.isCalibrated ? 'YES' : 'NO'}`);
    fetchTopology();
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
    document.getElementById("statCircuitStatus").textContent = data.circuitCalibrated ? "Calibrated" : "Learning";

    const sec = data.uptimeSec || 0;
    const h = String(Math.floor(sec / 3600)).padStart(2, "0");
    const m = String(Math.floor((sec % 3600) / 60)).padStart(2, "0");
    const s = String(sec % 60).padStart(2, "0");
    document.getElementById("statUptime").textContent = `${h}:${m}:${s}`;
  } catch (e) {
    // Detached preview mode
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
    if (discoveredNodes.length === 0) {
      loadMockPreviewNodes();
    }
  }
}

function loadMockPreviewNodes() {
  discoveredNodes = [
    { nodeId: "LOCO_4B5C", friendlyName: "Cargo Hauler 101", nodeType: "LOCO", isOnline: true, isPaired: true, speed: 35, currentBlock: 10, batteryMv: 3850, rssi: -45, lastSeenSec: 1 },
    { nodeId: "TRACK_1A2B", friendlyName: "Central Station Platform", nodeType: "TRACK", isOnline: true, isPaired: true, switchState: "STRAIGHT", beamOccupied: false, rssi: -52, lastSeenSec: 1 }
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
  document.querySelectorAll(".throttle-slider").forEach(sl => {
    sl.value = 0;
    sl.dispatchEvent(new Event("input"));
  });
}

// =================================================================
// Discovery & Pairing
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
    setTimeout(fetchNodes, 1500);
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

async function unpairLocomotive(nodeId) {
  if (!confirm(`Are you sure you want to remove and unpair locomotive ${nodeId}?\n\nThe locomotive will become unbonded and ready for re-pairing immediately without reflashing.`)) return;
  logMessage("system", `🗑️ Unpairing locomotive ${nodeId}...`);
  try {
    const res = await fetch("/api/locos/unpair", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ nodeId: nodeId })
    });
    if (res.ok) {
      logMessage("system", `✅ Locomotive ${nodeId} unpaired successfully!`);
      fetchNodes();
      fetchConfig();
    } else {
      logMessage("system", `❌ Failed to unpair ${nodeId}.`);
    }
  } catch (e) {
    logMessage("system", `Error unpairing: ${e.message}`);
  }
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
        <div style="background: rgba(255,255,255,0.04); border: 1px solid rgba(255,183,3,0.3); border-radius: 0; padding: 10px 14px; margin-top: 8px; display: flex; justify-content: space-between; align-items: center;">
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
    return;
  }
  if (empty) empty.style.display = "none";

  pairedLocos.forEach(loco => {
    let card = document.getElementById(`locoCard_${loco.nodeId}`);
    if (!card) {
      card = document.createElement("div");
      card.id = `locoCard_${loco.nodeId}`;
      card.className = "card loco-card";
      grid.appendChild(card);
    }

    card.innerHTML = `
      <div class="card-brick-studs studs-red"><span></span><span></span><span></span><span></span><span></span><span></span></div>
      <div class="card-header">
        <div class="card-title">
          <span class="icon loco-avatar">🚂</span>
          <div>
            <h3>${loco.friendlyName || loco.nodeId}</h3>
            <span class="node-id">${loco.nodeId} &bull; ${loco.rssi || -50} dBm</span>
          </div>
        </div>
        <span class="badge ${loco.isOnline ? 'badge-lego-green' : 'badge-lego-red'}">
          ${loco.isOnline ? 'ONLINE' : 'OFFLINE'}
        </span>
      </div>

      <div class="telemetry-bar">
        <div class="telem-item telem-yellow">
          <span class="label">SPEED</span>
          <span class="value" id="valSpeed_${loco.nodeId}">${loco.speed || 0}%</span>
        </div>
        <div class="telem-item telem-blue">
          <span class="label">BLOCK</span>
          <span class="value" id="valBlock_${loco.nodeId}">#${loco.currentBlock || 0}</span>
        </div>
        <div class="telem-item telem-orange">
          <span class="label">LENGTH</span>
          <span class="value" id="valLength_${loco.nodeId}">${loco.measuredLengthCm || '--'} cm</span>
        </div>
        <div class="telem-item telem-green">
          <span class="label">BATTERY</span>
          <span class="value" id="valBat_${loco.nodeId}">${((loco.batteryMv || 3800)/1000).toFixed(2)}V</span>
        </div>
      </div>

      <div class="throttle-control">
        <div class="throttle-header">
          <label>THROTTLE (SPEED)</label>
          <span class="throttle-display" id="throttleVal_${loco.nodeId}">${loco.speed || 0}%</span>
        </div>
        <input type="range" min="-100" max="100" value="${loco.speed || 0}" step="5" class="throttle-slider" id="slider_${loco.nodeId}" oninput="onThrottleInput('${loco.nodeId}', this.value)">
        <div class="throttle-presets">
          <button class="btn btn-lego-blue btn-sm" onclick="setLocoSpeed('${loco.nodeId}', -50)">REV 50%</button>
          <button class="btn btn-lego-red btn-sm" onclick="setLocoSpeed('${loco.nodeId}', 0)">🛑 STOP</button>
          <button class="btn btn-lego-green btn-sm" onclick="setLocoSpeed('${loco.nodeId}', 50)">FWD 50%</button>
        </div>
      </div>

      <div class="light-control-row">
        <button class="btn btn-lego-yellow-outline btn-sm" onclick="toggleLight('${loco.nodeId}', 'front')">💡 Front</button>
        <button class="btn btn-lego-yellow-outline btn-sm" onclick="toggleLight('${loco.nodeId}', 'cab')">💡 Cab</button>
        <button class="btn btn-lego-yellow-outline btn-sm" onclick="toggleLight('${loco.nodeId}', 'rear')">🚨 Rear</button>
        <button class="btn btn-lego-green btn-sm" onclick="startLearningLap('${loco.nodeId}')">🚀 Learn</button>
        <button class="btn btn-lego-red btn-sm" onclick="unpairLocomotive('${loco.nodeId}')" title="Unpair and release locomotive">🗑️ Unpair</button>
      </div>
    `;
  });
}

function onThrottleInput(nodeId, val) {
  const disp = document.getElementById(`throttleVal_${nodeId}`);
  if (disp) disp.textContent = `${val}%`;

  if (throttleSendTimers[nodeId]) clearTimeout(throttleSendTimers[nodeId]);
  throttleSendTimers[nodeId] = setTimeout(() => {
    sendLocoSpeed(nodeId, parseInt(val));
  }, 60);
}

function setLocoSpeed(nodeId, speed) {
  const slider = document.getElementById(`slider_${nodeId}`);
  if (slider) slider.value = speed;
  const disp = document.getElementById(`throttleVal_${nodeId}`);
  if (disp) disp.textContent = `${speed}%`;
  sendLocoSpeed(nodeId, speed);
}

function sendLocoSpeed(nodeId, speed) {
  sendWsCommand({
    cmd: "set_loco_speed",
    nodeId: nodeId,
    targetSpeed: parseInt(speed)
  });
}

function toggleLight(nodeId, zone) {
  sendWsCommand({
    cmd: "set_loco_lights",
    nodeId: nodeId,
    zone: zone,
    toggle: true
  });
}

function updateLocoCardTelemetry(d) {
  const s = document.getElementById(`valSpeed_${d.nodeId}`);
  const b = document.getElementById(`valBlock_${d.nodeId}`);
  const l = document.getElementById(`valLength_${d.nodeId}`);
  const bat = document.getElementById(`valBat_${d.nodeId}`);
  if (s) s.textContent = `${d.speed}%`;
  if (b) b.textContent = `#${d.currentBlock}`;
  if (l && d.measuredLengthCm) l.textContent = `${d.measuredLengthCm} cm`;
  if (bat) bat.textContent = `${(d.batteryMv / 1000).toFixed(2)}V`;
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
        <div style="background: rgba(255,255,255,0.04); border: 1px solid rgba(255,183,3,0.3); border-radius: 0; padding: 10px 14px; margin-top: 8px; display: flex; justify-content: space-between; align-items: center;">
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
    return;
  }
  if (empty) empty.style.display = "none";

  pairedTracks.forEach(trk => {
    let card = document.getElementById(`trackCard_${trk.nodeId}`);
    if (!card) {
      card = document.createElement("div");
      card.id = `trackCard_${trk.nodeId}`;
      card.className = "card track-card";
      grid.appendChild(card);
    }

    const isStraight = trk.switchState === "STRAIGHT";
    const isOcc = trk.beamOccupied;

    const stConf = (systemConfig.stations || []).find(s => s.nodeId === trk.nodeId);
    const swList = (stConf && stConf.switches && stConf.switches.length > 0)
      ? stConf.switches
      : [{ switchId: 1, gpioPin: 18, defaultPosition: 'STRAIGHT', description: 'Main Turnout' }];

    card.innerHTML = `
      <div class="card-brick-studs studs-blue"><span></span><span></span><span></span><span></span><span></span><span></span></div>
      <div class="card-header">
        <div class="card-title">
          <span class="icon track-avatar">🔀</span>
          <div>
            <h3>${trk.friendlyName || trk.nodeId}</h3>
            <span class="node-id">${trk.nodeId} &bull; ${trk.rssi || -50} dBm</span>
          </div>
        </div>
        <span class="badge ${trk.isOnline ? 'badge-lego-green' : 'badge-lego-red'}">
          ${trk.isOnline ? 'ONLINE' : 'OFFLINE'}
        </span>
      </div>

      <div class="telemetry-bar">
        <div class="telem-item telem-blue">
          <span class="label">SWITCH</span>
          <span class="value ${isStraight ? 'text-accent' : 'text-warning'}" id="valSwitch_${trk.nodeId}">
            ${trk.switchState || 'STRAIGHT'}
          </span>
        </div>
        <div class="telem-item telem-yellow">
          <span class="label">IR BEAM</span>
          <span class="value ${isOcc ? 'text-danger' : 'text-success'}" id="valBeam_${trk.nodeId}">
            ${isOcc ? 'OCCUPIED' : 'CLEAR'}
          </span>
        </div>
        <div class="telem-item telem-orange">
          <span class="label">TRAIN LEN</span>
          <span class="value" id="valTrkLen_${trk.nodeId}">${trk.lastMeasuredLengthCm || '--'} cm</span>
        </div>
      </div>

      <div class="switches-container" style="margin-top:14px; border-top:1px solid var(--border-color); padding-top:10px;">
        <span style="font-size:0.8rem; font-weight:700; color:var(--text-muted); text-transform:uppercase; letter-spacing:0.5px;">Turnout Switches (${swList.length})</span>
        ${swList.map((sw, swIdx) => `
          <div style="background:rgba(255,255,255,0.03); border:1px solid var(--border-color); border-radius: 0; padding:10px 12px; margin-top:8px; display:flex; justify-content:space-between; align-items:center;">
            <div>
              <strong style="color:var(--text); font-size:0.88rem;">Switch #${sw.switchId}</strong>
              <span class="badge badge-lego-yellow" style="font-size:0.7rem; margin-left:6px;">GPIO ${sw.gpioPin}</span>
              <span style="font-size:0.78rem; color:var(--text-muted); margin-left:6px;">${sw.description || ''}</span>
            </div>
            <div style="display:flex; gap:8px;">
              <button class="btn btn-sm btn-lego-grey" onclick="setTrackSwitch('${trk.nodeId}', 'STRAIGHT', ${swIdx})">➡️ Straight</button>
              <button class="btn btn-sm btn-lego-blue" onclick="setTrackSwitch('${trk.nodeId}', 'TURNOUT', ${swIdx})">🔀 Turnout</button>
            </div>
          </div>
        `).join("")}
      </div>
    `;
  });
}

function setTrackSwitch(nodeId, position, switchIndex = 0) {
  sendWsCommand({
    cmd: "set_switch",
    nodeId: nodeId,
    switchIndex: switchIndex,
    switchPosition: position
  });
}

function updateTrackCardTelemetry(d) {
  const sw = document.getElementById(`valSwitch_${d.nodeId}`);
  const bm = document.getElementById(`valBeam_${d.nodeId}`);
  const ln = document.getElementById(`valTrkLen_${d.nodeId}`);
  if (sw) {
    sw.textContent = d.switchState;
    sw.className = `value ${d.switchState === 'STRAIGHT' ? 'text-accent' : 'text-warning'}`;
  }
  if (bm) {
    bm.textContent = d.beamOccupied ? 'OCCUPIED' : 'CLEAR';
    bm.className = `value ${d.beamOccupied ? 'text-danger' : 'text-success'}`;
  }
  if (ln && d.lastMeasuredLengthCm) {
    ln.textContent = `${d.lastMeasuredLengthCm} cm`;
  }
}

// =================================================================
// Discovery Node Table
// =================================================================
function renderNodeTable(nodes) {
  const tbody = document.getElementById("nodeTableBody");
  if (!tbody) return;
  tbody.innerHTML = (nodes || []).map(n => `
    <tr>
      <td><code>${n.nodeId}</code></td>
      <td><strong>${n.friendlyName || '--'}</strong></td>
      <td><span class="badge ${n.nodeType === 'LOCO' ? 'badge-primary' : 'badge-accent'}">${n.nodeType}</span></td>
      <td>${n.rssi || -50} dBm</td>
      <td>
        ${n.isPaired 
          ? `<span class="badge badge-success">🔒 BONDED</span> <button class="btn btn-danger btn-sm" style="margin-left:6px;padding:2px 6px;font-size:0.75rem;" onclick="unpairLocomotive('${n.nodeId}')">🗑️ Unpair</button>` 
          : `<button class="btn btn-primary btn-sm" onclick="pairNode('${n.nodeId}')">🔗 Pair</button>`}
      </td>
      <td>${n.nodeType === 'LOCO' ? `Spd: ${n.speed || 0}%, Blk: #${n.currentBlock || 0}` : `Switch: ${n.switchState || 'STRAIGHT'}`}</td>
      <td>${n.lastSeenSec || 0}s ago</td>
      <td><span class="badge ${n.isOnline ? 'badge-success' : 'badge-danger'}">${n.isOnline ? 'ONLINE' : 'OFFLINE'}</span></td>
    </tr>
  `).join("");
}

function refreshNodesList() {
  fetchNodes();
  logMessage("system", "Node topology refreshed.");
}

// =================================================================
// CTC Synoptic Vector Map Renderer (SVG)
// =================================================================
async function fetchTopology() {
  try {
    const res = await fetch("/api/topology");
    if (res.ok) {
      circuitTopology = await res.json();
      renderSynopticMap();
    }
  } catch (e) {
    console.warn("Could not load topology:", e);
  }
}

function resetSynopticZoom() {
  const svg = document.getElementById("circuitMapSvg");
  if (svg) svg.setAttribute("viewBox", "0 0 1000 480");
}

function renderSynopticMap() {
  const tracksG = document.getElementById("svgTracksLayer");
  const switchesG = document.getElementById("svgSwitchesLayer");
  const stationsG = document.getElementById("svgStationsLayer");
  const beaconsG = document.getElementById("svgBeaconsLayer");
  const trainsG = document.getElementById("svgTrainsLayer");
  const statusPanel = document.getElementById("synopticStatusPanel");
  if (!tracksG) return;

  // Clear existing SVGs
  tracksG.innerHTML = "";
  switchesG.innerHTML = "";
  stationsG.innerHTML = "";
  beaconsG.innerHTML = "";
  trainsG.innerHTML = "";

  // 1. Draw Main Track Circuit (Orthogonal loop with rounded corners)
  // Outer Loop: X: 120 -> 880, Y: 140 -> 360
  const mainTrackPath = "M 220 140 L 780 140 A 100 100 0 0 1 880 240 L 880 260 A 100 100 0 0 1 780 360 L 220 360 A 100 100 0 0 1 120 260 L 120 240 A 100 100 0 0 1 220 140 Z";
  
  // Passing Loop / Siding (Diverges at X:260, parallel at Y:80, rejoins at X:740)
  const sidingTrackPath = "M 260 140 C 290 140, 310 80, 360 80 L 640 80 C 690 80, 710 140, 740 140";

  // Check track occupancy from smart track nodes
  let isMainOccupied = false;
  let isSidingOccupied = false;
  Object.values(liveTrackTelemetry).forEach(trk => {
    if (trk.beamOccupied) {
      if (trk.switchState === "TURNOUT") isSidingOccupied = true;
      else isMainOccupied = true;
    }
  });

  // Render Railroad ballast + rails
  tracksG.innerHTML = `
    <!-- Main Track Rails -->
    <path d="${mainTrackPath}" class="svg-track-rail" />
    <path d="${mainTrackPath}" class="svg-track-line ${isMainOccupied ? 'occupied' : ''}" id="pathMainTrack" />

    <!-- Siding Track Rails -->
    <path d="${sidingTrackPath}" class="svg-track-rail" style="stroke-width:10;" />
    <path d="${sidingTrackPath}" class="svg-track-siding ${isSidingOccupied ? 'occupied' : ''}" id="pathSidingTrack" />
  `;

  // 2. Draw Station Platforms
  stationsG.innerHTML = `
    <!-- Central Station Platform 1 (Main Line) -->
    <rect x="420" y="152" width="160" height="24" class="svg-station-platform" />
    <text x="500" y="168" class="svg-station-text">🚉 CENTRAL STATION (P1)</text>

    <!-- Passing Loop Platform 2 (Siding) -->
    <rect x="440" y="44" width="120" height="22" class="svg-station-platform" />
    <text x="500" y="58" class="svg-station-text">🚉 PASSING SIDING (P2)</text>
  `;

  // 3. Draw Switch Points & Interactive Divergence
  let swPos = "STRAIGHT";
  const trkNodes = Object.values(liveTrackTelemetry);
  if (trkNodes.length > 0 && trkNodes[0].switchState) {
    swPos = trkNodes[0].switchState;
  }
  const swActive = (swPos === "TURNOUT");

  switchesG.innerHTML = `
    <!-- West Turnout Switch Point -->
    <g class="svg-switch-node" onclick="toggleMainTurnout()" title="Click to toggle switch">
      <circle cx="260" cy="140" r="10" fill="${swActive ? '#ffb300' : '#00d2ff'}" stroke="#fff" stroke-width="2" />
      <text x="260" y="120" fill="#fff" font-size="10" text-anchor="middle" font-weight="700">SW-01 [${swPos}]</text>
    </g>

    <!-- East Converging Joint -->
    <g class="svg-switch-node" onclick="toggleMainTurnout()">
      <circle cx="740" cy="140" r="10" fill="${swActive ? '#ffb300' : '#00d2ff'}" stroke="#fff" stroke-width="2" />
      <text x="740" y="120" fill="#fff" font-size="10" text-anchor="middle" font-weight="700">SW-02</text>
    </g>

    <!-- Live Turnout Indicator Line -->
    <line x1="260" y1="140" x2="${swActive ? '290' : '290'}" y2="${swActive ? '120' : '140'}" stroke="${swActive ? '#ffb300' : '#00d2ff'}" stroke-width="4" stroke-linecap="round" />
  `;

  // 4. Draw Beacons
  const beacons = [
    { id: 10, x: 260, y: 140, role: "ROLE_APPROACH", name: "B10 (West Approach)" },
    { id: 11, x: 500, y: 140, role: "ROLE_STATION_ARRIVAL", name: "B11 (Platform 1 Stop)" },
    { id: 12, x: 740, y: 140, role: "ROLE_DEPARTURE", name: "B12 (East Exit)" },
    { id: 15, x: 500, y: 80,  role: "ROLE_SIDING", name: "B15 (Siding Tracker)" },
    { id: 20, x: 500, y: 360, role: "ROLE_LOCATOR", name: "B20 (South Localizer)" }
  ];

  beaconsG.innerHTML = beacons.map(b => `
    <g class="svg-beacon-marker" title="${b.name}">
      <circle cx="${b.x}" cy="${b.y}" r="6" fill="${b.role === 'ROLE_STATION_ARRIVAL' ? '#c77dff' : '#00f29b'}" stroke="#090d16" stroke-width="2" />
      <text x="${b.x}" y="${b.y + 18}" fill="#8a99b5" font-size="9" text-anchor="middle" font-family="var(--font-mono)">#${b.id}</text>
    </g>
  `).join("");

  // 5. Render Active Trains with Length-Scaled Markers
  const activeTrains = Object.values(liveTrainTelemetry);
  if (activeTrains.length === 0) {
    statusPanel.innerHTML = `
      <div style="font-size:0.85rem; color:var(--text-dim); text-align:center; width:100%;">
        No active train telemetry received. Power on locomotive to display live position.
      </div>
    `;
  } else {
    statusPanel.innerHTML = activeTrains.map(t => `
      <div class="train-status-badge">
        <span style="font-size:1.1rem;">🚂</span>
        <div>
          <strong style="color:var(--primary);">${t.nodeId}</strong>: 
          <span>Spd: <b>${t.speed || 0}%</b></span> &bull; 
          <span>Block: <b>#${t.currentBlock || 0}</b></span> &bull; 
          <span>Length: <b style="color:var(--warning);">${t.measuredLengthCm ? t.measuredLengthCm + ' cm' : 'Calibrating'}</b></span> &bull;
          <span>ETA: <b>${t.etaSeconds || 0}s</b></span>
        </div>
      </div>
    `).join("");

    activeTrains.forEach(t => {
      // Calculate coordinates along circuit loop
      let tx = 500, ty = 140;
      const blk = t.currentBlock || 10;
      if (blk === 10) { tx = 260; ty = 140; }
      else if (blk === 11) { tx = 500; ty = 140; }
      else if (blk === 12) { tx = 740; ty = 140; }
      else if (blk === 15) { tx = 500; ty = 80; }
      else if (blk === 20) { tx = 500; ty = 360; }
      else {
        // Interpolate along south curve
        tx = 350; ty = 360;
      }

      // Train visual length scaled proportionally to measured train length (cm)
      // Base: 25cm = 32px, 50cm = 56px, 100cm = 100px
      const trainLenCm = t.measuredLengthCm || 28;
      const trainPixelWidth = Math.max(30, Math.min(120, Math.round(trainLenCm * 1.15)));
      const trainPixelHeight = 16;

      trainsG.innerHTML += `
        <g transform="translate(${tx - trainPixelWidth/2}, ${ty - trainPixelHeight/2})">
          <rect width="${trainPixelWidth}" height="${trainPixelHeight}" class="svg-train-body" />
          <circle cx="8" cy="8" r="3" fill="#fff" />
          <circle cx="${trainPixelWidth - 8}" cy="8" r="3" fill="#ff3366" />
          <text x="${trainPixelWidth/2}" y="11" class="svg-train-text">${t.nodeId} (${trainLenCm}cm)</text>
        </g>
      `;
    });
  }
}

function toggleMainTurnout() {
  const trk = discoveredNodes.find(n => n.nodeType === "TRACK");
  const nodeId = trk ? trk.nodeId : "ALL";
  const current = (liveTrackTelemetry[nodeId] && liveTrackTelemetry[nodeId].switchState === "TURNOUT") ? "STRAIGHT" : "TURNOUT";
  setTrackSwitch(nodeId, current);
  logMessage("system", `🔀 Toggled Switch to: ${current}`);
}

// =================================================================
// Advanced Configuration Studio (3 Sub-Tabs)
// =================================================================
async function fetchConfig() {
  try {
    const res = await fetch("/api/config");
    if (res.ok) {
      systemConfig = await res.json();
    }
  } catch (e) {
    console.warn("Using local configuration fallback:", e);
  }
  renderGlobalConfig(systemConfig.system || {});
  renderLocoConfigs(systemConfig.locomotives || []);
  renderStationConfigs(systemConfig.stations || []);
}

function renderGlobalConfig(sys) {
  const nameEl = document.getElementById("cfgLayoutName");
  const ssidEl = document.getElementById("cfgWifiSsid");
  const chEl = document.getElementById("cfgWifiChannel");
  const safeEl = document.getElementById("cfgHeadwaySafeSec");
  const cautionEl = document.getElementById("cfgHeadwayCautionSec");
  const trimEl = document.getElementById("cfgHeadwaySpeedTrimPct");

  if (nameEl) nameEl.value = sys.layoutName || "Lego Central Layout";
  if (ssidEl) ssidEl.value = sys.wifiSsid || "LegoTrain_Master";
  if (chEl) chEl.value = sys.wifiChannel || 1;
  if (safeEl) safeEl.value = sys.headwaySafeSec || 12;
  if (cautionEl) cautionEl.value = sys.headwayCautionSec || 6;
  if (trimEl) trimEl.value = sys.headwaySpeedTrimPct || 40;
}

async function saveGlobalConfig() {
  if (!systemConfig.system) systemConfig.system = {};
  systemConfig.system.layoutName = document.getElementById("cfgLayoutName").value.trim();
  systemConfig.system.wifiSsid = document.getElementById("cfgWifiSsid").value.trim();
  systemConfig.system.wifiChannel = parseInt(document.getElementById("cfgWifiChannel").value) || 1;
  systemConfig.system.headwaySafeSec = parseInt(document.getElementById("cfgHeadwaySafeSec").value) || 12;
  systemConfig.system.headwayCautionSec = parseInt(document.getElementById("cfgHeadwayCautionSec").value) || 6;
  systemConfig.system.headwaySpeedTrimPct = parseInt(document.getElementById("cfgHeadwaySpeedTrimPct").value) || 40;

  const pwd = document.getElementById("cfgWifiPassword").value;
  if (pwd) systemConfig.system.wifiPassword = pwd;

  await postConfigUpdate();
  logMessage("system", "✅ Global Layout & Headway settings saved to Master Gateway.");
  alert("Global layout and Wi-Fi configuration saved successfully!");
}

function renderLocoConfigs(locos) {
  const container = document.getElementById("locoConfigContainer");
  if (!container) return;

  // Merge discovered locomotives
  const allLocos = [...locos];
  discoveredNodes.filter(n => n.nodeType === "LOCO").forEach(d => {
    if (!allLocos.some(l => l.nodeId === d.nodeId)) {
      allLocos.push({
        nodeId: d.nodeId,
        name: d.friendlyName || d.nodeId,
        maxSpeed: 70,
        learningSpeed: 35,
        accelRate: 40.0,
        decelRate: 60.0,
        brakeOffsetMs: 450,
        dwellTimeSec: 12,
        lightMode: "AUTO",
        measuredLengthCm: 28
      });
    }
  });

  if (allLocos.length === 0) {
    container.innerHTML = `
      <div style="background:var(--bg-card);border:1px solid var(--border-color);border-radius:0;padding:20px;text-align:center;color:var(--text-dim);">
        No locomotives discovered yet. Power on a locomotive and click <b>Scan for Locomotives</b>.
      </div>
    `;
    return;
  }

  container.innerHTML = allLocos.map((loco, idx) => `
    <div class="config-card" id="locoCfgCard_${idx}">
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;">
        <h3 style="display:flex; align-items:center; gap:8px;">
          <span>🚂</span>
          <span>${loco.name || loco.nodeId}</span>
          <code style="font-size:0.75rem; color:var(--text-muted); background:var(--bg-input); padding:2px 6px; border-radius:0;">${loco.nodeId}</code>
        </h3>
        <div style="display:flex; gap:6px;">
          <button class="btn btn-sm btn-primary" onclick="startLearningLap('${loco.nodeId}')">🚀 Learning Lap</button>
          <button class="btn btn-sm btn-secondary" onclick="resetCalibration('${loco.nodeId}')">🔄 Reset</button>
          <button class="btn btn-sm btn-danger" onclick="unpairLocomotive('${loco.nodeId}')">🗑️ Remove / Unpair</button>
        </div>
      </div>

      <div class="config-grid-layout">
        <div class="form-group">
          <label>Friendly Train Name:</label>
          <input type="text" class="form-control" id="locoName_${idx}" value="${loco.name || ''}" placeholder="e.g. Cargo Express">
        </div>

        <div class="form-group">
          <label>Max Cruising Throttle (%):</label>
          <input type="number" class="form-control" id="locoMaxSpeed_${idx}" min="20" max="100" value="${loco.maxSpeed || 70}">
        </div>

        <div class="form-group">
          <label>Calibration Speed (Learning Lap %):</label>
          <input type="number" class="form-control" id="locoLearnSpeed_${idx}" min="20" max="60" value="${loco.learningSpeed || 35}">
          <span class="field-hint">Standardized velocity (default: 35%) for loop closure learning.</span>
        </div>

        <div class="form-group">
          <label>Acceleration Rate (%/s):</label>
          <input type="number" class="form-control" id="locoAccel_${idx}" min="10" max="100" value="${loco.accelRate || 40}">
        </div>

        <div class="form-group">
          <label>Deceleration Rate (%/s):</label>
          <input type="number" class="form-control" id="locoDecel_${idx}" min="10" max="150" value="${loco.decelRate || 60}">
        </div>

        <div class="form-group">
          <label>Precision Braking Offset (ms):</label>
          <input type="number" class="form-control" id="locoBrakeOffset_${idx}" min="0" max="2000" value="${loco.brakeOffsetMs || 450}">
          <span class="field-hint">Anticipatory braking curve offset for smooth station centering.</span>
        </div>

        <div class="form-group">
          <label>Platform Dwell Duration (seconds):</label>
          <input type="number" class="form-control" id="locoDwell_${idx}" min="2" max="60" value="${loco.dwellTimeSec || 12}">
        </div>

        <div class="form-group">
          <label>Physical Train Length (cm):</label>
          <input type="number" class="form-control" id="locoLen_${idx}" min="10" max="250" value="${loco.measuredLengthCm || 28}">
          <span class="field-hint">Auto-measured via IR beam break or manually calibrated.</span>
        </div>
      </div>

      <div style="text-align:right; margin-top:10px;">
        <button class="btn btn-primary btn-sm" onclick="saveLocoConfig(${idx}, '${loco.nodeId}')">
          💾 Save Locomotive Settings
        </button>
      </div>
    </div>
  `).join("");
}

async function saveLocoConfig(idx, nodeId) {
  if (!systemConfig.locomotives) systemConfig.locomotives = [];
  let item = systemConfig.locomotives.find(l => l.nodeId === nodeId);
  if (!item) {
    item = { nodeId: nodeId };
    systemConfig.locomotives.push(item);
  }

  item.name = document.getElementById(`locoName_${idx}`).value.trim();
  item.maxSpeed = parseInt(document.getElementById(`locoMaxSpeed_${idx}`).value) || 70;
  item.learningSpeed = parseInt(document.getElementById(`locoLearnSpeed_${idx}`).value) || 35;
  item.accelRate = parseFloat(document.getElementById(`locoAccel_${idx}`).value) || 40.0;
  item.decelRate = parseFloat(document.getElementById(`locoDecel_${idx}`).value) || 60.0;
  item.brakeOffsetMs = parseInt(document.getElementById(`locoBrakeOffset_${idx}`).value) || 450;
  item.dwellTimeSec = parseInt(document.getElementById(`locoDwell_${idx}`).value) || 12;
  item.measuredLengthCm = parseInt(document.getElementById(`locoLen_${idx}`).value) || 28;

  await postConfigUpdate();
  logMessage("system", `✅ Saved parameters for Locomotive ${nodeId}.`);
  alert(`Locomotive ${nodeId} parameters updated!`);
}

function renderStationConfigs(stations) {
  const container = document.getElementById("stationConfigContainer");
  if (!container) return;

  const allStations = [...stations];
  discoveredNodes.filter(n => n.nodeType === "TRACK").forEach(d => {
    if (!allStations.some(s => s.nodeId === d.nodeId)) {
      allStations.push({
        nodeId: d.nodeId,
        name: d.friendlyName || d.nodeId,
        dwellTimeSec: 10,
        autoDivertOnOccupied: true,
        sidingCapacityCm: 65,
        switches: [
          { switchId: 1, gpioPin: 18, servoStraightAngle: 75, servoTurnoutAngle: 105, defaultPosition: "STRAIGHT", description: "Main Turnout" }
        ],
        beacons: [
          { beaconId: 10, gpioPin: 19, role: "ROLE_LOCATOR", description: "Approach Localizer", measureTrainLength: true },
          { beaconId: 11, gpioPin: 19, role: "ROLE_STATION_ARRIVAL", description: "Platform 1 Stop", measureTrainLength: true }
        ]
      });
    }
  });

  if (allStations.length === 0) {
    container.innerHTML = `
      <div style="background:var(--bg-card);border:1px solid var(--border-color);border-radius:0;padding:20px;text-align:center;color:var(--text-dim);">
        No stations discovered yet. Power on a track station and click <b>Scan for Stations</b>.
      </div>
    `;
    return;
  }

  container.innerHTML = allStations.map((st, sIdx) => {
    const swList = (st.switches && st.switches.length > 0) ? st.switches : [
      { switchId: 1, gpioPin: st.switchGpioPin || 18, servoStraightAngle: st.servoStraightAngle || 75, servoTurnoutAngle: st.servoTurnoutAngle || 105, defaultPosition: st.defaultSwitch || "STRAIGHT", description: "Main Turnout" }
    ];
    const beacons = st.beacons || [];
    return `
      <div class="config-card" id="stationCfgCard_${sIdx}">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;">
          <h3 style="display:flex; align-items:center; gap:8px;">
            <span>🚉</span>
            <span>${st.name || st.nodeId}</span>
            <code style="font-size:0.75rem; color:var(--text-muted); background:var(--bg-input); padding:2px 6px; border-radius:0;">${st.nodeId}</code>
          </h3>
        </div>

        <div class="config-grid-layout">
          <div class="form-group">
            <label>Station Friendly Name:</label>
            <input type="text" class="form-control" id="stName_${sIdx}" value="${st.name || ''}" placeholder="e.g. Grand Central">
          </div>

          <div class="form-group">
            <label>Platform Dwell Countdown (seconds):</label>
            <input type="number" class="form-control" id="stDwell_${sIdx}" min="0" max="120" value="${st.dwellTimeSec || 10}">
          </div>

          <div class="form-group">
            <label>Passing Siding Physical Capacity (cm):</label>
            <input type="number" class="form-control" id="stSidingCap_${sIdx}" min="20" max="250" value="${st.sidingCapacityCm || 65}">
            <span class="field-hint">Turnout diversion is inhibited if train length exceeds this limit.</span>
          </div>

          <div class="form-group" style="display:flex; align-items:flex-end;">
            <label style="display:flex; align-items:center; gap:8px; cursor:pointer; padding-bottom:10px;">
              <input type="checkbox" id="stAutoDivert_${sIdx}" ${st.autoDivertOnOccupied ? 'checked' : ''}>
              <span><b>Auto Siding Routing:</b> Divert trains when platform line is occupied.</span>
            </label>
          </div>
        </div>

        <!-- Multi-Switches Manager -->
        <div style="margin-top:16px; border-top:1px solid var(--border-color); padding-top:14px;">
          <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:8px;">
            <h4 style="font-size:0.92rem; color:var(--text); margin:0;">
              🔀 Configured Turnout Switches (${swList.length})
            </h4>
            <button class="btn btn-outline btn-sm" onclick="addSwitchToStation(${sIdx})">+ Add Switch</button>
          </div>
          <span class="field-hint" style="margin-bottom:10px;">
            Each turnout switch has a dedicated <b>Servo GPIO</b> and independent straight/turnout angle calibration.
          </span>

          <div class="table-container">
            <table class="beacon-config-table">
              <thead>
                <tr>
                  <th style="width:70px;">ID</th>
                  <th style="width:75px;">GPIO</th>
                  <th style="width:120px;">Default Pos</th>
                  <th style="width:80px;">Straight (°)</th>
                  <th style="width:80px;">Turnout (°)</th>
                  <th>Description</th>
                  <th style="width:110px; text-align:center;">Test</th>
                  <th style="width:40px;"></th>
                </tr>
              </thead>
              <tbody>
                ${swList.map((sw, swIdx) => `
                  <tr>
                    <td>
                      <input type="number" class="input-inline" style="width:60px;" id="swId_${sIdx}_${swIdx}" value="${sw.switchId || (swIdx+1)}" min="1" max="16">
                    </td>
                    <td>
                      <input type="number" class="input-inline" style="width:68px;" id="swGpio_${sIdx}_${swIdx}" value="${sw.gpioPin !== undefined ? sw.gpioPin : 18}" min="0" max="48" title="Servo PWM GPIO">
                    </td>
                    <td>
                      <select class="input-inline" style="width:100%;" id="swDefPos_${sIdx}_${swIdx}">
                        <option value="STRAIGHT" ${sw.defaultPosition === 'STRAIGHT' ? 'selected' : ''}>STRAIGHT</option>
                        <option value="TURNOUT" ${sw.defaultPosition === 'TURNOUT' ? 'selected' : ''}>TURNOUT</option>
                      </select>
                    </td>
                    <td>
                      <input type="number" class="input-inline" style="width:70px;" id="swAngStr_${sIdx}_${swIdx}" value="${sw.servoStraightAngle || 75}" min="0" max="180">
                    </td>
                    <td>
                      <input type="number" class="input-inline" style="width:70px;" id="swAngTur_${sIdx}_${swIdx}" value="${sw.servoTurnoutAngle || 105}" min="0" max="180">
                    </td>
                    <td>
                      <input type="text" class="input-inline" style="width:100%;" id="swDesc_${sIdx}_${swIdx}" value="${sw.description || ''}" placeholder="e.g. West Main Turnout">
                    </td>
                    <td style="text-align:center; white-space:nowrap;">
                      <button class="btn btn-sm btn-outline" style="padding:2px 6px;" title="Test Straight" onclick="setTrackSwitch('${st.nodeId}', 'STRAIGHT', ${swIdx})">➡️</button>
                      <button class="btn btn-sm btn-primary" style="padding:2px 6px; margin-left:4px;" title="Test Turnout" onclick="setTrackSwitch('${st.nodeId}', 'TURNOUT', ${swIdx})">🔀</button>
                    </td>
                    <td>
                      <button class="btn btn-sm btn-danger" style="padding:2px 6px;" onclick="removeSwitchFromStation(${sIdx}, ${swIdx})">✕</button>
                    </td>
                  </tr>
                `).join("")}
              </tbody>
            </table>
          </div>
        </div>

        <!-- Multi-Beacon Allocation Section -->
        <div style="margin-top:16px; border-top:1px solid var(--border-color); padding-top:14px;">
          <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:8px;">
            <h4 style="font-size:0.92rem; color:var(--text); margin:0;">
              📍 Configured Station Beacons &amp; Localizers (${beacons.length})
            </h4>
            <button class="btn btn-outline btn-sm" onclick="addBeaconToStation(${sIdx})">+ Add Beacon</button>
          </div>
          <span class="field-hint" style="margin-bottom:10px;">
            Rule: Exactly <b>1 Station Arrival</b> beacon at the platform (triggers dwell). Other beacons are <b>Localizers</b> for tracking and train length calculation.
          </span>

          <div class="table-container">
            <table class="beacon-config-table">
              <thead>
                <tr>
                  <th style="width:70px;">Beacon ID</th>
                  <th style="width:75px;">GPIO</th>
                  <th style="width:180px;">Role</th>
                  <th>Description / Sector</th>
                  <th style="width:100px; text-align:center;">Measure Length</th>
                  <th style="width:40px;"></th>
                </tr>
              </thead>
              <tbody id="beaconTableBody_${sIdx}">
                ${beacons.map((b, bIdx) => `
                  <tr>
                    <td>
                      <input type="number" class="input-inline" style="width:60px;" id="bId_${sIdx}_${bIdx}" value="${b.beaconId}">
                    </td>
                    <td>
                      <input type="number" class="input-inline" style="width:68px;" id="bGpio_${sIdx}_${bIdx}" value="${b.gpioPin !== undefined ? b.gpioPin : 19}" min="0" max="48" title="IR Transmitter/Sensor GPIO">
                    </td>
                    <td>
                      <select class="input-inline" style="width:100%;" id="bRole_${sIdx}_${bIdx}">
                        <option value="ROLE_STATION_ARRIVAL" ${b.role === 'ROLE_STATION_ARRIVAL' || b.role === 1 ? 'selected' : ''}>🚉 Station Arrival (Stop)</option>
                        <option value="ROLE_LOCATOR" ${b.role === 'ROLE_LOCATOR' || b.role === 0 ? 'selected' : ''}>📍 Localizer (Track Only)</option>
                        <option value="ROLE_APPROACH" ${b.role === 'ROLE_APPROACH' || b.role === 2 ? 'selected' : ''}>⚠️ Approach Warning</option>
                        <option value="ROLE_SIDING" ${b.role === 'ROLE_SIDING' || b.role === 4 ? 'selected' : ''}>🔀 Siding Waypoint</option>
                      </select>
                    </td>
                    <td>
                      <input type="text" class="input-inline" style="width:100%;" id="bDesc_${sIdx}_${bIdx}" value="${b.description || ''}" placeholder="e.g. Sector 1 Approach">
                    </td>
                    <td style="text-align:center;">
                      <input type="checkbox" id="bLen_${sIdx}_${bIdx}" ${b.measureTrainLength !== false ? 'checked' : ''} title="Measure train length via optical beam break">
                    </td>
                    <td>
                      <button class="btn btn-sm btn-danger" style="padding:2px 6px;" onclick="removeBeaconFromStation(${sIdx}, ${bIdx})">✕</button>
                    </td>
                  </tr>
                `).join("")}
              </tbody>
            </table>
          </div>
        </div>

        <div style="text-align:right; margin-top:16px;">
          <button class="btn btn-primary btn-sm" onclick="saveStationConfig(${sIdx}, '${st.nodeId}')">
            💾 Save Station Settings
          </button>
        </div>
      </div>
    `;
  }).join("");
}

function addSwitchToStation(sIdx) {
  if (!systemConfig.stations[sIdx]) return;
  if (!systemConfig.stations[sIdx].switches) systemConfig.stations[sIdx].switches = [];
  const swList = systemConfig.stations[sIdx].switches;
  const nextId = (swList.length > 0)
    ? Math.max(...swList.map(s => s.switchId)) + 1
    : 1;

  swList.push({
    switchId: nextId,
    gpioPin: (nextId === 1 ? 18 : 18 + nextId),
    servoStraightAngle: 75,
    servoTurnoutAngle: 105,
    defaultPosition: "STRAIGHT",
    description: `Turnout #${nextId}`
  });
  renderStationConfigs(systemConfig.stations);
  if (discoveredNodes) renderTrackCards(discoveredNodes.filter(n => n.nodeType === "TRACK"));
}

function removeSwitchFromStation(sIdx, swIdx) {
  if (!systemConfig.stations[sIdx] || !systemConfig.stations[sIdx].switches) return;
  systemConfig.stations[sIdx].switches.splice(swIdx, 1);
  renderStationConfigs(systemConfig.stations);
  if (discoveredNodes) renderTrackCards(discoveredNodes.filter(n => n.nodeType === "TRACK"));
}

function addBeaconToStation(sIdx) {
  if (!systemConfig.stations[sIdx]) return;
  if (!systemConfig.stations[sIdx].beacons) systemConfig.stations[sIdx].beacons = [];
  const nextId = (systemConfig.stations[sIdx].beacons.length > 0)
    ? Math.max(...systemConfig.stations[sIdx].beacons.map(b => b.beaconId)) + 1
    : 10;

  systemConfig.stations[sIdx].beacons.push({
    beaconId: nextId,
    gpioPin: 19,
    role: "ROLE_LOCATOR",
    description: `Sector ${nextId} Localizer`,
    measureTrainLength: true
  });
  renderStationConfigs(systemConfig.stations);
}

function removeBeaconFromStation(sIdx, bIdx) {
  if (!systemConfig.stations[sIdx] || !systemConfig.stations[sIdx].beacons) return;
  systemConfig.stations[sIdx].beacons.splice(bIdx, 1);
  renderStationConfigs(systemConfig.stations);
}

async function saveStationConfig(sIdx, nodeId) {
  if (!systemConfig.stations) systemConfig.stations = [];
  let item = systemConfig.stations.find(s => s.nodeId === nodeId);
  if (!item) {
    item = { nodeId: nodeId };
    systemConfig.stations.push(item);
  }

  item.name = document.getElementById(`stName_${sIdx}`).value.trim();
  item.dwellTimeSec = parseInt(document.getElementById(`stDwell_${sIdx}`).value) || 10;
  item.sidingCapacityCm = parseInt(document.getElementById(`stSidingCap_${sIdx}`).value) || 65;
  item.autoDivertOnOccupied = document.getElementById(`stAutoDivert_${sIdx}`).checked;

  // Collect switches table
  const swRows = document.querySelectorAll(`[id^="swId_${sIdx}_"]`);
  const updatedSwitches = [];
  swRows.forEach((r, swIdx) => {
    const swId = parseInt(document.getElementById(`swId_${sIdx}_${swIdx}`).value) || (swIdx + 1);
    const swGpio = parseInt(document.getElementById(`swGpio_${sIdx}_${swIdx}`).value) || 18;
    const defPos = document.getElementById(`swDefPos_${sIdx}_${swIdx}`).value || "STRAIGHT";
    const angStr = parseInt(document.getElementById(`swAngStr_${sIdx}_${swIdx}`).value) || 75;
    const angTur = parseInt(document.getElementById(`swAngTur_${sIdx}_${swIdx}`).value) || 105;
    const descEl = document.getElementById(`swDesc_${sIdx}_${swIdx}`);
    const descVal = descEl ? descEl.value.trim() : "";
    updatedSwitches.push({
      switchId: swId,
      gpioPin: swGpio,
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

  // Collect beacons table
  const beaconRows = document.querySelectorAll(`[id^="bId_${sIdx}_"]`);
  const updatedBeacons = [];
  let arrivalCount = 0;

  beaconRows.forEach((inputEl, bIdx) => {
    const role = document.getElementById(`bRole_${sIdx}_${bIdx}`).value;
    if (role === "ROLE_STATION_ARRIVAL") arrivalCount++;
    const bGpio = parseInt(document.getElementById(`bGpio_${sIdx}_${bIdx}`).value) || 19;

    updatedBeacons.push({
      beaconId: parseInt(inputEl.value) || (10 + bIdx),
      gpioPin: bGpio,
      role: role,
      description: document.getElementById(`bDesc_${sIdx}_${bIdx}`).value.trim(),
      measureTrainLength: document.getElementById(`bLen_${sIdx}_${bIdx}`).checked
    });
  });

  if (arrivalCount > 1) {
    alert("Warning: Exactly ONE beacon per station should be ROLE_STATION_ARRIVAL (Platform Stop). Other beacons must be Localizers.");
  }

  item.beacons = updatedBeacons;
  item.beaconCount = updatedBeacons.length;

  await postConfigUpdate();
  if (discoveredNodes) renderTrackCards(discoveredNodes.filter(n => n.nodeType === "TRACK"));
  logMessage("system", `✅ Saved parameters, ${updatedSwitches.length} switches and ${updatedBeacons.length} beacons for Station ${nodeId}.`);
  alert(`Station ${nodeId} parameters saved!`);
}

async function postConfigUpdate() {
  try {
    await fetch("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(systemConfig)
    });
  } catch (e) {
    console.error("Config save error:", e);
  }
}

async function saveActiveConfig() {
  await saveGlobalConfig();
}

// =================================================================
// Learning Lap Controls
// =================================================================
async function startLearningLap(targetLocoId) {
  logMessage("system", `🚀 Triggering Learning Lap for: ${targetLocoId}...`);
  try {
    const res = await fetch("/api/learning/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ targetLocoId: targetLocoId, calibrationSpeed: 35 })
    });
    if (res.ok) {
      logMessage("step", `🏁 Learning Lap sequence launched! Train will calibrate inter-beacon timings.`);
    }
  } catch (e) {
    logMessage("system", `Error triggering learning lap: ${e.message}`);
  }
}

async function resetCalibration(targetLocoId) {
  logMessage("system", `🔄 Resetting circuit calibration for: ${targetLocoId}...`);
  try {
    await fetch("/api/learning/reset", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ targetLocoId: targetLocoId })
    });
    logMessage("system", `Calibration reset. Train ready for fresh learning lap.`);
  } catch (e) {
    console.warn(e);
  }
}

// =================================================================
// Event Console Logging
// =================================================================
function logMessage(category, text) {
  const consoleEl = document.getElementById("eventLogConsole");
  if (!consoleEl) return;

  const now = new Date();
  const timeStr = now.toTimeString().split(" ")[0] + "." + String(now.getMilliseconds()).padStart(3, "0");

  const line = document.createElement("div");
  line.className = `log-line ${category}`;
  line.textContent = `[${timeStr}] ${text}`;

  consoleEl.appendChild(line);
  consoleEl.scrollTop = consoleEl.scrollHeight;

  // Trim old logs
  while (consoleEl.children.length > 200) {
    consoleEl.removeChild(consoleEl.firstChild);
  }
}

function clearLogs() {
  const consoleEl = document.getElementById("eventLogConsole");
  if (consoleEl) {
    consoleEl.innerHTML = `<div class="log-line system">[SYSTEM] Event log cleared.</div>`;
  }
}
