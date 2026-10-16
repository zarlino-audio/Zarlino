//==============================================================================
// Zarlino Plugin Factory - the built-in module library.
//
// Eleven capabilities adapted from Zarlino's shipped DSP are certified and
// registered here. Coverage is deliberate rather than exhaustive: the set is
// wide enough to build materially different conventional processors (channel
// strips, saturation units, vocal chains) out of the same kernel.
//
//   zarlino.gain                       - trimmed gain stage with polarity
//   zarlino.filter.multimode           - resonant LP/HP, 12 or 24 dB/oct
//   zarlino.eq.tilt                    - tilt EQ around a pivot
//   zarlino.eq.parametric              - four-band parametric EQ
//   zarlino.dynamics.compressor        - soft-knee compressor
//   zarlino.dynamics.gate              - gate / downward expander
//   zarlino.saturation                 - six-mode saturator (ZScorch curves + ADAA)
//   zarlino.limiter                    - brick-wall lookahead limiter
//   zarlino.shaping.transient          - transient shaper
//   zarlino.stereo.width               - mid/side width with bass-mono guard
//   zarlino.mix.drywet                 - dry/wet blend
//==============================================================================

#include "zfactory/ModuleRegistry.h"

#include "CompressorModule.h"
#include "DryWetModule.h"
#include "FilterModule.h"
#include "GainModule.h"
#include "GateModule.h"
#include "LimiterModule.h"
#include "ParametricEqModule.h"
#include "SaturationModule.h"
#include "StereoWidthModule.h"
#include "TiltEqModule.h"
#include "TransientShaperModule.h"

#include <memory>

namespace zfactory
{

namespace
{
/** Builds the registry entry for a module type from the module's own manifest,
    so the catalogue can never drift from what the module reports. The probe
    instance is discarded - every module returns a static manifest. */
template <typename ModuleType>
ModuleDescriptor descriptorFor()
{
    const ModuleType probe;

    ModuleDescriptor descriptor;
    descriptor.manifest = probe.getManifest();
    descriptor.create = []() -> DspModule* { return new ModuleType(); };
    return descriptor;
}
} // namespace

Result<size_t> registerBuiltinModules (ModuleRegistry& registry)
{
    ModuleDescriptor descriptors[] = {
        descriptorFor<GainModule>(),
        descriptorFor<FilterModule>(),
        descriptorFor<TiltEqModule>(),
        descriptorFor<ParametricEqModule>(),
        descriptorFor<CompressorModule>(),
        descriptorFor<GateModule>(),
        descriptorFor<SaturationModule>(),
        descriptorFor<LimiterModule>(),
        descriptorFor<TransientShaperModule>(),
        descriptorFor<StereoWidthModule>(),
        descriptorFor<DryWetModule>()
    };

    constexpr size_t count = sizeof (descriptors) / sizeof (descriptors[0]);

    for (size_t i = 0; i < count; ++i)
    {
        if (auto error = registry.add (std::move (descriptors[i])); error.failed())
            return Result<size_t>::failure (std::move (error));
    }

    return Result<size_t>::success (count);
}

} // namespace zfactory
