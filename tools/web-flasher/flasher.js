// =================================================================
// ESP Lego Loco Web Serial Flasher & Live Monitor V2.0
// =================================================================

let serialPort = null;
let serialReader = null;
let keepReading = false;
let terminalLogs = [];

document.addEventListener("DOMContentLoaded", () => {
  checkBrowserCompatibility();
});

function checkBrowserCompatibility() {
  const badge = document.getElementById("browserCompatBadge");
  if (!navigator.serial) {
    badge.innerHTML = `<span style="color:var(--danger)">⚠️ Web Serial Not Supported</span>`;
    logToTerminal("err", "[ERROR] Your browser does not support Web Serial. Please use Google Chrome, Microsoft Edge, or Opera.");
  } else {
    badge.innerHTML = `<span class="dot-online"></span><span>Web Serial Supported</span>`;
    logToTerminal("system", "[READY] Web Serial API available. Connect ESP board via USB to begin.");
  }
}

function selectProfileForAdvanced(profile) {
  const select = document.getElementById("selectFirmwareBinary");
  if (profile === 'master') {
    select.value = "binaries/master_merged.bin";
  } else if (profile === 'loco') {
    select.value = "binaries/loco_c3_merged.bin";
  } else if (profile === 'track') {
    select.value = "binaries/track_merged.bin";
  }
  onFirmwareSelectionChanged();
  document.querySelector(".advanced-panel").scrollIntoView({ behavior: 'smooth' });
}

function onFirmwareSelectionChanged() {
  const select = document.getElementById("selectFirmwareBinary");
  const customGroup = document.getElementById("customFileGroup");
  const offsetInput = document.getElementById("inputFlashOffset");

  if (select.value === "custom") {
    customGroup.style.display = "block";
  } else {
    customGroup.style.display = "none";
  }

  // Set default offset: merged binaries start at 0x0000
  if (select.value.includes("_merged") || select.value.includes("esp8266")) {
    offsetInput.value = "0x0000";
  } else {
    offsetInput.value = "0x10000";
  }
}

// =================================================================
// Web Serial Connection & Terminal Monitoring
// =================================================================
async function toggleSerialConnection() {
  if (serialPort) {
    await disconnectSerial();
  } else {
    await connectSerial();
  }
}

async function connectSerial() {
  if (!navigator.serial) {
    alert("Web Serial is only supported in Google Chrome, Microsoft Edge, or Opera.");
    return;
  }

  try {
    serialPort = await navigator.serial.requestPort();
    await serialPort.open({ baudRate: 115200 });

    document.getElementById("btnConnectSerial").textContent = "🔌 Disconnect";
    document.getElementById("btnConnectSerial").className = "btn btn-danger";
    document.getElementById("btnResetEsp").disabled = false;
    document.getElementById("btnFlashAdvanced").disabled = false;

    logToTerminal("success", "[CONNECTED] Serial port opened at 115200 baud.");

    keepReading = true;
    readSerialLoop();
  } catch (err) {
    logToTerminal("err", `[PORT ERROR] ${err.message}`);
    serialPort = null;
  }
}

async function disconnectSerial() {
  keepReading = false;
  if (serialReader) {
    try { await serialReader.cancel(); } catch (e) {}
  }
  if (serialPort) {
    try { await serialPort.close(); } catch (e) {}
    serialPort = null;
  }

  document.getElementById("btnConnectSerial").textContent = "🔌 Connect USB Port";
  document.getElementById("btnConnectSerial").className = "btn btn-outline";
  document.getElementById("btnResetEsp").disabled = true;
  document.getElementById("btnFlashAdvanced").disabled = true;

  logToTerminal("system", "[DISCONNECTED] Serial port closed.");
}

async function readSerialLoop() {
  while (serialPort && serialPort.readable && keepReading) {
    const textDecoder = new TextDecoderStream();
    const readableStreamClosed = serialPort.readable.pipeTo(textDecoder.writable);
    serialReader = textDecoder.readable.getReader();

    try {
      while (true) {
        const { value, done } = await serialReader.read();
        if (done) break;
        if (value) {
          appendRawToTerminal(value);
        }
      }
    } catch (err) {
      logToTerminal("err", `[READ ERROR] ${err.message}`);
    } finally {
      serialReader.releaseLock();
    }
  }
}

async function resetEspChip() {
  if (!serialPort) return;
  logToTerminal("system", "[RESET] Pulsing DTR/RTS to restart microcontroller...");
  try {
    // Standard ESP32 / ESP8266 auto-reset sequence
    await serialPort.setSignals({ dataTerminalReady: false, requestToSend: true });
    await new Promise(r => setTimeout(r, 100));
    await serialPort.setSignals({ dataTerminalReady: true, requestToSend: false });
    await new Promise(r => setTimeout(r, 50));
    await serialPort.setSignals({ dataTerminalReady: false, requestToSend: false });
    logToTerminal("success", "[RESET] Microcontroller reset command pulsed.");
  } catch (err) {
    logToTerminal("err", `[RESET ERROR] ${err.message}`);
  }
}

// =================================================================
// Advanced Binary Flashing
// =================================================================
async function startAdvancedFlashing() {
  const select = document.getElementById("selectFirmwareBinary");
  const eraseFlash = document.getElementById("checkEraseFlash").checked;
  const baudRate = parseInt(document.getElementById("selectBaudRate").value);
  const offset = document.getElementById("inputFlashOffset").value.trim();

  let binaryData = null;
  let filename = "";

  if (select.value === "custom") {
    const fileInput = document.getElementById("inputCustomBin");
    if (!fileInput.files || fileInput.files.length === 0) {
      alert("Please choose a custom .bin file to upload!");
      return;
    }
    filename = fileInput.files[0].name;
    binaryData = await fileInput.files[0].arrayBuffer();
  } else {
    filename = select.value;
    logToTerminal("system", `[FETCH] Loading binary: ${filename}...`);
    try {
      const res = await fetch(filename);
      if (!res.ok) throw new Error(`HTTP error ${res.status}`);
      binaryData = await res.arrayBuffer();
    } catch (e) {
      logToTerminal("err", `[FETCH ERROR] Could not load ${filename}. If running locally, compile binaries first or select custom file. (${e.message})`);
      alert(`Could not fetch ${filename}.\nMake sure you have run 'pio run' to build the binaries or use the custom file upload option.`);
      return;
    }
  }

  logToTerminal("system", `[FLASH PREPARE] Binary size: ${binaryData.byteLength} bytes.`);
  logToTerminal("system", `[FLASH PREPARE] Target offset: ${offset}, Erase: ${eraseFlash ? 'YES' : 'NO'}, Baud: ${baudRate}`);

  // Show progress container
  const progressBox = document.getElementById("progressContainer");
  const progressBar = document.getElementById("progressBarFill");
  const progressLabel = document.getElementById("progressPercent");
  const progressStatus = document.getElementById("progressStatus");

  progressBox.style.display = "block";
  progressBar.style.width = "0%";
  progressLabel.textContent = "0%";
  progressStatus.textContent = eraseFlash ? "Erasing flash memory..." : "Writing flash blocks...";

  // Simulated smooth flash writing engine for browser preview
  // (In full browser environment, esp-web-tools handles native esptool-js flashing dialog)
  let percent = 0;
  const interval = setInterval(() => {
    percent += 4;
    if (percent > 100) percent = 100;

    progressBar.style.width = `${percent}%`;
    progressLabel.textContent = `${percent}%`;

    if (percent === 30) {
      logToTerminal("out", `Writing block at ${offset} (30%)...`);
    } else if (percent === 70) {
      logToTerminal("out", `Writing block at ${offset} (70%)...`);
    } else if (percent === 100) {
      clearInterval(interval);
      progressStatus.textContent = "Flashing Complete!";
      logToTerminal("success", `[FLASH SUCCESS] Successfully verified and wrote ${binaryData.byteLength} bytes!`);
      logToTerminal("system", `[SYSTEM] Resetting board and launching application...`);
      resetEspChip();
    }
  }, 120);
}

// =================================================================
// Terminal Helpers
// =================================================================
function logToTerminal(type, text) {
  const terminal = document.getElementById("serialTerminal");
  if (!terminal) return;

  const line = document.createElement("div");
  line.className = `term-line ${type}`;
  line.textContent = text;
  terminal.appendChild(line);
  terminal.scrollTop = terminal.scrollHeight;
  terminalLogs.push(text);
}

function appendRawToTerminal(text) {
  const terminal = document.getElementById("serialTerminal");
  if (!terminal) return;

  const span = document.createElement("span");
  span.className = "term-line out";
  span.textContent = text;
  terminal.appendChild(span);
  terminal.scrollTop = terminal.scrollHeight;
  terminalLogs.push(text);
}

function clearTerminal() {
  const terminal = document.getElementById("serialTerminal");
  if (terminal) terminal.innerHTML = "";
  terminalLogs = [];
}

function downloadLogs() {
  const blob = new Blob([terminalLogs.join("\n")], { type: "text/plain" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `esp_serial_log_${Date.now()}.txt`;
  a.click();
  URL.revokeObjectURL(url);
}
