#pragma once

#include <cctype>
#include <string>

namespace autotune
{

enum class NoteLabelStyle
{
    namesWithCOctaves,
    scientific,
    helmholtz,
    solfege,
    midi,
    singer
};

namespace note_naming_detail
{
    inline int pitchClassForMidi (int midi)
    {
        return ((midi % 12) + 12) % 12;
    }

    inline std::string letterName (int midi, bool useFlats)
    {
        static const char* sharpNames[] = {
            "C", "C\xE2\x99\xAF", "D", "D\xE2\x99\xAF", "E", "F",
            "F\xE2\x99\xAF", "G", "G\xE2\x99\xAF", "A", "A\xE2\x99\xAF", "B"
        };
        static const char* flatNames[] = {
            "C", "D\xE2\x99\xAD", "D", "E\xE2\x99\xAD", "E", "F",
            "G\xE2\x99\xAD", "G", "A\xE2\x99\xAD", "A", "B\xE2\x99\xAD", "B"
        };

        return useFlats ? flatNames[pitchClassForMidi (midi)]
                        : sharpNames[pitchClassForMidi (midi)];
    }

    inline std::string solfegeName (int midi, bool useFlats)
    {
        static const char* sharpNames[] = {
            "Do", "Do\xE2\x99\xAF", "Re", "Re\xE2\x99\xAF", "Mi", "Fa",
            "Fa\xE2\x99\xAF", "Sol", "Sol\xE2\x99\xAF", "La", "La\xE2\x99\xAF", "Si"
        };
        static const char* flatNames[] = {
            "Do", "Re\xE2\x99\xAD", "Re", "Mi\xE2\x99\xAD", "Mi", "Fa",
            "Sol\xE2\x99\xAD", "Sol", "La\xE2\x99\xAD", "La", "Si\xE2\x99\xAD", "Si"
        };

        return useFlats ? flatNames[pitchClassForMidi (midi)]
                        : sharpNames[pitchClassForMidi (midi)];
    }

    inline int octaveForMidi (int midi)
    {
        return midi / 12 - 1;
    }
}

inline std::string formatNoteLabel (int midi, NoteLabelStyle style, bool useFlats = false)
{
    const int pc = note_naming_detail::pitchClassForMidi (midi);
    const int octave = note_naming_detail::octaveForMidi (midi);

    switch (style)
    {
        case NoteLabelStyle::midi:
            return std::to_string (midi);

        case NoteLabelStyle::scientific:
            return note_naming_detail::letterName (midi, useFlats) + std::to_string (octave);

        case NoteLabelStyle::helmholtz:
        {
            auto name = note_naming_detail::letterName (midi, useFlats);
            const bool upperCase = octave < 3;

            if (upperCase)
                name[0] = (char) std::toupper ((unsigned char) name[0]);
            else
                name[0] = (char) std::tolower ((unsigned char) name[0]);

            if (octave >= 4)
            {
                const int primeCount = octave - 3;
                for (int i = 0; i < primeCount; ++i)
                    name += "\xE2\x80\xB2";
            }

            return name;
        }

        case NoteLabelStyle::solfege:
        {
            auto name = note_naming_detail::solfegeName (midi, useFlats);
            if (pc == 0)
                name += std::to_string (octave);
            return name;
        }

        case NoteLabelStyle::singer:
            if (midi == 40)
                return "low E";
            if (midi == 60)
                return "middle C";
            if (midi == 72)
                return "tenor high C";
            if (midi == 84)
                return "soprano high C";
            break;

        case NoteLabelStyle::namesWithCOctaves:
        default:
            break;
    }

    auto name = note_naming_detail::letterName (midi, useFlats);
    if (pc == 0)
        name += std::to_string (octave);
    return name;
}

} // namespace autotune
