const streamEl = document.getElementById("stream");
const statusLine = document.getElementById("status-line");
const safeControls = document.getElementById("safe-controls");
const advancedControls = document.getElementById("advanced-controls");
const dangerousControls = document.getElementById("dangerous-controls");
const presetGrid = document.getElementById("preset-grid");
const dangerTools = document.getElementById("danger-tools");
const captureLink = document.getElementById("capture-link");

const STREAM_URL = `${location.protocol}//${location.hostname}:81/stream`;
const RISK_TEXT = "Queste impostazioni possono bloccare la camera, produrre immagine nera o richiedere un riavvio.";

const optionSets = {
  framesize: [
    [0, "96x96"], [1, "QQVGA 160x120"], [2, "128x128"], [3, "QCIF 176x144"],
    [4, "HQVGA 240x176"], [5, "240x240"], [6, "QVGA 320x240"],
    [7, "320x320"], [8, "CIF 400x296"], [9, "HVGA 480x320"],
    [10, "VGA 640x480"], [11, "SVGA 800x600"], [12, "XGA 1024x768"],
    [13, "HD 1280x720"], [14, "SXGA 1280x1024"], [15, "UXGA 1600x1200"],
    [16, "FHD 1920x1080"], [17, "P HD 720x1280"], [18, "P 3MP 864x1536"],
    [19, "QXGA 2048x1536"]
  ],
  gainceiling: [
    [0, "2x"], [1, "4x"], [2, "8x"], [3, "16x"], [4, "32x"], [5, "64x"], [6, "128x"]
  ],
  special_effect: [
    [0, "Nessuno"], [1, "Negativo"], [2, "Grayscale"], [3, "Rosso"], [4, "Verde"], [5, "Blu"], [6, "Sepia"]
  ],
  wb_mode: [
    [0, "Auto"], [1, "Sole"], [2, "Nuvoloso"], [3, "Ufficio"], [4, "Casa"]
  ]
};

const presets = {
  Dashboard: {
    framesize: 6, quality: 16, brightness: 0, contrast: 0, saturation: 0,
    awb: 1, agc: 1, aec: 1, hmirror: 0, vflip: 0
  },
  "Analisi condensa": {
    framesize: 6, quality: 10, brightness: 0, contrast: 1, saturation: -1,
    awb: 1, awb_gain: 1, agc: 1, aec: 1, dcw: 1, raw_gma: 1, lenc: 1
  },
  "Notte/IR": {
    framesize: 8, quality: 12, brightness: 1, contrast: 1, saturation: -2,
    awb: 0, agc: 1, aec: 1, ae_level: 1
  },
  Debug: {
    framesize: 6, quality: 12, brightness: 0, contrast: 0, saturation: 0,
    colorbar: 0, bpc: 1, wpc: 1, raw_gma: 1, lenc: 1
  },
  "Calibrazione AntiFrost": {
    framesize: 6, quality: 10, brightness: 0, contrast: 1, saturation: -2,
    awb: 0, awb_gain: 0, agc: 1, aec: 1, ae_level: 0, dcw: 1,
    bpc: 1, wpc: 1, raw_gma: 1, lenc: 1, special_effect: 2
  }
};

const controls = [];
const values = new Map();
const elements = new Map();
let lastCameraStatus = null;
let captureFallbackTimer = null;
let livePreviewActive = false;
let previewCaptureBusy = false;

function setStatus(text, mode = "") {
  statusLine.textContent = text;
  statusLine.className = `status-line ${mode}`.trim();
}

function normalizeRisk(risk) {
  return String(risk || "SAFE").toLowerCase();
}

function controlType(control) {
  if (optionSets[control.name]) {
    return "select";
  }
  if (Number(control.min) === 0 && Number(control.max) === 1) {
    return "toggle";
  }
  return "range";
}

function readControlValue(control, input) {
  return control.type === "toggle" ? (input.checked ? 1 : 0) : Number(input.value);
}

function writeControlValue(name, value) {
  const input = elements.get(name);
  const control = controls.find((item) => item.name === name);
  if (!input || !control || value === undefined || value === null) {
    return;
  }

  if (control.type === "toggle") {
    input.checked = Number(value) === 1;
  } else {
    input.value = value;
  }

  values.set(name, Number(value));
  const output = document.querySelector(`[data-output="${name}"]`);
  if (output) {
    output.textContent = control.type === "toggle" ? (Number(value) === 1 ? "ON" : "OFF") : String(Number(value));
  }
}

async function controlRequest(name, value, risk, dangerConfirmed = false) {
  if (risk === "dangerous" && !dangerConfirmed && !confirm(RISK_TEXT)) {
    return false;
  }

  const params = new URLSearchParams({ var: name, val: String(value) });
  if (risk === "dangerous") {
    params.set("dangerous", "1");
  }

  const response = await fetch(`/control?${params.toString()}`, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`control ${name} ${response.status}`);
  }
  return true;
}

function createBadge(control) {
  if (control.risk === "safe") {
    return null;
  }

  const badge = document.createElement("span");
  badge.className = `badge ${control.risk === "dangerous" ? "danger" : "advanced"}`;
  badge.textContent = control.risk === "dangerous" ? "RISCHIOSO" : "AVANZATO";
  return badge;
}

function createInput(control) {
  if (control.type === "select") {
    const select = document.createElement("select");
    for (const [value, text] of optionSets[control.name]) {
      if (Number(value) < Number(control.min) || Number(value) > Number(control.max)) {
        continue;
      }
      const option = document.createElement("option");
      option.value = value;
      option.textContent = text;
      select.appendChild(option);
    }
    return select;
  }

  const input = document.createElement("input");
  input.type = control.type === "toggle" ? "checkbox" : "range";
  if (control.type === "range") {
    input.min = control.min;
    input.max = control.max;
    input.step = control.step;
  }
  return input;
}

function createControl(control) {
  const card = document.createElement("article");
  card.className = "control";

  const head = document.createElement("div");
  head.className = "control-head";

  const label = document.createElement("label");
  label.htmlFor = `cam-${control.name}`;
  label.textContent = control.label;
  head.appendChild(label);

  const badge = createBadge(control);
  if (badge) {
    head.appendChild(badge);
  }

  const row = document.createElement("div");
  row.className = "input-row";

  const input = createInput(control);
  input.id = `cam-${control.name}`;

  const output = document.createElement("output");
  output.dataset.output = control.name;

  input.addEventListener("input", () => {
    const value = readControlValue(control, input);
    output.textContent = control.type === "toggle" ? (value === 1 ? "ON" : "OFF") : String(value);
  });

  if (control.risk === "dangerous") {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "danger";
    button.textContent = "Applica parametro rischioso";
    button.addEventListener("click", async () => {
      const value = readControlValue(control, input);
      try {
        const applied = await controlRequest(control.name, value, control.risk);
        if (applied) {
          await syncStatus(`${control.label}: ${value}`);
        }
      } catch (error) {
        setStatus(`Errore applicando ${control.label}`, "error");
      }
    });
    row.appendChild(input);
    row.appendChild(output);
    card.appendChild(head);
    card.appendChild(row);
    card.appendChild(button);
  } else {
    input.addEventListener("change", async () => {
      const value = readControlValue(control, input);
      try {
        const applied = await controlRequest(control.name, value, control.risk);
        if (applied) {
          await syncStatus(`${control.label}: ${value}`);
        }
      } catch (error) {
        setStatus(`Errore applicando ${control.label}`, "error");
      }
    });
    row.appendChild(input);
    row.appendChild(output);
    card.appendChild(head);
    card.appendChild(row);
  }

  if (control.help) {
    const help = document.createElement("p");
    help.className = "help";
    help.textContent = control.help;
    card.appendChild(help);
  }

  elements.set(control.name, input);
  values.set(control.name, Number(control.value ?? control.default ?? 0));
  const initialValue = Number(control.value ?? control.default ?? 0);
  if (control.type === "toggle") {
    input.checked = initialValue === 1;
    output.textContent = initialValue === 1 ? "ON" : "OFF";
  } else {
    input.value = initialValue;
    output.textContent = String(initialValue);
  }
  return card;
}

function renderControls() {
  safeControls.textContent = "";
  advancedControls.textContent = "";
  dangerousControls.textContent = "";

  for (const control of controls) {
    const node = createControl(control);
    if (control.risk === "safe") {
      safeControls.appendChild(node);
    } else if (control.risk === "advanced") {
      advancedControls.appendChild(node);
    } else {
      dangerousControls.appendChild(node);
    }
  }
}

function renderPresets() {
  presetGrid.textContent = "";
  for (const name of Object.keys(presets)) {
    if (name === "Calibrazione AntiFrost") {
      continue;
    }
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = name;
    button.addEventListener("click", () => applyPreset(name));
    presetGrid.appendChild(button);
  }
}

function renderDangerTools() {
  dangerTools.textContent = "";
  const unmanaged = ["/reg", "/pll", "/resolution"];

  for (const name of unmanaged) {
    const card = document.createElement("article");
    card.className = "tool-form unmanaged";

    const title = document.createElement("h3");
    title.textContent = name;
    card.appendChild(title);

    const badge = document.createElement("span");
    badge.className = "badge danger";
    badge.textContent = "NOT MANAGED";
    card.appendChild(badge);

    const text = document.createElement("p");
    text.textContent = "Parametro non gestito dal firmware AntiFrost in questa versione.";
    card.appendChild(text);

    dangerTools.appendChild(card);
  }
}

async function loadParameterSchema() {
  const response = await fetch("/api/camera/parameters", { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`parameters ${response.status}`);
  }

  const schema = await response.json();
  controls.length = 0;
  for (const item of schema.parameters || []) {
    const control = {
      name: item.name,
      label: item.label || item.name,
      risk: normalizeRisk(item.risk),
      min: Number(item.min),
      max: Number(item.max),
      step: Number(item.step || 1),
      default: Number(item.default ?? 0),
      value: Number(item.value ?? item.default ?? 0),
      help: item.help || ""
    };
    control.type = controlType(control);
    controls.push(control);
  }
}

async function syncStatus(successText = "Camera sincronizzata") {
  try {
    const response = await fetch("/status", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(String(response.status));
    }
    const status = await response.json();
    lastCameraStatus = status;
    for (const control of controls) {
      if (Object.prototype.hasOwnProperty.call(status, control.name)) {
        writeControlValue(control.name, status[control.name]);
      }
    }
    setStatus(successText, "ok");
    return true;
  } catch (error) {
    setStatus("Status camera non disponibile", "error");
    return false;
  }
}

async function applyValues(nextValues) {
  const hasDanger = Object.keys(nextValues).some((name) => {
    const control = controls.find((item) => item.name === name);
    return control && control.risk === "dangerous";
  });
  const dangerConfirmed = !hasDanger || confirm(RISK_TEXT);
  if (!dangerConfirmed) {
    return false;
  }

  for (const [name, value] of Object.entries(nextValues)) {
    const control = controls.find((item) => item.name === name);
    if (!control) {
      continue;
    }
    await controlRequest(name, value, control.risk, dangerConfirmed);
  }
  await syncStatus();
  return true;
}

async function applyPreset(name) {
  try {
    const applied = await applyValues(presets[name]);
    setStatus(applied ? `Preset ${name} applicato` : "Applicazione annullata", applied ? "ok" : "");
  } catch (error) {
    setStatus(`Preset ${name} fallito`, "error");
  }
}

async function applyAll() {
  const snapshot = {};
  for (const control of controls) {
    if (control.risk === "dangerous") {
      continue;
    }
    const input = elements.get(control.name);
    snapshot[control.name] = readControlValue(control, input);
  }
  try {
    const applied = await applyValues(snapshot);
    setStatus(applied ? "Parametri principali e avanzati applicati" : "Applicazione annullata", applied ? "ok" : "");
  } catch (error) {
    setStatus("Applicazione parametri fallita", "error");
  }
}

async function saveProfile() {
  if (lastCameraStatus === null && !(await syncStatus())) {
    setStatus("Salvataggio annullato: /status non disponibile", "error");
    return;
  }

  const profile = {
    saved_at_ms: Date.now(),
    source: "/status",
    values: lastCameraStatus || {}
  };

  try {
    const response = await fetch("/api/camera/profile", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(profile)
    });
    if (!response.ok) {
      throw new Error(String(response.status));
    }
    setStatus("Profilo salvato su FAT", "ok");
  } catch (error) {
    setStatus("Salvataggio profilo fallito", "error");
  }
}

async function restoreDefaults() {
  try {
    const response = await fetch("/api/camera/defaults", {
      method: "POST",
      cache: "no-store"
    });
    if (!response.ok) {
      throw new Error(String(response.status));
    }
    await syncStatus("Default camera ripristinati");
  } catch (error) {
    setStatus("Ripristino default fallito", "error");
  }
}

function setMode(mode) {
  document.querySelectorAll(".mode-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.mode === mode);
  });
  document.querySelectorAll(".mode-panel").forEach((panel) => {
    panel.classList.toggle("active", panel.dataset.panel === mode);
  });
}

async function captureReference() {
  try {
    const response = await fetch("/capture", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(String(response.status));
    }
    const blob = await response.blob();
    const saveResponse = await fetch("/api/camera/reference", {
      method: "POST",
      headers: { "Content-Type": "image/jpeg" },
      body: blob
    });
    if (!saveResponse.ok) {
      throw new Error(String(saveResponse.status));
    }
    const url = URL.createObjectURL(blob);
    captureLink.href = url;
    captureLink.download = `antifrost-reference-${Date.now()}.jpg`;
    captureLink.hidden = false;
    captureLink.textContent = "Apri ultimo riferimento";
    setStatus("Capture riferimento salvata su FAT", "ok");
  } catch (error) {
    setStatus("Capture riferimento fallita", "error");
  }
}

function startCaptureFallback() {
  if (captureFallbackTimer !== null) {
    return;
  }

  livePreviewActive = false;
  setStatus("Stream non disponibile, uso anteprima capture", "error");
  captureFallbackTimer = setInterval(() => {
    streamEl.src = `/capture?t=${Date.now()}`;
  }, 2500);
  streamEl.src = `/capture?t=${Date.now()}`;
}

function stopCaptureFallback() {
  if (captureFallbackTimer !== null) {
    clearInterval(captureFallbackTimer);
    captureFallbackTimer = null;
  }
}

function refreshPreviewCapture() {
  if (livePreviewActive || previewCaptureBusy) {
    return;
  }

  livePreviewActive = false;
  stopCaptureFallback();
  previewCaptureBusy = true;
  streamEl.src = `/capture?t=${Date.now()}`;
  setStatus("Preview aggiornata", "ok");
}

function startLivePreview() {
  livePreviewActive = true;
  stopCaptureFallback();
  streamEl.src = `${STREAM_URL}?t=${Date.now()}`;
  document.getElementById("preview-live").textContent = "Ferma live";
  setStatus("Live preview avviata", "ok");
}

function stopLivePreview() {
  livePreviewActive = false;
  streamEl.src = "";
  document.getElementById("preview-live").textContent = "Avvia live";
  setStatus("Live preview fermata", "ok");
  setTimeout(refreshPreviewCapture, 250);
}

function toggleLivePreview() {
  if (livePreviewActive) {
    stopLivePreview();
  } else {
    startLivePreview();
  }
}

async function init() {
  streamEl.addEventListener("load", () => {
    previewCaptureBusy = false;
  });
  streamEl.addEventListener("error", () => {
    previewCaptureBusy = false;
    if (livePreviewActive) {
      startCaptureFallback();
    }
  });
  renderPresets();
  renderDangerTools();
  document.querySelectorAll(".mode-button").forEach((button) => {
    button.addEventListener("click", () => setMode(button.dataset.mode));
  });
  document.getElementById("apply").addEventListener("click", applyAll);
  document.getElementById("preview-capture").addEventListener("click", refreshPreviewCapture);
  document.getElementById("preview-live").addEventListener("click", toggleLivePreview);
  document.getElementById("save-profile").addEventListener("click", saveProfile);
  document.getElementById("restore-defaults").addEventListener("click", restoreDefaults);
  document.getElementById("capture-reference").addEventListener("click", captureReference);
  document.getElementById("apply-calibration").addEventListener("click", () => applyPreset("Calibrazione AntiFrost"));
  document.getElementById("capture-calibration").addEventListener("click", captureReference);
  refreshPreviewCapture();

  try {
    await loadParameterSchema();
    renderControls();
    await syncStatus();
  } catch (error) {
    setStatus("Schema parametri camera non disponibile", "error");
  }
}

init();
