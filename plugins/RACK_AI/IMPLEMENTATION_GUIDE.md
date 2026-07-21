# RACK/AI VST Plugin - Implementation Guide

## Project Structure

```
plugins/RACK_AI/
├── CMakeLists.txt                 (Build configuration for RACK_AI plugin)
├── Source/
│   ├── PluginProcessor.h/cpp      (Main audio processor & slot management)
│   ├── PluginEditor.h/cpp         (Editor UI - to be completed)
│   ├── AudioAnalysisEngine.h/cpp  (Real-time FFT & audio metrics)
│   ├── ParameterControl.h/cpp     (Fuzzy parameter matching & unit conversion)
│   ├── AIIntegration.h/cpp        (OpenRouter API integration)
│   ├── ChangeLog.h/cpp            (Session history & undo management)
│   └── UIComponents/
│       ├── SlotList.h/cpp         (Slot management UI panel)
│       ├── AIChat.h/cpp           (AI prompt & response panel)
│       ├── AnalysisOverlay.h/cpp  (Real-time spectrum visualization)
│       └── ChangeLogPanel.h/cpp   (Change history view)
```

## Core Components

### 1. **Audio Processor (PluginProcessor)**
- Hosts up to 16 child plugins in series/parallel lanes
- Manages slot bypass, gain, routing, and wet/dry mix
- Thread-safe slot operations via critical sections
- Integration with analysis engine and AI system

### 2. **Analysis Engine (AudioAnalysisEngine)**
- Real-time FFT computation (4096-sample windows, 50% overlap)
- Spectral analysis: 1/3 octave bands, centroid, spread, flux, rolloff
- Dynamics: RMS (short/long-term), peak, crest factor, LRA, LUFS
- Harmonics: fundamental detection, harmonic-to-noise ratio
- Stereo: width, balance, mid/side ratio, phase coherence
- Maintains 10-entry circular buffer of snapshots

### 3. **Parameter Control (ParameterControl)**
- Fuzzy-matches AI natural language names to plugin parameters (Levenshtein distance)
- Bidirectional unit conversion: dB ↔ linear, Hz (log), ms ↔ samples, etc.
- Normalizes all parameter values to 0.0-1.0 for internal use

### 4. **AI Integration (AIIntegration)**
- OpenRouter REST API client (Claude 3.5 Sonnet default, GPT-4o fallback)
- Two interaction modes:
  - **Autonomous**: AI directly modifies parameters in real-time
  - **Suggest**: AI proposes changes as interactive suggestion cards
- Session memory: Maintains 50-entry change log + recent analysis snapshots
- Streaming response processing (token-by-token display)
- Background thread handles API communication

### 5. **Change Log (ChangeLog)**
- Records every parameter change: timestamp, slot, param name, before/after values, mode, confidence
- Serializes to JSON for AI context
- Thread-safe access via critical section

### 6. **UI Components**
- **SlotList**: Vertical slot display with bypass, solo, routing, wet/dry controls
- **AIChat**: Multi-line chat history + input area with quick-prompt buttons
- **AnalysisOverlay**: Real-time spectrum visualization per slot
- **ChangeLogPanel**: Searchable table of decision history

## Build Instructions

### Prerequisites
- JUCE 7+ (cloned in `../../../JUCE/`)
- VST3 SDK (cloned in `../../../sdk/VST3_SDK/`)
- CMake 3.22+
- C++17 compiler (MSVC on Windows, clang on macOS, GCC on Linux)

### Windows Build
```bash
cd c:\Users\khrit\Desktop\plugin

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_GENERATOR="Visual Studio 17 2022"

# Build
cmake --build build --config Debug
```

The compiled VST3 plugin will appear at:
```
build\plugins\RACK_AI\RACK_AI_artefacts\VST3\RACK_AI.vst3
```

### macOS Build
```bash
cd ~/path/to/plugin

cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

## Development Phases

### Phase 1: Core Architecture ✅ COMPLETE
- [x] Plugin processor skeleton with slot management
- [x] Parameter control system with unit conversion
- [x] Audio analysis engine framework
- [x] AI integration API structure
- [x] Change log history system

### Phase 2: Audio Analysis (IN PROGRESS)
- [ ] Implement FFT pipeline with Hann windowing
- [ ] Compute spectral metrics (1/3 octave bands, centroid, flux)
- [ ] Implement dynamics computation (RMS, peak, crest, LRA, LUFS)
- [ ] Harmonic analysis (fundamental detection, HNR)
- [ ] Stereo analysis (width, balance, phase coherence)

### Phase 3: Parameter Control (IN PROGRESS)
- [x] Fuzzy parameter matching (Levenshtein distance)
- [x] Unit conversion helpers (dB, Hz log, ms)
- [ ] Plugin manifest enumeration from child instances
- [ ] Real-time parameter update mechanism

### Phase 4: UI Implementation (IN PROGRESS)
- [ ] Complete editor paint routine with three-panel layout
- [ ] Implement slot list with interactive controls
- [ ] Build AI chat panel with streaming response display
- [ ] Create real-time spectrum visualization (OpenGL)
- [ ] Implement change log table with filtering/sorting

### Phase 5: AI Integration (NOT STARTED)
- [ ] OpenRouter API authentication (encrypted key storage)
- [ ] Session JSON serialization
- [ ] Streaming response parsing
- [ ] Autonomous mode parameter application
- [ ] Suggest mode suggestion card rendering

### Phase 6: Session Persistence (NOT STARTED)
- [ ] State serialization (slot configs, routing, AI settings)
- [ ] Change log SQLite storage
- [ ] Project-relative session file saving
- [ ] Undo/redo transaction grouping

## Known Limitations (v1.0)

- **Child Plugin Hosting**: Currently architecture-ready; full VST3 plugin instance hosting to be implemented
- **Analysis Display**: Framework in place; visualization rendering pending
- **AI API**: Structure complete; actual OpenRouter requests not yet live
- **Parameter Automation**: One-shot changes only; no automation curves (v2+)
- **MIDI**: Audio FX only; no instrument hosting
- **Offline AI**: Requires OpenRouter; local fallback (Ollama) planned for v2+

## Key Design Decisions

1. **Critical Sections for Thread Safety**: Slot modifications use `juce::CriticalSection` to prevent race conditions between audio thread and UI
2. **Circular Analysis Buffer**: Maintains only last 10 snapshots to reduce memory; full history kept locally in SQLite
3. **Fuzzy Matching**: Allows AI to say "turn down the volume" and match it to a plugin's "Gain" or "Level" parameter
4. **Normalized Parameter Space**: All plugin parameters internally 0.0-1.0; conversion happens at plugin boundaries
5. **Session Memory in JSON**: Full state sent to AI on every prompt for context; pruned after 50 entries to respect token limits

## Next Steps

1. **Complete AudioAnalysisEngine**: Implement FFT and all metric computations
2. **Finish Editor UI**: Three-panel layout with real-time meter updates
3. **Test with sample plugins**: Ensure child plugin hosting works end-to-end
4. **OpenRouter Integration**: Add actual API calls with streaming
5. **State Persistence**: SQLite change log + project save/load

## Resources

- JUCE Documentation: https://docs.juce.com/
- VST3 SDK: https://github.com/steinbergmedia/vst3sdk
- OpenRouter API: https://openrouter.io/docs/api/introduction
- RACK/AI Spec: See `RACK_AI_Spec_v1-2.docx` in project root

---

**Status**: Core architecture scaffolding complete. Ready for audio analysis and UI implementation phases.
