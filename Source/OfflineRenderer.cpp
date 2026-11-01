#include "OfflineRenderer.h"

#include "Core/Biquad.h"
#include "Core/GranularShifter.h"
#include "Core/NoteQuantizer.h"
#include "Core/PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr int blockSize = 256;

    float softClip (float x) noexcept
    {
        constexpr float threshold = 0.985f;
        if (x > threshold)
            return threshold + (1.0f - threshold) * std::tanh ((x - threshold) / (1.0f - threshold));
        if (x < -threshold)
            return -threshold - (1.0f - threshold) * std::tanh ((-x - threshold) / (1.0f - threshold));
        return x;
    }

    struct ChannelDsp
    {
        autotune::GranularShifter shifter;
        autotune::Biquad inputHighPass;
        autotune::Biquad outputHighPass;

        void prepare (double sampleRate, int maxDelay)
        {
            shifter.prepare (sampleRate, maxDelay);
            inputHighPass.setHighPass (sampleRate, 65.0);
            outputHighPass.setHighPass (sampleRate, 28.0);
            inputHighPass.reset();
            outputHighPass.reset();
        }
    };
}

OfflineRenderer::Result OfflineRenderer::renderFile (const juce::File& inputFile,
                                                      const juce::File& outputFile,
                                                      const Settings& settings,
                                                      const std::function<bool (double)>& progress)
{
    Result result;
    result.outputFile = outputFile;

    if (! inputFile.existsAsFile())
    {
        result.error = "The input file does not exist.";
        return result;
    }

    if (inputFile.getFullPathName().equalsIgnoreCase (outputFile.getFullPathName()))
    {
        result.error = "Choose an output file different from the input.";
        return result;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inputFile));
    if (reader == nullptr)
    {
        result.error = "That audio format could not be read. Use WAV, AIFF, FLAC, OGG or MP3.";
        return result;
    }

    const double sampleRate = reader->sampleRate;
    const int numChannels = (int) reader->numChannels;
    const int64_t lengthInSamples = reader->lengthInSamples;

    if (sampleRate < 8000.0 || sampleRate > 384000.0)
    {
        result.error = "The audio sample rate is not supported.";
        return result;
    }

    if (numChannels < 1 || numChannels > 2)
    {
        result.error = "Only mono and stereo audio files are supported.";
        return result;
    }

    if (lengthInSamples <= 0)
    {
        result.error = "The audio file is empty.";
        return result;
    }

    if (! outputFile.getParentDirectory().exists()
        && ! outputFile.getParentDirectory().createDirectory())
    {
        result.error = "The output folder could not be created.";
        return result;
    }

    if (outputFile.exists() && ! outputFile.deleteFile())
    {
        result.error = "The output file could not be overwritten.";
        return result;
    }

    auto fileStream = std::make_unique<juce::FileOutputStream> (outputFile);
    if (! fileStream->openedOk())
    {
        result.error = "The output file could not be opened.";
        return result;
    }

    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
    juce::WavAudioFormat wavFormat;
    const auto writerOptions = juce::AudioFormatWriterOptions{}
                                   .withSampleRate (sampleRate)
                                   .withNumChannels (numChannels)
                                   .withBitsPerSample (24);
    auto writer = wavFormat.createWriterFor (stream, writerOptions);

    if (writer == nullptr)
    {
        result.error = "Could not create the output WAV file.";
        return result;
    }

    const int minDelay = (int) std::lround (0.0025 * sampleRate) + 256;
    const int maxDelay = juce::jlimit (minDelay, (int) (sampleRate / 10.0),
                                       (int) std::lround (0.032 * sampleRate));

    ChannelDsp channelDsp[2];
    for (int channel = 0; channel < numChannels; ++channel)
        channelDsp[channel].prepare (sampleRate, maxDelay);

    autotune::PitchDetector detector;
    detector.prepare (sampleRate, 60.0f, 1100.0f, 128, 0);

    const int latency = channelDsp[0].shifter.getAverageDelaySamples();
    const double deltaSeconds = (double) blockSize / sampleRate;

    const float inputGain = juce::Decibels::decibelsToGain (
        juce::jlimit (-24.0f, 24.0f, settings.inputGainDb), -100.0f);
    const float gate = juce::Decibels::decibelsToGain (
        juce::jlimit (-80.0f, -20.0f, settings.gateDb), -100.0f);
    const float strength = juce::jlimit (0.0f, 1.0f, settings.strength);
    const float retuneMs = juce::jmax (0.0f, settings.retuneMs);
    const float humanize = juce::jlimit (0.0f, 1.0f, settings.humanize);
    const int keyRoot = juce::jlimit (0, 11, settings.keyRoot);
    const uint32_t scaleMask = settings.scaleMask == 0 ? 0x0FFFu : settings.scaleMask;
    const int transpose = juce::jlimit (-24, 24, settings.transpose);
    const float tuningA4 = juce::jlimit (415.0f, 466.0f, settings.tuningA4);

    juce::AudioBuffer<float> inputBuffer (numChannels, blockSize);
    juce::AudioBuffer<float> shifterBuffer (numChannels, blockSize);
    juce::AudioBuffer<float> analysisBuffer (1, blockSize);

    std::vector<float> f0History (5, 0.0f);
    int f0HistoryWrite = 0;
    float smoothedFrequency = 0.0f;
    float smoothedRatio = 1.0f;
    float lastPeriodSamples = (float) (sampleRate / 220.0);
    bool currentVoiced = false;
    float currentConfidence = 0.0f;
    int64_t voicedHoldSamples = 0;
    int64_t driftSamples = 0;
    float driftValue = 0.0f;
    float driftTarget = 0.0f;
    juce::Random random;

    int64_t fedSamples = 0;
    int64_t producedSamples = 0;
    int64_t writtenSamples = 0;
    int progressDivider = 0;
    bool cancelled = false;
    bool writeFailed = false;

    if (progress && ! progress (0.0))
    {
        result.cancelled = true;
        writer.reset();
        outputFile.deleteFile();
        return result;
    }

    juce::ScopedNoDenormals noDenormals;

    while (writtenSamples < lengthInSamples)
    {
        if (progress && ++progressDivider >= 16)
        {
            progressDivider = 0;
            if (! progress ((double) writtenSamples / (double) lengthInSamples))
            {
                cancelled = true;
                break;
            }
        }

        inputBuffer.clear();
        const int samplesToRead = (int) juce::jlimit ((int64_t) 0, (int64_t) blockSize,
                                                      lengthInSamples - fedSamples);

        if (samplesToRead > 0)
        {
            float* destinations[2] = { inputBuffer.getWritePointer (0),
                                       numChannels > 1 ? inputBuffer.getWritePointer (1) : nullptr };

            if (! reader->read (destinations, numChannels, fedSamples, samplesToRead))
            {
                result.error = "The input file could not be read.";
                break;
            }
        }

        fedSamples += blockSize;

        auto* analysis = analysisBuffer.getWritePointer (0);
        std::fill (analysis, analysis + blockSize, 0.0f);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* source = inputBuffer.getReadPointer (channel);
            auto* destination = inputBuffer.getWritePointer (channel);

            for (int i = 0; i < blockSize; ++i)
            {
                float sample = source[i] * inputGain;
                sample = channelDsp[channel].inputHighPass.process (sample);
                destination[i] = sample;
                analysis[i] += sample;
            }
        }

        const float channelScale = 1.0f / (float) numChannels;
        for (int i = 0; i < blockSize; ++i)
            analysis[i] *= channelScale;

        autotune::PitchFrame frame;
        const bool newFrame = detector.process (analysis, blockSize, frame);

        if (newFrame)
        {
            const bool strong = frame.voiced && frame.rms >= gate && frame.confidence >= 0.55f;

            if (strong)
            {
                f0History[(size_t) f0HistoryWrite] = frame.frequency;
                f0HistoryWrite = (f0HistoryWrite + 1) % (int) f0History.size();

                float sorted[5];
                int count = 0;
                for (float value : f0History)
                    if (value > 0.0f)
                        sorted[count++] = value;

                if (count > 0)
                {
                    std::sort (sorted, sorted + count);
                    smoothedFrequency = sorted[count / 2];
                    lastPeriodSamples = (float) (sampleRate / std::max (1.0f, smoothedFrequency));
                }

                currentConfidence = frame.confidence;
                currentVoiced = true;
                voicedHoldSamples = (int64_t) (0.032 * sampleRate);
            }
            else if (voicedHoldSamples > 0)
            {
                voicedHoldSamples -= blockSize;
            }
            else
            {
                if (currentVoiced)
                {
                    std::fill (f0History.begin(), f0History.end(), 0.0f);
                    smoothedFrequency = 0.0f;
                }

                currentVoiced = false;
                currentConfidence = 0.0f;
            }
        }

        driftSamples += blockSize;
        if (driftSamples >= (int64_t) (0.25 * sampleRate))
        {
            driftSamples -= (int64_t) (0.25 * sampleRate);
            driftTarget = random.nextFloat() * 2.0f - 1.0f;
        }

        driftValue += (driftTarget - driftValue) * std::min (1.0f, (float) deltaSeconds * 6.0f);

        if (currentVoiced && smoothedFrequency > 0.0f)
        {
            const float currentMidi = autotune::hzToMidi (smoothedFrequency, tuningA4);
            const int quantized = autotune::nearestAllowedMidi (currentMidi, keyRoot,
                                                                 (uint16_t) scaleMask);
            const float blended = currentMidi + strength * ((float) quantized - currentMidi);
            const float driftCents = humanize * 16.0f * driftValue * strength;
            const float targetMidi = blended + (float) transpose + driftCents * 0.01f;
            const float targetRatio = juce::jlimit (0.5f, 2.0f,
                                                    (float) std::pow (2.0f, (targetMidi - currentMidi) / 12.0f));
            const double alpha = retuneMs < 2.0f
                                     ? 1.0
                                     : 1.0 - std::exp (-deltaSeconds / (retuneMs * 0.001f));
            smoothedRatio += (float) alpha * (targetRatio - smoothedRatio);
        }
        else
        {
            const double alpha = 1.0 - std::exp (-deltaSeconds / 0.03f);
            smoothedRatio += (float) alpha * (1.0f - smoothedRatio);
        }

        autotune::ShifterParams shifterParams;
        shifterParams.periodSamples = lastPeriodSamples;
        shifterParams.ratio = smoothedRatio;
        shifterParams.voiced = currentVoiced;

        shifterBuffer.clear();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* source = inputBuffer.getReadPointer (channel);
            auto* destination = shifterBuffer.getWritePointer (channel);

            channelDsp[channel].shifter.pushInput (source, blockSize);
            channelDsp[channel].shifter.render (destination, blockSize, shifterParams);

            for (int i = 0; i < blockSize; ++i)
            {
                float sample = channelDsp[channel].outputHighPass.process (destination[i]);
                destination[i] = softClip (sample);
            }
        }

        const int skip = (int) juce::jlimit ((int64_t) 0, (int64_t) blockSize,
                                             (int64_t) latency - producedSamples);
        const int available = blockSize - skip;
        const int samplesToWrite = (int) juce::jlimit ((int64_t) 0, (int64_t) available,
                                                       lengthInSamples - writtenSamples);

        if (samplesToWrite > 0)
        {
            const float* destinations[2] = { shifterBuffer.getReadPointer (0, skip),
                                             numChannels > 1 ? shifterBuffer.getReadPointer (1, skip)
                                                             : nullptr };

            if (! writer->writeFromFloatArrays (destinations, numChannels, samplesToWrite))
            {
                writeFailed = true;
                break;
            }

            writtenSamples += samplesToWrite;
        }

        producedSamples += blockSize;
    }

    writer->flush();
    writer.reset();

    if (cancelled)
    {
        outputFile.deleteFile();
        result.cancelled = true;
        return result;
    }

    if (result.error.isNotEmpty() || writeFailed)
    {
        outputFile.deleteFile();
        if (result.error.isEmpty())
            result.error = "The output file could not be written.";
        return result;
    }

    if (writtenSamples < lengthInSamples)
    {
        outputFile.deleteFile();
        result.error = "The export ended before the whole file was written.";
        return result;
    }

    if (progress)
        progress (1.0);

    result.ok = true;
    result.samplesWritten = writtenSamples;
    result.sampleRate = sampleRate;
    result.numChannels = numChannels;
    return result;
}
