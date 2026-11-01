#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>

class OfflineRenderer
{
public:
    struct Settings
    {
        float strength = 0.62f;
        float retuneMs = 35.0f;
        float humanize = 0.22f;
        int keyRoot = 0;
        uint32_t scaleMask = 0x0FFF;
        int transpose = 0;
        float tuningA4 = 440.0f;
        float gateDb = -55.0f;
        float inputGainDb = 0.0f;
    };

    struct Result
    {
        bool ok = false;
        bool cancelled = false;
        juce::String error;
        juce::File outputFile;
        int64_t samplesWritten = 0;
        double sampleRate = 0.0;
        int numChannels = 0;
    };

    static Result renderFile (const juce::File& inputFile,
                              const juce::File& outputFile,
                              const Settings& settings,
                              const std::function<bool (double)>& progress = {});
};
