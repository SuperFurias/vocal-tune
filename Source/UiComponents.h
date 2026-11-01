#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class Dial : public juce::Component
{
public:
    Dial (const juce::String& labelText,
          juce::Range<double> valueRange,
          double initialValue,
          double resetValue,
          std::function<juce::String (double)> valueFormatter,
          bool large = false);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider& getSlider() noexcept { return slider; }
    double getValue() const;
    void setValue (double value, juce::NotificationType notification = juce::dontSendNotification);
    void setOnChange (std::function<void (double)> callback) { onChange = std::move (callback); }

private:
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::String label;
    std::function<juce::String (double)> formatter;
    std::function<void (double)> onChange;
    bool isLarge = false;
};

class PillToggle : public juce::Button
{
public:
    explicit PillToggle (const juce::String& labelText);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;
    void clicked() override;

    std::function<void (bool)> onToggle;
    void setOn (bool shouldBeOn, juce::NotificationType notification = juce::dontSendNotification);
    bool isOn() const noexcept { return getToggleState(); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PillToggle)
};

class RecordButton : public juce::Button
{
public:
    RecordButton();

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;
    void clicked() override;

    void setRecording (bool isRecording, double seconds);
    std::function<void()> onClick;

private:
    bool recording = false;
    double elapsed = 0.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordButton)
};

class Meter : public juce::Component
{
public:
    explicit Meter (const juce::String& labelText);

    void setLevel (float linearLevel);
    void paint (juce::Graphics&) override;

private:
    juce::String label;
    float level = 0.0f;
    float peak = 0.0f;
};
