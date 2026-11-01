#include "PitchView.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>

class PitchViewProbe
{
public:
    static juce::Rectangle<float> piano (const PitchView& view) { return view.getPianoBounds(); }
    static int whiteCount (const PitchView& view) { return view.whiteKeyCount(); }
    static int whiteMidi (const PitchView& view, int index) { return view.whiteMidiForIndex (index); }
    static juce::Rectangle<float> blackKey (const PitchView& view, int midi,
                                            juce::Rectangle<float> bounds)
    {
        return view.getBlackKeyBounds (midi, bounds);
    }
    static int midiAt (const PitchView& view, juce::Point<float> point) { return view.midiAtMouse (point); }
    static float noteCentre (const PitchView& view, int midi) { return view.noteCentreY (midi); }
    static float midiY (const PitchView& view, float midi) { return view.yForMidi (midi); }
    static int lowMidi() { return PitchView::lowMidi; }
    static int highMidi() { return PitchView::highMidi; }
};

namespace
{
    int failures = 0;

    void expect (bool condition, const juce::String& message)
    {
        if (! condition)
        {
            std::cerr << "FAIL: " << message << "\n";
            ++failures;
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    Theme::uiScale = 1.3f;

    AudioEngine engine;
    PitchView view (engine);
    view.setShowTarget (true);
    view.setSize (2000, 1280);

    const auto piano = PitchViewProbe::piano (view);
    const int whiteCount = PitchViewProbe::whiteCount (view);
    const float whiteHeight = piano.getHeight() / (float) whiteCount;

    expect (piano.getWidth() > 0.0f && piano.getHeight() > 0.0f, "keyboard bounds are empty");
    expect (whiteCount == 43, "expected 43 white keys");

    for (int index = 0; index < whiteCount; ++index)
    {
        const int midi = PitchViewProbe::whiteMidi (view, index);
        const float rowCentreY = piano.getBottom() - ((float) index + 0.5f) * whiteHeight;
        const float x = piano.getX() + piano.getWidth() * 0.85f;

        expect (PitchViewProbe::midiAt (view, { x, rowCentreY }) == midi,
                "white key click did not resolve to " + juce::String (midi));
        expect (std::abs (PitchViewProbe::noteCentre (view, midi) - rowCentreY) < 0.01f,
                "white key pitch centre is misaligned for " + juce::String (midi));
        expect (std::abs (PitchViewProbe::midiY (view, (float) midi) - rowCentreY) < 0.01f,
                "pitch mapping is misaligned for white key " + juce::String (midi));
    }

    int blackCount = 0;

    for (int midi = PitchViewProbe::lowMidi(); midi <= PitchViewProbe::highMidi(); ++midi)
    {
        const int pc = ((midi % 12) + 12) % 12;
        const bool isBlack = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;

        if (! isBlack)
            continue;

        ++blackCount;
        const auto key = PitchViewProbe::blackKey (view, midi, piano);
        const auto centre = key.getCentre();

        expect (key.getWidth() < piano.getWidth(), "black key is not shorter than white keys");
        expect (std::abs (PitchViewProbe::noteCentre (view, midi) - centre.y) < 0.01f,
                "black key pitch centre is misaligned for " + juce::String (midi));
        expect (std::abs (PitchViewProbe::midiY (view, (float) midi) - centre.y) < 0.01f,
                "pitch mapping is misaligned for black key " + juce::String (midi));
        expect (PitchViewProbe::midiAt (view, centre) == midi,
                "black key click was intercepted by a white key at " + juce::String (midi));
    }

    expect (blackCount == 30, "expected 30 black keys");

    const float middleY = piano.getCentreY();
    expect (PitchViewProbe::midiAt (view, { -1.0f, middleY }) == -1,
            "click outside the keyboard was accepted");
    expect (PitchViewProbe::midiAt (view, { piano.getCentreX(), piano.getBottom() + 1.0f }) == -1,
            "click below the keyboard was accepted");

    auto artifacts = juce::File::getCurrentWorkingDirectory().getChildFile ("Artifacts");
    artifacts.createDirectory();

    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
    const auto imageFile = artifacts.getChildFile ("pitchview-offscreen.png");

    if (auto stream = imageFile.createOutputStream())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (image, *stream);
    }
    else
    {
        expect (false, "could not create offscreen snapshot");
    }

    std::cout << "PitchView tests: white=" << whiteCount
              << " black=" << blackCount
              << " snapshot=" << imageFile.getFullPathName() << "\n";

    if (failures != 0)
    {
        std::cerr << "PitchView tests failed: " << failures << "\n";
        return 1;
    }

    std::cout << "PitchView tests passed\n";
    return 0;
}
