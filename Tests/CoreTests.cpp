#include "Core/NoteNaming.h"
#include "Core/NoteQuantizer.h"
#include "Core/PitchDetector.h"
#include "Core/PitchStabilizer.h"
#include "Core/GranularShifter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

using namespace autotune;

namespace
{
    int failures = 0;

    void check (bool condition, const char* message)
    {
        std::printf ("%s  %s\n", condition ? "[ ok ]" : "[FAIL]", message);
        if (! condition)
            ++failures;
    }

    std::vector<float> makeSine (double sampleRate, double frequencyHz, int numSamples, float amplitude = 0.4f)
    {
        std::vector<float> samples ((size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
            samples[(size_t) i] = amplitude * (float) std::sin (2.0 * 3.14159265358979323846 * frequencyHz * (double) i / sampleRate);
        return samples;
    }

    std::vector<float> makeSaw (double sampleRate, double frequencyHz, int numSamples, float amplitude = 0.4f)
    {
        std::vector<float> samples ((size_t) numSamples);
        const double increment = frequencyHz / sampleRate;
        double phase = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
            samples[(size_t) i] = amplitude * (float) (2.0 * phase - 1.0);
        }
        return samples;
    }

    void testDetector()
    {
        PitchDetector detector;
        detector.prepare (48000.0, 60.0f, 1200.0f, 128);

        const double sr = 48000.0;
        const int window = detector.getWindowSize();
        const int length = window * 4;

        for (double frequency : { 82.0, 110.0, 220.0, 440.0, 880.0 })
        {
            auto sine = makeSine (sr, frequency, length);
            const auto frame = detector.analyseWindow (sine.data() + (length - window), window);
            const double error = std::abs ((double) frame.frequency - frequency);

            char msg[160];
            std::snprintf (msg, sizeof (msg), "detect %.1f Hz -> %.3f Hz (err %.3f, conf %.2f, voiced %d)",
                           frequency, (double) frame.frequency, error, (double) frame.confidence, frame.voiced ? 1 : 0);
            check (frame.voiced && error < 1.0, msg);
        }

        auto saw = makeSaw (sr, 220.0, length);
        const auto sawFrame = detector.analyseWindow (saw.data() + (length - window), window);
        check (sawFrame.voiced && std::abs ((double) sawFrame.frequency - 220.0) < 2.0, "detect sawtooth 220 Hz");

        std::vector<float> silence ((size_t) window, 0.0f);
        const auto silentFrame = detector.analyseWindow (silence.data(), window);
        check (! silentFrame.voiced, "silence is unvoiced");
    }

    void testNoteNaming()
    {
        using Style = NoteLabelStyle;

        check (formatNoteLabel (60, Style::namesWithCOctaves, false) == "C4",
               "C octave markers name C4");
        check (formatNoteLabel (62, Style::namesWithCOctaves, false) == "D",
               "C octave markers leave D plain");
        check (formatNoteLabel (61, Style::namesWithCOctaves, true) == std::string ("D\xE2\x99\xAD"),
               "flat names use the flat symbol");
        check (formatNoteLabel (61, Style::scientific, false)
                   == std::string ("C\xE2\x99\xAF") + "4",
               "scientific names include every octave");
        check (formatNoteLabel (60, Style::helmholtz, false) == std::string ("c\xE2\x80\xB2"),
               "Helmholtz names use lowercase prime notation");
        check (formatNoteLabel (72, Style::helmholtz, false)
                   == std::string ("c\xE2\x80\xB2\xE2\x80\xB2"),
               "Helmholtz names add primes for higher octaves");
        check (formatNoteLabel (60, Style::solfege, false) == "Do4",
               "solfège names use fixed-do");
        check (formatNoteLabel (61, Style::solfege, true) == std::string ("Re\xE2\x99\xAD"),
               "solfège names follow the accidental choice");
        check (formatNoteLabel (40, Style::midi, false) == "40",
               "MIDI labels show note numbers");
        check (formatNoteLabel (40, Style::singer, false) == "low E",
               "singer labels use low E");
        check (formatNoteLabel (72, Style::singer, false) == "tenor high C",
               "singer labels use tenor high C");
        check (formatNoteLabel (84, Style::singer, false) == "soprano high C",
               "singer labels use soprano high C");
    }

    void testPitchStabilizer()
    {
        PitchStabilizer stabilizer;
        double nowMs = 0.0;
        float output = 0.0f;

        for (int i = 0; i < 12; ++i)
        {
            output = stabilizer.process (220.0f, nowMs);
            nowMs += 5.0;
        }

        check (std::abs (output - 220.0f) < 0.1f, "stabilizer locks onto 220 Hz");

        for (int i = 0; i < 4; ++i)
        {
            output = stabilizer.process (440.0f, nowMs);
            nowMs += 5.0;
        }

        check (std::abs (output - 220.0f) < 0.1f, "stabilizer rejects a short octave spike");

        for (int i = 0; i < 4; ++i)
        {
            output = stabilizer.process (220.0f, nowMs);
            nowMs += 5.0;
        }

        check (std::abs (output - 220.0f) < 0.1f, "stabilizer stays on 220 Hz after the spike");

        for (int i = 0; i < 20; ++i)
        {
            output = stabilizer.process (440.0f, nowMs);
            nowMs += 5.0;
        }

        check (std::abs (output - 440.0f) < 0.1f, "stabilizer accepts a sustained octave change");

        nowMs += 60.0;
        output = stabilizer.markUnvoiced (nowMs);
        check (output == 0.0f, "stabilizer releases pitch after silence");

        output = stabilizer.process (110.0f, nowMs);
        check (std::abs (output - 110.0f) < 0.1f, "stabilizer starts a new note immediately");
    }

    void testQuantizer()
    {
        const auto& scales = getScales();
        uint16_t major = 0;
        for (const auto& scale : scales)
            if (std::string (scale.name) == "Major")
                major = scale.mask;

        check (nearestAllowedMidi (61.2f, 0, major) == 62, "C major snaps 61.2 to D4 (62)");
        check (nearestAllowedMidi (60.1f, 0, major) == 60, "C major snaps 60.1 to C4");
        check (nearestAllowedMidi (61.4f, 0, 0x0FFF) == 61, "chromatic snaps to nearest semitone");
        check (std::string (midiToName (60.0f)) == "C4", "midi 60 names C4");
        check (std::abs (midiToHz (69.0f) - 440.0f) < 0.001f, "A4 = 440 Hz");
    }

    void testShifter()
    {
        const double sr = 48000.0;
        const int block = 128;
        const int lookahead = 1024;
        const int seconds = 3;
        const int total = (int) (sr * seconds);

        const double inputHz = 220.0;
        const double targetMidi = 61.0f; // C#4 = 277.18 Hz
        const double targetHz = 440.0 * std::pow (2.0, (targetMidi - 69.0) / 12.0);
        const double ratio = targetHz / inputHz;

        auto input = makeSine (sr, inputHz, total + block, 0.35f);

        GranularShifter shifter;
        shifter.prepare (sr, lookahead);
        shifter.setMaxDelay (lookahead);

        std::vector<float> output ((size_t) total, 0.0f);
        ShifterParams params;
        params.periodSamples = (float) (sr / inputHz);
        params.ratio = (float) ratio;
        params.voiced = true;

        for (int offset = 0; offset + block <= total; offset += block)
        {
            shifter.pushInput (input.data() + offset, block);
            shifter.render (output.data() + offset, block, params);
        }

        bool finite = true;
        for (float v : output)
            if (! std::isfinite (v))
            {
                finite = false;
                break;
            }
        check (finite, "granular output is finite");

        const int settle = (int) (sr * 0.5);
        double rms = 0.0;
        for (int i = settle; i < total; ++i)
            rms += (double) output[(size_t) i] * output[(size_t) i];
        rms = std::sqrt (rms / (double) (total - settle));
        std::printf ("       shifted output RMS = %.4f\n", rms);
        for (int s = 0; s < seconds; ++s)
        {
            double seg = 0.0;
            for (int i = s * 48000; i < (s + 1) * 48000; ++i)
                seg += (double) output[(size_t) i] * output[(size_t) i];
            std::printf ("       shifted second %d RMS = %.5f\n", s, std::sqrt (seg / 48000.0));
        }
        check (rms > 0.05, "granular output has signal");

        PitchDetector detector;
        detector.prepare (sr, 60.0f, 1200.0f, 128);
        const int window = detector.getWindowSize();
        const auto frame = detector.analyseWindow (output.data() + (total - window - 512), window);
        const double errorHz = std::abs ((double) frame.frequency - targetHz);

        char msg[200];
        std::snprintf (msg, sizeof (msg), "granular 220 -> %.2f Hz: measured %.2f Hz (err %.2f Hz, ratio %.4f)",
                       targetHz, (double) frame.frequency, errorHz, ratio);
        check (frame.voiced && errorHz < 1.5, msg);

        // Unity ratio should reconstruct the delayed input nearly perfectly.
        std::vector<float> unity ((size_t) total, 0.0f);
        GranularShifter unityShifter;
        unityShifter.prepare (sr, lookahead);
        unityShifter.setMaxDelay (lookahead);
        ShifterParams unityParams;
        unityParams.periodSamples = (float) (sr / inputHz);
        unityParams.ratio = 1.0f;
        unityParams.voiced = true;

        for (int offset = 0; offset + block <= total; offset += block)
        {
            unityShifter.pushInput (input.data() + offset, block);
            unityShifter.render (unity.data() + offset, block, unityParams);
        }

        double error = 0.0, signal = 0.0;
        const int dryDelay = unityShifter.getAverageDelaySamples();
        for (int i = settle; i < total - lookahead; ++i)
        {
            const double expected = (double) input[(size_t) (i - dryDelay)];
            const double actual = (double) unity[(size_t) i];
            error += (actual - expected) * (actual - expected);
            signal += expected * expected;
        }
        const double relError = std::sqrt (error / std::max (1.0e-12, signal));
        double unityRms = 0.0;
        for (int i = settle; i < total - lookahead; ++i)
            unityRms += (double) unity[(size_t) i] * unity[(size_t) i];
        unityRms = std::sqrt (unityRms / (double) (total - lookahead - settle));
        std::printf ("       unity output RMS = %.4f, input RMS = %.4f, rel err = %.3f%%\n",
                     unityRms, std::sqrt (signal / (double) (total - lookahead - settle)), relError * 100.0);
        char unityMsg[160];
        std::snprintf (unityMsg, sizeof (unityMsg), "granular unity-ratio reconstruction error %.3f%%", relError * 100.0);
        check (relError < 0.005, unityMsg);
    }

    void testSustainedShifter()
    {
        const double sr = 48000.0;
        const int block = 128;
        const int lookahead = 1022;
        const int seconds = 10;
        const int total = (int) (sr * seconds);
        const double inputHz = 110.0;
        auto input = makeSine (sr, inputHz, total + block, 0.35f);

        for (float ratio : { 1.0f, 1.03f, 1.059f })
        {
            GranularShifter shifter;
            shifter.prepare (sr, lookahead);

            ShifterParams params;
            params.periodSamples = (float) (sr / inputHz);
            params.ratio = ratio;
            params.voiced = true;

            std::vector<float> output ((size_t) total, 0.0f);
            for (int offset = 0; offset + block <= total; offset += block)
            {
                shifter.pushInput (input.data() + offset, block);
                shifter.render (output.data() + offset, block, params);
            }

            double firstSecond = 0.0, lastSecond = 0.0;
            for (int i = 48000; i < 96000; ++i)
                firstSecond += (double) output[(size_t) i] * output[(size_t) i];
            for (int i = (seconds - 1) * 48000; i < total; ++i)
                lastSecond += (double) output[(size_t) i] * output[(size_t) i];

            firstSecond = std::sqrt (firstSecond / 48000.0);
            lastSecond = std::sqrt (lastSecond / 48000.0);

            char msg[180];
            std::snprintf (msg, sizeof (msg),
                           "sustained 110 Hz ratio %.3f: first %.4f, last %.4f RMS",
                           ratio, firstSecond, lastSecond);
            check (lastSecond > firstSecond * 0.7, msg);
        }
    }

    void testStreamingDetector()
    {
        const double sr = 48000.0;
        auto input = makeSine (sr, 330.0, 48000, 0.3f);
        PitchDetector detector;
        detector.prepare (sr, 60.0f, 1200.0f, 128);

        PitchFrame frame;
        bool gotFrame = false;
        for (int i = 0; i + 128 <= (int) input.size(); i += 128)
            if (detector.process (input.data() + i, 128, frame))
                gotFrame = true;

        check (gotFrame && frame.valid, "streaming detector produces frames");
        check (frame.voiced && std::abs ((double) frame.frequency - 330.0) < 1.0, "streaming detector tracks 330 Hz");
    }
}

int main()
{
    std::printf ("Auto-Tune DSP self-tests\n========================\n");
    testDetector();
    testNoteNaming();
    testPitchStabilizer();
    testQuantizer();
    testShifter();
    testSustainedShifter();
    testStreamingDetector();
    std::printf ("========================\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
