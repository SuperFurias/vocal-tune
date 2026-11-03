#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace autotune
{

class PitchStabilizer
{
public:
    struct Settings
    {
        float largeStepSemitones = 4.0f;
        float pendingMatchSemitones = 1.5f;
        double octaveConfirmMs = 80.0;
        double jumpConfirmMs = 35.0;
        double silenceHoldMs = 45.0;
    };

    void reset()
    {
        stableFrequency = 0.0f;
        pendingFrequency = 0.0f;
        pendingSinceMs = 0.0;
        lastStrongMs = 0.0;
        recentCount = 0;
        recentWrite = 0;
        recent.fill (0.0f);
        hasStable = false;
        hasPending = false;
    }

    float process (float frequencyHz, double nowMs)
    {
        if (! (frequencyHz > 0.0f) || ! std::isfinite (frequencyHz))
            return markUnvoiced (nowMs);

        lastStrongMs = nowMs;

        if (! hasStable)
        {
            setStable (frequencyHz);
            return stableFrequency;
        }

        const float step = semitonesBetween (frequencyHz, stableFrequency);

        if (std::abs (step) <= settings.largeStepSemitones)
        {
            hasPending = false;
            pushRecent (frequencyHz);
            stableFrequency = recentMedian();
            return stableFrequency;
        }

        if (! hasPending
            || std::abs (semitonesBetween (frequencyHz, pendingFrequency))
                   > settings.pendingMatchSemitones)
        {
            pendingFrequency = frequencyHz;
            pendingSinceMs = nowMs;
            hasPending = true;
        }
        else
        {
            pendingFrequency = frequencyHz;
        }

        const double confirmMs = isOctaveRelated (frequencyHz, stableFrequency)
                                     ? settings.octaveConfirmMs
                                     : settings.jumpConfirmMs;

        if (hasPending && nowMs - pendingSinceMs >= confirmMs)
        {
            setStable (pendingFrequency);
            hasPending = false;
        }

        return stableFrequency;
    }

    float markUnvoiced (double nowMs)
    {
        hasPending = false;

        if (hasStable && nowMs - lastStrongMs >= settings.silenceHoldMs)
        {
            hasStable = false;
            stableFrequency = 0.0f;
            recentCount = 0;
            recentWrite = 0;
        }

        return stableFrequency;
    }

    float getFrequency() const noexcept { return stableFrequency; }
    bool isStable() const noexcept { return hasStable; }

private:
    static constexpr int recentSize = 5;

    static float semitonesBetween (float a, float b)
    {
        return (float) (12.0 * std::log2 ((double) a / (double) b));
    }

    static bool isOctaveRelated (float a, float b)
    {
        const float step = semitonesBetween (a, b);
        const float nearest = std::round (step / 12.0f) * 12.0f;
        return std::abs (nearest) >= 12.0f && std::abs (step - nearest) <= 1.0f;
    }

    void setStable (float frequencyHz)
    {
        hasStable = true;
        stableFrequency = frequencyHz;
        recentCount = 0;
        recentWrite = 0;
        pushRecent (frequencyHz);
    }

    void pushRecent (float frequencyHz)
    {
        recent[(size_t) recentWrite] = frequencyHz;
        recentWrite = (recentWrite + 1) % recentSize;
        recentCount = std::min (recentSize, recentCount + 1);
    }

    float recentMedian() const
    {
        std::array<float, recentSize> sorted = recent;
        std::sort (sorted.begin(), sorted.begin() + recentCount);
        return sorted[(size_t) (recentCount / 2)];
    }

    Settings settings;
    std::array<float, recentSize> recent {};
    int recentCount = 0;
    int recentWrite = 0;
    float stableFrequency = 0.0f;
    float pendingFrequency = 0.0f;
    double pendingSinceMs = 0.0;
    double lastStrongMs = 0.0;
    bool hasStable = false;
    bool hasPending = false;
};

} // namespace autotune
