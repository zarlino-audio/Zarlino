# VST Plugin Development Workspace

This workspace is a JUCE-based VST plugin development environment scaffolded for the Khritical Research Initiative.

## What is included

- `JUCE/` - expected JUCE framework clone
- `sdk/VST3_SDK/` - expected Steinberg VST3 SDK clone
- `plugins/` - plugin project folders and a starter template
- `shared/` - reusable DSP/UI code
- `dissection/` - study notes and annotated JUCE files
- `build/` - CMake build output
- `tools/` - helper scripts

## Build

From the workspace root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

## Add a plugin

Create a new plugin by copying `plugins/_template/`, renaming the target in `CMakeLists.txt`, and adding the new project to the root `CMakeLists.txt`.

## Useful scripts

- `tools/new_plugin.sh` - scaffold a new plugin from the template
- `tools/clean_build.sh` - wipe and rebuild the project
- `tools/install_vst3.sh` - install a built `.vst3` to the system VST3 folder

## Notes

The workspace structure and setup are documented in `VST_WORKSPACE_SETUP-1.md`.


