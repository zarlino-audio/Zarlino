//==============================================================================
// ZarlinoFactory_Check - factory kernel CLI.
//
//   ZarlinoFactory_Check --catalog
//       Print the certified module catalogue and its fingerprint.
//
//   ZarlinoFactory_Check <spec.json> [--sample-rate N] [--block N] [--render]
//       Load a PluginSpec from disk, run strict validation, resolve every module
//       through the registry, build the graph, prepare it, and (with --render)
//       push a deterministic test signal through it and report the output.
//
// Exit codes: 0 success, 1 spec/validation failure, 2 usage error.
//==============================================================================

#include "zfactory/GraphEngine.h"
#include "zfactory/ModuleRegistry.h"
#include "zfactory/PluginSpec.h"
#include "zfactory/Version.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int printCatalog (const zfactory::ModuleRegistry& registry)
{
    std::cout << "Zarlino Plugin Factory kernel " << zfactory::kKernelVersionString
              << " (schema v" << zfactory::kSchemaVersion << ")\n";
    std::cout << "certified modules: " << registry.size() << "\n";
    std::cout << registry.describeCatalog();

    std::cout << "catalog fingerprint: 0x" << std::hex
              << registry.catalogFingerprint() << std::dec << "\n";

    return 0;
}

} // namespace

int main (int argc, char** argv)
{
    using namespace zfactory;

    std::string specPath;
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool render = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string argument (argv[i]);

        if (argument == "--catalog")
        {
            return printCatalog (builtinRegistry());
        }
        else if (argument == "--render")
        {
            render = true;
        }
        else if (argument == "--sample-rate" && i + 1 < argc)
        {
            sampleRate = std::stod (argv[++i]);
        }
        else if (argument == "--block" && i + 1 < argc)
        {
            blockSize = std::stoi (argv[++i]);
        }
        else if (! argument.empty() && argument[0] == '-')
        {
            std::cerr << "unknown option: " << argument << "\n";
            return 2;
        }
        else if (specPath.empty())
        {
            specPath = argument;
        }
        else
        {
            std::cerr << "only one spec file may be given\n";
            return 2;
        }
    }

    if (specPath.empty())
    {
        std::cerr << "usage: ZarlinoFactory_Check <spec.json> [--sample-rate N] [--block N] [--render]\n"
                  << "       ZarlinoFactory_Check --catalog\n";
        return 2;
    }

    const auto& registry = builtinRegistry();

    auto spec = PluginSpec::fromJsonFile (specPath);

    if (! spec.ok())
    {
        std::cerr << "FAIL " << spec.error().describe() << "\n";
        return 1;
    }

    std::cout << "spec    : " << spec.value().pluginId << " "
              << spec.value().pluginVersion << " (schema v" << spec.value().schemaVersion << ")\n";
    std::cout << "channels: " << channelModeName (spec.value().channels) << "\n";
    std::cout << "modules : " << spec.value().modules.size()
              << ", parameters: " << spec.value().parameters.size()
              << " (" << spec.value().exposedParameterCount() << " exposed)"
              << ", macros: " << spec.value().macros.size() << "\n";

    GraphOptions options;
    auto engine = GraphEngine::create (spec.value(), registry, options);

    if (! engine.ok())
    {
        std::cerr << "FAIL " << engine.error().describe() << "\n";
        return 1;
    }

    if (auto error = engine.value()->prepare (sampleRate, blockSize); error.failed())
    {
        std::cerr << "FAIL " << error.describe() << "\n";
        return 1;
    }

    std::cout << engine.value()->describeGraph();
    std::cout << "graph fingerprint: 0x" << std::hex << engine.value()->graphFingerprint()
              << std::dec << "\n";

    if (! render)
        return 0;

    // Deterministic test signal: unit impulse followed by a 440 Hz tone, so the
    // report shows both the transient behaviour and the steady-state level.
    const int channels = engine.value()->getChannelCount();
    std::vector<std::vector<float>> buffers (static_cast<size_t> (channels),
                                             std::vector<float> (static_cast<size_t> (blockSize), 0.0f));
    std::vector<float*> pointers (static_cast<size_t> (channels));

    for (int ch = 0; ch < channels; ++ch)
        pointers[static_cast<size_t> (ch)] = buffers[static_cast<size_t> (ch)].data();

    float peak = 0.0f;
    double sumSquares = 0.0;
    long long sampleCount = 0;
    int blocks = 0;

    for (int block = 0; block < 64; ++block)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& buffer = buffers[static_cast<size_t> (ch)];

            for (int i = 0; i < blockSize; ++i)
            {
                const long long index = static_cast<long long> (block) * blockSize + i;

                buffer[static_cast<size_t> (i)] =
                    (index == 0) ? 0.5f
                                 : 0.4f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 440.0
                                                                        * static_cast<double> (index)
                                                                        / sampleRate));
            }
        }

        engine.value()->process (pointers.data(), channels, blockSize);
        ++blocks;

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto& buffer = buffers[static_cast<size_t> (ch)];

            for (int i = 0; i < blockSize; ++i)
            {
                const float value = buffer[static_cast<size_t> (i)];

                if (! std::isfinite (value))
                {
                    std::cerr << "FAIL non-finite sample produced at block " << block << "\n";
                    return 1;
                }

                peak = std::max (peak, std::abs (value));
                sumSquares += static_cast<double> (value) * static_cast<double> (value);
                ++sampleCount;
            }
        }
    }

    const double rms = sampleCount > 0 ? std::sqrt (sumSquares / static_cast<double> (sampleCount)) : 0.0;

    std::cout << "render  : " << blocks << " blocks x " << blockSize << " samples @ "
              << sampleRate << " Hz, " << channels << " channel(s)\n";
    std::cout << "output  : peak " << peak << ", rms " << rms << ", finite\n";

    if (peak <= 0.0f || rms <= 0.0)
    {
        std::cerr << "FAIL rendered output is silent\n";
        return 1;
    }

    return 0;
}
