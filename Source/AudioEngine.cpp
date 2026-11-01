#include "AudioEngine.h"

#include "Core/NoteQuantizer.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int internalChunkSize = 256;

    float softClip (float x) noexcept
    {
        constexpr float threshold = 0.985f;
        if (x > threshold)
            return threshold + (1.0f - threshold) * std::tanh ((x - threshold) / (1.0f - threshold));
        if (x < -threshold)
            return -threshold - (1.0f - threshold) * std::tanh ((-x - threshold) / (1.0f - threshold));
        return x;
    }

    float softLimitReference (float x) noexcept
    {
        constexpr float threshold = 0.55f;
        const float magnitude = std::abs (x);

        if (magnitude <= threshold)
            return x;

        const float limited = threshold
                                + (1.0f - threshold) * std::tanh ((magnitude - threshold) / (1.0f - threshold));
        return x < 0.0f ? -limited : limited;
    }
}

AudioEngine::AudioEngine()
{
    traceRing.assign ((size_t) traceCapacity, TracePoint{});
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

juce::String AudioEngine::initialise (const juce::XmlElement* savedDeviceState)
{
    shutdown();

    juce::String error;

    if (savedDeviceState != nullptr)
    {
        error = deviceManager.initialise (1, 2, savedDeviceState, true);
    }

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        juce::AudioDeviceManager::AudioDeviceSetup preferred;
        preferred.sampleRate = 48000.0;
        preferred.bufferSize = 256;
        preferred.useDefaultInputChannels = true;
        preferred.useDefaultOutputChannels = true;

        error = deviceManager.initialise (1, 2, nullptr, true, {}, &preferred);

        if (deviceManager.getCurrentAudioDevice() == nullptr)
            error = deviceManager.initialise (1, 2, nullptr, true);
    }

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        const juce::ScopedLock sl (statusLock);
        lastError = error.isNotEmpty() ? error : "No audio input device could be opened.";
        return lastError;
    }

    {
        const juce::ScopedLock sl (statusLock);
        lastError.clear();
    }

    deviceManager.addAudioCallback (this);
    return {};
}

void AudioEngine::shutdown()
{
    stopRecording();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    prepared = false;
    audioRunning.store (false, std::memory_order_release);
}

bool AudioEngine::isOpen() const
{
    return deviceManager.getCurrentAudioDevice() != nullptr;
}

juce::String AudioEngine::getLastError() const
{
    const juce::ScopedLock sl (statusLock);
    return lastError;
}

int AudioEngine::getLatencyModeSamples() const
{
    const int mode = std::clamp (params.latencyMode.load (std::memory_order_relaxed), 0, 2);
    const double seconds = mode == 0 ? 0.016 : (mode == 1 ? 0.0213 : 0.032);
    return std::max (256, (int) std::lround (seconds * currentSampleRate));
}

double AudioEngine::getAlgorithmLatencyMs() const
{
    if (currentSampleRate <= 0.0)
        return 0.0;

    if (prepared)
        return 1000.0 * (double) shifter.getAverageDelaySamples() / currentSampleRate;

    const int maxDelay = getLatencyModeSamples();
    const int minDelay = std::max (32, (int) std::lround (0.0025 * currentSampleRate));
    return 1000.0 * (double) ((minDelay + maxDelay) / 2) / currentSampleRate;
}

AudioEngine::Snapshot AudioEngine::getSnapshot() const
{
    Snapshot snapshot;

    snapshot.midi = uiMidi.load (std::memory_order_relaxed);
    snapshot.targetMidi = uiTarget.load (std::memory_order_relaxed);
    snapshot.frequency = uiHz.load (std::memory_order_relaxed);
    snapshot.confidence = uiConfidence.load (std::memory_order_relaxed);
    snapshot.voiced = uiVoiced.load (std::memory_order_relaxed);
    snapshot.inputPeak = uiInputPeak.load (std::memory_order_relaxed);
    snapshot.outputPeak = uiOutputPeak.load (std::memory_order_relaxed);
    snapshot.monitoring = params.monitor.load (std::memory_order_relaxed);
    snapshot.bypassed = params.bypass.load (std::memory_order_relaxed);
    snapshot.keyRoot = params.keyRoot.load (std::memory_order_relaxed);
    snapshot.scaleMask = params.scaleMask.load (std::memory_order_relaxed);
    snapshot.strength = params.strength.load (std::memory_order_relaxed);
    snapshot.audioRunning = audioRunning.load (std::memory_order_relaxed);
    snapshot.recording = recorder.isRecording();
    snapshot.recordingSeconds = recorder.getDurationSeconds();
    snapshot.recordingPath = recorder.getLastFilePath();

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        snapshot.sampleRate = device->getCurrentSampleRate();
        snapshot.bufferSize = device->getCurrentBufferSizeSamples();
    }
    else
    {
        snapshot.sampleRate = currentSampleRate;
    }

    const auto setup = deviceManager.getAudioDeviceSetup();
    snapshot.inputName = setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : "Default input";
    snapshot.outputName = setup.outputDeviceName.isNotEmpty() ? setup.outputDeviceName : "Default output";
    snapshot.deviceType = deviceManager.getCurrentAudioDeviceType();

    snapshot.ioLatencyMs = storedIoLatencyMs.load (std::memory_order_relaxed);
    snapshot.algorithmLatencyMs = getAlgorithmLatencyMs();
    snapshot.totalLatencyMs = snapshot.ioLatencyMs + snapshot.algorithmLatencyMs;

    return snapshot;
}

void AudioEngine::getTrace (std::vector<TracePoint>& destination, double historySeconds, uint64_t* newestSequence) const
{
    destination.clear();

    const juce::SpinLock::ScopedLockType lock (traceLock);
    if (traceCount <= 0)
    {
        if (newestSequence != nullptr)
            *newestSequence = traceSequence;
        return;
    }

    const auto& newest = traceRing[(size_t) ((traceHead - 1 + traceCapacity) % traceCapacity)];
    const double cutoff = newest.timeMs - historySeconds * 1000.0;

    destination.reserve ((size_t) std::min (traceCount, 2400));

    const int oldest = (traceHead - traceCount + traceCapacity) % traceCapacity;
    for (int i = 0; i < traceCount; ++i)
    {
        const auto& point = traceRing[(size_t) ((oldest + i) % traceCapacity)];
        if (point.timeMs >= cutoff)
            destination.push_back (point);
    }

    if (newestSequence != nullptr)
        *newestSequence = newest.seq;
}

void AudioEngine::getPointsSince (uint64_t afterSequence, std::vector<TracePoint>& destination, uint64_t& newestSequence) const
{
    destination.clear();

    const juce::SpinLock::ScopedLockType lock (traceLock);
    newestSequence = traceSequence;

    if (traceCount <= 0)
        return;

    const int oldest = (traceHead - traceCount + traceCapacity) % traceCapacity;
    destination.reserve ((size_t) std::min (traceCount, 256));

    for (int i = 0; i < traceCount; ++i)
    {
        const auto& point = traceRing[(size_t) ((oldest + i) % traceCapacity)];
        if (point.seq > afterSequence)
            destination.push_back (point);
    }
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    currentSampleRate = std::max (8000.0, device->getCurrentSampleRate());
    currentBlockSize = device->getCurrentBufferSizeSamples();
    algorithmDelaySamples = getLatencyModeSamples();

    inputBuffer.assign (std::max (internalChunkSize, currentBlockSize), 0.0f);
    wetBuffer.assign (std::max (internalChunkSize, currentBlockSize), 0.0f);
    monoOutput.assign (std::max (8192, currentBlockSize), 0.0f);
    referenceBuffer.assign (std::max (8192, currentBlockSize), 0.0f);

    for (auto& voice : referenceVoices)
        voice = ReferenceVoice{};

    for (int i = 0; i < referenceTableSize; ++i)
    {
        const double phase = 2.0 * juce::MathConstants<double>::pi * (double) i / (double) referenceTableSize;
        referenceTable[(size_t) i] = (float) (std::sin (phase) * 0.62
                                              + std::sin (phase * 2.0) * 0.24
                                              + std::sin (phase * 3.0) * 0.10
                                              + std::sin (phase * 4.0) * 0.04);
    }
    referenceTable[(size_t) referenceTableSize] = referenceTable[0];

    referenceDecayCoefficient = (float) std::exp (-2.8 / currentSampleRate);
    referenceAttackSamples = (float) (0.005 * currentSampleRate);

    referenceQueueRead.store (0, std::memory_order_relaxed);
    referenceQueueWrite.store (0, std::memory_order_relaxed);

    detector.prepare (currentSampleRate, 60.0f, 1100.0f, 128, 0);
    shifter.prepare (currentSampleRate, algorithmDelaySamples);

    inputHighPass.setHighPass (currentSampleRate, 65.0);
    outputHighPass.setHighPass (currentSampleRate, 28.0);
    inputHighPass.reset();
    outputHighPass.reset();

    f0History.assign (5, 0.0f);
    f0HistoryWrite = 0;
    smoothedFrequency = 0.0f;
    smoothedRatio = 1.0f;
    lastPeriodSamples = (float) (currentSampleRate / 220.0);
    currentTargetMidi = 0.0f;
    currentMidi = 0.0f;
    currentConfidence = 0.0f;
    currentVoiced = false;
    voicedHoldUntilMs = 0.0;
    driftValue = driftTarget = 0.0f;
    driftTimer = 0.0;
    smoothedInputGain = 1.0f;
    smoothedOutputGain = 1.0f;
    gainSmoothingCoefficient = 1.0f - std::exp (-1.0f / (0.015f * (float) currentSampleRate));

    const double inputLatency = std::max (0, device->getInputLatencyInSamples());
    const double outputLatency = std::max (0, device->getOutputLatencyInSamples());
    ioLatencyMs = 1000.0 * (inputLatency + outputLatency) / currentSampleRate;
    storedIoLatencyMs.store (ioLatencyMs, std::memory_order_relaxed);

    {
        const juce::SpinLock::ScopedLockType lock (traceLock);
        traceHead = 0;
        traceCount = 0;
    }

    prepared = true;
    audioRunning.store (true, std::memory_order_release);
}

void AudioEngine::audioDeviceStopped()
{
    prepared = false;
    audioRunning.store (false, std::memory_order_release);
}

void AudioEngine::audioDeviceError (const juce::String& errorMessage)
{
    const juce::ScopedLock sl (statusLock);
    lastError = errorMessage;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    if (! prepared || numSamples <= 0 || numSamples > (int) monoOutput.size())
        return;

    if (const int desiredDelay = getLatencyModeSamples(); desiredDelay != algorithmDelaySamples)
    {
        algorithmDelaySamples = desiredDelay;
        shifter.setMaxDelay (desiredDelay);
    }

    for (int offset = 0; offset < numSamples; offset += internalChunkSize)
    {
        const int chunk = std::min (internalChunkSize, numSamples - offset);
        const float* input = (numInputChannels > 0 && inputChannelData[0] != nullptr)
                                 ? inputChannelData[0] + offset
                                 : nullptr;
        processChunk (input, offset, chunk);
    }

    if (recorder.isRecording())
        recorder.writeSamples (monoOutput.data(), numSamples);

    renderReferenceNotes (referenceBuffer.data(), numSamples);

    const bool monitorOn = params.monitor.load (std::memory_order_relaxed);

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] == nullptr)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            const float voice = monitorOn ? monoOutput[(size_t) i] : 0.0f;
            outputChannelData[channel][i] = softClip (voice + referenceBuffer[(size_t) i]);
        }
    }
}

void AudioEngine::processChunk (const float* input, int outputOffset, int numSamples)
{
    const float inputGainDb = params.inputGainDb.load (std::memory_order_relaxed);
    const float targetInputGain = juce::Decibels::decibelsToGain (inputGainDb);
    const float gateLinear = juce::Decibels::decibelsToGain (params.gateDb.load (std::memory_order_relaxed));
    const bool bypass = params.bypass.load (std::memory_order_relaxed);

    float inputPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float raw = input != nullptr ? input[i] : 0.0f;
        inputPeak = std::max (inputPeak, std::abs (raw));

        smoothedInputGain += gainSmoothingCoefficient * (targetInputGain - smoothedInputGain);
        float sample = raw * smoothedInputGain;
        sample = inputHighPass.process (sample);
        inputBuffer[(size_t) i] = sample;
    }

    autotune::PitchFrame frame;
    const bool newFrame = detector.process (inputBuffer.data(), numSamples, frame);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    if (newFrame)
    {
        const bool strong = frame.voiced && frame.rms >= gateLinear && frame.confidence >= 0.55f;

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
                lastPeriodSamples = (float) (currentSampleRate / std::max (1.0f, smoothedFrequency));
            }

            currentConfidence = frame.confidence;
            voicedHoldUntilMs = nowMs + 32.0;
            currentVoiced = true;
        }
        else if (nowMs > voicedHoldUntilMs)
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

    updateTargetAndRatio ((float) numSamples / (float) currentSampleRate);

    if (newFrame)
    {
        if (currentVoiced && smoothedFrequency > 0.0f)
            pushTracePoint (currentMidi, currentTargetMidi, currentConfidence, true);
        else
            pushTracePoint (0.0f, 0.0f, 0.0f, false);
    }

    const bool audioVoiced = currentVoiced && ! bypass;

    autotune::ShifterParams shifterParams;
    shifterParams.periodSamples = lastPeriodSamples;
    shifterParams.ratio = smoothedRatio;
    shifterParams.voiced = audioVoiced;

    shifter.pushInput (inputBuffer.data(), numSamples);
    shifter.render (wetBuffer.data(), numSamples, shifterParams);

    const float targetOutputGain = juce::Decibels::decibelsToGain (params.outputGainDb.load (std::memory_order_relaxed));
    float outputPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        smoothedOutputGain += gainSmoothingCoefficient * (targetOutputGain - smoothedOutputGain);
        float sample = wetBuffer[(size_t) i] * smoothedOutputGain;
        sample = outputHighPass.process (sample);
        sample = softClip (sample);
        monoOutput[(size_t) (outputOffset + i)] = sample;
        outputPeak = std::max (outputPeak, std::abs (sample));
    }

    uiInputPeak.store (std::max (inputPeak, uiInputPeak.load (std::memory_order_relaxed) * 0.88f),
                       std::memory_order_relaxed);
    uiOutputPeak.store (std::max (outputPeak, uiOutputPeak.load (std::memory_order_relaxed) * 0.88f),
                        std::memory_order_relaxed);
}

void AudioEngine::updateTargetAndRatio (float deltaSeconds)
{
    const float strength = std::clamp (params.strength.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const float retuneMs = std::max (0.0f, params.retuneMs.load (std::memory_order_relaxed));
    const float humanize = std::clamp (params.humanize.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const int keyRoot = params.keyRoot.load (std::memory_order_relaxed);
    const uint16_t scaleMask = (uint16_t) params.scaleMask.load (std::memory_order_relaxed);
    const int transpose = params.transpose.load (std::memory_order_relaxed);
    const float tuningA4 = params.tuningA4.load (std::memory_order_relaxed);

    driftTimer += deltaSeconds;
    if (driftTimer >= 0.25)
    {
        driftTimer = 0.0;
        driftTarget = random.nextFloat() * 2.0f - 1.0f;
    }
    driftValue += (driftTarget - driftValue) * std::min (1.0f, deltaSeconds * 6.0f);

    if (currentVoiced && smoothedFrequency > 0.0f)
    {
        currentMidi = autotune::hzToMidi (smoothedFrequency, tuningA4);

        const int quantized = autotune::nearestAllowedMidi (currentMidi, keyRoot, scaleMask);
        const float blended = currentMidi + strength * ((float) quantized - currentMidi);
        const float driftCents = humanize * 16.0f * driftValue * strength;

        currentTargetMidi = blended + (float) transpose + driftCents * 0.01f;

        float targetRatio = std::pow (2.0f, (currentTargetMidi - currentMidi) / 12.0f);
        targetRatio = std::clamp (targetRatio, 0.5f, 2.0f);

        const float alpha = retuneMs < 2.0f
                                ? 1.0f
                                : 1.0f - std::exp (-deltaSeconds / (retuneMs * 0.001f));
        smoothedRatio += alpha * (targetRatio - smoothedRatio);

        uiMidi.store (currentMidi, std::memory_order_relaxed);
        uiTarget.store (currentTargetMidi, std::memory_order_relaxed);
        uiHz.store (smoothedFrequency, std::memory_order_relaxed);
        uiConfidence.store (currentConfidence, std::memory_order_relaxed);
        uiVoiced.store (true, std::memory_order_relaxed);
    }
    else
    {
        currentTargetMidi = 0.0f;

        const float alpha = 1.0f - std::exp (-deltaSeconds / 0.03f);
        smoothedRatio += alpha * (1.0f - smoothedRatio);

        uiMidi.store (0.0f, std::memory_order_relaxed);
        uiTarget.store (0.0f, std::memory_order_relaxed);
        uiConfidence.store (0.0f, std::memory_order_relaxed);
        uiVoiced.store (false, std::memory_order_relaxed);
    }
}

void AudioEngine::pushTracePoint (float midi, float target, float confidence, bool voiced)
{
    TracePoint point;
    point.timeMs = juce::Time::getMillisecondCounterHiRes();
    point.midi = midi;
    point.targetMidi = target;
    point.confidence = confidence;
    point.voiced = voiced;

    const juce::SpinLock::ScopedLockType lock (traceLock);
    point.seq = ++traceSequence;
    traceRing[(size_t) traceHead] = point;
    traceHead = (traceHead + 1) % traceCapacity;
    traceCount = std::min (traceCapacity, traceCount + 1);
}

void AudioEngine::playReferenceNote (int midiNote)
{
    if (! audioRunning.load (std::memory_order_acquire))
        return;

    if (midiNote < 0 || midiNote > 127)
        return;

    const auto write = referenceQueueWrite.load (std::memory_order_relaxed);
    const auto next = (write + 1) % (uint32_t) referenceQueueSize;

    if (next == referenceQueueRead.load (std::memory_order_acquire))
        return;

    referenceQueue[(size_t) write] = midiNote;
    referenceQueueWrite.store (next, std::memory_order_release);
}

void AudioEngine::startReferenceVoice (int midiNote)
{
    const float frequency = autotune::midiToHz ((float) midiNote,
                                                 params.tuningA4.load (std::memory_order_relaxed));
    if (frequency <= 0.0f || currentSampleRate <= 0.0)
        return;

    ReferenceVoice* voice = nullptr;

    for (auto& candidate : referenceVoices)
    {
        if (! candidate.active)
        {
            voice = &candidate;
            break;
        }
    }

    if (voice == nullptr)
        return;

    voice->phase = 0.0f;
    voice->phaseIncrement = frequency / (float) currentSampleRate;
    voice->envelope = 0.0f;
    voice->ageSamples = 0.0;
    voice->active = true;
}

void AudioEngine::renderReferenceNotes (float* output, int numSamples)
{
    if (output == nullptr || numSamples <= 0)
        return;

    auto read = referenceQueueRead.load (std::memory_order_acquire);
    const auto write = referenceQueueWrite.load (std::memory_order_acquire);

    while (read != write)
    {
        startReferenceVoice (referenceQueue[(size_t) read]);
        read = (read + 1) % (uint32_t) referenceQueueSize;
    }

    referenceQueueRead.store (read, std::memory_order_release);

    bool anyActive = false;
    for (const auto& voice : referenceVoices)
        anyActive = anyActive || voice.active;

    if (! anyActive)
    {
        std::fill (output, output + numSamples, 0.0f);
        return;
    }

    const double maxAgeSamples = 2.8 * currentSampleRate;
    const float master = std::max (0.0f, smoothedOutputGain);

    for (int i = 0; i < numSamples; ++i)
    {
        float mix = 0.0f;

        for (auto& voice : referenceVoices)
        {
            if (! voice.active)
                continue;

            const float tablePosition = voice.phase * (float) referenceTableSize;
            int tableIndex = (int) tablePosition;

            if (tableIndex < 0)
                tableIndex = 0;
            else if (tableIndex >= referenceTableSize)
                tableIndex = referenceTableSize - 1;

            const float fraction = tablePosition - (float) tableIndex;
            const float wave = referenceTable[(size_t) tableIndex]
                               + fraction * (referenceTable[(size_t) tableIndex + 1]
                                             - referenceTable[(size_t) tableIndex]);

            if (voice.ageSamples < (double) referenceAttackSamples)
                voice.envelope = (float) (voice.ageSamples / (double) referenceAttackSamples);
            else
                voice.envelope *= referenceDecayCoefficient;

            mix += wave * voice.envelope * 0.20f;

            voice.phase += voice.phaseIncrement;
            if (voice.phase >= 1.0f)
                voice.phase -= std::floor (voice.phase);

            voice.ageSamples += 1.0;

            if (voice.ageSamples > maxAgeSamples
                || (voice.ageSamples > (double) referenceAttackSamples && voice.envelope < 0.0004f))
                voice.active = false;
        }

        output[i] = softLimitReference (mix * master);
    }
}

juce::File AudioEngine::getRecordingsDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile ("Recordings");
}

bool AudioEngine::startRecording()
{
    if (! prepared || recorder.isRecording())
        return false;

    const auto result = recorder.start (currentSampleRate, getRecordingsDirectory());
    return result.isNotEmpty();
}

juce::String AudioEngine::stopRecording()
{
    if (! recorder.isRecording())
        return recorder.getLastFilePath();

    recorder.stop();
    return recorder.getLastFilePath();
}
