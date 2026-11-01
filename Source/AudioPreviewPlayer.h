#pragma once

#include "OfflineRenderer.h"
#include "Core/Biquad.h"
#include "Core/GranularShifter.h"
#include "Core/PitchDetector.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>
#include <vector>

class AudioPreviewPlayer : private juce::AudioSource
{
public:
    AudioPreviewPlayer();
    ~AudioPreviewPlayer() override;

    bool loadFile (const juce::File& file, juce::String& error);
    void release();

    juce::String prepareForPlayback();
    juce::String start();
    void pause();
    void stop();
    void seekToSeconds (double seconds);
    bool isPlaying() const;
    bool hasFile() const;
    double getPositionSeconds() const;
    double getDurationSeconds() const;

    void setTuned (bool shouldBeTuned);
    bool isTuned() const;
    void setSettings (const OfflineRenderer::Settings& settings);

private:
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;

    juce::String ensureAudioReady();
    void resetDsp();

    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;

    autotune::PitchDetector detector;
    autotune::GranularShifter shifters[2];
    autotune::Biquad inputHighPass[2];
    autotune::Biquad outputHighPass[2];
    juce::AudioBuffer<float> workBuffer;
    juce::AudioBuffer<float> processedBuffer;
    juce::AudioBuffer<float> analysisBuffer;

    std::vector<float> f0History;
    int f0HistoryWrite = 0;
    float smoothedFrequency = 0.0f;
    float smoothedRatio = 1.0f;
    float lastPeriodSamples = 220.0f;
    bool currentVoiced = false;
    int64_t voicedHoldSamples = 0;
    int64_t driftSamples = 0;
    float driftValue = 0.0f;
    float driftTarget = 0.0f;
    float tunedMix = 1.0f;
    juce::Random random;

    std::atomic<bool> tuned { true };
    std::atomic<float> strength { 0.62f };
    std::atomic<float> retuneMs { 35.0f };
    std::atomic<float> humanize { 0.22f };
    std::atomic<float> tuningA4 { 440.0f };
    std::atomic<float> gateDb { -55.0f };
    std::atomic<float> inputGainDb { 0.0f };
    std::atomic<int> keyRoot { 0 };
    std::atomic<int> transpose { 0 };
    std::atomic<uint32_t> scaleMask { 0x0FFFu };

    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    int sourceChannels = 0;
    double durationSeconds = 0.0;
    bool prepared = false;
    bool audioReady = false;
    bool callbackAdded = false;
};
