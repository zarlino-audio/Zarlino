#include "zfactory/shell/ShellEditor.h"

#include <algorithm>

namespace zfactory
{
namespace shell
{

//==============================================================================
// The three Zarlino UI templates.
//
// All three draw the same compiled plan, in the same order, from the same
// widgets and the same design tokens. They differ only in how much of the plan
// they surface and how tightly they pack it:
//
//   Minimal   one horizontal row, no group captions, label only
//   Standard  grouped blocks, two columns, group captions
//   Advanced  grouped blocks, three columns, value readouts, per-module sections
//
// That is why the three densities are coherent rather than merely different: a
// control that appears in Minimal appears in the same place in the reading order
// in Standard and Advanced, drawn the same way.
//==============================================================================

namespace
{

template <typename T>
int widest (const std::vector<T*>& widgets)
{
    int width = 0;

    for (const auto* widget : widgets)
        if (widget != nullptr)
            width = std::max (width, widget->preferredWidth());

    return width;
}

struct Cursor
{
    int x = 0;
    int y = 0;
    int rowHeight = 0;
    int columns = 1;
    int column = 0;
    int columnWidth = 0;
    int gapX = 0;
    int gapY = 0;
    int originX = 0;
    int originY = 0;
    int maxRight = 0;
};

juce::Rectangle<int> place (Cursor& cursor, int width, int height)
{
    const auto x = cursor.originX + cursor.x;
    const auto y = cursor.originY + cursor.y;

    cursor.rowHeight = std::max (cursor.rowHeight, height);
    cursor.maxRight = std::max (cursor.maxRight, x + width - cursor.originX);

    const juce::Rectangle<int> bounds (x, y, width, height);

    if (++cursor.column >= cursor.columns)
    {
        cursor.column = 0;
        cursor.x = 0;
        cursor.y += cursor.rowHeight + cursor.gapY;
        cursor.rowHeight = 0;
    }
    else
    {
        cursor.x += cursor.columnWidth + cursor.gapX;
    }

    return bounds;
}

void finishRow (Cursor& cursor)
{
    if (cursor.column != 0)
    {
        cursor.column = 0;
        cursor.x = 0;
        cursor.y += cursor.rowHeight + cursor.gapY;
        cursor.rowHeight = 0;
    }
}

//==============================================================================
// Minimal - a performance strip.
//
// Deliberately one row and deliberately unlabelled beyond the control names: the
// point of the minimal template is that a player can see every essential control
// at once without reading.
//==============================================================================
TemplateLayout layoutMinimalImpl (TemplateSurface& surface)
{
    int width = 0;
    int height = 0;

    for (auto* widget : surface.controls)
    {
        if (widget == nullptr)
            continue;

        width += widget->preferredWidth() + design::Metrics::gap;
        height = std::max (height, widget->preferredHeight());
    }

    if (width > 0)
        width -= design::Metrics::gap;   // no trailing gap

    int x = design::Metrics::inset;

    for (auto* widget : surface.controls)
    {
        if (widget == nullptr)
            continue;

        widget->setBounds (x, templateContentTop(),
                           widget->preferredWidth(), widget->preferredHeight());
        x += widget->preferredWidth() + design::Metrics::gap;
    }

    return { design::Metrics::inset * 2 + width, height };
}

//==============================================================================
// Standard - every control the specification exposes, grouped, two columns.
//==============================================================================
TemplateLayout layoutStandardImpl (TemplateSurface& surface)
{
    const auto columnWidth = std::max (design::Metrics::minControlWidth, widest (surface.controls));
    const auto columns = 2;

    Cursor cursor;
    cursor.columns = columns;
    cursor.columnWidth = columnWidth;
    cursor.gapX = design::Metrics::gap;
    cursor.gapY = design::Metrics::rowGap;
    cursor.originX = design::Metrics::inset;
    cursor.originY = templateContentTop();

    int y = templateContentTop();
    int maxRight = 0;

    for (size_t g = 0; g < surface.plan->groups.size(); ++g)
    {
        const auto& group = surface.plan->groups[g];

        if (auto* header = g < surface.headers.size() ? surface.headers[g] : nullptr)
        {
            finishRow (cursor);
            cursor.y = y - templateContentTop();
            cursor.x = 0;

            header->setBounds (design::Metrics::inset, y,
                               design::Metrics::inset * 2 + columns * columnWidth
                                   + (columns - 1) * design::Metrics::gap
                                   - design::Metrics::inset * 2,
                               design::Metrics::headerHeight);
            y += design::Metrics::headerHeight + design::Metrics::headerGap;
            cursor.y = y - templateContentTop();
        }

        for (const auto index : group.controlIndices)
        {
            if (index < 0 || index >= static_cast<int> (surface.controls.size()))
                continue;

            auto* widget = surface.controls[static_cast<size_t> (index)];

            if (widget == nullptr)
                continue;

            const auto bounds = place (cursor, columnWidth, widget->preferredHeight());
            widget->setBounds (bounds);
            maxRight = std::max (maxRight, bounds.getRight() - design::Metrics::inset);
        }
    }

    finishRow (cursor);

    const auto width = design::Metrics::inset * 2 + columns * columnWidth
                       + (columns - 1) * design::Metrics::gap;

    return { std::max (width, maxRight + design::Metrics::inset),
             std::max (cursor.y + cursor.rowHeight, design::Metrics::knobSize) };
}

//==============================================================================
// Advanced - everything, including the module-level controls the specification
// leaves unexposed, packed into three columns with value readouts.
//==============================================================================
TemplateLayout layoutAdvancedImpl (TemplateSurface& surface)
{
    const auto columnWidth = std::max (design::Metrics::minControlWidth, widest (surface.controls));
    const auto columns = 3;

    Cursor cursor;
    cursor.columns = columns;
    cursor.columnWidth = columnWidth;
    cursor.gapX = design::Metrics::gap;
    cursor.gapY = design::Metrics::gap;
    cursor.originX = design::Metrics::inset;
    cursor.originY = templateContentTop();

    int y = templateContentTop();
    int maxRight = 0;

    const auto gridWidth = columns * columnWidth + (columns - 1) * design::Metrics::gap;

    for (size_t g = 0; g < surface.plan->groups.size(); ++g)
    {
        const auto& group = surface.plan->groups[g];

        if (auto* header = g < surface.headers.size() ? surface.headers[g] : nullptr)
        {
            finishRow (cursor);

            header->setBounds (design::Metrics::inset, y, gridWidth, design::Metrics::headerHeight);
            y += design::Metrics::headerHeight + design::Metrics::headerGap;
            cursor.y = y - templateContentTop();
            cursor.x = 0;
        }

        for (const auto index : group.controlIndices)
        {
            if (index < 0 || index >= static_cast<int> (surface.controls.size()))
                continue;

            auto* widget = surface.controls[static_cast<size_t> (index)];

            if (widget == nullptr)
                continue;

            const auto bounds = place (cursor, columnWidth, widget->preferredHeight());
            widget->setBounds (bounds);
            maxRight = std::max (maxRight, bounds.getRight() - design::Metrics::inset);
        }
    }

    finishRow (cursor);

    const auto width = design::Metrics::inset * 2 + gridWidth;

    return { std::max (width, maxRight + design::Metrics::inset),
             std::max (cursor.y + cursor.rowHeight, design::Metrics::knobSize) };
}

} // namespace

//==============================================================================
TemplateLayout layoutMinimal (TemplateSurface& surface)
{
    return layoutMinimalImpl (surface);
}

TemplateLayout layoutStandard (TemplateSurface& surface)
{
    return layoutStandardImpl (surface);
}

TemplateLayout layoutAdvanced (TemplateSurface& surface)
{
    return layoutAdvancedImpl (surface);
}

TemplateLayout layoutTemplate (TemplateSurface& surface)
{
    if (surface.plan == nullptr)
        return { 0, 0 };

    switch (surface.plan->uiTemplate)
    {
        case UiTemplate::Minimal:  return layoutMinimalImpl (surface);
        case UiTemplate::Standard: return layoutStandardImpl (surface);
        case UiTemplate::Advanced: return layoutAdvancedImpl (surface);
    }

    return layoutStandardImpl (surface);
}

} // namespace shell
} // namespace zfactory
