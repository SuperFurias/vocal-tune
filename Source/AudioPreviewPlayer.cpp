#include "AudioPreviewPlayer.h"
#include "Core/NoteQuantizer.h"

#include <algorithm>
#include <cmath>

namespace
{
    float softClip (float x) noexcept
    {
        constexpr float threshold = 0.985f;
        if (x > threshold)
            return threshold + (1.0f - threshold) * std::tanh ((x - threshold) / (1.0f - threshold));
        if (x < -threshold)
            return -threshold - (1.0f - threshold) * std::tanh ((-x - threshold) / (1.0f - threshold));
        return x;
    }
}

AudioPreviewPlayer::AudioPreviewPlayer()
{
    formatManager.registerBasicFormats();
    f0History.assign (5, 0.0f);
}

AudioPreviewPlayer::~AudioPreviewPlayer()
{
    release();
}

bool AudioPreviewPlayer::loadFile (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> newReader (formatManager.createReaderFor (file));
    if (newReader == nullptr)
    {
        error = "That file could not be read for preview.";
        return false;
    }

    if (newReader->numChannels < 1 || newReader->numChannels > 2)
    {
        error = "Only mono and stereo files can be previewed.";
        return false;
    }

    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    reader = std::move (newReader);

    sourceChannels = (int) reader->numChannels;
    durationSeconds = reader->sampleRate > 0.0
                          ? (double) reader->lengthInSamples / reader->sampleRate
                          : 0.0;
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.get(), false);
    transport.setSource (readerSource.get(), 0, nullptr, reader->sampleRate, sourceChannels);
    transport.setPosition (0.0);
    resetDsp();
    error.clear();
    return true;
}

void AudioPreviewPlayer::release()
{
    transport.stop();

    if (callbackAdded)
    {
        deviceManager.removeAudioCallback (&sourcePlayer);
        callbackAdded = false;
    }

    sourcePlayer.setSource (nullptr);
    transport.setSource (nullptr);

    if (audioReady)
    {
        deviceManager.closeAudioDevice();
        audioReady = false;
    }

    readerSource.reset();
    reader.reset();
    sourceChannels = 0;
    durationSeconds = 0.0;
}

juce::String AudioPreviewPlayer::ensureAudioReady()
{
    if (audioReady)
    {
        if (deviceManager.getCurrentAudioDevice() != nullptr)
            return {};
    }

    const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);

    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return error.isNotEmpty() ? error : "No audio output device is available.";

    if (! callbackAdded)
    {
        sourcePlayer.setSource (this);
        deviceManager.addAudioCallback (&sourcePlayer);
        callbackAdded = true;
    }

    audioReady = true;
    return {};
}

juce::String AudioPreviewPlayer::prepareForPlayback()
{
    return ensureAudioReady();
}

juce::String AudioPreviewPlayer::start()
{
    if (readerSource == nullptr)
        return "Choose an audio file first.";

    const auto error = ensureAudioReady();
    if (error.isNotEmpty())
        return error;

    if (! transport.isPlaying())
    {
        if (transport.hasStreamFinished()
            || transport.getCurrentPosition() >= juce::jmax (0.0, durationSeconds - 0.05))
        {
            transport.setPosition (0.0);
            resetDsp();
        }

        transport.start();
    }

    return {};
}

void AudioPreviewPlayer::pause()
{
    transport.stop();
}

void AudioPreviewPlayer::stop()
{
    transport.stop();
    transport.setPosition (0.0);
    resetDsp();
}

void AudioPreviewPlayer::seekToSeconds (double seconds)
{
    if (readerSource == nullptr)
        return;

    if (! prepared)
    {
        ensureAudioReady();
        if (! prepared)
            return;
    }

    transport.setPosition (juce::jlimit (0.0, durationSeconds, seconds));
    resetDsp();
}

bool AudioPreviewPlayer::isPlaying() const
{
    return transport.isPlaying();
}

bool AudioPreviewPlayer::hasFile() const
{
    return readerSource != nullptr;
}

double AudioPreviewPlayer::getPositionSeconds() const
{
    return transport.getCurrentPosition();
}

double AudioPreviewPlayer::getDurationSeconds() const
{
    return durationSeconds;
}

void AudioPreviewPlayer::setTuned (bool shouldBeTuned)
{
    tuned.store (shouldBeTuned, std::memory_order_relaxed);
}

bool AudioPreviewPlayer::isTuned() const
{
    return tuned.load (std::memory_order_relaxed);
}

void AudioPreviewPlayer::setSettings (const OfflineRenderer::Settings& settings)
{
    strength.store (juce::jlimit (0.0f, 1.0f, settings.strength), std::memory_order_relaxed);
    retuneMs.store (juce::jmax (0.0f, settings.retuneMs), std::memory_order_relaxed);
    humanize.store (juce::jlimit (0.0f, 1.0f, settings.humanize), std::memory_order_relaxed);
    tuningA4.store (juce::jlimit (415.0f, 466.0f, settings.tuningA4), std::memory_order_relaxed);
    gateDb.store (juce::jlimit (-80.0f, -20.0f, settings.gateDb), std::memory_order_relaxed);
    inputGainDb.store (juce::jlimit (-24.0f, 24.0f, settings.inputGainDb), std::memory_order_relaxed);
    keyRoot.store (juce::jlimit (0, 11, settings.keyRoot), std::memory_order_relaxed);
    transpose.store (juce::jlimit (-24, 24, settings.transpose), std::memory_order_relaxed);
    scaleMask.store (settings.scaleMask == 0 ? 0x0FFFu : settings.scaleMask,
                     std::memory_order_relaxed);
}

void AudioPreviewPlayer::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    if (samplesPerBlockExpected <= 0 || sampleRate <= 0.0)
        return;

    preparedSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlockExpected;
    prepared = true;

    transport.prepareToPlay (samplesPerBlockExpected, sampleRate);
    workBuffer.setSize (2, samplesPerBlockExpected);
    processedBuffer.setSize (2, samplesPerBlockExpected);
    analysisBuffer.setSize (1, samplesPerBlockExpected);

    detector.prepare (sampleRate, 60.0f, 1100.0f, 128, 0);

    const int minDelay = (int) std::lround (0.0025 * sampleRate) + 256;
    const int maxDelay = juce::jlimit (minDelay, (int) (sampleRate / 10.0),
                                       (int) std::lround (0.032 * sampleRate));

    for (int channel = 0; channel < 2; ++channel)
    {
        shifters[channel].prepare (sampleRate, maxDelay);
        inputHighPass[channel].setHighPass (sampleRate, 65.0);
        outputHighPass[channel].setHighPass (sampleRate, 28.0);
    }

    resetDsp();
}

void AudioPreviewPlayer::releaseResources()
{
    transport.releaseResources();
    prepared = false;
}

void AudioPreviewPlayer::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    if (info.buffer == nullptr || info.numSamples <= 0)
        return;

    if (readerSource == nullptr || ! transport.isPlaying() || ! prepared)
    {
        info.clearActiveBufferRegion();
        return;
    }

    juce::ScopedNoDenormals noDenormals;
    transport.getNextAudioBlock (info);

    const int numSamples = info.numSamples;
    const int numOutputChannels = info.buffer->getNumChannels();
    const int numProcessedChannels = juce::jmin (2, numOutputChannels);

    if (numProcessedChannels <= 0 || numSamples > workBuffer.getNumSamples())
    {
        info.clearActiveBufferRegion();
        return;
    }

    if (sourceChannels == 1 && numOutputChannels > 1)
    {
        for (int channel = 1; channel < numOutputChannels; ++channel)
            info.buffer->copyFrom (channel, info.startSample, *info.buffer, 0, info.startSample,
                                   numSamples);
    }

    const float inputGain = juce::Decibels::decibelsToGain (
        inputGainDb.load (std::memory_order_relaxed), -100.0f);
    auto* analysis = analysisBuffer.getWritePointer (0);
    std::fill (analysis, analysis + numSamples, 0.0f);

    for (int channel = 0; channel < numProcessedChannels; ++channel)
    {
        const auto* source = info.buffer->getReadPointer (channel, info.startSample);
        auto* destination = workBuffer.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
        {
            float sample = source[i] * inputGain;
            sample = inputHighPass[channel].process (sample);
            destination[i] = sample;
            analysis[i] += sample;
        }
    }

    const float channelScale = 1.0f / (float) numProcessedChannels;
    for (int i = 0; i < numSamples; ++i)
        analysis[i] *= channelScale;

    autotune::PitchFrame frame;
    const bool newFrame = detector.process (analysis, numSamples, frame);
    const float gate = juce::Decibels::decibelsToGain (
        gateDb.load (std::memory_order_relaxed), -100.0f);

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
                lastPeriodSamples = (float) (preparedSampleRate / juce::jmax (1.0f, smoothedFrequency));
            }

            currentVoiced = true;
            voicedHoldSamples = (int64_t) (0.032 * preparedSampleRate);
        }
        else if (voicedHoldSamples > 0)
        {
            voicedHoldSamples -= numSamples;
        }
        else
        {
            if (currentVoiced)
            {
                std::fill (f0History.begin(), f0History.end(), 0.0f);
                smoothedFrequency = 0.0f;
            }

            currentVoiced = false;
        }
    }

    const double deltaSeconds = (double) numSamples / preparedSampleRate;
    const float strengthValue = strength.load (std::memory_order_relaxed);
    const float humanizeValue = humanize.load (std::memory_order_relaxed);
    const float retuneValue = retuneMs.load (std::memory_order_relaxed);
    const float tuning = tuningA4.load (std::memory_order_relaxed);
    const int root = keyRoot.load (std::memory_order_relaxed);
    const int semitones = transpose.load (std::memory_order_relaxed);
    const auto mask = (uint16_t) scaleMask.load (std::memory_order_relaxed);

    driftSamples += numSamples;
    if (driftSamples >= (int64_t) (0.25 * preparedSampleRate))
    {
        driftSamples -= (int64_t) (0.25 * preparedSampleRate);
        driftTarget = random.nextFloat() * 2.0f - 1.0f;
    }

    driftValue += (driftTarget - driftValue) * std::min (1.0f, (float) deltaSeconds * 6.0f);

    if (currentVoiced && smoothedFrequency > 0.0f)
    {
        const float currentMidi = autotune::hzToMidi (smoothedFrequency, tuning);
        const int quantized = autotune::nearestAllowedMidi (currentMidi, root, mask);
        const float blended = currentMidi + strengthValue * ((float) quantized - currentMidi);
        const float driftCents = humanizeValue * 16.0f * driftValue * strengthValue;
        const float targetMidi = blended + (float) semitones + driftCents * 0.01f;
        const float targetRatio = juce::jlimit (0.5f, 2.0f,
                                                (float) std::pow (2.0f, (targetMidi - currentMidi) / 12.0f));
        const double alpha = retuneValue < 2.0f
                                 ? 1.0
                                 : 1.0 - std::exp (-deltaSeconds / (retuneValue * 0.001f));
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

    for (int channel = 0; channel < numProcessedChannels; ++channel)
    {
        shifters[channel].pushInput (workBuffer.getReadPointer (channel), numSamples);
        shifters[channel].render (processedBuffer.getWritePointer (channel), numSamples, shifterParams);

        auto* destination = processedBuffer.getWritePointer (channel);
        for (int i = 0; i < numSamples; ++i)
        {
            float sample = outputHighPass[channel].process (destination[i]);
            destination[i] = softClip (sample);
        }
    }

    const float targetMix = tuned.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const float mixStep = (float) (1.0 / (0.012 * preparedSampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        tunedMix += juce::jlimit (-mixStep, mixStep, targetMix - tunedMix);

        if (tunedMix <= 0.0f)
            continue;

        for (int channel = 0; channel < numProcessedChannels; ++channel)
        {
            auto* destination = info.buffer->getWritePointer (channel, info.startSample);
            const auto* processed = processedBuffer.getReadPointer (channel);
            destination[i] += (processed[i] - destination[i]) * tunedMix;
        }
    }
}

void AudioPreviewPlayer::resetDsp()
{
    detector.reset();

    for (int channel = 0; channel < 2; ++channel)
    {
        shifters[channel].reset();
        inputHighPass[channel].reset();
        outputHighPass[channel].reset();
    }

    std::fill (f0History.begin(), f0History.end(), 0.0f);
    f0HistoryWrite = 0;
    smoothedFrequency = 0.0f;
    smoothedRatio = 1.0f;
    lastPeriodSamples = (float) (preparedSampleRate > 0.0 ? preparedSampleRate / 220.0 : 220.0);
    currentVoiced = false;
    voicedHoldSamples = 0;
    driftSamples = 0;
    driftValue = driftTarget = 0.0f;
    tunedMix = tuned.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
}
