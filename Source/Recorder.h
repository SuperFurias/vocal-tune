#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <thread>

/** Records processed audio to 24-bit mono WAV on a background thread. */
class Recorder
{
public:
    Recorder();
    ~Recorder();

    juce::String start (double sampleRate, const juce::File& directory);
    void stop();

    void writeSamples (const float* samples, int numSamples);

    bool isRecording() const noexcept { return recording.load (std::memory_order_relaxed); }
    double getDurationSeconds() const;
    juce::String getLastFilePath() const;

private:
    void threadLoop();
    void drainToFile();

    static constexpr int ringSize = 1 << 18;

    juce::AudioBuffer<float> ring { 1, ringSize };
    int writeIndex = 0;
    int readIndex = 0;
    int samplesUsed = 0;
    juce::SpinLock ringLock;

    std::unique_ptr<juce::AudioFormatWriter> writer;

    std::thread worker;
    std::atomic<bool> recording { false };
    std::atomic<bool> stopRequested { false };
    std::atomic<uint64_t> samplesWritten { 0 };

    double sampleRate = 48000.0;
    juce::String lastFilePath;
    juce::CriticalSection fileLock;
    juce::String pendingError;
};
