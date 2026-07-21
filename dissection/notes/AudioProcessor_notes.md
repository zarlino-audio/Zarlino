# AudioProcessor Notes

- `juce::AudioProcessor` is the base class for all audio plugins.
- `prepareToPlay()` is called before audio processing starts.
- `processBlock()` handles the audio buffer and optional MIDI.
- Use `getStateInformation()` and `setStateInformation()` for presets and state.
