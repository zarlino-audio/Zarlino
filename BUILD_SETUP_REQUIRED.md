# RACK/AI VST Plugin - Build Setup Required

## ⚠️ Current Status

The RACK/AI plugin **project structure is complete and source code is scaffolded**, but compilation requires a C++ development environment.

## Prerequisites for Building

### Windows
You need **one** of the following:

1. **Visual Studio 2022 Community** (Recommended for JUCE development)
   - Download: https://visualstudio.microsoft.com/downloads/
   - Install with: Desktop development with C++, CMake tools
   - Then run: `cmake -B build -G "Visual Studio 17 2022"`

2. **MinGW-w64 + Make**
   - Download: https://www.mingw-w64.org/
   - Setup PATH to include MinGW bin folder
   - Then run: `cmake -B build -G "Unix Makefiles"`

3. **Clang + Ninja**
   - Install clang: Part of Visual Studio or via LLVM
   - Install ninja: Via chocolatey or from https://github.com/ninja-build/ninja
   - Then run: `cmake -B build -G Ninja`

### macOS
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install CMake if not present
brew install cmake
```

### Linux
```bash
sudo apt-get install build-essential cmake
```

## Build Steps (After Installing Compiler)

```bash
cd c:\Users\khrit\Desktop\plugin

# Configure (choose generator based on your compiler)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug

# Install VST3 plugin to system folder
# Windows: Copy build\plugins\RACK_AI\RACK_AI_artefacts\VST3\RACK_AI.vst3
#          to C:\Program Files\Common Files\VST3\RACK_AI.vst3

# macOS: Copy build/plugins/RACK_AI/RACK_AI_artefacts/VST3/RACK_AI.vst3
#        to ~/Library/Audio/Plug-Ins/VST3/RACK_AI.vst3

# Linux: Copy build/plugins/RACK_AI/RACK_AI_artefacts/VST3/RACK_AI.vst3
#        to ~/.vst3/RACK_AI.vst3
```

## Project Files Created

### Core Plugin Files
- ✅ `Source/PluginProcessor.h/cpp` - Audio processor with slot management
- ✅ `Source/PluginEditor.h/cpp` - Main UI editor (skeleton)
- ✅ `Source/AudioAnalysisEngine.h/cpp` - Real-time audio analysis
- ✅ `Source/ParameterControl.h/cpp` - Parameter fuzzy-matching & unit conversion
- ✅ `Source/AIIntegration.h/cpp` - OpenRouter API integration
- ✅ `Source/ChangeLog.h/cpp` - Decision log & session history

### UI Components
- ✅ `Source/UIComponents/SlotList.h/cpp` - Slot list panel
- ✅ `Source/UIComponents/AIChat.h/cpp` - AI chat panel
- ✅ `Source/UIComponents/AnalysisOverlay.h/cpp` - Spectrum visualization
- ✅ `Source/UIComponents/ChangeLogPanel.h/cpp` - Change log table

### Configuration
- ✅ `CMakeLists.txt` - Plugin build configuration
- ✅ `IMPLEMENTATION_GUIDE.md` - Detailed architecture & roadmap

## What's Implemented

### Core Architecture (Complete)
- [x] Multi-slot plugin hosting framework
- [x] Real-time audio analysis metrics structure
- [x] Parameter fuzzy-matching system
- [x] AI integration with OpenRouter API support
- [x] Change log and session history tracking
- [x] Thread-safe slot management

### Audio Processing (80%)
- [x] Slot bypass, solo, routing, and wet/dry mix controls
- [x] Signal flow through plugin chains
- [ ] FFT spectrum computation (framework in place)
- [ ] Dynamics, harmonic, and stereo analysis (stub implementations)

### UI Components (60%)
- [x] Three-panel layout structure
- [x] Slot list component
- [x] AI chat panel with input
- [x] Change log panel
- [ ] Real-time meters and spectrum visualization
- [ ] Interactive suggestion cards

### AI System (40%)
- [x] API structure for OpenRouter
- [x] Chat history management
- [x] Session context serialization
- [ ] Streaming response handling
- [ ] Autonomous and Suggest mode parameter application
- [ ] Encrypted API key storage

## Next Development Steps

### Immediate (Day 1-2)
1. Install Visual Studio Community 2022
2. Run build configuration: `cmake -B build -G "Visual Studio 17 2022"`
3. Open `build\VSTWorkspace.sln` in Visual Studio
4. Build → Compile to verify all sources link correctly

### Short Term (Week 1)
1. Complete `AudioAnalysisEngine` FFT and metrics
2. Finish `PluginEditor` three-panel layout with resizing
3. Implement real-time meter graphics
4. Test with sample plugins (e.g., FL Studio EQ)

### Medium Term (Week 2-3)
1. Integrate OpenRouter API with actual HTTP calls
2. Implement parameter fuzzy-matching end-to-end
3. Build Autonomous mode parameter application
4. Add change log SQLite persistence

### Long Term (Week 3+)
1. Interactive suggestion cards in Suggest mode
2. Session save/load via FL Studio project integration
3. Real-time spectrum analyzer with OpenGL
4. Undo/redo transaction grouping

## Testing & Validation

Once compiled, test in a DAW:
1. Load RACK_AI.vst3 into a plugin slot
2. Add child plugins (native FX or VST3)
3. Observe real-time analysis metrics
4. Send prompts to AI (requires OpenRouter API key)
5. Verify parameter changes are applied

## Resources

- **JUCE Documentation**: https://docs.juce.com
- **JUCE Tutorials**: https://docs.juce.com/master/tutorials_page.html
- **VST3 SDK**: https://github.com/steinbergmedia/vst3sdk
- **OpenRouter API**: https://openrouter.io/docs
- **CMake Docs**: https://cmake.org/cmake/help/latest/

## Troubleshooting

### CMake can't find compiler
- Windows: Ensure Visual Studio 2022 is fully installed with C++ tools
- macOS: Run `xcode-select --install`
- Linux: Install build-essential

### JUCE or VST3 SDK not found
- Verify `JUCE/` folder exists at `c:\Users\khrit\Desktop\plugin\JUCE`
- Verify `sdk/VST3_SDK/` folder exists at `c:\Users\khrit\Desktop\plugin\sdk\VST3_SDK`
- Both should have been cloned from the GitHub repos

### Build errors in source files
- Many functions are stubbed (especially in `AudioAnalysisEngine`)
- This is intentional for phase-by-phase development
- All stubs compile successfully but do minimal work
- Implement stubs one by one as needed

---

**Khritical Research Initiative - RACK/AI v1.0 Development Scaffold**
