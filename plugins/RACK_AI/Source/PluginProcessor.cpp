#include "PluginProcessor.h"
#include "PluginEditor.h"

RackAIAudioProcessor::RackAIAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
    mAnalysisEngine = std::make_unique<AudioAnalysisEngine>();
    mParameterControl = std::make_unique<ParameterControl>();
    mAIIntegration = std::make_unique<AIIntegration>();

    seedDefaultSession();
}
RackAIAudioProcessor::~RackAIAudioProcessor() {}

const juce::String RackAIAudioProcessor::getName() const { return JucePlugin_Name; }
bool RackAIAudioProcessor::acceptsMidi() const { return false; }
bool RackAIAudioProcessor::producesMidi() const { return false; }
bool RackAIAudioProcessor::isMidiEffect() const { return false; }
double RackAIAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int RackAIAudioProcessor::getNumPrograms() { return 1; }
int RackAIAudioProcessor::getCurrentProgram() { return 0; }
void RackAIAudioProcessor::setCurrentProgram (int) {}
const juce::String RackAIAudioProcessor::getProgramName (int) { return {}; }
void RackAIAudioProcessor::changeProgramName (int, const juce::String&) {}

void RackAIAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    if (mAnalysisEngine)
        mAnalysisEngine->prepare (sampleRate, samplesPerBlock);
}

void RackAIAudioProcessor::releaseResources()
{
    // nothing to free currently
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool RackAIAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}
#endif

void RackAIAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    const bool anySolo = std::any_of (slots.begin(), slots.end(), [] (const SlotState& slot) { return slot.solo; });
    float cumulativeGain = 1.0f;

    for (const auto& slot : slots)
    {
        const bool active = ! slot.bypassed && (! anySolo || slot.solo);
        if (active)
            cumulativeGain *= gainFromDb (slot.preGainDb + slot.postGainDb) * juce::jmap (slot.blend, 0.0f, 1.0f, 0.25f, 1.0f);
    }

    buffer.applyGain (juce::jlimit (0.0f, 2.0f, cumulativeGain));

    auto peak = 0.0f;
    auto sumSquares = 0.0;
    auto widthValue = 0.0f;
    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto value = std::abs (data[sample]);
            peak = juce::jmax (peak, value);
            sumSquares += static_cast<double> (data[sample]) * data[sample];
        }
    }

    if (channels == 2)
    {
        const auto left = buffer.getRMSLevel (0, 0, samples);
        const auto right = buffer.getRMSLevel (1, 0, samples);
        const auto mid = (left + right) * 0.5f;
        const auto side = std::abs (left - right);
        widthValue = mid > 0.000001f ? juce::jlimit (0.0f, 1.0f, side / mid) : 0.0f;
    }

    const auto denom = static_cast<double> (juce::jmax (1, samples * channels));
    const auto rms = static_cast<float> (std::sqrt (sumSquares / denom));
    const auto peakDbValue = juce::Decibels::gainToDecibels (peak, -100.0f);
    const auto rmsDbValue = juce::Decibels::gainToDecibels (rms, -100.0f);

    peakDb.store (peakDbValue);
    rmsDb.store (rmsDbValue);
    crestDb.store (peakDbValue - rmsDbValue);
    stereoWidth.store (widthValue);
}

bool RackAIAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* RackAIAudioProcessor::createEditor() { return new RackAIAudioProcessorEditor (*this); }

void RackAIAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void RackAIAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreFromStateTree (juce::ValueTree::fromXml (*xml));
}

int RackAIAudioProcessor::getSlotCount() const { return slots.size(); }
const RackAIAudioProcessor::SlotState& RackAIAudioProcessor::getSlot (int index) const { return slots.getReference (index); }

RackAIAudioProcessor::AnalysisSnapshot RackAIAudioProcessor::getAnalysisSnapshot() const
{
    return { rmsDb.load(), peakDb.load(), crestDb.load(), stereoWidth.load() };
}

juce::StringArray RackAIAudioProcessor::getChatHistory() const { return chatHistory; }
juce::Array<RackAIAudioProcessor::DecisionLogEntry> RackAIAudioProcessor::getDecisionLog() const { return decisionLog; }
RackAIAudioProcessor::AiMode RackAIAudioProcessor::getAiMode() const { return aiMode; }
void RackAIAudioProcessor::setAiMode (AiMode newMode) { aiMode = newMode; }
juce::String RackAIAudioProcessor::getSessionIntent() const { return sessionIntent; }
void RackAIAudioProcessor::setSessionIntent (const juce::String& intent) { sessionIntent = intent; }

bool RackAIAudioProcessor::addPlaceholderSlot()
{
    if (slots.size() >= maxSlots)
        return false;

    SlotState slot;
    slot.name = "Empty Slot " + juce::String (slots.size() + 1);
    slots.add (slot);
    appendDecision ({ nowForLog(), slot.name, "slot", "-", "created", aiModeToString (aiMode), "Applied" });
    return true;
}

void RackAIAudioProcessor::removeSlot (int index)
{
    if (juce::isPositiveAndBelow (index, slots.size()))
    {
        const auto name = slots[index].name;
        slots.remove (index);
        appendDecision ({ nowForLog(), name, "slot", "present", "removed", aiModeToString (aiMode), "Applied" });
    }
}

void RackAIAudioProcessor::moveSlot (int from, int to)
{
    if (juce::isPositiveAndBelow (from, slots.size()) && juce::isPositiveAndBelow (to, slots.size()) && from != to)
        slots.move (from, to);
}

void RackAIAudioProcessor::setSlotBypassed (int index, bool shouldBypass)
{
    if (juce::isPositiveAndBelow (index, slots.size()))
        slots.getReference (index).bypassed = shouldBypass;
}

void RackAIAudioProcessor::setSlotSolo (int index, bool shouldSolo)
{
    if (juce::isPositiveAndBelow (index, slots.size()))
        slots.getReference (index).solo = shouldSolo;
}

void RackAIAudioProcessor::setSlotRouting (int index, Routing routing)
{
    if (juce::isPositiveAndBelow (index, slots.size()))
        slots.getReference (index).routing = routing;
}

void RackAIAudioProcessor::setSlotBlend (int index, float blend)
{
    if (juce::isPositiveAndBelow (index, slots.size()))
        slots.getReference (index).blend = juce::jlimit (0.0f, 1.0f, blend);
}

void RackAIAudioProcessor::submitPrompt (const juce::String& prompt)
{
    if (prompt.trim().isEmpty())
        return;

    chatHistory.add ("Producer: " + prompt);

    const auto snapshot = getAnalysisSnapshot();
    const auto action = aiMode == AiMode::autonomous ? "I would apply a grouped, undoable rack move after validation." : "I prepared suggestion cards for approval.";
    chatHistory.add ("RACK/AI: " + action + " Current input is "
                     + juce::String (snapshot.rmsDb, 1) + " dB RMS, "
                     + juce::String (snapshot.peakDb, 1) + " dB peak, crest "
                     + juce::String (snapshot.crestDb, 1) + " dB.");

    appendDecision ({ nowForLog(),
                      slots.isEmpty() ? "Rack" : slots.getReference (0).name,
                      "AI prompt",
                      "-",
                      prompt,
                      aiModeToString (aiMode),
                      aiMode == AiMode::autonomous ? "Applied" : "Suggested" });
}

juce::String RackAIAudioProcessor::routingToString (Routing routing)
{
    switch (routing)
    {
        case Routing::series: return "Series";
        case Routing::busA:   return "Bus A";
        case Routing::busB:   return "Bus B";
        case Routing::busC:   return "Bus C";
        case Routing::busD:   return "Bus D";
    }

    return "Series";
}

RackAIAudioProcessor::Routing RackAIAudioProcessor::routingFromString (const juce::String& routing)
{
    if (routing == "Bus A") return Routing::busA;
    if (routing == "Bus B") return Routing::busB;
    if (routing == "Bus C") return Routing::busC;
    if (routing == "Bus D") return Routing::busD;
    return Routing::series;
}

juce::String RackAIAudioProcessor::aiModeToString (AiMode mode)
{
    return mode == AiMode::autonomous ? "Auto" : "Suggest";
}

void RackAIAudioProcessor::seedDefaultSession()
{
    slots.clear();

    for (int i = 0; i < 4; ++i)
    {
        SlotState slot;
        slot.name = "Empty Slot " + juce::String (i + 1);
        slot.routing = i < 2 ? Routing::series : (i == 2 ? Routing::busA : Routing::busB);
        slot.blend = i < 2 ? 1.0f : 0.65f;
        slots.add (slot);
    }

    sessionIntent = "Genre, reference, mood, and target loudness";
    chatHistory = { "RACK/AI: Session ready. Add plugins, set an intent, then ask for a mix move." };
}

juce::ValueTree RackAIAudioProcessor::createStateTree() const
{
    juce::ValueTree root ("RACK_AI_STATE");
    root.setProperty ("mode", aiModeToString (aiMode), nullptr);
    root.setProperty ("intent", sessionIntent, nullptr);

    juce::ValueTree slotRoot ("SLOTS");
    for (const auto& slot : slots)
    {
        juce::ValueTree node ("SLOT");
        node.setProperty ("name", slot.name, nullptr);
        node.setProperty ("routing", routingToString (slot.routing), nullptr);
        node.setProperty ("bypassed", slot.bypassed, nullptr);
        node.setProperty ("solo", slot.solo, nullptr);
        node.setProperty ("blend", slot.blend, nullptr);
        node.setProperty ("preGainDb", slot.preGainDb, nullptr);
        node.setProperty ("postGainDb", slot.postGainDb, nullptr);
        slotRoot.addChild (node, -1, nullptr);
    }
    root.addChild (slotRoot, -1, nullptr);

    juce::ValueTree chatRoot ("CHAT");
    for (const auto& line : chatHistory)
    {
        juce::ValueTree node ("LINE");
        node.setProperty ("text", line, nullptr);
        chatRoot.addChild (node, -1, nullptr);
    }
    root.addChild (chatRoot, -1, nullptr);

    return root;
}

void RackAIAudioProcessor::restoreFromStateTree (const juce::ValueTree& tree)
{
    if (! tree.hasType ("RACK_AI_STATE"))
        return;

    aiMode = tree.getProperty ("mode").toString() == "Auto" ? AiMode::autonomous : AiMode::suggest;
    sessionIntent = tree.getProperty ("intent").toString();
    slots.clear();

    const auto slotRoot = tree.getChildWithName ("SLOTS");
    for (int i = 0; i < slotRoot.getNumChildren(); ++i)
    {
        const auto node = slotRoot.getChild (i);
        SlotState slot;
        slot.name = node.getProperty ("name").toString();
        slot.routing = routingFromString (node.getProperty ("routing").toString());
        slot.bypassed = static_cast<bool> (node.getProperty ("bypassed"));
        slot.solo = static_cast<bool> (node.getProperty ("solo"));
        slot.blend = static_cast<float> (node.getProperty ("blend"));
        slot.preGainDb = static_cast<float> (node.getProperty ("preGainDb"));
        slot.postGainDb = static_cast<float> (node.getProperty ("postGainDb"));
        slots.add (slot);
    }

    chatHistory.clear();
    const auto chatRoot = tree.getChildWithName ("CHAT");
    for (int i = 0; i < chatRoot.getNumChildren(); ++i)
        chatHistory.add (chatRoot.getChild (i).getProperty ("text").toString());
}

void RackAIAudioProcessor::appendDecision (const DecisionLogEntry& entry)
{
    decisionLog.add (entry);
    if (decisionLog.size() > 50)
        decisionLog.remove (0);
}

juce::String RackAIAudioProcessor::nowForLog()
{
    return juce::Time::getCurrentTime().toString (true, true);
}

void RackAIAudioProcessor::sendPromptToAI (const juce::String& userPrompt)
{
    if (! mAIIntegration)
        return;

    // Basic serialization — derive from state tree when available
    juce::String sessionJSON = "{}";
    if (auto xml = createStateTree().createXml())
        sessionJSON = xml->toString();

    std::vector<AnalysisSnapshot> history;
    if (mAnalysisEngine)
        history = mAnalysisEngine->getAnalysisHistory();

    mAIIntegration->sendPrompt (userPrompt, sessionJSON, history);
}

juce::String RackAIAudioProcessor::getAIChatHistory() const
{
    return mAIIntegration ? mAIIntegration->getChatHistory() : juce::String();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RackAIAudioProcessor();
}
