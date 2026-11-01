#include "PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace autotune
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    int nextPowerOfTwo (int v)
    {
        int p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }
}

void PitchDetector::prepare (double newSampleRate, float newMinHz, float newMaxHz, int newHopSize, int requestedWindow)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    minHz = std::max (20.0f, newMinHz);
    maxHz = std::max (minHz + 50.0f, newMaxHz);
    hopSize = std::max (16, newHopSize);

    if (requestedWindow > 0)
        windowSize = nextPowerOfTwo (requestedWindow);
    else
        windowSize = nextPowerOfTwo ((int) std::ceil (sampleRate / (double) minHz * 2.2));

    windowSize = std::clamp (windowSize, 1024, 4096);
    fftSize = nextPowerOfTwo (windowSize * 2);

    prepareFft();

    history.assign ((size_t) windowSize, 0.0f);
    windowScratch.assign ((size_t) windowSize, 0.0f);
    energies.assign ((size_t) windowSize + 1, 0.0);
    diff.assign ((size_t) windowSize + 2, 0.0);
    cmndf.assign ((size_t) windowSize + 2, 0.0);
    fftRe.assign ((size_t) fftSize, 0.0);
    fftIm.assign ((size_t) fftSize, 0.0);
    reset();
}

void PitchDetector::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (windowScratch.begin(), windowScratch.end(), 0.0f);
    samplesSinceHop = 0;
}

void PitchDetector::prepareFft()
{
    bitRev.assign ((size_t) fftSize, 0);
    int bits = 0;
    while ((1 << bits) < fftSize)
        ++bits;

    for (int i = 0; i < fftSize; ++i)
    {
        int reversed = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                reversed |= 1 << (bits - 1 - b);
        bitRev[(size_t) i] = reversed;
    }
}

void PitchDetector::fft (std::vector<double>& re, std::vector<double>& im, bool inverse) const
{
    const int n = (int) re.size();

    for (int i = 0; i < n; ++i)
    {
        const int j = bitRev[(size_t) i];
        if (j > i)
        {
            std::swap (re[(size_t) i], re[(size_t) j]);
            std::swap (im[(size_t) i], im[(size_t) j]);
        }
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        const double angle = (inverse ? 2.0 : -2.0) * kPi / (double) len;
        const double wlenR = std::cos (angle);
        const double wlenI = std::sin (angle);
        const int half = len / 2;

        for (int start = 0; start < n; start += len)
        {
            double wr = 1.0, wi = 0.0;
            for (int j = 0; j < half; ++j)
            {
                const int a = start + j;
                const int b = a + half;

                const double ur = re[(size_t) a];
                const double ui = im[(size_t) a];
                const double vr = re[(size_t) b] * wr - im[(size_t) b] * wi;
                const double vi = re[(size_t) b] * wi + im[(size_t) b] * wr;

                re[(size_t) a] = ur + vr;
                im[(size_t) a] = ui + vi;
                re[(size_t) b] = ur - vr;
                im[(size_t) b] = ui - vi;

                const double nextWr = wr * wlenR - wi * wlenI;
                wi = wr * wlenI + wi * wlenR;
                wr = nextWr;
            }
        }
    }

    if (inverse)
    {
        const double scale = 1.0 / (double) n;
        for (int i = 0; i < n; ++i)
        {
            re[(size_t) i] *= scale;
            im[(size_t) i] *= scale;
        }
    }
}

bool PitchDetector::process (const float* samples, int numSamples, PitchFrame& result)
{
    if (samples == nullptr || numSamples <= 0)
        return false;

    samplesSinceHop += numSamples;

    if (numSamples >= windowSize)
    {
        std::memcpy (history.data(), samples + (numSamples - windowSize), sizeof (float) * (size_t) windowSize);
    }
    else
    {
        std::memmove (history.data(), history.data() + numSamples,
                      sizeof (float) * (size_t) (windowSize - numSamples));
        std::memcpy (history.data() + (windowSize - numSamples), samples,
                     sizeof (float) * (size_t) numSamples);
    }

    if (samplesSinceHop < hopSize)
        return false;

    samplesSinceHop %= hopSize;

    result = analyseInternal (history.data(), windowSize);
    result.valid = true;
    return true;
}

PitchFrame PitchDetector::analyseWindow (const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return {};

    if (numSamples > windowSize)
        samples += numSamples - windowSize;

    auto frame = analyseInternal (samples, std::min (numSamples, windowSize));
    frame.valid = true;
    return frame;
}

PitchFrame PitchDetector::analyseInternal (const float* samples, int numSamples)
{
    PitchFrame out;

    const int n = std::min (numSamples, windowSize);
    if (n < 64 || fftSize < n * 2)
        return out;

    double mean = 0.0;
    for (int i = 0; i < n; ++i)
        mean += (double) samples[i];
    mean /= (double) n;

    double sumSq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double v = (double) samples[i] - mean;
        windowScratch[(size_t) i] = (float) v;
        sumSq += v * v;
    }

    out.rms = (float) std::sqrt (sumSq / (double) n);

    if (sumSq < 1.0e-12)
        return out;

    energies[0] = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double v = (double) windowScratch[(size_t) i];
        energies[(size_t) i + 1] = energies[(size_t) i] + v * v;
    }

    std::fill (fftRe.begin(), fftRe.end(), 0.0);
    std::fill (fftIm.begin(), fftIm.end(), 0.0);
    for (int i = 0; i < n; ++i)
        fftRe[(size_t) i] = (double) windowScratch[(size_t) i];

    fft (fftRe, fftIm, false);

    for (int i = 0; i < fftSize; ++i)
    {
        const double re = fftRe[(size_t) i];
        const double im = fftIm[(size_t) i];
        fftRe[(size_t) i] = re * re + im * im;
        fftIm[(size_t) i] = 0.0;
    }

    fft (fftRe, fftIm, true);

    const int tauMin = std::max (2, (int) std::floor (sampleRate / (double) maxHz));
    const int tauMax = std::min (n / 2, (int) std::ceil (sampleRate / (double) minHz));

    if (tauMax <= tauMin + 2)
        return out;

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const double e0 = energies[(size_t) (n - tau)];
        const double e1 = energies[(size_t) n] - energies[(size_t) tau];
        const double r = fftRe[(size_t) tau];
        diff[(size_t) tau] = std::max (0.0, e0 + e1 - 2.0 * r);
    }

    cmndf[0] = 1.0;
    double runningSum = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        runningSum += diff[(size_t) tau];
        cmndf[(size_t) tau] = runningSum > 1.0e-12
                                ? diff[(size_t) tau] * (double) tau / runningSum
                                : 0.0;
    }

    int bestTau = -1;
    double bestValue = 1.0e9;

    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        const double value = cmndf[(size_t) tau];
        if (value < bestValue)
        {
            bestValue = value;
            bestTau = tau;
        }
    }

    if (bestTau < 0)
        return out;

    // Prefer the earliest local minimum that is below the YIN threshold. This is
    // what keeps the detector tracking the fundamental rather than a subharmonic.
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        const double value = cmndf[(size_t) tau];
        if (value < (double) yinThreshold
            && value <= cmndf[(size_t) (tau - 1)]
            && value <= cmndf[(size_t) (tau + 1)])
        {
            bestTau = tau;
            bestValue = value;
            break;
        }
    }

    // Refine around the chosen dip.
    if (bestTau >= 2 && bestTau < tauMax)
    {
        int refined = bestTau;
        for (int t = std::max (tauMin, bestTau - 2); t <= std::min (tauMax, bestTau + 2); ++t)
        {
            if (cmndf[(size_t) t] < cmndf[(size_t) refined])
                refined = t;
        }
        bestTau = refined;
        bestValue = cmndf[(size_t) refined];
    }

    double tauAdjusted = (double) bestTau;
    if (bestTau > tauMin && bestTau < tauMax)
    {
        const double y0 = cmndf[(size_t) (bestTau - 1)];
        const double y1 = cmndf[(size_t) bestTau];
        const double y2 = cmndf[(size_t) (bestTau + 1)];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs (denom) > 1.0e-12)
        {
            double offset = 0.5 * (y0 - y2) / denom;
            offset = std::clamp (offset, -0.5, 0.5);
            tauAdjusted = (double) bestTau + offset;
            const double interpolated = y1 - 0.25 * (y0 - y2) * offset;
            bestValue = std::clamp (interpolated, 0.0, 1.0);
        }
    }

    const double freq = sampleRate / std::max (1.0, tauAdjusted);
    if (! std::isfinite (freq) || freq < (double) minHz || freq > (double) maxHz)
        return out;

    out.frequency = (float) freq;
    out.midi = 69.0f + 12.0f * std::log2 ((float) freq / 440.0f);
    out.confidence = (float) std::clamp (1.0 - bestValue, 0.0, 1.0);
    out.voiced = bestValue < (double) maxAcceptedCmndf && out.rms > minRms;

    if (! out.voiced)
        out.frequency = 0.0f;

    return out;
}

} // namespace autotune
