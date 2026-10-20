#pragma once

#include "zfactory/shell/DesignSystem.h"
#include "zfactory/shell/ShellProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace zfactory
{
namespace shell
{

//==============================================================================
/** One drawn control.
 *
 *  The widget is a plain view over ShellProcessor::get/setControlNormalised, so
 *  the same class serves host parameters, macros and module-level values without
 *  the editor ever needing to know which is which. It refreshes on a timer,
 *  which is also what makes a macro-driven parameter show the value the engine
 *  is actually running rather than the stale value the host last wrote.
 */
class ControlWidget : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::Timer
{
public:
    ControlWidget (ShellProcessor& processor, int controlIndex, bool compact, bool showValue);

    void paint (juce::Graphics& g) override;
    void resized() override;

    int preferredWidth() const;
    int preferredHeight() const;

    int controlIndex() const noexcept { return m_controlIndex; }

    void setPalette (const design::Palette& palette) { m_palette = palette; repaint(); }

private:
    void timerCallback() override;

    void applyUserValue (double normalised);
    void refreshFromProcessor();

    ShellProcessor& m_processor;
    int m_controlIndex;
    bool m_compact;
    bool m_showValue;

    const UiControl* m_control = nullptr;
    design::Palette m_palette;

    juce::Slider m_slider;
    juce::TextButton m_switch { "-" };
    juce::Label m_name;
    juce::Label m_value;

    bool m_useSwitch = false;
    double m_lastShownValue = -1.0;
};

//==============================================================================
/** A group caption. Standard and Advanced draw these; Minimal does not. */
class SectionHeader : public juce::Component
{
public:
    SectionHeader (const juce::String& title, const juce::String& subtitle, const design::Palette& palette);

    void paint (juce::Graphics& g) override;

private:
    juce::String m_title;
    juce::String m_subtitle;
    design::Palette m_palette;
};

//==============================================================================
/** The one editor every generated product uses.
 *
 *  It contains no layout logic of its own: it builds the widgets the compiled
 *  plan describes and hands them to the layout engine for the product's
 *  template. Adding a product, or changing a product's control set, never means
 *  editing this file.
 */
class ShellEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit ShellEditor (ShellProcessor& processor);
    ~ShellEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Rendered interface size, for the offscreen QA capture. */
    juce::Rectangle<int> layoutBounds() const { return m_layoutBounds; }

private:
    void timerCallback() override;
    void buildInterface();
    void layoutInterface();

    ShellProcessor& m_processor;
    design::Palette m_palette;
    juce::LookAndFeel_V4 m_lookAndFeel;

    std::vector<std::unique_ptr<ControlWidget>> m_controls;   // indexed by plan control index
    std::vector<std::unique_ptr<SectionHeader>> m_headers;    // indexed by plan group index

    juce::Rectangle<int> m_layoutBounds;
    juce::String m_footer;
    juce::String m_error;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellEditor)
};

//==============================================================================
// The three Zarlino UI templates.
//
// Each one is a hand-written layout engine, not a stylesheet and not generated
// code. They receive the same widgets and the same compiled plan and differ only
// in how much of it they draw and how densely - which is what keeps three
// genuinely different information densities visually coherent.
//==============================================================================

/** The widgets a template lays out. Indices match the compiled UI plan. */
struct TemplateSurface
{
    std::vector<ControlWidget*> controls;   // plan control index -> widget, or nullptr
    std::vector<SectionHeader*> headers;    // plan group index -> header, or nullptr
    const UiPlan* plan = nullptr;
    design::Palette palette;
};

/** Result of laying out a template: the size of the CONTENT area the template
    needs. The editor adds its own chrome (title bar, footer) around it. */
struct TemplateLayout
{
    int width = 0;
    int height = 0;
};

/** Vertical offset of the template content area inside the editor window. The
    editor's paint() and the three layout engines both derive their geometry from
    these two numbers so the chrome can never overlap the controls. */
constexpr int templateContentTop()
{
    return design::Metrics::inset + design::Metrics::headerHeight + design::Metrics::unit;
}

constexpr int templateContentBottom()
{
    return design::Metrics::headerHeight + design::Metrics::inset;
}

TemplateLayout layoutMinimal (TemplateSurface& surface);
TemplateLayout layoutStandard (TemplateSurface& surface);
TemplateLayout layoutAdvanced (TemplateSurface& surface);

/** Dispatches on the plan's template. */
TemplateLayout layoutTemplate (TemplateSurface& surface);

} // namespace shell
} // namespace zfactory
