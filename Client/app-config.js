/*
  Copyright 2026 Wenhao Yang

  Author: Wenhao Yang
  Contributor: Wenhao Yang

  Pico-Eurorack Bootloader Apps client app catalog, page parameters, hardware notes, and LED themes.
*/

    window.PICO_API_BASE = "https://pico-api.fmsynth.workers.dev";

    function app(name, description, pages, hardware, information = info(false, "")) {
      return { name, description, pages, hardware, info: information };
    }

    function page(name, params, ledTheme = led("red")) {
      return { name, params, led: ledTheme };
    }

    function io(input, button, cv, out) {
      return { cv, input, out, button };
    }

    function info(enabled, note) {
      return { enabled, note };
    }

    function led(theme) {
      return { theme };
    }

    const DATA = {
      pico: {
        label: "Pico",
        image: "../images/Pico_2D.png",
        apps: [
          app("Branches", "Bernoulli gate", [
            page("Main", ["Probability", "Output level", "Unused", "Unused"], led("red"))
          ], io("Trigger in", "Short click to turn on/off Toggle*, long press to turn on/off Latch* ", "Red out", "Green output"), info(true, "Toggle: probability of changing output. \n Latch: Output full length gate.")),
          app("DejaVu", "Generative sequencer", [
            page("RED", ["Gate density", "Probability of changing gates", "Note range", "DejaVu memory"], led("red")),
            page("GREEN", ["Unused", "Unused", "Unused", "CV offset"], led("green"))
          ], io("Clock in", "Click to change pages", "Gate out", "CV out"), info(true, "DejaVu would capture and repeat last several steps randomly")),
          app("Drums", "Drum voice", [
            page("808Kick", ["Output level", "Tone", "Decay", "Frequency"], led("red")),
            page("909Kick", ["Output level", "Tone", "Decay", "Frequency"], led("orange")),
            page("Hihats", ["Output level", "Tone", "Decay", "Frequency"], led("yellow")),
            page("Snare", ["Output level", "Tone", "Decay", "Frequency"], led("aqua"))
          ], io("Trigger in", "Click to change models", "FM in", "Audio out"), info(false, "")),
          app("DualClock", "Dual clock generator", [
            page("Free", ["Clock 1 tempo", "Clock 2 tempo", "Clock 1 swing", "Clock 2 random timing"], led("red")),
            page("Sync", ["Clock 1 tempo", "Clock 2 ratio", "Clock 1 swing", "Clock 2 random timing"], led("orange")),
            page("External", ["Clock 1 ratio", "Clock 2 ratio", "Clock 1 swing", "Clock 2 random timing"], led("green"))
          ], io("Clock in", "Tap tempo, hold 3s to change sync mode", "External clock in", "Clock 2 out"), info(true, "Output divides external clock automatically. \n Sync mode : clock 1 master, and clock 2 slave to clock 1 depending on ratio")),
          app("GridsSampler", "Random sample sequencer", [
            page("RED", ["Drum Map X", "Drum Map Y", "Chaos", "Random sample possibility on channel 4"], led("red")),
            page("GREEN", ["Channel 1 fill", "Channel 2 fill", "Channel 3 fill", "Channel 4 fill"], led("green")),
            page("AQUA", ["Channel 1 sample", "Channel 2 sample", "Channel 3 sample", "Channel 4 sample"], led("aqua"))
          ], io("Clock in", "Click to change pages", "Reset in", "Audio out"), info(false, "")),
          app("Modulation", "ADSR / LFO", [
            page("ADSR", ["Attack", "Decay", "Sustain", "Release"], led("tiffany")),
            page("LFO", ["Rise", "Fall", "Waveform", "Output level"], led("aqua"))
          ], io("Gate / sync", "Click to change pages", "Gate in", "CV out"), info(true, "For LFO, there are 6 waveforms: ramp, sine, exponentia, quartic, random, pulse.")),
          app("Moogvoice", "Three oscillator subtractive voice", [
            page("RED", ["Oscillator 1 tuning", "Oscillator 2 tuning", "Oscillator 3 tuning", "Waveform"], led("red")),
            page("ORANGE", ["Filter frequency", "Filter resonance", "Envelope depth", "LFO depth"], led("orange")),
            page("GREEN", ["Attack", "Decay", "Sustain", "Release"], led("green")),
            page("BLUE", ["LFO frequency", "LFO waveform", "Unused", "Unused"], led("blue"))
          ], io("Trigger in", "Click to change pages", "V/Oct in", "Audio out"), info(false, "")),
          app("MotionRecorder", "Dual channel CV motion recorder", [
            page("Main", ["Manual CV 1 motion", "Manual CV 2 motion", "Sequence length", "Motion smoothing"], led("green"))
          ], io("Clock input", "Press to start a new recording", "CV 1 out", "CV 2 out"), info(true, "red indicates recording, green solid means waiting for the next recording starting point \n Blink yellow at the first step")),
          app("OneshotSampler", "One-shot sample player", [
            page("GREEN", ["Level", "Tone", "CV in target(v/oct or sample selection)", "Pitch"], led("green")),
            page("AQUA", ["Start", "End", "Attack", "Decay"], led("aqua")),
            page("BLUE", ["Bank", "Sample", "Sample randomness", "Reverse"], led("blue"))
          ], io("Trigger in", "Click to change pages", "CV in", "Audio out"), info(false, "")),
          app("Plaits", "Macro oscillator", [
            page("Click", ["Pitch +/- 12 semitones", "Harmonics", "Timbre", "Morph"], led("aqua")),
            page("Hold", ["Octave -3 to +5", "Main / Aux output mix", "LPG response", "LPG time / decay"], led("aqua"))
          ], io("Trigger in", "Click to change models, double click to change between 3 banks", "V/Oct in", "Audio out"), info(true, "FM models are seperated in PlaitsFM")),
          app("PlaitsFM", "Six-op FM voice", [
            page("Bank", ["Pitch +/- 12 semitones", "Patch number", "Timbre / operator feedback", "Morph / decay character"], led("blue")),
            page("Hold", ["Octave -3 to +5", "Unused", "Unused", "Unused"], led("violet"))
          ], io("Trigger in", "Click patch bank, hold page 2", "V/Oct in", "Audio out"), info(false, "")),
          app("Rings", "Resonator", [
            page("AQUA", ["Position", "Structure", "Brightness", "Frequency"], led("aqua")),
            page("VIOLET", ["Position", "Slide", "Polyphony", "Resonator type"], led("violet"))
          ], io("Trigger in", "Click to change pages, long press to save", "V/Oct in", "Audio out"), info(true, "Polyphony Models indicates colors: Mono(Green), Dual(Orange), Quad(Red)")),
          app("Sequencer", "Pitch and gate sequencer", [
            page("Steps 1-4", ["Step 1 pitch", "Step 2 pitch", "Step 3 pitch", "Step 4 pitch"], led("red")),
            page("Steps 5-8", ["Step 5 pitch", "Step 6 pitch", "Step 7 pitch", "Step 8 pitch"], led("violet")),
            page("Steps 9-12", ["Step 9 pitch", "Step 10 pitch", "Step 11 pitch", "Step 12 pitch"], led("blue")),
            page("Steps 13-16", ["Step 13 pitch", "Step 14 pitch", "Step 15 pitch", "Step 16 pitch"], led("aqua")),
            page("Global", ["Scale", "Internal clock divider", "Steps length 1-16", "Overall pitch"], led("green"))
          ], io("Clock in", "Click to change pages", "Gate out", "V/oct out"), info(true, "Hold to edit ratchet for each step \n Blink yellow at the first step")),
          app("TripleOSC", "Three oscillator voice", [
            page("Main", ["Waveform: Sine / Triangle / Saw / Ramp", "Oscillator 1 tuning", "Oscillator 2 tuning", "Oscillator 3 tuning"], led("red"))
          ], io("V/Oct in", "Unused", "FM in", "Audio out"), info(false, ""))
        ]
      },
      picofx: {
        label: "PicoFX",
        image: "../images/PicoFX_2D.png",
        apps: [
          app("BeatBreaker", "Recall, reverse and repeat", [
            page("RED", ["Break probability", "Reverse probability", "Repeat probability", "Output level"], led("red"))
          ], io("Audio input", "Main", "Clock in", "Audio out"), info(true, "Blink red when recall. purple when reverse, orange when repeat")),
          app("Bitcrush", "Resolution and downsampling", [
            page("GREEN", ["Resolution", "Downsampling", "Mix", "Output level"], led("green"))
          ], io("Audio in", "Main", "Mix CV in", "Audio out"), info(false, "")),
          app("Chorus", "Chorus modulation", [
            page("RED", ["Delay", "Feedback", "LFO frequency", "LFO depth"], led("red")),
            page("GREEN", ["Mix", "Output level", "Unused", "Unused"], led("green"))
          ], io("Audio in", "Click to change pages", "Right audio out", "Left audio out"), info(false, "")),
          app("Delay", "Mono/stereo delay", [
            page("MONO", ["Delay time", "Feedback", "Mix", "Output level"], led("red")),
            page("STEREO", ["Delay time", "Feedback", "Mix", "Output level"], led("green"))
          ], io("Audio in", "Click to change pages", "Clock in (Mono) or right audio out (Stereo)", "Left / mono out"), info(false, "")),
          app("Flanger", "Stereo flanger", [
            page("RED", ["Delay", "Feedback", "LFO frequency", "LFO depth"], led("red")),
            page("GREEN", ["Mix", "Output level", "Stereo width", "LFO waveform"], led("green"))
          ], io("Audio input / right out", "Click to change pages", "Right audio out", "Left audio out"), info(true, "6 internal waveforms available: \ntriangle(blue), \nSine(violet), \nSquare(white), \nSaw(yellow), \nSmooth Random(aqua), \nStepped Random(orange)")),
          app("Granular", "Granular texture processor", [
            page("RED", ["Grain size", "Grain density", "Grain pitch", "Mix"], led("red"))
          ], io("Audio in", "Main", "Right audio out", "Left audio out"), info(false, "")),
          app("Ladderfilter", "moog inspired ladder filter", [
            page("RED", ["Resonance", "Frequency", "CV scale 0-1.5x", "Output level"], led("red"))
          ], io("Audio input", "Main", "Frequency CV input", "Audio out"), info(false, "")),
          app("Panner", "Stereo spreader / panner", [
            page("SPREAD", ["Width", "Rate", "Delay / LFO shape", "Output level"], led("aqua"))
          ], io("Audio in", "Main", "Right audio out", "Left audio out"), info(false, "")),
          app("Reverb", "Stereo reverb", [
            page("RED", ["Reverb time", "Damp", "Mix", "Output level"], led("red"))
          ], io("Audio in", "Main", "Right audio out", "Left audio out"), info(false, "")),
          app("Sidechain", "Trigger ducking processor", [
            page("RED", ["Attack", "Decay", "Knee curve", "Output level"], led("red"))
          ], io("Audio input", "Main", "Trigger in", "Audio out"), info(true, "Green led indicates output level, red led indicates ducking active")),
          app("SpectralSmash", "Freeze / tear spectral processor", [
            page("SPECTRAL", ["Warp / tear", "Blur", "Time smear", "Wet / dry mix"], led("blue"))
          ], io("Audio in", "Short press freeze, hold recaptures", "Right audio out", "Left audio out"), info(false, ""))
        ]
      }
    };

    const LED_THEMES = {
      red: {
        color: "#ff3b30",
        hot: "#fc9e94",
        dark: "#ba2c24",
        glow: "rgba(255, 100, 92, 0.42)",
        glowSoft: "rgba(247, 0, 0, 0.1)"
      },
      orange: {
        color: "#ff8c00",
        hot: "#fec593",
        dark: "#9a5200",
        glow: "rgba(255, 149, 0, 0.42)",
        glowSoft: "rgba(255, 149, 0, 0.24)"
      },
      yellow: {
        color: "#ffd60a",
        hot: "#fff1a8",
        dark: "#987300",
        glow: "rgba(255, 214, 10, 0.42)",
        glowSoft: "rgba(255, 214, 10, 0.24)"
      },
      green: {
        color: "#30d158",
        hot: "#a0fdb4",
        dark: "#16732f",
        glow: "rgba(48, 209, 88, 0.42)",
        glowSoft: "rgba(48, 209, 88, 0.24)"
      },
      aqua: {
        color: "#2fa5ff",
        hot: "#9ce4f9",
        dark: "#0b7199",
        glow: "rgba(85, 207, 255, 0.42)",
        glowSoft: "rgba(100, 210, 255, 0.24)"
      },
      tiffany: {
        color: "#0edfc0",
        hot: "#8ff5ef",
        dark: "#066f6c",
        glow: "rgba(34, 199, 194, 0.34)",
        glowSoft: "rgba(10, 186, 181, 0.24)"
      },
      blue: {
        color: "#0a2bff",
        hot: "#88affd",
        dark: "#062b91",
        glow: "rgba(10, 75, 255, 0.42)",
        glowSoft: "rgba(10, 59, 255, 0.24)"
      },
      violet: {
        color: "#ff4ddb",
        hot: "#ffa3fa",
        dark: "#64258a",
        glow: "rgba(239, 90, 242, 0.42)",
        glowSoft: "rgba(237, 90, 242, 0.24)"
      },
      white: {
        color: "#f5f5f7",
        hot: "#ffffff",
        dark: "#b8bcc2",
        glow: "rgba(255, 255, 255, 0.46)",
        glowSoft: "rgba(210, 220, 232, 0.26)"
      },
      grey: {
        color: "#8e8e93",
        hot: "#c7c7cc",
        dark: "#4f5358",
        glow: "rgba(142, 142, 147, 0.34)",
        glowSoft: "rgba(142, 142, 147, 0.18)"
      }
    };
