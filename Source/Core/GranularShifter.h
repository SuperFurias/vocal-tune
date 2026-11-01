#pragma once

#include <cstdint>
#include <vector>

namespace autotune
{

struct ShifterParams
{
    float periodSamples = 0.0f;
    float ratio = 1.0f;
    bool voiced = false;
};

/**
    Low-latency granular pitch shifter.

    Two resampling read taps sweep relative to the input write pointer at the
    requested ratio and cross-fade with Hann windows. Grain lengths are aligned
    to whole pitch periods so the two taps stay phase-coherent when they cross-
    fade, which keeps periodic material clean. The read taps always stay within a
    fixed delay window, so the signal remains time-aligned with no pitch drift.
*/
class GranularShifter
{
public:
    GranularShifter();

    void prepare (double sampleRate, int maxDelaySamples);
    void reset();

    void pushInput (const float* samples, int numSamples);
    void render (float* output, int numSamples, const ShifterParams& params);

    void setMaxDelay (int samples);
    int getMaxDelaySamples() const noexcept { return maxDelay; }
    int getAverageDelaySamples() const noexcept { return (minDelay + maxDelay) / 2; }
    double getSampleRate() const noexcept { return sampleRate; }

private:
    struct Tap
    {
        double readPos = 0.0;
        double age = 0.0;
        double grainLength = 0.0;
        double ratio = 1.0;
        bool active = false;
    };

    float readInput (int64_t index) const;
    float readInterpolated (double position) const;
    void computeGrain (double ratio, double period, double& grainLength, double& startDelay) const;
    void startTap (Tap& tap, int64_t outputIndex, double ratio, double period, double ageOffset);

    static constexpr int inputRingSize = 1 << 16;
    static constexpr int inputMask = inputRingSize - 1;

    double sampleRate = 48000.0;
    int maxDelay = 1024;
    int pendingMaxDelay = 1024;
    int minDelay = 128;

    std::vector<float> inputRing;
    int64_t inputWritePos = 0;
    int64_t outputReadPos = 0;

    Tap taps[2];
    bool tapsPrimed = false;
    float wetGain = 0.0f;
};

} // namespace autotune
