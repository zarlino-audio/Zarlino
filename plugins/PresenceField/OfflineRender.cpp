//==============================================================================
// PresenceField Offline Render Tool
//
// Renders a REAL input audio file (guitar/vocal/other) through the ACTUAL
// PresenceField DSP (PresenceFieldAudioProcessor::processBlock) and writes:
//   <dryOut.wav>  — the unprocessed source (A / before)
//   <procOut.wav> — the source run through the presence-field path (B / after)
//
// Usage:
//   PresenceField_OfflineRender <in.wav> <dryOut.wav> <procOut.wav> [startSec] [durSec]
//==============================================================================

#include <JuceHeader.h>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"

using juce::String;

//--------------------------------------------------------------------------
// Minimal 16-bit PCM WAV writer (avoids JUCE writer quirks).
//--------------------------------------------------------------------------
static bool writePcmWav (const juce::File& out, const juce::AudioBuffer<float>& buf, double sr)
{
    const int ch = buf.getNumChannels();
    const int frames = buf.getNumSamples();
    if (ch < 1 || frames < 1) return false;

    const int dataBytes = frames * ch * 2;
    const int riffSize = 36 + dataBytes;

    out.deleteFile(); // FileOutputStream APPENDS — ensure a fresh file
    juce::FileOutputStream fos (out);
    if (fos.failedToOpen()) return false;

    auto writeU32 = [&] (juce::uint32 v) { for (int i = 0; i < 4; ++i) fos.writeByte ((char) ((v >> (8 * i)) & 0xff)); };
    auto writeU16 = [&] (juce::uint16 v) { for (int i = 0; i < 2; ++i) fos.writeByte ((char) ((v >> (8 * i)) & 0xff)); };

    fos.write ("RIFF", 4);  writeU32 ((juce::uint32) riffSize);  fos.write ("WAVE", 4);
    fos.write ("fmt ", 4);  writeU32 (16);
    writeU16 (1);
    writeU16 ((juce::uint16) ch);
    writeU32 ((juce::uint32) sr);
    writeU32 ((juce::uint32) (sr * ch * 2));
    writeU16 ((juce::uint16) (ch * 2));
    writeU16 (16);
    fos.write ("data", 4);  writeU32 ((juce::uint32) dataBytes);

    for (int f = 0; f < frames; ++f)
        for (int c = 0; c < ch; ++c)
        {
            float s = juce::jlimit (-1.0f, 1.0f, buf.getSample (c, f));
            writeU16 ((juce::uint16) (short) (s * 32767.0f));
        }
    return true;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 4)
    {
        std::cerr << "Usage: PresenceField_OfflineRender <in.wav> <dryOut.wav> <procOut.wav> [startSec] [durSec]\n";
        return 1;
    }

    const juce::File inFile  { juce::String (argv[1]) };
    const juce::File dryFile { juce::String (argv[2]) };
    const juce::File wetFile { juce::String (argv[3]) };

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (inFile));
    if (! reader)
    {
        std::cerr << "Cannot read input file.\n";
        return 2;
    }

    const double sr = reader->sampleRate;
    const int fileSamples = (int) reader->lengthInSamples;

    double startSec = 0.0, durSec = 0.0;
    if (argc > 4) startSec = std::atof (argv[4]);
    if (argc > 5) durSec   = std::atof (argv[5]);

    const int startSample = juce::jlimit (0, fileSamples, (int) (startSec * sr));
    int totalSamples = fileSamples - startSample;
    if (durSec > 0.0)
        totalSamples = juce::jmin (totalSamples, (int) (durSec * sr));

    juce::AudioBuffer<float> input (2, totalSamples);
    input.clear();
    if (reader->numChannels == 1)
    {
        reader->read (&input, 0, totalSamples, startSample, true, false);
        input.copyFrom (1, 0, input, 0, 0, totalSamples);
    }
    else
    {
        reader->read (&input, 0, totalSamples, startSample, true, true);
    }

    // Write the DRY (unprocessed) reference.
    if (! writePcmWav (dryFile, input, sr))
    {
        std::cerr << "Cannot write dry output.\n";
        return 3;
    }

    // Instantiate the REAL PresenceField processor and render through it.
    PresenceFieldAudioProcessor proc;
    proc.prepareToPlay (sr, 512);
    proc.setPlayConfigDetails (2, 2, sr, 512);

    // Leave all params at defaults => master=50 (the presence field engaged).
    // Optionally set the master field higher for a clearly audible presence lift.
    if (auto* p = proc.getValueTreeState().getParameter ("master"))
        p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (62.0f));

    juce::AudioBuffer<float> processed (2, totalSamples);
    processed.clear();

    const int blockSize = 512;
    const int numBlocks = (totalSamples + blockSize - 1) / blockSize;

    for (int b = 0; b < numBlocks; ++b)
    {
        const int start = b * blockSize;
        const int n = juce::jmin (blockSize, totalSamples - start);

        juce::AudioBuffer<float> blockBuf (2, blockSize);
        blockBuf.clear();
        blockBuf.copyFrom (0, 0, input, 0, start, n);
        blockBuf.copyFrom (1, 0, input, 1, start, n);

        juce::MidiBuffer midi;
        proc.processBlock (blockBuf, midi);

        for (int i = 0; i < n; ++i)
        {
            processed.setSample (0, start + i, blockBuf.getSample (0, i));
            processed.setSample (1, start + i, blockBuf.getSample (1, i));
        }
    }

    if (! writePcmWav (wetFile, processed, sr))
    {
        std::cerr << "Cannot write processed output.\n";
        return 4;
    }

    std::cout << "OK source=" << inFile.getFileName()
              << " samples=" << totalSamples
              << " rate=" << (int) sr
              << " dry=" << dryFile.getFullPathName()
              << " proc=" << wetFile.getFullPathName() << "\n";
    return 0;
}
