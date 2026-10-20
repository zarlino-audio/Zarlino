//==============================================================================
// ZarlinoFactory_Generate - turn a PluginSpec into a buildable product project.
//
//   ZarlinoFactory_Generate --spec <spec.json> --template <minimal|standard|advanced>
//                           --out <dir> [--build-id <id>] [--commit <sha>]
//                           [--timestamp <iso8601>] [--platform <name>]
//
// The generator writes DATA, never code:
//
//   <out>/CMakeLists.txt                    copied verbatim from the fixed template
//   <out>/generated/ProductIdentity.cmake    identity + build context
//   <out>/generated/ProductData.cpp          three byte-escaped string literals
//   <out>/spec.zfactory.json                 the canonical specification
//   <out>/ui-plan.json                       the compiled interface plan
//   <out>/provenance.json                    the build provenance record
//   <out>/README.md                          what this artifact is
//
// SECURITY
//   Nothing from the specification reaches a compiler flag, a file path, a shell
//   command or a CMake command. The three payload strings are emitted as
//   per-byte \xNN escapes in adjacent literals, so no byte sequence in a spec can
//   terminate a literal, open a block, or introduce a preprocessor directive.
//   Identity values placed into CMake are escaped for CMake's own quoting rules,
//   including '\$', so a display name cannot expand a variable.
//
// Exit codes: 0 success, 1 spec/validation failure, 2 usage error.
//==============================================================================

#include <JuceHeader.h>

#include "zfactory/GraphEngine.h"
#include "zfactory/UiPlan.h"
#include "zfactory/Version.h"
#include "zfactory/shell/ProductRuntime.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace zfactory;

namespace
{

constexpr const char* kGeneratorVersion = "1.0.0";

struct Options
{
    juce::String specPath;
    juce::String templateName;
    juce::String outDir;
    juce::String buildId;
    juce::String commit;
    juce::String timestamp;
    juce::String platform = "windows-x86_64";
    juce::String formats = "VST3";
};

int usage (const juce::String& message)
{
    std::cerr << "ZarlinoFactory_Generate: " << message << std::endl
              << "usage: ZarlinoFactory_Generate --spec <spec.json>"
              << " --template <minimal|standard|advanced> --out <dir>"
              << " [--build-id <id>] [--commit <sha>] [--timestamp <iso8601>]"
              << " [--platform <name>] [--formats <list>]" << std::endl;
    return 2;
}

int failSpec (const FactoryError& error)
{
    std::cerr << "ZarlinoFactory_Generate: specification rejected" << std::endl
              << "  " << error.describe() << std::endl;
    return 1;
}

//==============================================================================
/** Emit a byte-exact C++ string literal for arbitrary content.
 *
 *  Every byte becomes \xNN, and adjacent literals are concatenated by the
 *  compiler, so the payload cannot escape its literal no matter what it contains
 *  - quotes, backslashes, newlines, NUL bytes, preprocessor directives. This is
 *  the only reason a user-supplied document can be embedded in source safely. */
juce::String emitByteLiteral (const juce::String& name, const juce::String& content)
{
    juce::String out;
    out << "const char* const " << name << " =\n";

    const auto* data = reinterpret_cast<const unsigned char*> (content.toRawUTF8());
    const auto size = content.getNumBytesAsUTF8();

    // The content is stored as UTF-8 but the escaping is byte-wise, and a
    // multi-byte sequence must not be split across two adjacent literals in a
    // way that changes the decoded bytes - it cannot, because adjacent literals
    // are concatenated before decoding. A newline is inserted every 16 bytes
    // purely to keep the generated file readable by a compiler and a human.
    for (size_t i = 0; i < size; ++i)
    {
        if (i % 16 == 0)
            out << "    ";

        out << "\"\\x" << juce::String::toHexString (static_cast<int> (data[i])).paddedLeft ('0', 2) << "\"";

        if (i % 16 == 15 || i + 1 == size)
            out << "\n";
        else
            out << " ";
    }

    if (size == 0)
        out << "    \"\"\n";

    out << ";\n\n";
    return out;
}

/** Escape a value for use inside a CMake quoted argument. `$` is escaped as well,
    because CMake expands ${} inside quoted arguments and a display name must not
    be able to introduce one. Control characters are rejected outright. */
bool escapeForCmake (const juce::String& value, juce::String& out, juce::String& why)
{
    for (const auto character : value)
    {
        if (character < ' ' || character == 0x7f)
        {
            why = "value contains a control character";
            return false;
        }
    }

    out = value;
    out = out.replace ("\\", "\\\\");
    out = out.replace ("\"", "\\\"");
    out = out.replace ("$", "\\$");
    out = out.replace (";", "\\;");
    return true;
}

juce::String hexOf (uint64_t value)
{
    return "0x" + juce::String::toHexString (static_cast<juce::int64> (value)).paddedLeft ('0', 16).toLowerCase();
}

juce::String sha256Of (const juce::String& text)
{
    return juce::SHA256 (text.toRawUTF8(), text.getNumBytesAsUTF8()).toHexString().toLowerCase();
}

juce::String canonicalJson (const juce::var& value)
{
    return juce::JSON::toString (value, true);
}

bool writeFile (const juce::File& file, const juce::String& content, juce::String& error)
{
    if (! file.getParentDirectory().createDirectory())
    {
        error = "cannot create directory " + file.getParentDirectory().getFullPathName();
        return false;
    }

    if (! file.replaceWithText (content))
    {
        error = "cannot write " + file.getFullPathName();
        return false;
    }

    return true;
}

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Options options;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String argument (argv[i]);
        const auto next = [&] () -> juce::String { return (i + 1 < argc) ? juce::String (argv[++i]) : juce::String(); };

        if (argument == "--spec")            options.specPath = next();
        else if (argument == "--template")   options.templateName = next();
        else if (argument == "--out")        options.outDir = next();
        else if (argument == "--build-id")   options.buildId = next();
        else if (argument == "--commit")     options.commit = next();
        else if (argument == "--timestamp")  options.timestamp = next();
        else if (argument == "--platform")   options.platform = next();
        else if (argument == "--formats")    options.formats = next();
        else if (argument == "--help" || argument == "-h") return usage ("");
        else return usage ("unrecognised argument '" + argument + "'");
    }

    if (options.specPath.isEmpty())     return usage ("--spec is required");
    if (options.templateName.isEmpty()) return usage ("--template is required");
    if (options.outDir.isEmpty())       return usage ("--out is required");

    UiTemplate uiTemplate;

    if (! parseUiTemplate (options.templateName.toStdString(), uiTemplate))
        return usage ("unknown UI template '" + options.templateName + "'");

    //--------------------------------------------------------------------------
    // 1. Validate the specification. Nothing is generated from a document the
    //    kernel would not itself accept.
    //--------------------------------------------------------------------------
    const juce::File specFile (options.specPath);

    if (! specFile.existsAsFile())
        return usage ("specification not found: " + options.specPath);

    const auto specResult = PluginSpec::fromJsonFile (options.specPath.toStdString());

    if (! specResult.ok())
        return failSpec (specResult.error());

    const auto& spec = specResult.value();
    const auto specJson = canonicalJson (spec.toJson());

    //--------------------------------------------------------------------------
    // 2. Resolve the certified modules and compile the interface.
    //--------------------------------------------------------------------------
    const auto& registry = builtinRegistry();

    auto engineResult = GraphEngine::create (spec, registry, GraphOptions());

    if (! engineResult.ok())
        return failSpec (engineResult.error());

    const auto engine = engineResult.takeValue();

    const auto planResult = UiPlan::compile (spec, registry, uiTemplate);

    if (! planResult.ok())
        return failSpec (planResult.error());

    const auto& plan = planResult.value();
    const auto planJson = plan.toJsonText();
    const auto specFingerprint = hexOf (spec.fingerprint());

    //--------------------------------------------------------------------------
    // 3. Provenance.
    //--------------------------------------------------------------------------
    shell::Provenance provenance;
    provenance.productId = spec.pluginId;
    provenance.productName = spec.pluginName;
    provenance.productVersion = spec.pluginVersion;
    provenance.uiTemplate = uiTemplateName (uiTemplate);
    provenance.specSha256 = sha256Of (specJson).toStdString();
    provenance.specFingerprint = specFingerprint.toStdString();
    provenance.uiPlanFingerprint = hexOf (plan.fingerprint()).toStdString();
    provenance.catalogFingerprint = hexOf (registry.catalogFingerprint()).toStdString();
    provenance.kernelVersion = kKernelVersionString;
    provenance.factorySchemaVersion = kSchemaVersion;
    provenance.sourceCommit = options.commit.toStdString();
    provenance.buildTimestamp = options.timestamp.toStdString();
    provenance.targetPlatform = options.platform.toStdString();
    provenance.generatorVersion = kGeneratorVersion;

    // A build id is an INPUT, not something the generator invents from the clock:
    // the same specification, template and build id must produce byte-identical
    // output. When none is supplied, a stable local id is derived from the inputs.
    provenance.buildId = options.buildId.isNotEmpty()
                             ? options.buildId.toStdString()
                             : (juce::String ("local-")
                                + hexOf (spec.fingerprint() ^ (plan.fingerprint() * 1099511628211ull))
                                      .substring (2, 18)).toStdString();

    for (const auto& instance : spec.modules)
    {
        const auto entry = registry.resolve (instance.moduleId, instance.moduleVersion,
                                             CertificationState::Certified);

        shell::Provenance::ModuleRef module;
        module.instanceId = instance.instanceId;
        module.moduleId = instance.moduleId;
        module.moduleVersion = instance.moduleVersion.to_string();
        module.manifestFingerprint = entry.ok() ? hexOf (entry.value()->manifest.fingerprint()).toStdString()
                                                : std::string();

        provenance.modules.push_back (std::move (module));
    }

    //--------------------------------------------------------------------------
    // 4. Identity values for CMake.
    //--------------------------------------------------------------------------
    const auto pluginCode = shell::pluginCodeFor (spec.pluginId);
    const auto bundleId = shell::bundleIdFor (spec.pluginId);
    const auto slug = shell::slugFor (spec.pluginId);

    const auto targetName = "ZarlinoProduct_" + juce::String (slug);

    if (pluginCode.size() != 4)
    {
        std::cerr << "ZarlinoFactory_Generate: could not derive a four character plugin code" << std::endl;
        return 1;
    }

    juce::String escapedName, escapedVendor, escapedVersion, why;

    if (! escapeForCmake (juce::String::fromUTF8 (spec.pluginName.c_str()), escapedName, why)
        || ! escapeForCmake (juce::String::fromUTF8 (spec.vendor.c_str()), escapedVendor, why)
        || ! escapeForCmake (juce::String::fromUTF8 (spec.pluginVersion.c_str()), escapedVersion, why))
    {
        std::cerr << "ZarlinoFactory_Generate: " << why.toStdString() << std::endl;
        return 1;
    }

    //--------------------------------------------------------------------------
    // 5. Write the project.
    //--------------------------------------------------------------------------
    const juce::File outDir (options.outDir);
    const juce::File generatedDir = outDir.getChildFile ("generated");

    juce::String error;

    // --- the fixed template ---------------------------------------------------
    const juce::File templateFile (juce::String (ZFACTORY_PROJECT_TEMPLATE_DIR) + "/CMakeLists.txt");

    if (! templateFile.existsAsFile())
    {
        std::cerr << "ZarlinoFactory_Generate: project template missing at "
                  << templateFile.getFullPathName() << std::endl;
        return 1;
    }

    if (! writeFile (outDir.getChildFile ("CMakeLists.txt"), templateFile.loadFileAsString(), error))
    {
        std::cerr << "ZarlinoFactory_Generate: " << error << std::endl;
        return 1;
    }

    // --- identity (data only) -------------------------------------------------
    {
        juce::String identity;
        identity << "#==============================================================================\n"
                 << "# GENERATED - do not edit. Product identity and build context.\n"
                 << "# Written by ZarlinoFactory_Generate " << kGeneratorVersion
                 << " from " << specFile.getFileName() << ".\n"
                 << "#==============================================================================\n\n"
                 << "set(ZFACTORY_PRODUCT_ID \"" << juce::String (spec.pluginId) << "\")\n"
                 << "set(ZFACTORY_PRODUCT_NAME \"" << escapedName << "\")\n"
                 << "set(ZFACTORY_PRODUCT_VENDOR \"" << escapedVendor << "\")\n"
                 << "set(ZFACTORY_PRODUCT_VERSION \"" << escapedVersion << "\")\n"
                 << "set(ZFACTORY_PLUGIN_CODE " << pluginCode << ")\n"
                 << "set(ZFACTORY_BUNDLE_ID \"" << bundleId << "\")\n"
                 << "set(ZFACTORY_TARGET_NAME " << targetName << ")\n"
                 << "set(ZFACTORY_QA_TARGET_NAME " << targetName << "_QA)\n"
                 << "set(ZFACTORY_UI_TEMPLATE \"" << uiTemplateName (uiTemplate) << "\")\n"
                 << "set(ZFACTORY_BUILD_ID \"" << provenance.buildId << "\")\n"
                 << "set(ZFACTORY_SOURCE_COMMIT \"" << provenance.sourceCommit << "\")\n"
                 << "set(ZFACTORY_TARGET_PLATFORM \"" << provenance.targetPlatform << "\")\n"
                 << "set(ZFACTORY_FORMATS " << options.formats << ")\n"
                 << "set(ZFACTORY_SPEC_FINGERPRINT \"" << provenance.specFingerprint << "\")\n"
                 << "set(ZFACTORY_UI_PLAN_FINGERPRINT \"" << provenance.uiPlanFingerprint << "\")\n"
                 << "set(ZFACTORY_CATALOG_FINGERPRINT \"" << provenance.catalogFingerprint << "\")\n";

        if (! writeFile (generatedDir.getChildFile ("ProductIdentity.cmake"), identity, error))
        {
            std::cerr << "ZarlinoFactory_Generate: " << error << std::endl;
            return 1;
        }
    }

    // --- the three payload literals ------------------------------------------
    {
        juce::String data;
        data << "//==============================================================================\n"
             << "// GENERATED - do not edit.\n"
             << "//\n"
             << "// The whole product payload: the canonical PluginSpec document, the interface\n"
             << "// plan compiled from it at generation time, and the build provenance record.\n"
             << "//\n"
             << "// Every byte is emitted as a \\xNN escape inside adjacent string literals, so no\n"
             << "// content of a specification can terminate a literal or introduce code.\n"
             << "//==============================================================================\n\n"
             << "#include \"zfactory/shell/ProductRuntime.h\"\n\n"
             << "namespace zfactory\n{\nnamespace shell\n{\n\n";

        data << emitByteLiteral ("kGeneratedSpecJson", specJson);
        data << emitByteLiteral ("kGeneratedUiPlanJson", planJson);
        data << emitByteLiteral ("kGeneratedProvenanceJson",
                                 juce::String (provenance.toJsonText()));

        data << "} // namespace shell\n} // namespace zfactory\n";

        if (! writeFile (generatedDir.getChildFile ("ProductData.cpp"), data, error))
        {
            std::cerr << "ZarlinoFactory_Generate: " << error << std::endl;
            return 1;
        }
    }

    // --- evidence files -------------------------------------------------------
    if (! writeFile (outDir.getChildFile ("spec.zfactory.json"), specJson, error)
        || ! writeFile (outDir.getChildFile ("ui-plan.json"), juce::String (planJson), error)
        || ! writeFile (outDir.getChildFile ("provenance.json"),
                        juce::JSON::toString (provenance.toJson(), false), error))
    {
        std::cerr << "ZarlinoFactory_Generate: " << error << std::endl;
        return 1;
    }

    // --- README ---------------------------------------------------------------
    {
        juce::String readme;
        readme << "# " << escapedName << "\n\n"
               << "Generated Zarlino factory product. **Do not edit anything in this directory by hand** -\n"
               << "it is regenerated from the specification below and any manual change is lost.\n\n"
               << "| | |\n|---|---|\n"
               << "| Product id | `" << juce::String (spec.pluginId) << "` |\n"
               << "| Version | `" << escapedVersion << "` |\n"
               << "| UI template | `" << uiTemplateName (uiTemplate) << "` |\n"
               << "| Channels | `" << channelModeName (spec.channels) << "` |\n"
               << "| Build id | `" << provenance.buildId << "` |\n"
               << "| Source commit | `" << (provenance.sourceCommit.empty() ? "uncommitted" : provenance.sourceCommit) << "` |\n"
               << "| Spec fingerprint | `" << provenance.specFingerprint << "` |\n"
               << "| UI plan fingerprint | `" << provenance.uiPlanFingerprint << "` |\n"
               << "| Module catalogue | `" << provenance.catalogFingerprint << "` |\n"
               << "| Specification sha256 | `" << provenance.specSha256 << "` |\n\n"
               << "## Build\n\n"
               << "```\n"
               << "cmake -S . -B build -G \"Visual Studio 17 2022\" -A x64 -DZARLINO_ROOT=<monorepo>\n"
               << "cmake --build build --config Release --target " << targetName << " " << targetName << "_QA\n"
               << "```\n\n"
               << "## Gate\n\n"
               << "```\n"
               << "build/" << targetName << "_QA_artefacts/Release/" << targetName << "_QA.exe \\\n"
               << "    --plugin <path to " << targetName << ".vst3> --json qa-report.json\n"
               << "```\n\n"
               << "A non-zero exit from the gate means the artifact must not be released, however\n"
               << "cleanly it compiled.\n\n"
               << "## Chain\n\n"
               << engine->describeGraph() << "\n";

        if (! writeFile (outDir.getChildFile ("README.md"), readme, error))
        {
            std::cerr << "ZarlinoFactory_Generate: " << error << std::endl;
            return 1;
        }
    }

    std::cout << "generated " << targetName << " from " << spec.pluginId
              << " (template " << uiTemplateName (uiTemplate) << ", build " << provenance.buildId << ")\n"
              << "  spec sha256        " << provenance.specSha256 << "\n"
              << "  spec fingerprint   " << provenance.specFingerprint << "\n"
              << "  plan fingerprint   " << provenance.uiPlanFingerprint << "\n"
              << "  catalog            " << provenance.catalogFingerprint << "\n"
              << "  controls           " << plan.drawnControlCount << "/" << plan.availableControlCount()
              << " (" << plan.groupCount << " groups, " << plan.macroCount << " macros)\n"
              << "  output             " << outDir.getFullPathName() << std::endl;

    return 0;
}
