#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "Core/Biquad.h"
#include "Core/GranularShifter.h"
#include "Core/PitchDetector.h"
#include "Core/PitchStabilizer.h"
#include "Recorder.h"

#include <atomic>
#include <array>
#include <deque>
#include <vector>

struct TracePoint
{
    uint64_t seq = 0;
    double timeMs = 0.0;
    float midi = 0.0f;
    float targetMidi = 0.0f;
    float confidence = 0.0f;
    bool voiced = false;
};

class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    struct Params
    {
        std::atomic<float> strength { 0.62f };
        std::atomic<float> retuneMs { 35.0f };
        std::atomic<float> humanize { 0.22f };
        std::atomic<int> keyRoot { 0 };
        std::atomic<uint32_t> scaleMask { 0x0FFF };
        std::atomic<int> transpose { 0 };
        std::atomic<float> tuningA4 { 440.0f };
        std::atomic<float> gateDb { -55.0f };
        std::atomic<float> inputGainDb { 0.0f };
        std::atomic<float> outputGainDb { 0.0f };
        std::atomic<bool> monitor { true };
        std::atomic<bool> bypass { false };
        std::atomic<int> latencyMode { 1 };
    };

    struct Snapshot
    {
        float midi = 0.0f;
        float targetMidi = 0.0f;
        float frequency = 0.0f;
        float confidence = 0.0f;
        bool voiced = false;
        bool monitoring = true;
        bool bypassed = false;
        int keyRoot = 0;
        uint32_t scaleMask = 0;
        float strength = 0.82f;
        float inputPeak = 0.0f;
        float outputPeak = 0.0f;
        double sampleRate = 48000.0;
        int bufferSize = 0;
        double ioLatencyMs = 0.0;
        double algorithmLatencyMs = 0.0;
        double totalLatencyMs = 0.0;
        juce::String deviceType;
        juce::String inputName;
        juce::String outputName;
        bool audioRunning = false;
        bool recording = false;
        double recordingSeconds = 0.0;
        juce::String recordingPath;
    };

    AudioEngine();
    ~AudioEngine() override;

    juce::String initialise (const juce::XmlElement* savedDeviceState);
    void shutdown();
    bool isOpen() const;
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    juce::String getLastError() const;

    Params params;

    Snapshot getSnapshot() const;
    void getTrace (std::vector<TracePoint>& destination, double historySeconds, uint64_t* newestSequence = nullptr) const;
    void getPointsSince (uint64_t afterSequence, std::vector<TracePoint>& destination, uint64_t& newestSequence) const;
    double getAlgorithmLatencyMs() const;
    int getLatencyModeSamples() const;

    bool startRecording();
    juce::String stopRecording();
    bool isRecording() const { return recorder.isRecording(); }
    double getRecordingSeconds() const { return recorder.getDurationSeconds(); }
    juce::String getLastRecordingPath() const { return recorder.getLastFilePath(); }
    juce::File getRecordingsDirectory() const;

    /** Plays a short reference tone for a MIDI note (keyboard audition). */
    void playReferenceNote (int midiNote);

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

private:
    void processChunk (const float* input, int outputOffset, int numSamples);
    void updateTargetAndRatio (float deltaSeconds);
    void pushTracePoint (float midi, float target, float confidence, bool voiced);
    void prepareForDevice (double sampleRate);
    void updateLatencyInfo();
    void renderReferenceNotes (float* output, int numSamples);
    void startReferenceVoice (int midiNote);

    struct ReferenceVoice
    {
        float phase = 0.0f;
        float phaseIncrement = 0.0f;
        float envelope = 0.0f;
        double ageSamples = 0.0;
        bool active = false;
    };

    juce::AudioDeviceManager deviceManager;
    mutable juce::CriticalSection statusLock;
    juce::String lastError;

    autotune::PitchDetector detector;
    autotune::PitchStabilizer pitchStabilizer;
    autotune::GranularShifter shifter;
    autotune::Biquad inputHighPass;
    autotune::Biquad outputHighPass;
    Recorder recorder;

    double currentSampleRate = 48000.0;
    int currentBlockSize = 0;
    double ioLatencyMs = 0.0;
    int algorithmDelaySamples = 0;
    bool prepared = false;

    std::vector<float> inputBuffer;
    std::vector<float> wetBuffer;
    std::vector<float> monoOutput;
    std::vector<float> referenceBuffer;

    static constexpr int maxReferenceVoices = 96;
    static constexpr int referenceQueueSize = 96;
    static constexpr int referenceTableSize = 4096;
    std::array<ReferenceVoice, maxReferenceVoices> referenceVoices {};
    std::array<int, referenceQueueSize> referenceQueue {};
    std::array<float, referenceTableSize + 1> referenceTable {};
    std::atomic<uint32_t> referenceQueueWrite { 0 };
    std::atomic<uint32_t> referenceQueueRead { 0 };
    float referenceDecayCoefficient = 0.0f;
    float referenceAttackSamples = 1.0f;

    float smoothedFrequency = 0.0f;
    float smoothedRatio = 1.0f;
    float lastPeriodSamples = 220.0f;
    float currentTargetMidi = 0.0f;
    float currentMidi = 0.0f;
    float currentConfidence = 0.0f;
    bool currentVoiced = false;
    double voicedHoldUntilMs = 0.0;
    float smoothedInputGain = 1.0f;
    float smoothedOutputGain = 1.0f;
    float gainSmoothingCoefficient = 0.01f;

    juce::Random random;
    float driftValue = 0.0f;
    float driftTarget = 0.0f;
    double driftTimer = 0.0;

    std::atomic<float> uiMidi { 0.0f };
    std::atomic<float> uiTarget { 0.0f };
    std::atomic<float> uiHz { 0.0f };
    std::atomic<float> uiConfidence { 0.0f };
    std::atomic<bool> uiVoiced { false };
    std::atomic<float> uiInputPeak { 0.0f };
    std::atomic<float> uiOutputPeak { 0.0f };
    std::atomic<bool> audioRunning { false };
    std::atomic<double> storedIoLatencyMs { 0.0 };

    mutable juce::SpinLock traceLock;
    static constexpr int traceCapacity = 8192;
    std::vector<TracePoint> traceRing;
    int traceHead = 0;
    int traceCount = 0;
    uint64_t traceSequence = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
