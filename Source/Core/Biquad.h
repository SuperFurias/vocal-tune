#pragma once

#include <cmath>

namespace autotune
{

/** Minimal RBJ biquad usable on the audio thread. */
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0f; }

    void setHighPass (double sampleRate, double frequencyHz, double q = 0.7071) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * frequencyHz / sampleRate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        b0 = (float) (((1.0 + cosw) * 0.5) / a0);
        b1 = (float) (-(1.0 + cosw) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * cosw) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    void setLowPass (double sampleRate, double frequencyHz, double q = 0.7071) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * frequencyHz / sampleRate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        b0 = (float) (((1.0 - cosw) * 0.5) / a0);
        b1 = (float) ((1.0 - cosw) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * cosw) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

} // namespace autotune
