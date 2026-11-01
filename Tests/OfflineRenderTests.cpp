#include "OfflineRenderer.h"
#include "Core/PitchDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int testSeconds = 2;
    constexpr double inputFrequency = 440.0;
    constexpr double expectedFrequency = 523.2511306;

    bool writeInputFile (const juce::File& file)
    {
        auto fileStream = std::make_unique<juce::FileOutputStream> (file);
        if (! fileStream->openedOk())
            return false;

        std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (2)
                                 .withBitsPerSample (24);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return false;

        juce::AudioBuffer<float> buffer (2, 512);
        const double increment = 2.0 * 3.14159265358979323846 * inputFrequency / sampleRate;
        double phase = 0.0;

        for (int position = 0; position < (int) (sampleRate * testSeconds); position += 512)
        {
            const int count = std::min (512, (int) (sampleRate * testSeconds) - position);

            for (int i = 0; i < count; ++i)
            {
                const float sample = 0.4f * (float) std::sin (phase);
                phase += increment;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            const float* channels[2] = { buffer.getReadPointer (0), buffer.getReadPointer (1) };
            if (! writer->writeFromFloatArrays (channels, 2, count))
                return false;
        }

        return writer->flush();
    }

    double estimateOutputFrequency (const juce::File& file, double& durationSeconds, int& channels)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->numChannels < 1)
            return 0.0;

        durationSeconds = (double) reader->lengthInSamples / reader->sampleRate;
        channels = (int) reader->numChannels;

        const int start = (int) (reader->sampleRate * 0.5);
        const int count = (int) (reader->sampleRate * 1.0);
        juce::AudioBuffer<float> buffer (1, count);
        reader->read (&buffer, 0, count, start, true, false);

        autotune::PitchDetector detector;
        detector.prepare (reader->sampleRate, 60.0f, 1100.0f, 128, 0);

        std::vector<float> frequencies;

        for (int offset = 0; offset + 256 <= count; offset += 256)
        {
            autotune::PitchFrame frame;
            if (detector.process (buffer.getReadPointer (0) + offset, 256, frame)
                && frame.voiced && frame.frequency > 0.0f)
                frequencies.push_back (frame.frequency);
        }

        if (frequencies.empty())
            return 0.0;

        std::sort (frequencies.begin(), frequencies.end());
        return frequencies[frequencies.size() / 2];
    }
}

int main()
{
    juce::AudioFormatManager registeredFormats;
    registeredFormats.registerBasicFormats();

    const bool hasMp3 = registeredFormats.findFormatForFileExtension ("mp3") != nullptr;
    std::cout << "known_formats=" << registeredFormats.getNumKnownFormats()
              << " mp3=" << (hasMp3 ? "yes" : "no") << "\n";

    if (! hasMp3)
    {
        std::cerr << "FAIL: MP3 reader is not registered\n";
        return 1;
    }

    auto testFolder = juce::File::getCurrentWorkingDirectory().getChildFile ("Temp")
                          .getChildFile ("offline-render-tests");
    testFolder.deleteRecursively();

    if (! testFolder.createDirectory())
    {
        std::cerr << "FAIL: could not create test folder\n";
        return 1;
    }

    const auto inputFile = testFolder.getChildFile ("input.wav");
    const auto outputFile = testFolder.getChildFile ("output.wav");

    if (! writeInputFile (inputFile))
    {
        std::cerr << "FAIL: could not write test input\n";
        return 1;
    }

    OfflineRenderer::Settings settings;
    settings.strength = 1.0f;
    settings.retuneMs = 1.0f;
    settings.humanize = 0.0f;
    settings.keyRoot = 0;
    settings.scaleMask = 0x0FFFu;
    settings.transpose = 3;
    settings.tuningA4 = 440.0f;
    settings.gateDb = -80.0f;
    settings.inputGainDb = 0.0f;

    const auto result = OfflineRenderer::renderFile (inputFile, outputFile, settings);
    if (! result.ok)
    {
        std::cerr << "FAIL: render failed: " << result.error << "\n";
        return 1;
    }

    double duration = 0.0;
    int channels = 0;
    const double measuredFrequency = estimateOutputFrequency (outputFile, duration, channels);

    std::cout << "rendered=" << result.samplesWritten
              << " duration=" << duration
              << " channels=" << channels
              << " frequency=" << measuredFrequency << "\n";

    const bool lengthOk = std::abs (duration - (double) testSeconds) < 0.02;
    const bool channelsOk = channels == 2;
    const bool pitchOk = std::abs (measuredFrequency - expectedFrequency) < 12.0;

    if (! lengthOk || ! channelsOk || ! pitchOk)
    {
        std::cerr << "FAIL: length=" << lengthOk
                  << " channels=" << channelsOk
                  << " pitch=" << pitchOk << "\n";
        return 1;
    }

    std::cout << "Offline render tests passed\n";
    return 0;
}
