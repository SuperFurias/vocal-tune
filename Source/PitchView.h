#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include <functional>
#include <vector>

class PitchView : public juce::Component,
                  public juce::SettableTooltipClient,
                  private juce::Timer
{
public:
    explicit PitchView (AudioEngine& engineToUse);
    ~PitchView() override = default;

    void setShowTarget (bool shouldShow);
    void setOnNoteClicked (std::function<void (int)> callback) { onNoteClicked = std::move (callback); }
    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Colour colourForNote (int midi, bool inScale) const;
    float noteCentreY (int midi) const;
    float yForMidi (float midi) const;
    juce::Rectangle<float> getPianoBounds() const;
    int whiteKeyCount() const;
    int whiteKeyIndexForMidi (int midi) const;
    int whiteMidiForIndex (int index) const;
    juce::Rectangle<float> getBlackKeyBounds (int midi, juce::Rectangle<float> piano) const;
    int midiAtMouse (juce::Point<float> position) const;
    void triggerNoteAt (juce::Point<float> position);

    AudioEngine& engine;
    std::vector<TracePoint> points;
    AudioEngine::Snapshot snapshot;
    bool showTarget = true;
    double historySeconds = 7.0;
    double lastFrameTimeMs = 0.0;

    std::function<void (int)> onNoteClicked;
    int hoverMidi = -1;
    int lastTriggeredMidi = -1;
    int flashMidi = -1;
    double flashUntilMs = 0.0;

    static constexpr int lowMidi = 40;   // E2
    static constexpr int highMidi = 84;  // C6

    friend class PitchViewProbe;
};
