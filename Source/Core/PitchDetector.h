#pragma once

#include <cstdint>
#include <vector>

namespace autotune
{

struct PitchFrame
{
    float frequency = 0.0f;   // Hz
    float midi = 0.0f;        // fractional MIDI note
    float confidence = 0.0f;  // 0..1
    float rms = 0.0f;         // linear RMS of the analysis window
    bool voiced = false;
    bool valid = false;       // true when an analysis actually ran
};

/**
    YIN pitch detector with FFT-accelerated autocorrelation.

    Streaming use: call prepare(), then feed blocks with process(). Detection is
    performed once per hop on the most recent window of samples.
*/
class PitchDetector
{
public:
    PitchDetector() = default;

    void prepare (double sampleRate,
                  float minHz = 60.0f,
                  float maxHz = 1100.0f,
                  int hopSize = 128,
                  int windowSize = 0);

    void reset();

    /** Feeds samples; returns true when a new PitchFrame has been written. */
    bool process (const float* samples, int numSamples, PitchFrame& result);

    /** Analysis of an explicit window (no streaming state affected). */
    PitchFrame analyseWindow (const float* samples, int numSamples);

    double getSampleRate() const noexcept { return sampleRate; }
    int getWindowSize() const noexcept { return windowSize; }
    int getHopSize() const noexcept { return hopSize; }
    float getMinHz() const noexcept { return minHz; }
    float getMaxHz() const noexcept { return maxHz; }

private:
    PitchFrame analyseInternal (const float* samples, int numSamples);
    void prepareFft();
    void fft (std::vector<double>& re, std::vector<double>& im, bool inverse) const;

    double sampleRate = 48000.0;
    float minHz = 60.0f;
    float maxHz = 1100.0f;
    int hopSize = 128;
    int windowSize = 2048;
    int fftSize = 4096;
    float yinThreshold = 0.15f;
    float maxAcceptedCmndf = 0.32f;
    float minRms = 0.0006f;

    std::vector<float> history;
    int samplesSinceHop = 0;

    std::vector<int> bitRev;
    std::vector<float> windowScratch;
    std::vector<double> energies;
    std::vector<double> diff;
    std::vector<double> cmndf;
    std::vector<double> fftRe;
    std::vector<double> fftIm;
};

} // namespace autotune
