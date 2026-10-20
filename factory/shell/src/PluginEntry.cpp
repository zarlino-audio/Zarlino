//==============================================================================
// The plugin entry point.
//
// Every JUCE plugin build calls createPluginFilter() once per instantiation, and
// it is the ONLY place a product's class is named. Because the shell has exactly
// one processor class, this file is identical for every generated product: the
// product is distinguished by its embedded specification and its generated
// identity, never by a different processor.
//==============================================================================

#include "zfactory/shell/ShellProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new zfactory::shell::ShellProcessor();
}
