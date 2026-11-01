#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace autotune
{

struct ScaleDefinition
{
    const char* name;
    uint16_t mask; // bit i set => semitone i above the key root is allowed
};

const std::vector<ScaleDefinition>& getScales();
const char* getNoteName (int pitchClass); // "C", "C#", ...
std::string midiToName (float midi, bool includeOctave = true);
int nearestAllowedMidi (float midi, int keyRoot, uint16_t scaleMask);
float midiToHz (float midi, float a4Hz = 440.0f);
float hzToMidi (float hz, float a4Hz = 440.0f);
float centsBetween (float referenceHz, float measuredHz);

} // namespace autotune
