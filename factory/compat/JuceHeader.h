#pragma once

//==============================================================================
// JuceHeader compatibility shim for the Zarlino Plugin Factory.
//
// The factory kernel reuses DSP that ships inside the plugin projects (most
// notably ZScorch's `Saturator`), and those sources include `<JuceHeader.h>`,
// a per-target header that JUCE only generates for juce_add_* targets. Rather
// than converting the kernel into a plugin target, this directory supplies the
// minimal JuceHeader surface those sources need.
//
// This directory is a PRIVATE include path of the zarlino_factory target only,
// so it can never shadow a plugin's own generated JuceHeader.h.
//==============================================================================

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
