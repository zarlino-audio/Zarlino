#include "PluginEditor.h"

namespace
{
    const auto bg = juce::Colour (0xff17191f);
    const auto panel = juce::Colour (0xff242833);
    const auto panelEdge = juce::Colour (0xff3a4050);
    const auto text = juce::Colour (0xffdce3ee);
    const auto muted = juce::Colour (0xff8992a3);
    const auto accent = juce::Colour (0xff00c8a0);
    const auto warning = juce::Colour (0xffffc857);

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour colour = panel)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    juce::String dbText (float value)
    {
        return value <= -99.0f ? "-inf dB" : juce::String (value, 1) + " dB";
    }
}

class RackAIAudioProcessorEditor::SlotRow : public juce::Component
{
public:
    SlotRow (RackAIAudioProcessor& processorToUse, int slotIndexToUse)
        : processor (processorToUse), slotIndex (slotIndexToUse)
    {
        bypassButton.setButtonText ("B");
        soloButton.setButtonText ("S");

        routingBox.addItem ("Series", 1);
        routingBox.addItem ("Bus A", 2);
        routingBox.addItem ("Bus B", 3);
        routingBox.addItem ("Bus C", 4);
        routingBox.addItem ("Bus D", 5);

        blendSlider.setRange (0.0, 100.0, 1.0);
        blendSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        blendSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
        blendSlider.setTextValueSuffix ("%");

        addAndMakeVisible (nameLabel);
        addAndMakeVisible (routingBox);
        addAndMakeVisible (bypassButton);
        addAndMakeVisible (soloButton);
        addAndMakeVisible (blendSlider);

        bypassButton.onClick = [this] { processor.setSlotBypassed (slotIndex, bypassButton.getToggleState()); };
        soloButton.onClick = [this] { processor.setSlotSolo (slotIndex, soloButton.getToggleState()); };
        routingBox.onChange = [this]
        {
            processor.setSlotRouting (slotIndex, RackAIAudioProcessor::routingFromString (routingBox.getText()));
        };
        blendSlider.onValueChange = [this] { processor.setSlotBlend (slotIndex, static_cast<float> (blendSlider.getValue() / 100.0)); };
    }

    void sync()
    {
        const auto& slot = processor.getSlot (slotIndex);
        nameLabel.setText (juce::String (slotIndex + 1).paddedLeft ('0', 2) + "  " + slot.name, juce::dontSendNotification);
        bypassButton.setToggleState (slot.bypassed, juce::dontSendNotification);
        soloButton.setToggleState (slot.solo, juce::dontSendNotification);
        routingBox.setText (RackAIAudioProcessor::routingToString (slot.routing), juce::dontSendNotification);
        blendSlider.setValue (slot.blend * 100.0f, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().reduced (2);
        drawPanel (g, bounds, juce::Colour (0xff20242e));

        const auto meterArea = bounds.removeFromRight (58).reduced (8, 13).toFloat();
        g.setColour (juce::Colour (0xff111319));
        g.fillRoundedRectangle (meterArea, 3.0f);
        g.setColour (accent.withAlpha (0.7f));
        g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * 0.62f), 3.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);
        area.removeFromRight (64);
        bypassButton.setBounds (area.removeFromRight (30));
        area.removeFromRight (4);
        soloButton.setBounds (area.removeFromRight (30));
        area.removeFromRight (8);
        blendSlider.setBounds (area.removeFromRight (116));
        area.removeFromRight (8);
        routingBox.setBounds (area.removeFromRight (92));
        area.removeFromRight (10);
        nameLabel.setBounds (area);
    }

private:
    RackAIAudioProcessor& processor;
    int slotIndex = 0;
    juce::Label nameLabel;
    juce::ComboBox routingBox;
    juce::ToggleButton bypassButton;
    juce::ToggleButton soloButton;
    juce::Slider blendSlider;
};

class RackAIAudioProcessorEditor::DecisionLogTable : public juce::Component,
                                                     public juce::TableListBoxModel
{
public:
    explicit DecisionLogTable (RackAIAudioProcessor& processorToUse)
        : processor (processorToUse)
    {
        table.getHeader().addColumn ("Time", 1, 70);
        table.getHeader().addColumn ("Slot", 2, 120);
        table.getHeader().addColumn ("Parameter", 3, 110);
        table.getHeader().addColumn ("Before", 4, 90);
        table.getHeader().addColumn ("After", 5, 150);
        table.getHeader().addColumn ("Mode", 6, 70);
        table.getHeader().addColumn ("Response", 7, 90);
        table.setModel (this);
        addAndMakeVisible (table);
    }

    void refresh()
    {
        entries = processor.getDecisionLog();
        table.updateContent();
        repaint();
    }

    int getNumRows() override { return entries.size(); }

    void paintRowBackground (juce::Graphics& g, int, int, int, bool selected) override
    {
        g.fillAll (selected ? accent.withAlpha (0.18f) : juce::Colour (0xff1d2028));
    }

    void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (row, entries.size()))
            return;

        const auto& entry = entries.getReference (row);
        juce::String value;

        if (column == 1) value = entry.time;
        if (column == 2) value = entry.slot;
        if (column == 3) value = entry.parameter;
        if (column == 4) value = entry.before;
        if (column == 5) value = entry.after;
        if (column == 6) value = entry.mode;
        if (column == 7) value = entry.response;

        g.setColour (text);
        g.setFont (13.0f);
        g.drawFittedText (value, juce::Rectangle<int> (width, height).reduced (6, 0), juce::Justification::centredLeft, 1);
    }

    void resized() override { table.setBounds (getLocalBounds()); }

private:
    RackAIAudioProcessor& processor;
    juce::TableListBox table;
    juce::Array<RackAIAudioProcessor::DecisionLogEntry> entries;
};

RackAIAudioProcessorEditor::RackAIAudioProcessorEditor (RackAIAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setResizable (true, true);
    setResizeLimits (900, 600, 1600, 1000);
    setSize (1100, 680);

    titleLabel.setText ("RACK/AI", juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, text);
    titleLabel.setFont (juce::FontOptions (26.0f, juce::Font::bold));

    rackSummaryLabel.setColour (juce::Label::textColourId, muted);
    analysisLabel.setColour (juce::Label::textColourId, text);
    analysisLabel.setJustificationType (juce::Justification::centred);

    intentEditor.setMultiLine (true);
    intentEditor.setText (audioProcessor.getSessionIntent(), false);
    intentEditor.onFocusLost = [this] { audioProcessor.setSessionIntent (intentEditor.getText()); };

    chatHistory.setMultiLine (true);
    chatHistory.setReadOnly (true);
    chatHistory.setScrollbarsShown (true);
    promptEditor.setMultiLine (true);
    sendButton.onClick = [this] { submitPrompt(); };

    autoButton.setClickingTogglesState (true);
    suggestButton.setClickingTogglesState (true);
    autoButton.setRadioGroupId (10);
    suggestButton.setRadioGroupId (10);
    autoButton.onClick = [this] { audioProcessor.setAiMode (RackAIAudioProcessor::AiMode::autonomous); };
    suggestButton.onClick = [this] { audioProcessor.setAiMode (RackAIAudioProcessor::AiMode::suggest); };

    addSlotButton.onClick = [this]
    {
        audioProcessor.addPlaceholderSlot();
        refreshSlotRows();
    };

    const char* prompts[] = { "Make it punch harder", "Reduce muddiness", "Brighten the top end", "Check gain staging", "Explain what changed" };
    for (size_t i = 0; i < quickPromptButtons.size(); ++i)
    {
        quickPromptButtons[i] = std::make_unique<juce::TextButton> (prompts[i]);
        quickPromptButtons[i]->onClick = [this, i, prompts]
        {
            promptEditor.setText (prompts[i], false);
            submitPrompt();
        };
        addAndMakeVisible (*quickPromptButtons[i]);
    }

    logTable = std::make_unique<DecisionLogTable> (audioProcessor);
    bottomTabs.addTab ("Change Log", panel, logTable.get(), false);

    slotViewport.setViewedComponent (&slotContent, false);
    slotViewport.setScrollBarsShown (true, false);

    for (auto* component : { &titleLabel, &rackSummaryLabel, &slotViewport, &addSlotButton, &analysisToggle,
                            &autoButton, &suggestButton, &analysisLabel, &intentEditor, &chatHistory,
                            &promptEditor, &sendButton, &bottomTabs, &pluginHostPlaceholder })
        addAndMakeVisible (component);

    refreshSlotRows();
    refreshChat();
    startTimerHz (12);
}

RackAIAudioProcessorEditor::~RackAIAudioProcessorEditor()
{
    audioProcessor.setSessionIntent (intentEditor.getText());
}

void RackAIAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    auto area = getLocalBounds().reduced (14);
    auto header = area.removeFromTop (48);
    g.setColour (accent);
    g.fillRoundedRectangle (header.removeFromLeft (5).toFloat(), 2.0f);

    auto content = area;
    const auto left = content.removeFromLeft (360);
    content.removeFromLeft (12);
    auto right = content.removeFromRight (310);
    content.removeFromRight (12);

    drawPanel (g, left);
    drawPanel (g, content);
    drawPanel (g, right);
}

void RackAIAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    auto header = area.removeFromTop (48);
    header.removeFromLeft (14);
    titleLabel.setBounds (header.removeFromLeft (180));
    rackSummaryLabel.setBounds (header);

    auto content = area;
    auto left = content.removeFromLeft (360).reduced (12);
    content.removeFromLeft (12);
    auto right = content.removeFromRight (310).reduced (12);
    content.removeFromRight (12);
    auto centre = content.reduced (12);

    auto leftTop = left.removeFromTop (34);
    addSlotButton.setBounds (leftTop.removeFromLeft (110));
    leftTop.removeFromLeft (8);
    analysisToggle.setBounds (leftTop.removeFromLeft (110));
    slotViewport.setBounds (left);

    auto chatTop = right.removeFromTop (64);
    autoButton.setBounds (chatTop.removeFromLeft (72));
    chatTop.removeFromLeft (8);
    suggestButton.setBounds (chatTop.removeFromLeft (88));
    right.removeFromTop (8);
    intentEditor.setBounds (right.removeFromTop (82));
    right.removeFromTop (8);
    layoutQuickPrompts (right.removeFromTop (70));
    right.removeFromTop (8);
    sendButton.setBounds (right.removeFromBottom (30));
    right.removeFromBottom (8);
    promptEditor.setBounds (right.removeFromBottom (86));
    right.removeFromBottom (8);
    chatHistory.setBounds (right);

    analysisLabel.setBounds (centre.removeFromTop (48));
    centre.removeFromTop (10);
    bottomTabs.setBounds (centre.removeFromBottom (180));
    centre.removeFromBottom (10);
    pluginHostPlaceholder.setBounds (centre);

    refreshSlotRows();
}

void RackAIAudioProcessorEditor::timerCallback()
{
    refreshAnalysisLabels();
    logTable->refresh();
    autoButton.setToggleState (audioProcessor.getAiMode() == RackAIAudioProcessor::AiMode::autonomous, juce::dontSendNotification);
    suggestButton.setToggleState (audioProcessor.getAiMode() == RackAIAudioProcessor::AiMode::suggest, juce::dontSendNotification);
}

void RackAIAudioProcessorEditor::refreshSlotRows()
{
    slotRows.clear();
    slotContent.removeAllChildren();

    const auto rowHeight = 64;
    for (int i = 0; i < audioProcessor.getSlotCount(); ++i)
    {
        auto* row = slotRows.add (new SlotRow (audioProcessor, i));
        row->sync();
        slotContent.addAndMakeVisible (row);
        row->setBounds (0, i * rowHeight, juce::jmax (320, slotViewport.getWidth() - 18), rowHeight);
    }

    slotContent.setSize (juce::jmax (320, slotViewport.getWidth() - 18),
                         juce::jmax (slotViewport.getHeight(), audioProcessor.getSlotCount() * rowHeight));
    rackSummaryLabel.setText (juce::String (audioProcessor.getSlotCount()) + "/16 slots | VST3 rack shell | "
                              + RackAIAudioProcessor::aiModeToString (audioProcessor.getAiMode()) + " mode",
                              juce::dontSendNotification);
}

void RackAIAudioProcessorEditor::refreshChat()
{
    chatHistory.setText (audioProcessor.getChatHistory().joinIntoString ("\n\n"), false);
}

void RackAIAudioProcessorEditor::refreshAnalysisLabels()
{
    const auto snapshot = audioProcessor.getAnalysisSnapshot();
    analysisLabel.setText ("Input analysis  RMS " + dbText (snapshot.rmsDb)
                           + "    Peak " + dbText (snapshot.peakDb)
                           + "    Crest " + juce::String (snapshot.crestDb, 1) + " dB"
                           + "    Width " + juce::String (snapshot.stereoWidth * 100.0f, 0) + "%",
                           juce::dontSendNotification);

    rackSummaryLabel.setText (juce::String (audioProcessor.getSlotCount()) + "/16 slots | VST3 rack shell | "
                              + RackAIAudioProcessor::aiModeToString (audioProcessor.getAiMode()) + " mode",
                              juce::dontSendNotification);
}

void RackAIAudioProcessorEditor::submitPrompt()
{
    audioProcessor.submitPrompt (promptEditor.getText());
    promptEditor.clear();
    refreshChat();
}

void RackAIAudioProcessorEditor::layoutQuickPrompts (juce::Rectangle<int> area)
{
    for (auto& button : quickPromptButtons)
    {
        auto buttonArea = area.removeFromTop (22);
        button->setBounds (buttonArea.removeFromLeft (area.getWidth() / 2 - 4));
        if (&button != &quickPromptButtons.back())
            area.removeFromTop (2);
    }
}
