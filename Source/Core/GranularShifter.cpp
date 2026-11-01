#include "GranularShifter.h"

#include <algorithm>
#include <cmath>

namespace autotune
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
}

GranularShifter::GranularShifter()
{
    inputRing.assign ((size_t) inputRingSize, 0.0f);
}

void GranularShifter::prepare (double newSampleRate, int maxDelaySamples)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    minDelay = std::max (32, (int) std::lround (0.0025 * sampleRate));
    maxDelay = std::clamp (maxDelaySamples, minDelay + 128, (int) sampleRate / 10);
    pendingMaxDelay = maxDelay;
    reset();
}

void GranularShifter::reset()
{
    std::fill (inputRing.begin(), inputRing.end(), 0.0f);
    inputWritePos = 0;
    outputReadPos = 0;
    for (auto& tap : taps)
        tap = Tap{};
    tapsPrimed = false;
    wetGain = 0.0f;
}

void GranularShifter::setMaxDelay (int samples)
{
    pendingMaxDelay = std::clamp (samples, minDelay + 128, (int) sampleRate / 10);
}

void GranularShifter::pushInput (const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        inputRing[(size_t) (inputWritePos & inputMask)] = samples[i];
        ++inputWritePos;
    }
}

float GranularShifter::readInput (int64_t index) const
{
    if (index < 0 || index >= inputWritePos)
        return 0.0f;
    return inputRing[(size_t) (index & inputMask)];
}

float GranularShifter::readInterpolated (double position) const
{
    const int64_t i = (int64_t) std::floor (position);
    const float t = (float) (position - (double) i);

    // Catmull-Rom interpolation.
    const float p0 = readInput (i - 1);
    const float p1 = readInput (i);
    const float p2 = readInput (i + 1);
    const float p3 = readInput (i + 2);
    const float t2 = t * t;
    const float t3 = t2 * t;

    return 0.5f * ((2.0f * p1)
                   + (-p0 + p2) * t
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                   + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void GranularShifter::computeGrain (double ratio, double period, double& grainLength, double& startDelay) const
{
    const double maxSweep = (double) (maxDelay - minDelay - 32);
    const double driftPerSample = std::abs (1.0 - ratio);

    if (driftPerSample < 1.0e-6)
    {
        grainLength = 8192.0;
        startDelay = 0.5 * (double) (minDelay + maxDelay);
        return;
    }

    double desired = std::clamp (maxSweep / driftPerSample, 512.0, 8192.0);
    double sweep = desired * driftPerSample;

    // Make the delay difference between the two cross-fading taps a whole
    // number of pitch periods so periodic material stays in phase.
    if (period > 16.0)
    {
        const double k = std::round (sweep / (2.0 * period));
        if (k >= 1.0)
        {
            const double alignedSweep = 2.0 * k * period;
            if (alignedSweep <= maxSweep)
            {
                sweep = alignedSweep;
                desired = sweep / driftPerSample;
            }
        }
    }

    grainLength = std::clamp (desired, 512.0, 8192.0);
    grainLength = std::min (grainLength, maxSweep / driftPerSample);
    startDelay = ratio > 1.0 ? (double) maxDelay : (double) minDelay;
}

void GranularShifter::startTap (Tap& tap, int64_t outputIndex, double ratio, double period, double ageOffset)
{
    double grain = 0.0, startDelay = 0.0;
    computeGrain (ratio, period, grain, startDelay);

    tap.ratio = ratio;
    tap.grainLength = grain;
    tap.age = ageOffset;
    tap.readPos = (double) outputIndex - startDelay + (1.0 - ratio) * ageOffset;
    tap.active = true;
}

void GranularShifter::render (float* output, int numSamples, const ShifterParams& params)
{
    if (output == nullptr || numSamples <= 0)
        return;

    if (pendingMaxDelay != maxDelay)
    {
        maxDelay = pendingMaxDelay;
        tapsPrimed = false;
        wetGain = 0.0f;
    }

    const int64_t blockStart = outputReadPos;
    const bool voiced = params.voiced && std::isfinite (params.ratio);
    const double ratio = std::clamp ((double) params.ratio, 0.5, 2.0);
    const double period = (params.periodSamples > 0.0f && std::isfinite (params.periodSamples))
                            ? std::clamp ((double) params.periodSamples, 8.0, sampleRate / 40.0)
                            : 0.0;

    if (! tapsPrimed)
    {
        startTap (taps[0], blockStart, ratio, period, 0.0);
        const double g = taps[0].grainLength;
        startTap (taps[1], blockStart, ratio, period, 0.5 * g);
        tapsPrimed = true;
    }

    const float attack = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));
    const float release = 1.0f - std::exp (-1.0f / (0.025f * (float) sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        const int64_t outIndex = blockStart + i;

        float wet = 0.0f;
        float weight = 0.0f;

        for (auto& tap : taps)
        {
            if (! tap.active)
                continue;

            if (tap.age >= tap.grainLength)
                startTap (tap, outIndex, ratio, period, 0.0);

            const double phase = tap.age / std::max (1.0, tap.grainLength);
            const float w = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * phase));

            wet += w * readInterpolated (tap.readPos);
            weight += w;

            tap.readPos += tap.ratio;
            tap.age += 1.0;
        }

        if (weight > 1.0e-5f)
            wet /= weight;

        const float dry = readInput (outIndex - (int64_t) getAverageDelaySamples());
        const float target = voiced ? 1.0f : 0.0f;
        const float coeff = target > wetGain ? attack : release;
        wetGain += coeff * (target - wetGain);

        output[i] = dry + (wet - dry) * wetGain;
    }

    outputReadPos = blockStart + numSamples;
}

} // namespace autotune
