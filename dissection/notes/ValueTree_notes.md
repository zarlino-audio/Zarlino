# ValueTree Notes

- `juce::ValueTree` is a hierarchical data structure used for state storage.
- It is commonly paired with `AudioProcessorValueTreeState` for plugin parameters.
- Each node can emit change notifications, making it useful for UI binding.
