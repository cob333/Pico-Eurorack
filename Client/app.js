/*
  Copyright 2026 Wenhao Yang

  Author: Wenhao Yang
  Contributor: Wenhao Yang

  Pico-Eurorack Bootloader Apps client behavior.
*/

    const DATA = {
      pico: {
        label: "Pico",
        image: "../images/Pico_2D.png",
        apps: [
          app("TripleOSC", "Three oscillator voice", [
            page("RED", ["Waveform: Sine / Triangle / Saw / Ramp", "Oscillator 1 tuning", "Oscillator 2 tuning", "Oscillator 3 tuning"])
          ], io("V/Oct in", "Unused", "FM in", "Audio out"), info(false, "")),
          app("Plaits", "Macro oscillator", [
            page("Model", ["Pitch +/- 12 semitones", "Harmonics", "Timbre", "Morph"]),
            page("Hold", ["Octave -3 to +5", "Main / Aux output mix", "LPG response", "LPG time / decay"])
          ], io("Trigger in", "Click to change models, double click to change between 3 banks", "V/Oct in", "Audio out"), info(true, "FM models are seperated in PlaitsFM")),
          app("PlaitsFM", "Six-op FM voice", [
            page("Bank", ["Pitch +/- 12 semitones", "Patch number", "Timbre / operator feedback", "Morph / decay character"]),
            page("Hold", ["Octave -3 to +5", "Unused", "Unused", "Unused"])
          ], io("Trigger in", "Click patch bank, hold page 2", "V/Oct in", "Audio out"), info(false, "")),
          app("Rings", "Resonator", [
            page("RED", ["Position", "Structure", "Brightness", "Frequency"]),
            page("VIOLET", ["Position", "Slide", "Polyphony", "Resonator type"])
          ], io("Trigger in", "Click to change pages", "V/Oct in", "Audio out"), info(true, "Polyphony Models indicates colors: Mono(Green), Dual(Orange), Quad(Red)")),
          app("Moogvoice", "Three oscillator subtractive voice", [
            page("RED", ["Oscillator 1 tuning", "Oscillator 2 tuning", "Oscillator 3 tuning", "Waveform"]),
            page("ORANGE", ["Filter frequency", "Filter resonance", "Envelope depth", "LFO depth"]),
            page("GREEN", ["Attack", "Decay", "Sustain", "Release"]),
            page("BLUE", ["LFO frequency", "LFO waveform", "Unused", "Unused"])
          ], io("Trigger in", "Click to change pages", "V/Oct in", "Audio out"), info(false, "")),
          app("Drums", "Drum voice", [
            page("808Kick(RED)", ["Output level", "Tone", "Decay", "Frequency"]),
            page("909Kick(ORANGE)", ["Output level", "Tone", "Decay", "Frequency"]),
            page("Hihats(YELLOW)", ["Output level", "Tone", "Decay", "Frequency"]),
            page("Snare(AQUA)", ["Output level", "Tone", "Decay", "Frequency"])
          ], io("Trigger in", "Click to change models", "FM in", "Audio out"), info(true, "Drums: Drum voice. 4 pages. Click to change models")),
          app("Branches", "Bernoulli gate", [
            page("RED/GREEN", ["Probability", "Output level 0-5V", "Unused", "Unused"])
          ], io("Trigger in", "Short click to change modes, long press to change output types", "Trigger in", "Green output"), info(true, "Bernoulli gate divide an input into 2")),
          app("DualClock", "Dual clock generator", [
            page("CLOCK", ["Clock 1 tempo / ratio", "Clock 2 tempo / ratio", "Clock 1 swing", "Clock 2 random timing"])
          ], io("Clock in", "Tap tempo, hold 3s to change sync mode", "External clock in", "Clock 2 out"), info(true, "DualClock: Dual clock generator. 1 page. Tap tempo, hold 3s to change sync mode")),
          app("Sequencer", "Pitch and gate sequencer", [
            page("Steps 1-4", ["Step 1 pitch", "Step 2 pitch", "Step 3 pitch", "Step 4 pitch"]),
            page("Steps 5-8", ["Step 5 pitch", "Step 6 pitch", "Step 7 pitch", "Step 8 pitch"]),
            page("Steps 9-12", ["Step 9 pitch", "Step 10 pitch", "Step 11 pitch", "Step 12 pitch"]),
            page("Steps 13-16", ["Step 13 pitch", "Step 14 pitch", "Step 15 pitch", "Step 16 pitch"]),
            page("Global", ["Scale", "Clock divider", "Steps 1-16", "Overall pitch"])
          ], io("Clock in", "Short press pages, hold ratchet edit", "Gate out", "V/oct out"), info(true, "Sequencer: Pitch and gate sequencer. 5 pages. Short press pages, hold ratchet edit")),
          app("MotionRecorder", "Dual channel CV motion recorder", [
            page("Motion", ["Manual CV 1 motion", "Manual CV 2 motion", "Sequence length", "Motion smoothing"])
          ], io("Clock input", "Press to start a new recording", "CV 1 out", "CV 2 out"), info(true, "MotionRecorder: Dual channel CV motion recorder. 1 page. Press to start a new recording")),
          app("GridsSampler", "Random sample sequencer", [
            page("RED", ["Drum Map X", "Drum Map Y", "Chaos", "Random sample possibility on channel 4"]),
            page("GREEN", ["Channel 1 fill", "Channel 2 fill", "Channel 3 fill", "Channel 4 fill"]),
            page("AQUA", ["Channel 1 sample", "Channel 2 sample", "Channel 3 sample", "Channel 4 sample"])
          ], io("Clock in", "Button changes page", "Reset in", "Audio out"), info(true, "GridsSampler: Random sample sequencer. 3 pages. Button changes page")),
          app("DejaVu", "Generative sequencer", [
            page("RED", ["Gate density", "Probability of changing gates", "Note range", "DejaVu memory"]),
            page("GREEN", ["Unused", "Unused", "Unused", "CV offset"])
          ], io("Clock in", "Click to change pages", "Gate out", "CV out"), info(true, "DejaVu: Generative sequencer. 2 pages. Click to change pages")),
          app("Modulation", "ADSR / LFO", [
            page("ADSR", ["Attack", "Decay", "Sustain", "Release"]),
            page("LFO", ["Rise", "Fall", "Waveform", "Output level"])
          ], io("Gate / sync", "Click to change pages", "Gate in", "CV out"), info(true, "Modulation: ADSR / LFO. 2 pages. Click to change pages")),
          app("OneshotSampler", "One-shot sample player", [
            page("GREEN", ["Level", "Tone", "CV in target(v/oct or sample selection)", "Pitch"]),
            page("AQUA", ["Start", "End", "Attack", "Decay"]),
            page("BLUE", ["Bank", "Sample", "Sample randomness", "Reverse"])
          ], io("Trigger in", "Button switches pages", "CV in", "Audio out"), info(true, "OneshotSampler: One-shot sample player. 3 pages. Button switches pages"))
        ]
      },
      picofx: {
        label: "PicoFX",
        image: "../images/PicoFX_2D.png",
        apps: [
          app("Delay", "Mono/stereo delay", [
            page("MONO", ["Delay time", "Feedback", "Wet / dry blend", "Output level"]),
            page("STEREO", ["Delay time", "Feedback", "Wet / dry blend", "Output level"])
          ], io("Audio input", "Button switches pages", "Clock in (Mono) or right audio out (Stereo)", "Left / mono out"), info(true, "Delay: Mono/stereo delay. 2 pages. Button switches pages")),
          app("Reverb", "Stereo reverb", [
            page("RED", ["Reverb time", "Filter cutoff", "Wet / dry blend", "Output level"])
          ], io("Audio / CV input", "Single page", "Audio input", "Left audio out"), info(true, "Reverb: Stereo reverb. 1 page. Single page")),
          app("Chorus", "Chorus modulation", [
            page("RED", ["Delay", "Feedback", "LFO frequency", "LFO depth"]),
            page("GREEN", ["Wet / dry mix", "Output level", "Unassigned", "Unassigned"])
          ], io("Audio input / right out", "Button changes page", "Audio input", "Left audio out"), info(true, "Chorus: Chorus modulation. 2 pages. Button changes page")),
          app("Flanger", "Stereo flanger", [
            page("RED", ["Delay", "Feedback", "LFO frequency", "LFO depth"]),
            page("GREEN", ["Wet / dry mix", "Output level", "Stereo width", "LFO waveform"])
          ], io("Audio input / right out", "Button changes page", "Audio input", "Left audio out"), info(true, "Flanger: Stereo flanger. 2 pages. Button changes page")),
          app("Ladderfilter", "CV-scaled ladder filter", [
            page("RED", ["Filter resonance", "Initial frequency", "CV scale 0-1.5x", "Output level"])
          ], io("Audio input", "Single page", "Frequency CV input", "Audio out"), info(true, "Ladderfilter: CV-scaled ladder filter. 1 page. Single page")),
          app("Bitcrush", "Resolution and downsampling effect", [
            page("GREEN", ["Resolution 1-16 bit", "Downsampling 1x-20x", "Mix", "Output level"])
          ], io("Audio input", "Button switches page", "Mix CV input", "Audio out"), info(true, "Bitcrush: Resolution and downsampling effect. 1 page. Button switches page")),
          app("Granular", "Granular texture processor", [
            page("RED", ["Grain size", "Grain density", "Grain pitch", "Wet / dry mix"])
          ], io("Audio input / CV", "Single page", "Audio input", "Left audio out"), info(true, "Granular: Granular texture processor. 1 page. Single page")),
          app("BeatBreaker", "Clock-sliced beat repeater", [
            page("RED", ["Break probability", "Reverse probability", "Repeat macro", "Output level"])
          ], io("Audio input", "Button clears beat history", "Clock input", "Audio out"), info(true, "BeatBreaker: Clock-sliced beat repeater. 1 page. Button clears beat history")),
          app("Sidechain", "Trigger ducking processor", [
            page("RED", ["Attack", "Decay", "Knee curve", "Output level"])
          ], io("Audio input", "Trigger input ducks audio", "Trigger input", "Audio out"), info(true, "Sidechain: Trigger ducking processor. 1 page. Trigger input ducks audio")),
          app("Panner", "Stereo spreader / panner", [
            page("SPREAD", ["Width", "Rate", "Delay / LFO shape", "Output level"])
          ], io("Audio input / right out", "Single page", "Audio input", "Left audio out"), info(true, "Panner: Stereo spreader / panner. 1 page. Single page")),
          app("Space", "Spectral reverb / space processor", [
            page("SPACE", ["Size", "Tone", "Modulation", "Wet / dry mix"])
          ], io("Audio input / right out", "Single page", "Audio input", "Left audio out"), info(true, "Space: Spectral reverb / space processor. 1 page. Single page")),
          app("SpectralSmash", "Freeze / tear spectral processor", [
            page("SPECTRAL", ["Warp / tear", "Blur", "Time smear", "Wet / dry mix"])
          ], io("Audio input / right out", "Short press freeze, hold recaptures", "Audio input", "Left audio out"), info(true, "SpectralSmash: Freeze / tear spectral processor. 1 page. Short press freeze, hold recaptures"))
        ]
      }
    };

    function app(name, description, pages, hardware, information = info(false, "")) {
      return { name, description, pages, hardware, info: information };
    }

    function page(name, params) {
      return { name, params };
    }

    function io(input, button, cv, out) {
      return { cv, input, out, button };
    }

    function info(enabled, note) {
      return { enabled, note };
    }

    const APP_INDEX = Object.fromEntries(
      Object.entries(DATA).map(([deviceKey, device]) => [
        deviceKey,
        new Map(device.apps.map((item) => [item.name, item]))
      ])
    );
    Object.values(DATA).forEach((device) => {
      device.apps.forEach((item) => {
        item.searchText = `${item.name} ${item.description} ${item.pages.map((p) => p.params.join(" ")).join(" ")}`.toLowerCase();
      });
    });

    const state = {
      device: "pico",
      selectedApp: 0,
      selectedPage: 0,
      selectedSlot: 0,
      generating: false,
      buildJobId: "",
      progressTimer: 0,
      slots: ["", "", "", "", "", ""]
    };

    const appList = document.getElementById("appList");
    const search = document.getElementById("search");
    const pageTabs = document.getElementById("pageTabs");
    const slots = document.getElementById("slots");
    const usageFill = document.getElementById("usageFill");
    const usageLabel = document.getElementById("usageLabel");
    const slotTrash = document.getElementById("slotTrash");
    const clearAllSlots = document.getElementById("clearAllSlots");
    const generateButton = document.getElementById("generateButton");
    const generationStatus = document.getElementById("generationStatus");
    const buildProgressFill = document.getElementById("buildProgressFill");
    const buildProgressPercent = document.getElementById("buildProgressPercent");
    const buildProgressMessage = document.getElementById("buildProgressMessage");
    const buildCancel = document.getElementById("buildCancel");
    const footerRevealSpace = document.getElementById("footerRevealSpace");
    const functionList = document.getElementById("functionList");
    const modulePreview = document.querySelector(".module");
    const sampleModal = document.getElementById("sampleModal");
    const sampleTitle = document.getElementById("sampleTitle");
    const sampleSubtitle = document.getElementById("sampleSubtitle");
    const sampleClose = document.getElementById("sampleClose");
    const sampleUploadForm = document.getElementById("sampleUploadForm");
    const sampleBankName = document.getElementById("sampleBankName");
    const sampleFiles = document.getElementById("sampleFiles");
    const sampleUploadButton = document.getElementById("sampleUploadButton");
    const sampleStatus = document.getElementById("sampleStatus");
    const sampleBankList = document.getElementById("sampleBankList");
    const DRAG_TYPE = {
      app: "application/x-app-index",
      slot: "application/x-slot-index"
    };
    const SAMPLE_CLIENTS = {
      GridsSampler: {
        subtitle: "Upload WAV files to the active sampler library used by GridsSampler.",
        bankless: true
      },
      OneshotSampler: {
        subtitle: "Upload WAV files into an existing or new Oneshot bank.",
        bankless: false
      }
    };
    const DEFAULT_STORAGE_BYTES = 3584 * 1024;
    const apiState = {
      available: false,
      storageBytes: DEFAULT_STORAGE_BYTES,
      slotAlignBytes: 4096,
      maxSlots: 6,
      appsByName: new Map(),
      baseUrl: null
    };
    const sampleState = {
      app: "",
      payload: null
    };
    const PANEL_FOCUS = {
      IN: { x: "50%", y: "3%", scale: 2.5 },
      PAGE: { x: "50%", y: "17%", scale: 2.5 },
      "PARAM 1": { x: "50%", y: "29%", scale: 2.5 },
      "PARAM 2": { x: "50%", y: "41%", scale: 2.5 },
      "PARAM 3": { x: "50%", y: "53%", scale: 2.5 },
      "PARAM 4": { x: "50%", y: "65%", scale: 2.5 },
      AUX: { x: "50%", y: "81%", scale: 2.5 },
      OUT: { x: "50%", y: "92%", scale: 2.5 }
    };
    const SLOT_COLOR_CLASSES = ["orange", "yellow", "tiffany", "aqua", "blue", "violet"];

    generateButton.addEventListener("click", generateFirmware);
    buildCancel.addEventListener("click", cancelBuild);
    sampleClose.addEventListener("click", closeSampleClient);
    sampleModal.addEventListener("click", (event) => {
      if (event.target === sampleModal) closeSampleClient();
    });
    sampleUploadForm.addEventListener("submit", uploadSampleFiles);

    document.querySelectorAll("[data-device]").forEach((button) => {
      button.addEventListener("click", () => {
        state.device = button.dataset.device;
        state.selectedApp = 0;
        state.selectedPage = 0;
        resetDeviceDefaults();
        render();
        slideSegmentToActive(button.closest(".device-switch"));
      });
    });

    search.addEventListener("input", () => renderAppList());
    window.addEventListener("resize", updateSegmentSliders);
    window.addEventListener("scroll", updateFooterReveal, { passive: true });
    window.addEventListener("resize", updateFooterReveal);

    function resetDeviceDefaults() {
      state.selectedSlot = 0;
      state.slots = Array.from({ length: apiState.maxSlots }, () => "");
    }

    function currentDevice() {
      return DATA[state.device];
    }

    function currentApp() {
      return currentDevice().apps[state.selectedApp];
    }

    function currentPage() {
      const appData = currentApp();
      return appData.pages[Math.min(state.selectedPage, appData.pages.length - 1)];
    }

    function updateFooterReveal() {
      if (!footerRevealSpace) return;
      const revealRect = footerRevealSpace.getBoundingClientRect();
      const revealDistance = Math.max(1, footerRevealSpace.offsetHeight);
      const progress = Math.min(1, Math.max(0, (window.innerHeight - revealRect.top) / revealDistance));
      document.documentElement.style.setProperty("--footer-progress", progress.toFixed(3));
    }

    function apiCandidates() {
      const sameOrigin = window.location.protocol === "http:" || window.location.protocol === "https:";
      const bases = sameOrigin ? [""] : [];
      for (let port = 8765; port < 8785; port += 1) {
        bases.push(`http://127.0.0.1:${port}`);
      }
      return bases;
    }

    async function apiFetch(path, options = {}) {
      const hasPinnedBase = apiState.baseUrl !== null;
      const bases = hasPinnedBase ? [apiState.baseUrl] : apiCandidates();
      let lastError = null;
      for (const base of bases) {
        try {
          const response = await fetch(`${base}${path}`, options);
          if (response.ok || hasPinnedBase) {
            apiState.baseUrl = base;
            return response;
          }
          lastError = new Error(`HTTP ${response.status}`);
        } catch (error) {
          lastError = error;
        }
      }
      throw lastError || new Error("API unavailable");
    }

    async function loadManifest(showProgress = false) {
      if (showProgress) {
        buildCancel.disabled = true;
        beginBuildUi();
        startProgressTicker("Loading...", 8, 88);
      }
      try {
        const response = await apiFetch("/api/manifest", { cache: "no-store" });
        if (!response.ok) throw new Error(`Manifest HTTP ${response.status}`);
        const manifest = await response.json();
        apiState.available = true;
        apiState.storageBytes = manifest.storageBytes || DEFAULT_STORAGE_BYTES;
        apiState.slotAlignBytes = manifest.slotAlignBytes || 4096;
        apiState.maxSlots = manifest.maxSlots || 6;
        apiState.appsByName = new Map(manifest.apps.map((item) => [item.name, item]));
        hydrateAppMetadata();
        setStatus("Ready");
      } catch (_error) {
        apiState.available = false;
        hydrateAppMetadata();
        setStatus("API offline", true);
      }
      render();
      if (showProgress) {
        try {
          setBuildProgress(100, apiState.available ? "Catalog ready" : "API offline");
          await delay(220);
        } finally {
          endBuildUi(false);
          buildCancel.disabled = false;
        }
      }
    }

    function hydrateAppMetadata() {
      Object.values(DATA).forEach((device) => {
        device.apps.forEach((item) => {
          applyAppMetadata(item, apiState.appsByName.get(item.name));
        });
      });
    }

    function applyAppMetadata(item, meta) {
      item.id = meta?.id || item.name;
      item.sizeBytes = Number.isFinite(meta?.sizeBytes) ? meta.sizeBytes : null;
      item.allocatedBytes = Number.isFinite(meta?.allocatedBytes) ? meta.allocatedBytes : item.sizeBytes;
      item.fitsRegion = meta?.fitsRegion;
    }

    function updateCatalogAppMetadata(meta) {
      if (!meta?.name) return;
      apiState.appsByName.set(meta.name, meta);
      Object.keys(DATA).forEach((deviceKey) => {
        const item = APP_INDEX[deviceKey].get(meta.name);
        if (item) applyAppMetadata(item, meta);
      });
      renderAppList();
      renderSlots();
      renderDetails();
      updateGenerateAvailability();
    }

    function setStatus(message, isError = false) {
      generationStatus.textContent = message;
      generationStatus.classList.toggle("error", isError);
    }

    function render() {
      const device = currentDevice();
      document.getElementById("panelImage").src = device.image;
      document.getElementById("panelImage").alt = `${device.label} panel preview`;
      document.querySelectorAll("[data-device]").forEach((button) => {
        button.classList.toggle("active", button.dataset.device === state.device);
      });
      renderAppList();
      renderSlots();
      renderDetails();
      updateSegmentSliders();
    }

    function renderAppList() {
      const needle = search.value.trim().toLowerCase();
      const apps = currentDevice().apps;
      document.getElementById("catalogCount").textContent = `${apps.length} apps`;
      appList.innerHTML = "";
      let visibleCount = 0;
      apps.forEach((item, index) => {
        if (needle && !item.searchText.includes(needle)) return;
        visibleCount += 1;
        const isInSlot = isAppInSlot(item.name);
        const hasSampleClient = Boolean(SAMPLE_CLIENTS[item.name]);
        const wrap = document.createElement("div");
        wrap.className = `app-card-wrap${hasSampleClient ? " has-sample-settings" : ""}`;
        const button = document.createElement("button");
        button.type = "button";
        button.className = `app-card${index === state.selectedApp ? " active" : ""}${isInSlot ? " in-slot" : ""}`;
        button.draggable = !isInSlot;
        button.innerHTML = `<strong>${item.name}</strong><span>${item.description} · ${appSizeLabel(item)}</span>`;
        button.addEventListener("dragstart", (event) => {
          if (isAppInSlot(item.name)) {
            event.preventDefault();
            return;
          }
          event.dataTransfer.setData(DRAG_TYPE.app, String(index));
          event.dataTransfer.effectAllowed = "copy";
          beginDrag("app", button);
        });
        button.addEventListener("dragend", () => {
          clearDragState();
        });
        button.addEventListener("click", () => {
          state.selectedApp = index;
          state.selectedPage = 0;
          renderAppList();
          renderDetails();
        });
        wrap.appendChild(button);
        if (hasSampleClient) {
          const settings = document.createElement("button");
          settings.type = "button";
          settings.className = "sample-settings";
          settings.textContent = "Edit";
          settings.addEventListener("click", (event) => {
            event.stopPropagation();
            openSampleClient(item.name);
          });
          wrap.appendChild(settings);
        }
        appList.appendChild(wrap);
      });
      if (!visibleCount) {
        appList.innerHTML = `<div class="empty-state">No apps match this search.</div>`;
      }
    }

    async function openSampleClient(appName) {
      sampleState.app = appName;
      sampleState.payload = null;
      sampleTitle.textContent = `${appName} Samples`;
      sampleSubtitle.textContent = SAMPLE_CLIENTS[appName]?.subtitle || "";
      sampleStatus.textContent = "Loading samples...";
      sampleStatus.classList.remove("error");
      sampleBankList.innerHTML = "";
      sampleFiles.value = "";
      sampleBankName.value = "";
      sampleModal.classList.add("open");
      sampleModal.setAttribute("aria-hidden", "false");
      await refreshSampleClient();
    }

    function closeSampleClient() {
      sampleModal.classList.remove("open");
      sampleModal.setAttribute("aria-hidden", "true");
      sampleState.app = "";
      sampleState.payload = null;
    }

    async function refreshSampleClient() {
      if (!sampleState.app) return;
      try {
        const response = await apiFetch(`/api/samples?app=${encodeURIComponent(sampleState.app)}`, { cache: "no-store" });
        const payload = await response.json();
        if (!response.ok) throw new Error(payload.error || `Samples HTTP ${response.status}`);
        sampleState.payload = payload;
        renderSampleClient();
        sampleStatus.textContent = "Ready";
        sampleStatus.classList.remove("error");
      } catch (error) {
        sampleStatus.textContent = error.message || "Failed to load samples";
        sampleStatus.classList.add("error");
      }
    }

    function renderSampleClient() {
      const payload = sampleState.payload;
      if (!payload) return;
      if (payload.bankless) {
        sampleBankName.hidden = true;
        sampleBankName.disabled = true;
        const libraryFiles = payload.libraryFiles || [];
        const wavs = payload.wavs || [];
        const files = libraryFiles.slice(0, 72).map((item) => {
          const size = Number.isFinite(item.bytes) ? ` · ${formatCapacityBytes(item.bytes)}` : "";
          return `<span class="sample-file-pill">${escapeHtml(item.name)}${size}</span>`;
        }).join("");
        const hiddenCount = Math.max(0, libraryFiles.length - 72);
        sampleBankList.innerHTML = `
          <section class="sample-bank">
            <div class="sample-bank-head">
              <strong>Sample library</strong>
              <span>${libraryFiles.length} headers · ${wavs.length} WAV files</span>
            </div>
            <div class="sample-file-list">
              ${files || "<span>No library headers found in Samples.h / samples.h.</span>"}
              ${hiddenCount ? `<span class="sample-file-pill">+${hiddenCount} more</span>` : ""}
            </div>
          </section>
        `;
        return;
      }

      sampleBankName.hidden = false;
      const banks = payload.banks || [];
      sampleBankName.disabled = false;
      sampleBankName.placeholder = "New Bank";

      sampleBankList.innerHTML = banks.map((bank) => {
        const files = (bank.samples || []).slice(0, 36).map((item) => {
          return `<span class="sample-file-pill">${escapeHtml(item.name)} · ${formatCapacityBytes(item.bytes)}</span>`;
        }).join("");
        const hiddenCount = Math.max(0, (bank.samples || []).length - 36);
        return `
          <section class="sample-bank">
            <div class="sample-bank-head">
              <div class="sample-bank-title">
                <strong>${escapeHtml(bank.name)}</strong>
                <button class="sample-delete-bank" type="button" data-bank="${escapeHtml(bank.name)}">Delete</button>
              </div>
              <span>${bank.count} samples · ${formatCapacityBytes(bank.bytes)}</span>
            </div>
            <div class="sample-file-list">
              ${files || "<span>No WAV files yet.</span>"}
              ${hiddenCount ? `<span class="sample-file-pill">+${hiddenCount} more</span>` : ""}
            </div>
          </section>
        `;
      }).join("") || `<div class="empty-state">No sample banks found.</div>`;
      sampleBankList.querySelectorAll(".sample-delete-bank").forEach((button) => {
        button.addEventListener("click", () => deleteSampleBank(button.dataset.bank || ""));
      });
    }

    async function uploadSampleFiles(event) {
      event.preventDefault();
      if (!sampleState.app) {
        sampleStatus.textContent = "Open a sampler first.";
        sampleStatus.classList.add("error");
        return;
      }
      const payload = sampleState.payload;
      const bankless = Boolean(payload?.bankless);
      const bankName = bankless ? "" : (sampleBankName.value.trim() || "Custom");
      const form = new FormData();
      form.append("app", sampleState.app);
      form.append("bank", bankName);
      Array.from(sampleFiles.files).forEach((file) => form.append("files", file));
      const hasFiles = sampleFiles.files.length > 0;

      sampleUploadButton.disabled = true;
      buildCancel.disabled = true;
      beginBuildUi();
      setBuildProgress(5, hasFiles ? "Uploading samples" : "Compiling capacity estimate");
      if (hasFiles) {
        startProgressTicker("Refreshing sample headers", 8, 48);
      } else {
        startProgressTicker("Compiling capacity estimate", 12, 92);
      }
      sampleStatus.textContent = hasFiles ? "Uploading and refreshing headers..." : "Compiling capacity estimate...";
      sampleStatus.classList.remove("error");
      try {
        let result = null;
        if (hasFiles) {
          const response = await apiFetch("/api/samples/upload", {
            method: "POST",
            body: form
          });
          result = await response.json();
          if (!response.ok) throw new Error(result.error || `Upload HTTP ${response.status}`);
          sampleState.payload = result;
          sampleFiles.value = "";
          sampleBankName.value = "";
          renderSampleClient();
          startProgressTicker("Compiling capacity estimate", 55, 92);
        }
        await refreshAppCapacity(sampleState.app);
        setBuildProgress(100, "Capacity updated");
        await delay(240);
        endBuildUi(false);
        sampleModal.classList.add("open");
        sampleModal.setAttribute("aria-hidden", "false");
        if (hasFiles) {
          const destination = result.bank ? ` to ${result.bank}` : "";
          sampleStatus.textContent = `Uploaded ${result.saved.length} file${result.saved.length === 1 ? "" : "s"}${destination}. Capacity updated.`;
        } else {
          sampleStatus.textContent = "Capacity updated.";
        }
      } catch (error) {
        endBuildUi(false);
        sampleStatus.textContent = error.message || (hasFiles ? "Upload failed" : "Capacity update failed");
        sampleStatus.classList.add("error");
      } finally {
        if (state.progressTimer) {
          clearTimeout(state.progressTimer);
          state.progressTimer = 0;
        }
        if (state.generating) endBuildUi(false);
        buildCancel.disabled = false;
        sampleUploadButton.disabled = false;
      }
    }

    async function deleteSampleBank(bankName) {
      if (!sampleState.app || !bankName) return;
      if (!window.confirm(`Delete bank "${bankName}" and all WAV/header files inside it?`)) return;

      sampleStatus.textContent = `Deleting ${bankName}...`;
      sampleStatus.classList.remove("error");
      try {
        const response = await apiFetch("/api/samples/delete-bank", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            app: sampleState.app,
            bank: bankName
          })
        });
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || `Delete HTTP ${response.status}`);
        sampleState.payload = result;
        renderSampleClient();
        sampleStatus.textContent = `Deleted ${result.deleted || bankName}. Click Upload to compile capacity and update Catalog.`;
      } catch (error) {
        sampleStatus.textContent = error.message || "Delete failed";
        sampleStatus.classList.add("error");
      }
    }

    async function refreshAppCapacity(appName) {
      const response = await apiFetch(`/api/app-size?app=${encodeURIComponent(appName)}`, { cache: "no-store" });
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error || `Capacity HTTP ${response.status}`);
      updateCatalogAppMetadata(payload);
      return payload;
    }

    function escapeHtml(value) {
      return String(value).replace(/[&<>"']/g, (char) => ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#39;"
      }[char]));
    }

    function renderSlots() {
      slots.innerHTML = "";
      state.slots.forEach((name, index) => {
        const appData = appByName(name);
        const row = document.createElement("div");
        row.className = `slot-row${index === state.selectedSlot ? " active" : ""}`;
        row.draggable = Boolean(name);
        const slotIndexClass = name ? SLOT_COLOR_CLASSES[index] : "empty";
        row.innerHTML = `
          <div class="slot-index ${slotIndexClass}">${index + 1}</div>
          <strong class="${name ? "" : "slot-empty"}">${name || "Empty"}</strong>
          <span class="slot-size">${appData ? appSizeLabel(appData) : "0KB"}</span>
          <span class="drag-handle" aria-hidden="true"></span>
        `;
        row.addEventListener("dragstart", (event) => {
          if (!name) {
            event.preventDefault();
            return;
          }
          event.dataTransfer.setData(DRAG_TYPE.slot, String(index));
          event.dataTransfer.effectAllowed = "move";
          beginDrag("slot", row);
        });
        row.addEventListener("dragend", () => {
          clearDragState();
        });
        row.addEventListener("dragover", (event) => {
          const dragKind = getDragKind(event);
          if (!dragKind) return;
          event.preventDefault();
          event.dataTransfer.dropEffect = dragKind === "slot" ? "move" : "copy";
          row.classList.add("drag-over");
        });
        row.addEventListener("dragleave", () => {
          row.classList.remove("drag-over");
        });
        row.addEventListener("drop", (event) => {
          const dragKind = getDragKind(event);
          if (!dragKind) return;
          event.preventDefault();
          row.classList.remove("drag-over");
          dragKind === "slot" ? swapSlotFromDrag(event, index) : assignAppFromDrag(event, index);
        });
        row.addEventListener("click", () => {
          state.selectedSlot = index;
          selectAppByName(name);
          renderAllSlotsAndDetails();
        });
        slots.appendChild(row);
      });
      renderCapacityUsage();
    }

    function assignAppFromDrag(event, slotIndex) {
      const appIndex = Number(event.dataTransfer.getData(DRAG_TYPE.app));
      const appData = currentDevice().apps[appIndex];
      if (!appData || isAppInSlot(appData.name)) {
        clearDragState();
        return;
      }
      state.selectedSlot = slotIndex;
      state.selectedApp = appIndex;
      state.selectedPage = 0;
      state.slots[slotIndex] = appData.name;
      clearDragState();
      renderAllSlotsAndDetails();
    }

    function swapSlotFromDrag(event, targetIndex) {
      const sourceIndex = Number(event.dataTransfer.getData(DRAG_TYPE.slot));
      if (!Number.isInteger(sourceIndex) || sourceIndex === targetIndex) {
        clearDragState();
        return;
      }
      [state.slots[sourceIndex], state.slots[targetIndex]] = [state.slots[targetIndex], state.slots[sourceIndex]];
      state.selectedSlot = targetIndex;
      selectAppByName(state.slots[targetIndex]);
      clearDragState();
      renderAllSlotsAndDetails();
    }

    function renderAllSlotsAndDetails() {
      renderSlots();
      renderAppList();
      renderDetails();
    }

    function selectAppByName(name) {
      const appIndex = currentDevice().apps.findIndex((item) => item.name === name);
      if (appIndex < 0) return;
      state.selectedApp = appIndex;
      state.selectedPage = 0;
    }

    function isAppInSlot(name) {
      return Boolean(name) && state.slots.includes(name);
    }

    function appByName(name) {
      return APP_INDEX[state.device].get(name);
    }

    function selectedSlotSummary() {
      const apps = [];
      let knownBytes = 0;
      let allocatedBytes = 0;
      let unknownCount = 0;
      state.slots.forEach((name) => {
        const item = appByName(name);
        if (!item) return;
        apps.push(item);
        if (Number.isFinite(item.sizeBytes)) knownBytes += item.sizeBytes;
        else unknownCount += 1;
        if (Number.isFinite(item.allocatedBytes)) allocatedBytes += item.allocatedBytes;
      });
      return { apps, knownBytes, allocatedBytes, unknownCount };
    }

    function appSizeLabel(appData) {
      if (!appData || !Number.isFinite(appData.sizeBytes)) return "Build";
      const suffix = appData.fitsRegion === false ? " > storage" : "";
      return `${formatCapacityBytes(appData.sizeBytes)}${suffix}`;
    }

    function beginDrag(kind, element) {
      clearDragState();
      element.classList.add("dragging");
      document.body.classList.add(`${kind}-dragging`);
    }

    function getDragKind(event) {
      if (hasDragType(event, DRAG_TYPE.slot)) return "slot";
      if (hasDragType(event, DRAG_TYPE.app)) return "app";
      return "";
    }

    function hasDragType(event, type) {
      return Array.from(event.dataTransfer.types).includes(type);
    }

    function clearDragState() {
      document.body.classList.remove("app-dragging");
      document.body.classList.remove("slot-dragging");
      document.querySelectorAll(".app-card.dragging").forEach((card) => {
        card.classList.remove("dragging");
      });
      document.querySelectorAll(".slot-row.dragging").forEach((row) => {
        row.classList.remove("dragging");
      });
      document.querySelectorAll(".slot-row.drag-over").forEach((row) => {
        row.classList.remove("drag-over");
      });
      slotTrash.classList.remove("drag-over");
    }

    slotTrash.addEventListener("dragover", (event) => {
      if (!hasDragType(event, DRAG_TYPE.slot)) return;
      event.preventDefault();
      event.dataTransfer.dropEffect = "move";
      slotTrash.classList.add("drag-over");
    });

    slotTrash.addEventListener("dragleave", () => {
      slotTrash.classList.remove("drag-over");
    });

    slotTrash.addEventListener("drop", (event) => {
      event.preventDefault();
      slotTrash.classList.remove("drag-over");
      const slotIndex = Number(event.dataTransfer.getData(DRAG_TYPE.slot));
      if (!Number.isInteger(slotIndex) || !state.slots[slotIndex]) {
        clearDragState();
        return;
      }
      state.slots[slotIndex] = "";
      state.selectedSlot = slotIndex;
      clearDragState();
      renderAllSlotsAndDetails();
    });

    clearAllSlots.addEventListener("click", () => {
      state.slots = state.slots.map(() => "");
      state.selectedSlot = 0;
      state.selectedApp = 0;
      state.selectedPage = 0;
      renderAllSlotsAndDetails();
    });

    function renderCapacityUsage() {
      const { apps, knownBytes, allocatedBytes, unknownCount } = selectedSlotSummary();
      const totalBytes = apiState.storageBytes;
      const percent = totalBytes ? Math.min(100, Math.round((allocatedBytes / totalBytes) * 100)) : 0;
      usageFill.style.width = `${percent}%`;
      usageLabel.textContent = `${formatCapacityBytes(knownBytes)} / ${formatCapacityBytes(totalBytes)}${unknownCount ? ` · ${unknownCount} build` : ""}`;
      updateGenerateAvailability(allocatedBytes, apps.length);
    }

    function updateGenerateAvailability(allocatedBytes = null, selectedCount = null, updateStatus = true) {
      const summary = selectedCount === null || allocatedBytes === null ? selectedSlotSummary() : null;
      const count = selectedCount === null ? summary.apps.length : selectedCount;
      const usedBytes = allocatedBytes === null ? summary.allocatedBytes : allocatedBytes;
      const withinStorage = usedBytes <= apiState.storageBytes;
      const canGenerate = apiState.available && count > 0 && withinStorage && !state.generating;

      generateButton.disabled = !canGenerate;
      generateButton.classList.toggle("ready", canGenerate);
      generateButton.classList.toggle("blocked", !canGenerate);

      if (updateStatus && !state.generating) {
        if (!apiState.available) setStatus("API offline", true);
        else if (!count) setStatus("Select app");
        else if (!withinStorage) setStatus("Storage full", true);
        else setStatus("Ready");
      }
    }

    function setBuildProgress(progress, message = "Building") {
      const value = Math.max(0, Math.min(100, Number(progress) || 0));
      buildProgressFill.style.width = `${value}%`;
      buildProgressPercent.textContent = `${Math.round(value)}%`;
      buildProgressMessage.textContent = message;
    }

    function startProgressTicker(message, start, limit) {
      if (state.progressTimer) {
        clearTimeout(state.progressTimer);
        state.progressTimer = 0;
      }
      let value = Math.max(0, Math.min(100, Number(start) || 0));
      const max = Math.max(value, Math.min(99, Number(limit) || 90));
      setBuildProgress(value, message);
      const tick = () => {
        if (!state.generating) return;
        value = Math.min(max, value + Math.max(1, Math.ceil((max - value) * 0.16)));
        setBuildProgress(value, message);
        if (value < max) {
          state.progressTimer = window.setTimeout(tick, 420);
        }
      };
      state.progressTimer = window.setTimeout(tick, 420);
    }

    function delay(ms) {
      return new Promise((resolve) => window.setTimeout(resolve, ms));
    }

    function beginBuildUi() {
      state.generating = true;
      document.querySelector(".app").classList.add("generating");
      setBuildProgress(0, "Queued");
      updateGenerateAvailability();
    }

    function endBuildUi(updateStatus = true) {
      state.generating = false;
      state.buildJobId = "";
      if (state.progressTimer) {
        clearTimeout(state.progressTimer);
        state.progressTimer = 0;
      }
      document.querySelector(".app").classList.remove("generating");
      updateGenerateAvailability(null, null, updateStatus);
    }

    function formatCapacityBytes(bytes) {
      if (!Number.isFinite(bytes)) return "Build";
      const kb = Math.ceil(bytes / 1024);
      return kb >= 1024 ? `${Number((kb / 1024).toFixed(1))}MB` : `${kb}KB`;
    }

    function renderDetails() {
      const appData = currentApp();
      const pageData = currentPage();
      const infoData = appData.info || info(false, "");
      const infoMount = document.getElementById("selectedInfoMount");
      document.getElementById("selectedName").textContent = appData.name;
      document.getElementById("selectedDescription").textContent = `${appData.description} · ${appSizeLabel(appData)} slot use`;
      renderInfoButton(infoMount, infoData);
      pageTabs.innerHTML = "";
      appData.pages.forEach((pageItem, index) => {
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = pageItem.name;
        button.classList.toggle("active", index === state.selectedPage);
        button.addEventListener("click", () => {
          state.selectedPage = index;
          renderDetails();
          slideSegmentToActive(pageTabs);
        });
        pageTabs.appendChild(button);
      });

      const rows = [
        ["IN", appData.hardware.input],
        ["PAGE", appData.hardware.button],
        ["PARAM 1", pageData.params[0]],
        ["PARAM 2", pageData.params[1]],
        ["PARAM 3", pageData.params[2]],
        ["PARAM 4", pageData.params[3]],
        ["AUX", appData.hardware.cv],
        ["OUT", appData.hardware.out]
      ];
      functionList.innerHTML = rows.map(([label, value]) => `
        <div class="function-row" data-focus="${label}"><b>${label}</b><span>${value || "Not used / TBD"}</span></div>
      `).join("");
      bindFunctionFocus();
      updateSegmentSliders();
    }

    function renderInfoButton(mount, infoData) {
      mount.innerHTML = "";
      const note = infoData.note.trim();
      if (!infoData.enabled || !note) return;

      const button = document.createElement("button");
      button.className = "info-button";
      button.type = "button";
      button.setAttribute("aria-label", "App info");

      const symbol = document.createElement("span");
      symbol.className = "info-symbol";
      symbol.setAttribute("aria-hidden", "true");

      const popover = document.createElement("span");
      popover.className = "info-popover";
      popover.role = "tooltip";
      popover.textContent = note;

      button.append(symbol, popover);
      mount.appendChild(button);
    }

    function updateSegmentSliders() {
      requestAnimationFrame(() => {
        document.querySelectorAll(".device-switch, .page-tabs").forEach((segment) => {
          slideSegmentToActive(segment);
        });
      });
    }

    function slideSegmentToActive(segment) {
      if (!segment) return;
      const activeButton = segment.querySelector("button.active");
      if (!activeButton) return;
      segment.style.setProperty("--slider-x", `${activeButton.offsetLeft}px`);
      segment.style.setProperty("--slider-width", `${activeButton.offsetWidth}px`);
      if (segment.classList.contains("page-tabs")) {
        activeButton.scrollIntoView({ behavior: "smooth", inline: "center", block: "nearest" });
      }
    }

    function bindFunctionFocus() {
      functionList.querySelectorAll(".function-row").forEach((row) => {
        row.addEventListener("mouseenter", () => focusPanelControl(row.dataset.focus));
        row.addEventListener("mouseleave", clearPanelFocus);
      });
    }

    function focusPanelControl(label) {
      const target = PANEL_FOCUS[label];
      if (!target) return;
      modulePreview.classList.add("focusing");
      modulePreview.style.setProperty("--focus-x", target.x);
      modulePreview.style.setProperty("--focus-y", target.y);
      modulePreview.style.setProperty("--focus-scale", target.scale);
    }

    function clearPanelFocus() {
      modulePreview.classList.remove("focusing");
      modulePreview.style.setProperty("--focus-scale", 1);
    }

    async function generateFirmware() {
      if (!apiState.available) {
        setStatus("Start server.py", true);
        return;
      }

      const slotIds = state.slots.map((name) => {
        const appData = appByName(name);
        return appData ? appData.id : "";
      });
      if (!slotIds.some(Boolean)) {
        setStatus("No slots", true);
        return;
      }
      const { allocatedBytes } = selectedSlotSummary();
      if (allocatedBytes > apiState.storageBytes) {
        setStatus("Storage full", true);
        return;
      }

      beginBuildUi();
      setStatus("Building...");
      try {
        const response = await apiFetch("/api/generate/start", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            device: state.device,
            slots: slotIds,
            active: state.selectedSlot
          })
        });
        if (!response.ok) {
          const payload = await response.json().catch(() => ({}));
          throw new Error(payload.error || `Generate HTTP ${response.status}`);
        }
        const job = await response.json();
        state.buildJobId = job.id;
        setBuildProgress(job.progress || 0, job.message || "Queued");
        pollBuildStatus();
      } catch (error) {
        setStatus(error.message || "Generate failed", true);
        endBuildUi(false);
      }
    }

    async function pollBuildStatus() {
      if (!state.buildJobId) return;
      try {
        const response = await apiFetch(`/api/generate/status?id=${encodeURIComponent(state.buildJobId)}`, { cache: "no-store" });
        if (!response.ok) throw new Error(`Status HTTP ${response.status}`);
        const job = await response.json();
        setBuildProgress(job.progress || 0, job.message || "Building");

        if (job.status === "done") {
          await downloadBuildOutput(job.id);
          setStatus("Generated");
          endBuildUi(false);
          return;
        }
        if (job.status === "cancelled") {
          setStatus("Cancelled", true);
          endBuildUi(false);
          return;
        }
        if (job.status === "error") {
          throw new Error(job.error || "Generate failed");
        }

        state.progressTimer = window.setTimeout(pollBuildStatus, 500);
      } catch (error) {
        setStatus(error.message || "Generate failed", true);
        endBuildUi(false);
      }
    }

    async function downloadBuildOutput(jobId) {
      const response = await apiFetch(`/api/generate/download?id=${encodeURIComponent(jobId)}`);
      if (!response.ok) throw new Error(`Download HTTP ${response.status}`);
      const blob = await response.blob();
      const filename = filenameFromDisposition(response.headers.get("Content-Disposition"))
        || `Pico-Eurorack-${state.device}.uf2`;
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = filename;
      document.body.appendChild(link);
      link.click();
      link.remove();
      URL.revokeObjectURL(url);
    }

    async function cancelBuild() {
      if (!state.buildJobId) return;
      buildCancel.disabled = true;
      setBuildProgress(Number.parseInt(buildProgressPercent.textContent, 10) || 0, "Cancelling");
      try {
        await apiFetch("/api/generate/cancel", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ id: state.buildJobId })
        });
      } finally {
        buildCancel.disabled = false;
      }
    }

    function filenameFromDisposition(header) {
      if (!header) return "";
      const match = header.match(/filename="([^"]+)"/);
      return match ? match[1] : "";
    }

    loadManifest(true);
    updateFooterReveal();
