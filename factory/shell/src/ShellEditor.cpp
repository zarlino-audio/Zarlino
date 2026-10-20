#include "zfactory/shell/ShellEditor.h"

#include "zfactory/shell/LicenseGate.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{
namespace shell
{

namespace
{

/** Rule thickness used by the shared chrome. */
constexpr int kHairline = 4;

/** True when the engine currently has a value for this control that differs from
    what the widget is showing. Comparing at the widget's own display precision
    avoids pointless repaints on a 30 Hz timer. */
bool differs (double a, double b)
{
    return std::abs (a - b) > 1.0e-6;
}

} // namespace

//==============================================================================
ControlWidget::ControlWidget (ShellProcessor& processor, int controlIndex, bool compact, bool showValue)
    : m_processor (processor),
      m_controlIndex (controlIndex),
      m_compact (compact),
      m_showValue (showValue),
      m_control (processor.getControl (controlIndex))
{
    m_palette = design::Palette::fromAccent (processor.product().accentColor());

    if (m_control == nullptr)
        return;

    m_useSwitch = m_control->kind == UiControlKind::Switch;

    if (m_useSwitch)
    {
        m_switch.setButtonText (juce::String (m_control->label));
        m_switch.setClickingTogglesState (true);
        m_switch.onClick = [this] { applyUserValue (m_switch.getToggleState() ? 1.0 : 0.0); };
        addAndMakeVisible (m_switch);
    }
    else
    {
        const auto isKnob = m_control->kind == UiControlKind::Knob;

        m_slider.setSliderStyle (isKnob ? juce::Slider::RotaryVerticalDrag
                                        : juce::Slider::LinearHorizontal);
        m_slider.setTextBoxStyle (m_showValue ? juce::Slider::TextBoxBelow
                                              : juce::Slider::NoTextBox, false, 0, 0);

        if (m_control->stepCount > 1)
        {
            m_slider.setRange (m_control->minValue, m_control->maxValue,
                               (m_control->maxValue - m_control->minValue)
                                   / static_cast<double> (m_control->stepCount - 1));
        }
        else
        {
            m_slider.setRange (m_control->minValue, m_control->maxValue, 0.0);
        }

        if (m_control->logarithmic && m_control->minValue > 0.0f)
            m_slider.setSkewFactorFromMidPoint (std::sqrt (static_cast<double> (m_control->minValue)
                                                           * static_cast<double> (m_control->maxValue)));

        m_slider.setDoubleClickReturnValue (true, m_control->defaultValue);
        m_slider.onDragStart = [this] { m_processor.beginControlGesture (m_controlIndex); };
        m_slider.onDragEnd = [this] { m_processor.endControlGesture (m_controlIndex); };
        m_slider.onValueChange = [this]
        {
            const auto range = m_slider.getRange();
            const auto span = range.getLength();

            const auto normalised = span > 0.0 ? (m_slider.getValue() - range.getStart()) / span : 0.0;
            applyUserValue (normalised);
        };

        addAndMakeVisible (m_slider);
    }

    // Minimal shows the label only for the controls it draws at all, which is
    // what makes it a "performance" view; the others always label.
    m_name.setText (juce::String (m_control->label), juce::dontSendNotification);
    m_name.setJustificationType (juce::Justification::centred);
    m_name.setFont (design::labelFont());
    m_name.setColour (juce::Label::textColourId, m_palette.textDim);
    m_name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (m_name);

    if (m_showValue)
    {
        m_value.setJustificationType (juce::Justification::centred);
        m_value.setFont (design::valueFont());
        m_value.setColour (juce::Label::textColourId, m_palette.text);
        m_value.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (m_value);
    }

    setTooltip (juce::String::fromUTF8 (m_control->tooltip.c_str()));

    refreshFromProcessor();
    startTimerHz (30);
}

void ControlWidget::applyUserValue (double normalised)
{
    if (m_control == nullptr)
        return;

    m_processor.setControlFromUser (m_controlIndex, static_cast<float> (normalised));
}

void ControlWidget::refreshFromProcessor()
{
    if (m_control == nullptr)
        return;

    const auto normalised = static_cast<double> (m_processor.getControlNormalised (m_controlIndex));

    if (differs (normalised, m_lastShownValue))
    {
        m_lastShownValue = normalised;

        if (m_useSwitch)
        {
            m_switch.setToggleState (normalised >= 0.5, juce::dontSendNotification);
        }
        else
        {
            const auto range = m_slider.getRange();
            const auto span = range.getLength();

            m_slider.setValue (range.getStart() + span * normalised, juce::dontSendNotification);
        }
    }

    if (m_showValue)
        m_value.setText (m_processor.getControlText (m_controlIndex), juce::dontSendNotification);
}

void ControlWidget::timerCallback()
{
    // A macro can move a parameter without the user touching this widget, and the
    // host can move it without notifying us at all, so the widget re-reads the
    // engine rather than trusting its own last value.
    refreshFromProcessor();
}

//==============================================================================
int ControlWidget::preferredWidth() const
{
    if (m_control == nullptr)
        return design::Metrics::minControlWidth;

    if (m_useSwitch)
        return design::Metrics::switchWidth;

    switch (m_control->kind)
    {
        case UiControlKind::Fader:
        case UiControlKind::Select: return design::Metrics::faderWidth;
        case UiControlKind::Knob:
        case UiControlKind::Switch:
        default:                    return m_compact ? design::Metrics::knobSizeCompact
                                                      : design::Metrics::knobSize;
    }
}

int ControlWidget::preferredHeight() const
{
    if (m_control == nullptr)
        return design::Metrics::knobSize;

    const auto labelHeight = design::Metrics::labelHeight;
    const auto valueHeight = m_showValue ? design::Metrics::valueHeight : 0;

    if (m_useSwitch)
        return design::Metrics::switchHeight + labelHeight + valueHeight;

    if (m_control->kind == UiControlKind::Fader || m_control->kind == UiControlKind::Select)
        return design::Metrics::faderHeight + labelHeight + valueHeight;

    const auto knob = m_compact ? design::Metrics::knobSizeCompact : design::Metrics::knobSize;

    // A slider with a text box needs room for the readout below the knob.
    return knob + labelHeight + valueHeight + (m_showValue ? design::Metrics::labelHeight : 0);
}

void ControlWidget::resized()
{
    auto area = getLocalBounds();

    if (m_control == nullptr)
        return;

    if (m_useSwitch)
    {
        auto labelArea = area.removeFromBottom (design::Metrics::labelHeight);
        m_name.setBounds (labelArea);

        if (m_showValue)
            m_value.setBounds (area.removeFromBottom (design::Metrics::valueHeight));

        m_switch.setBounds (area.reduced (design::Metrics::gap, 0));
        return;
    }

    auto nameArea = area.removeFromBottom (design::Metrics::labelHeight);
    m_name.setBounds (nameArea);

    if (m_showValue)
        m_value.setBounds (area.removeFromBottom (design::Metrics::valueHeight));

    m_slider.setBounds (area);
}

void ControlWidget::paint (juce::Graphics& g)
{
    if (m_control == nullptr)
        return;

    // A module-level control is not a host parameter: it is marked so a user can
    // tell an automatable control from a shell-owned one at a glance.
    if (m_control->moduleLevel)
    {
        g.setColour (m_palette.outline.withAlpha (0.35f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f),
                                static_cast<float> (kHairline),
                                1.0f);
    }
}

//==============================================================================
SectionHeader::SectionHeader (const juce::String& title, const juce::String& subtitle,
                              const design::Palette& palette)
    : m_title (title), m_subtitle (subtitle), m_palette (palette)
{
    setInterceptsMouseClicks (false, false);
}

void SectionHeader::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (m_palette.text);
    g.setFont (design::bodyFontBold());
    g.drawText (m_title, area.removeFromTop (area.getHeight() / 2),
                juce::Justification::centredLeft, true);

    if (m_subtitle.isNotEmpty())
    {
        g.setColour (m_palette.textDim);
        g.setFont (design::labelFont());
        g.drawText (m_subtitle, area, juce::Justification::centredLeft, true);
    }

    design::paintHorizontalRule (g, getLocalBounds().removeFromBottom (kHairline),
                                 m_palette);
}

//==============================================================================
ShellEditor::ShellEditor (ShellProcessor& processor)
    : juce::AudioProcessorEditor (processor),
      m_processor (processor)
{
    m_palette = design::Palette::fromAccent (processor.product().accentColor());
    design::applyLookAndFeel (m_lookAndFeel, m_palette);
    setLookAndFeel (&m_lookAndFeel);

    setResizable (false, false);

    initializeLicensing (processor.product().pluginId());
    m_footer = juce::String (processor.product().buildStamp())
               + juce::String (" | ") + juce::String (currentLicense().describe());

    buildInterface();
    layoutInterface();

    startTimerHz (4);
}

ShellEditor::~ShellEditor()
{
    setLookAndFeel (nullptr);
}

void ShellEditor::buildInterface()
{
    const auto& plan = m_processor.product().plan();

    if (! m_processor.isProductValid())
    {
        m_error = juce::String (m_processor.initError().describe());
        return;
    }

    m_controls.resize (plan.controls.size());
    m_headers.resize (plan.groups.size());

    const auto compact = plan.uiTemplate == UiTemplate::Minimal;
    const auto showValue = plan.uiTemplate == UiTemplate::Advanced;

    for (size_t i = 0; i < plan.controls.size(); ++i)
    {
        auto widget = std::make_unique<ControlWidget> (m_processor, static_cast<int> (i),
                                                       compact, showValue);
        widget->setPalette (m_palette);
        addAndMakeVisible (*widget);
        m_controls[i] = std::move (widget);
    }

    // Minimal carries no group captions: that is a deliberate part of its lower
    // information density, not an omission.
    if (plan.uiTemplate == UiTemplate::Minimal)
        return;

    for (size_t g = 0; g < plan.groups.size(); ++g)
    {
        const auto& group = plan.groups[g];

        juce::String subtitle;

        if (plan.uiTemplate == UiTemplate::Advanced)
            subtitle << group.controlIndices.size()
                     << (group.controlIndices.size() == 1 ? " control" : " controls");

        auto header = std::make_unique<SectionHeader> (juce::String::fromUTF8 (group.name.c_str()),
                                                       subtitle, m_palette);
        addAndMakeVisible (*header);
        m_headers[g] = std::move (header);
    }
}

void ShellEditor::layoutInterface()
{
    TemplateSurface surface;
    surface.plan = &m_processor.product().plan();
    surface.palette = m_palette;

    for (auto& widget : m_controls)
        surface.controls.push_back (widget.get());

    for (auto& header : m_headers)
        surface.headers.push_back (header.get());

    TemplateLayout layout;

    if (m_processor.isProductValid())
        layout = layoutTemplate (surface);

    if (layout.width <= 0 || layout.height <= 0)
    {
        layout.width = 420;
        layout.height = 160;
    }

    m_layoutBounds = { 0, 0, layout.width, layout.height };
    setSize (layout.width, templateContentTop() + layout.height + templateContentBottom());
}

void ShellEditor::resized()
{
    if (! m_processor.isProductValid())
        return;

    TemplateSurface surface;
    surface.plan = &m_processor.product().plan();

    for (auto& widget : m_controls)
        surface.controls.push_back (widget.get());

    for (auto& header : m_headers)
        surface.headers.push_back (header.get());

    // The layout engine is run twice per resize: once to size the editor from the
    // template's natural extent, and once from resized() to place the children.
    // It is deterministic, so both runs place everything identically.
    layoutTemplate (surface);
}

void ShellEditor::timerCallback()
{
    // The licence and build stamp never change while the plugin is open, so the
    // only periodic work here is repainting the footer after a licence change.
    repaint();
}

void ShellEditor::paint (juce::Graphics& g)
{
    g.fillAll (m_palette.background);

    auto area = getLocalBounds().reduced (design::Metrics::inset);

    const auto headerHeight = design::Metrics::headerHeight;
    auto headerArea = area.removeFromTop (headerHeight);

    g.setColour (m_palette.text);
    g.setFont (design::titleFont());
    g.drawText (juce::String::fromUTF8 (m_processor.product().name().c_str()),
                headerArea.removeFromLeft (area.getWidth() - 120),
                juce::Justification::centredLeft, true);

    g.setColour (m_palette.accent);
    g.setFont (design::labelFont());
    g.drawText (juce::String::fromUTF8 (m_processor.product().category().c_str()),
                headerArea, juce::Justification::centredRight, true);

    design::paintHorizontalRule (g, area.removeFromTop (kHairline), m_palette);

    // Footer: build stamp + licence, so a user or a reviewer can tell exactly
    // which artifact they are looking at without opening a build system.
    auto footerArea = getLocalBounds().removeFromBottom (design::Metrics::headerHeight)
                          .reduced (design::Metrics::inset, 0);

    design::paintFooterText (g, footerArea, m_palette, m_footer);

    if (m_error.isNotEmpty())
    {
        g.setColour (juce::Colours::orange);
        g.setFont (design::bodyFont());
        g.drawFittedText (m_error, area.reduced (design::Metrics::inset),
                          juce::Justification::centred, 4);
    }
}

} // namespace shell
} // namespace zfactory
