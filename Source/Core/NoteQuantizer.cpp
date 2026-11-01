#include "NoteQuantizer.h"

#include <algorithm>
#include <cmath>

namespace autotune
{

const std::vector<ScaleDefinition>& getScales()
{
    static const std::vector<ScaleDefinition> scales
    {
        { "Chromatic",        0x0FFF },
        { "Major",            0x0AB5 },
        { "Natural Minor",    0x05AD },
        { "Harmonic Minor",   0x09AD },
        { "Melodic Minor",    0x0AAD },
        { "Dorian",           0x06AD },
        { "Phrygian",         0x05AB },
        { "Lydian",           0x0AD5 },
        { "Mixolydian",       0x06B5 },
        { "Locrian",          0x056B },
        { "Major Pentatonic", 0x0295 },
        { "Minor Pentatonic", 0x04A9 },
        { "Blues",            0x04E9 },
        { "Whole Tone",       0x0555 },
        { "Hirajoshi",        0x018D },
        { "Egyptian",         0x04A5 },
    };
    return scales;
}

const char* getNoteName (int pitchClass)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int pc = ((pitchClass % 12) + 12) % 12;
    return names[pc];
}

std::string midiToName (float midi, bool includeOctave)
{
    if (! std::isfinite (midi))
        return "--";

    const int rounded = (int) std::lround (midi);
    const int pc = ((rounded % 12) + 12) % 12;
    const int octave = rounded / 12 - 1;

    std::string name = getNoteName (pc);
    if (includeOctave)
        name += std::to_string (octave);
    return name;
}

int nearestAllowedMidi (float midi, int keyRoot, uint16_t scaleMask)
{
    if (scaleMask == 0)
        return (int) std::lround (midi);

    const int root = ((keyRoot % 12) + 12) % 12;
    int bestNote = (int) std::lround (midi);
    float bestDistance = 1.0e9f;

    for (int step = -2; step <= 2; ++step)
    {
        const int octaveBase = (int) std::floor (midi / 12.0f) * 12 + step * 12;

        for (int pc = 0; pc < 12; ++pc)
        {
            if ((scaleMask & (1u << pc)) == 0)
                continue;

            const int note = octaveBase + ((root + pc) % 12);
            const float distance = std::abs ((float) note - midi);
            if (distance < bestDistance - 1.0e-6f
                || (std::abs (distance - bestDistance) <= 1.0e-6f && note > bestNote))
            {
                bestDistance = distance;
                bestNote = note;
            }
        }
    }

    return bestNote;
}

float midiToHz (float midi, float a4Hz)
{
    return a4Hz * std::pow (2.0f, (midi - 69.0f) / 12.0f);
}

float hzToMidi (float hz, float a4Hz)
{
    if (hz <= 0.0f)
        return 0.0f;
    return 69.0f + 12.0f * std::log2 (hz / a4Hz);
}

float centsBetween (float referenceHz, float measuredHz)
{
    if (referenceHz <= 0.0f || measuredHz <= 0.0f)
        return 0.0f;
    return 1200.0f * std::log2 (measuredHz / referenceHz);
}

} // namespace autotune
