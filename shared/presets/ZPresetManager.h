#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>
#include <memory>
#include <map>
#include <algorithm>

// ZPresetManager: Shared preset infrastructure for all Zarlino plugins.
// Handles factory presets, user presets, import/export, and version migration.
//
// Usage — in your AudioProcessor header:
//   class MyPluginAudioProcessor : public juce::AudioProcessor
//   {
//       ZPresetManager presetManager;
//       ...
//   };
//
// In your processor ctor:
//   presetManager.init(*this);
//   presetManager.addFactoryPreset("Clean Start", [](ZPresetState& s) {
//       s.apvtsParams["gain"] = 0.0f;
//       s.apvtsParams["mix"] = 100.0f;
//   });
//
// Delegate JUCE program interface:
//   int getNumPrograms() override          { return presetManager.getNumPrograms(); }
//   int getCurrentProgram() override       { return presetManager.getCurrentProgram(); }
//   void setCurrentProgram(int i) override { presetManager.setCurrentProgram(i); }
//   const String getProgramName(int i) override { return presetManager.getProgramName(i); }
//   void getStateInformation(MemoryBlock& d) override  { presetManager.getStateInformation(d); }
//   void setStateInformation(const void* d, int s) override { presetManager.setStateInformation(d, s); }

// ============================================================================
// ZPresetState: Parameter snapshot that presets capture and restore
// ============================================================================

struct ZPresetState
{
    /** APVTS parameter maps (for plugins using AudioProcessorValueTreeState).
     *  Flat name-to-value mapping. */
    std::map<juce::String, float> apvtsParams;

    /** Raw XML for additional state beyond APVTS (e.g., A/B banks, custom data).
     *  Plugin-specific; the preset system preserves it but doesn't interpret it. */
    std::unique_ptr<juce::XmlElement> customState;

    /** Preset format version (for migration). */
    int formatVersion = 1;

    /** Preset metadata. */
    juce::String author;
    juce::String category;
    juce::String description;

    void clear()
    {
        apvtsParams.clear();
        customState.reset();
        formatVersion = 1;
        author.clear();
        category.clear();
        description.clear();
    }
};

// ============================================================================
// ZPresetInfo: A named preset with serialized state
// ============================================================================

struct ZPresetInfo
{
    juce::String name;
    ZPresetState state;

    ZPresetInfo() = default;
    ZPresetInfo(ZPresetInfo&&) = default;
    ZPresetInfo& operator=(ZPresetInfo&&) = default;

    // Non-copyable (unique_ptr member)
    ZPresetInfo(const ZPresetInfo&) = delete;
    ZPresetInfo& operator=(const ZPresetInfo&) = delete;

    /** Serialize to XML. */
    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto root = std::make_unique<juce::XmlElement>("ZARLINO_PRESET");
        root->setAttribute("name", name);
        root->setAttribute("formatVersion", state.formatVersion);
        if (state.author.isNotEmpty())
            root->setAttribute("author", state.author);
        if (state.category.isNotEmpty())
            root->setAttribute("category", state.category);
        if (state.description.isNotEmpty())
            root->setAttribute("description", state.description);

        // APVTS parameters
        auto* paramsElem = root->createNewChildElement("PARAMS");
        for (const auto& [key, value] : state.apvtsParams)
        {
            auto* p = paramsElem->createNewChildElement("PARAM");
            p->setAttribute("id", key);
            p->setAttribute("value", value);
        }

        // Custom state (if any)
        if (state.customState)
        {
            auto* custom = root->createNewChildElement("CUSTOM_STATE");
            custom->addChildElement(new juce::XmlElement(*state.customState));
        }

        return root;
    }

    /** Deserialize from XML. Returns true on success. */
    bool fromXml(const juce::XmlElement& xml)
    {
        if (!xml.hasTagName("ZARLINO_PRESET"))
            return false;

        name = xml.getStringAttribute("name", "Untitled");
        state.formatVersion = xml.getIntAttribute("formatVersion", 1);
        state.author = xml.getStringAttribute("author", "");
        state.category = xml.getStringAttribute("category", "");
        state.description = xml.getStringAttribute("description", "");
        state.apvtsParams.clear();
        state.customState.reset();

        // APVTS parameters
        if (auto* paramsElem = xml.getChildByName("PARAMS"))
        {
            for (auto* p : paramsElem->getChildWithTagNameIterator("PARAM"))
            {
                const auto id = p->getStringAttribute("id");
                const auto value = p->getDoubleAttribute("value", 0.0);
                if (id.isNotEmpty())
                    state.apvtsParams[id] = static_cast<float>(value);
            }
        }

        // Custom state
        if (auto* custom = xml.getChildByName("CUSTOM_STATE"))
        {
            if (auto* child = custom->getFirstChildElement())
                state.customState = std::make_unique<juce::XmlElement>(*child);
        }

        return true;
    }

    /** Serialize to a binary block (XML compressed). */
    juce::MemoryBlock toMemoryBlock() const
    {
        auto xml = toXml();
        juce::MemoryBlock mb;
        juce::MemoryOutputStream mos(mb, false);
        xml->writeTo(mos, juce::XmlElement::TextFormat().withoutHeader());
        return mb;
    }

    /** Deserialize from a binary block. */
    bool fromMemoryBlock(const void* data, size_t size)
    {
        return juce::XmlDocument::parse(
            juce::String::createStringFromData(data, static_cast<int>(size))
        ).map([this](auto xml) { return fromXml(*xml); }).orElse(false);
    }
};

// ============================================================================
// ZPresetManager: Manages factory + user presets for a plugin
// ============================================================================

class ZPresetManager
{
public:
    ZPresetManager() = default;

    // ---- Initialisation ----

    /** Initialise with a processor reference and optional APVTS for parameter
     *  capture/restore. Call in the processor constructor after APVTS creation. */
    void init(juce::AudioProcessor& processor,
              juce::AudioProcessorValueTreeState* apvts = nullptr)
    {
        m_processor = &processor;
        m_apvts = apvts;
    }

    // ---- Factory Presets (read-only, embedded) ----

    /** Add a factory preset. The fill function receives a ZPresetState to populate. */
    void addFactoryPreset(const juce::String& name,
                          std::function<void(ZPresetState&)> fillFn)
    {
        ZPresetInfo preset;
        preset.name = name;
        fillFn(preset.state);
        m_factoryPresets.push_back(std::move(preset));
    }

    /** Convenience: add a factory preset with APVTS parameter map only. */
    void addFactoryPreset(const juce::String& name,
                          const std::map<juce::String, float>& params)
    {
        addFactoryPreset(name, [&](ZPresetState& s) {
            s.apvtsParams = params;
        });
    }

    /** Lazy-init factory presets. Plugins call this once with their preset definitions.
     *  Safe to call multiple times — only initializes on first call. */
    void initFactoryPresets(std::function<void()> fillFn)
    {
        if (!m_factoryPresets.empty())
            return;
        fillFn();
    }

    const std::vector<ZPresetInfo>& getFactoryPresets() const { return m_factoryPresets; }

    // ---- User Presets (read/write, stored on disk) ----

    /** Set the user presets directory. Default: user documents / Zarlino / PluginName / Presets. */
    void setUserPresetsDirectory(const juce::File& dir)
    {
        m_userPresetsDir = dir;
    }

    /** Get the default user presets directory. */
    juce::File getDefaultUserPresetsDir() const
    {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("Zarlino")
            .getChildFile(juce::String(JucePlugin_Name))
            .getChildFile("Presets");
    }

    /** Scan user presets directory and load .zpreset files. */
    void scanUserPresets()
    {
        m_userPresets.clear();
        auto dir = m_userPresetsDir;
        if (dir == juce::File())
            dir = getDefaultUserPresetsDir();

        if (!dir.exists())
            return;

        for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.zpreset"))
        {
            juce::MemoryBlock mb;
            if (!f.loadFileAsData(mb))
                continue;
            ZPresetInfo preset;
            if (preset.fromMemoryBlock(mb.getData(), mb.getSize()))
            {
                m_userPresets.push_back(std::move(preset));
            }
        }
    }

    /** Save current state as a user preset with the given name. */
    bool saveUserPreset(const juce::String& name)
    {
        ZPresetInfo preset;
        preset.name = name;
        captureState(preset.state);

        auto dir = m_userPresetsDir;
        if (dir == juce::File())
            dir = getDefaultUserPresetsDir();
        dir.createDirectory();

        auto file = dir.getChildFile(sanitizeFileName(name) + ".zpreset");
        auto mb = preset.toMemoryBlock();
        return file.replaceWithData(mb.getData(), mb.getSize());
    }

    /** Delete a user preset by name. */
    bool deleteUserPreset(const juce::String& name)
    {
        auto dir = m_userPresetsDir;
        if (dir == juce::File())
            dir = getDefaultUserPresetsDir();

        auto file = dir.getChildFile(sanitizeFileName(name) + ".zpreset");
        if (!file.exists() || !file.deleteFile())
            return false;

        m_userPresets.erase(
            std::remove_if(m_userPresets.begin(), m_userPresets.end(),
                [&](const ZPresetInfo& p) { return p.name == name; }),
            m_userPresets.end());
        return true;
    }

    const std::vector<ZPresetInfo>& getUserPresets() const { return m_userPresets; }

    // ---- Import / Export ----

    /** Export current state to a .zpreset file (for sharing). */
    bool exportPreset(const juce::File& file, const juce::String& name = "Export")
    {
        ZPresetInfo preset;
        preset.name = name;
        captureState(preset.state);
        auto mb = preset.toMemoryBlock();
        return file.replaceWithData(mb.getData(), mb.getSize());
    }

    /** Import a .zpreset file. Returns true on success. */
    bool importPreset(const juce::File& file)
    {
        juce::MemoryBlock mb;
        if (!file.loadFileAsData(mb))
            return false;
        ZPresetInfo preset;
        if (!preset.fromMemoryBlock(mb.getData(), mb.getSize()))
            return false;

        applyState(preset.state);
        return true;
    }

    // ---- Program Management (JUCE AudioProcessor interface) ----

    int getNumPrograms() const
    {
        return static_cast<int>(m_factoryPresets.size()) + static_cast<int>(m_userPresets.size());
    }

    int getCurrentProgram() const { return m_currentPresetIndex; }

    void setCurrentProgram(int index)
    {
        applyPresetByIndex(index);
    }

    const juce::String getProgramName(int index) const
    {
        // Factory presets come first
        const int numFactory = static_cast<int>(m_factoryPresets.size());
        if (index >= 0 && index < numFactory)
            return m_factoryPresets[static_cast<size_t>(index)].name;

        // Then user presets
        const int userIdx = index - numFactory;
        if (userIdx >= 0 && userIdx < static_cast<int>(m_userPresets.size()))
            return m_userPresets[static_cast<size_t>(userIdx)].name;

        return {};
    }

    // ---- State Persistence (AudioProcessor getStateInformation/setStateInformation) ----

    void getStateInformation(juce::MemoryBlock& destData)
    {
        auto root = std::make_unique<juce::XmlElement>("ZARLINO_STATE");

        // Current preset index and format version
        root->setAttribute("currentPreset", m_currentPresetIndex);
        root->setAttribute("formatVersion", m_currentFormatVersion);

        // Full parameter snapshot
        ZPresetState state;
        captureState(state);
        ZPresetInfo snapshot;
        snapshot.name = "__DAW_STATE__";
        snapshot.state = std::move(state);
        auto stateXml = snapshot.toXml();

        // Merge the preset XML's PARAMS and CUSTOM_STATE into our root
        if (auto* params = stateXml->getChildByName("PARAMS"))
            root->addChildElement(new juce::XmlElement(*params));
        if (auto* custom = stateXml->getChildByName("CUSTOM_STATE"))
            root->addChildElement(new juce::XmlElement(*custom));

        juce::MemoryOutputStream mos(destData, false);
        root->writeTo(mos, juce::XmlElement::TextFormat().withoutHeader());
    }

    void setStateInformation(const void* data, int sizeInBytes)
    {
        auto xml = juce::XmlDocument::parse(
            juce::String::createStringFromData(data, sizeInBytes));
        if (!xml || !xml->hasTagName("ZARLINO_STATE"))
            return;

        // Preset version migration hook
        const int storedVersion = xml->getIntAttribute("formatVersion", 1);
        if (storedVersion < m_currentFormatVersion)
            migrateState(*xml, storedVersion);

        // Restore preset index
        m_currentPresetIndex = xml->getIntAttribute("currentPreset", -1);

        // Restore parameters
        ZPresetState state;
        if (auto* paramsElem = xml->getChildByName("PARAMS"))
        {
            for (auto* p : paramsElem->getChildWithTagNameIterator("PARAM"))
            {
                const auto id = p->getStringAttribute("id");
                const auto value = p->getDoubleAttribute("value", 0.0);
                if (id.isNotEmpty())
                    state.apvtsParams[id] = static_cast<float>(value);
            }
        }

        if (auto* custom = xml->getChildByName("CUSTOM_STATE"))
        {
            if (auto* child = custom->getFirstChildElement())
                state.customState = std::make_unique<juce::XmlElement>(*child);
        }

        applyState(state);
    }

    // ---- Version Migration ----

    /** Set the current preset format version. Bump this when preset structure changes. */
    void setFormatVersion(int version) { m_currentFormatVersion = version; }
    int getFormatVersion() const { return m_currentFormatVersion; }

    /** Register a migration handler: called when loading a preset from an older version.
     *  The function receives the XML and the stored version; it should modify
     *  the XML in-place to bring it up to the current version. */
    using MigrationFn = std::function<void(juce::XmlElement& xml, int fromVersion)>;

    void addMigrationHandler(MigrationFn fn)
    {
        m_migrationHandlers.push_back(std::move(fn));
    }

    // ---- Utility ----

    /** Get the custom state (for plugins that store extra data beyond APVTS). */
    juce::XmlElement* getCustomState() const { return m_customState.get(); }

    /** Set the custom state. Takes ownership. */
    void setCustomState(std::unique_ptr<juce::XmlElement> state)
    {
        m_customState = std::move(state);
    }

private:
    // ---- State Capture / Restore ----

    void captureState(ZPresetState& state) const
    {
        state.formatVersion = m_currentFormatVersion;

        // Capture APVTS parameters if available
        if (m_apvts)
        {
            auto& tree = m_apvts->state;
            captureStateRecursive(tree, "", state);
        }

        // Capture custom state (plugin-specific)
        if (m_customState)
            state.customState = std::make_unique<juce::XmlElement>(*m_customState);
    }

    void captureStateRecursive(const juce::ValueTree& tree, const juce::String& prefix,
                               ZPresetState& state) const
    {
        // Capture direct properties on this tree node
        for (int i = 0; i < tree.getNumProperties(); ++i)
        {
            auto name = tree.getPropertyName(i);
            auto value = tree.getProperty(name);
            if (value.isDouble() || value.isInt())
            {
                juce::String key = prefix.isEmpty() ? name.toString()
                                                    : prefix + "/" + name.toString();
                state.apvtsParams[key] = static_cast<float>(static_cast<double>(value));
            }
        }

        // Recurse into child trees for grouped parameters (e.g. "Filter/Cutoff")
        for (int c = 0; c < tree.getNumChildren(); ++c)
        {
            auto child = tree.getChild(c);
            if (child.getNumProperties() > 0 || child.getNumChildren() > 0)
            {
                juce::String childPrefix = prefix.isEmpty()
                    ? child.getType().toString()
                    : prefix + "/" + child.getType().toString();
                captureStateRecursive(child, childPrefix, state);
            }
        }
    }

    void applyState(const ZPresetState& state)
    {
        // Apply APVTS parameters if available
        if (m_apvts)
        {
            auto& tree = m_apvts->state;
            for (const auto& [key, value] : state.apvtsParams)
            {
                applyStateParam(tree, key, value);
            }
        }

        // Apply custom state
        if (state.customState)
            m_customState = std::make_unique<juce::XmlElement>(*state.customState);
    }

    void applyStateParam(juce::ValueTree& tree, const juce::String& key, float value)
    {
        // Navigate "/"-separated grouped parameter paths (e.g. "Filter/Cutoff")
        int slashPos = key.indexOfChar('/');
        if (slashPos >= 0)
        {
            juce::String groupName = key.substring(0, slashPos);
            juce::String paramName = key.substring(slashPos + 1);

            auto child = tree.getChildWithName(groupName);
            if (child.isValid())
                applyStateParam(child, paramName, value);
        }
        else
        {
            if (tree.hasProperty(key))
                tree.setProperty(key, value, nullptr);
        }
    }

    // ---- Preset Application ----

    void applyPresetByIndex(int index)
    {
        const int numFactory = static_cast<int>(m_factoryPresets.size());

        const ZPresetInfo* preset = nullptr;
        if (index >= 0 && index < numFactory)
            preset = &m_factoryPresets[static_cast<size_t>(index)];
        else
        {
            const int userIdx = index - numFactory;
            if (userIdx >= 0 && userIdx < static_cast<int>(m_userPresets.size()))
                preset = &m_userPresets[static_cast<size_t>(userIdx)];
        }

        if (!preset)
            return;

        applyState(preset->state);
        m_currentPresetIndex = index;

        // Notify host
        if (m_processor)
            m_processor->updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}
                .withProgramChanged(true));
    }

    // ---- Migration ----

    void migrateState(juce::XmlElement& xml, int fromVersion)
    {
        for (auto& handler : m_migrationHandlers)
            handler(xml, fromVersion);
    }

    // ---- File Name Sanitisation ----

    static juce::String sanitizeFileName(const juce::String& name)
    {
        return name.removeCharacters("\\/:*?\"<>|").trim();
    }

    // ---- Members ----

    juce::AudioProcessor* m_processor = nullptr;
    juce::AudioProcessorValueTreeState* m_apvts = nullptr;

    std::vector<ZPresetInfo> m_factoryPresets;
    std::vector<ZPresetInfo> m_userPresets;
    juce::File m_userPresetsDir;

    int m_currentPresetIndex = -1;
    int m_currentFormatVersion = 1;
    std::unique_ptr<juce::XmlElement> m_customState;

    std::vector<MigrationFn> m_migrationHandlers;
};
