#include "UiComponents.h"
#include "Theme.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

Dial::Dial (const juce::String& labelText,
            juce::Range<double> valueRange,
            double initialValue,
            double resetValue,
            std::function<juce::String (double)> valueFormatter,
            bool large)
    : label (labelText),
      formatter (std::move (valueFormatter)),
      isLarge (large)
{
    addAndMakeVisible (slider);
    slider.setRange (valueRange.getStart(), valueRange.getEnd(), 0.0);
    slider.setValue (initialValue, juce::dontSendNotification);
    slider.setDoubleClickReturnValue (true, resetValue);
    slider.setColour (juce::Slider::rotarySliderFillColourId, Theme::accent);
    slider.setColour (juce::Slider::thumbColourId, Theme::accent);
    slider.setMouseDragSensitivity (150);
    slider.setWantsKeyboardFocus (false);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 1.25f
                                    + juce::MathConstants<float>::pi * 1.5f,
                                true);
    slider.onValueChange = [this]
    {
        repaint();
        if (onChange)
            onChange (slider.getValue());
    };
}

void Dial::resized()
{
    const int labelHeight = 18;
    const int available = juce::jmin (getWidth() - 10, getHeight() - labelHeight - 4);
    const int size = juce::jmax (40, available);
    slider.setBounds ((getWidth() - size) / 2, labelHeight + 2, size, size);
}

void Dial::paint (juce::Graphics& g)
{
    const auto sliderBounds = slider.getBounds().toFloat();
    const auto centre = sliderBounds.getCentre();
    const float radius = juce::jmin (sliderBounds.getWidth(), sliderBounds.getHeight()) * 0.5f;

    g.setFont (Theme::font (13.0f, true));
    g.setColour (Theme::textDim);
    g.drawText (label, 0, 0, getWidth(), 18, juce::Justification::centred, false);

    // Soft recessed disc behind the ring.
    const float discRadius = radius - (isLarge ? 20.0f : 15.0f);
    if (discRadius > 10.0f)
    {
        juce::ColourGradient disc (Theme::panelRaised.brighter (0.06f), centre.x, centre.y - discRadius,
                                   Theme::field, centre.x, centre.y + discRadius, false);
        g.setGradientFill (disc);
        g.fillEllipse (centre.x - discRadius, centre.y - discRadius, discRadius * 2.0f, discRadius * 2.0f);

        g.setColour (Theme::strokeSoft);
        g.drawEllipse (centre.x - discRadius, centre.y - discRadius, discRadius * 2.0f, discRadius * 2.0f, 1.0f);
    }

    const auto text = formatter ? formatter (slider.getValue()) : juce::String (slider.getValue(), 0);
    g.setColour (Theme::text);
    g.setFont (Theme::display (isLarge ? 34.0f : 20.0f));
    g.drawText (text, sliderBounds.toNearestInt().reduced (6), juce::Justification::centred, false);
}

double Dial::getValue() const
{
    return slider.getValue();
}

void Dial::setValue (double value, juce::NotificationType notification)
{
    slider.setValue (value, notification);
    repaint();
}

PillToggle::PillToggle (const juce::String& labelText)
    : juce::Button (labelText)
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PillToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const bool on = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = bounds.getHeight() * 0.5f;

    auto fill = on ? Theme::accent : Theme::panelRaised;
    if (highlighted)
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (on ? Theme::accent : Theme::stroke);
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    const auto dotArea = juce::Rectangle<float> (bounds.getX() + 11.0f,
                                                 bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour (on ? Theme::background.withAlpha (0.8f) : Theme::textFaint);
    g.fillEllipse (dotArea);

    g.setColour (on ? Theme::background : Theme::text);
    g.setFont (Theme::font (13.0f, true));
    g.drawText (getButtonText(), bounds.withTrimmedLeft (24.0f).withTrimmedRight (8.0f),
                juce::Justification::centredLeft, false);
}

void PillToggle::clicked()
{
    if (onToggle)
        onToggle (getToggleState());
}

void PillToggle::setOn (bool shouldBeOn, juce::NotificationType)
{
    setToggleState (shouldBeOn, juce::dontSendNotification);
    repaint();
}

RecordButton::RecordButton()
    : juce::Button ("Record")
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RecordButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = juce::jmin (10.0f, bounds.getHeight() * 0.5f);

    auto fill = recording ? Theme::recordRed.withAlpha (0.15f) : Theme::panelRaised;
    if (highlighted)
        fill = fill.brighter (0.08f);
    if (down)
        fill = fill.darker (0.15f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (recording ? Theme::recordRed.withAlpha (0.6f) : Theme::stroke);
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    const auto dotCentre = juce::Point<float> (bounds.getX() + 16.0f, bounds.getCentreY());
    g.setColour (Theme::recordRed);
    g.fillEllipse (dotCentre.x - 4.0f, dotCentre.y - 4.0f, 8.0f, 8.0f);

    juce::String labelText = "REC";
    if (recording)
    {
        const int total = (int) std::floor (elapsed);
        labelText << "  " << total / 60 << ":" << juce::String (total % 60).paddedLeft ('0', 2);
    }

    g.setColour (recording ? Theme::text : Theme::textDim);
    g.setFont (Theme::mono (11.5f, true));
    g.drawText (labelText, bounds.withTrimmedLeft (28.0f).withTrimmedRight (6.0f),
                juce::Justification::centredLeft, false);
}

void RecordButton::clicked()
{
    if (onClick)
        onClick();
}

void RecordButton::setRecording (bool isRecording, double seconds)
{
    if (recording != isRecording || std::abs (elapsed - seconds) > 0.1)
    {
        recording = isRecording;
        elapsed = seconds;
        repaint();
    }
}

Meter::Meter (const juce::String& labelText)
    : label (labelText)
{
    setInterceptsMouseClicks (false, false);
}

void Meter::setLevel (float linearLevel)
{
    level = juce::jmax (0.0f, linearLevel);
    peak = juce::jmax (level, peak * 0.985f);
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const int labelWidth = 34;
    const int valueWidth = 52;
    const int barHeight = 7;
    const int barY = bounds.getCentreY() - barHeight / 2;
    const auto barArea = juce::Rectangle<int> (labelWidth, barY,
                                               juce::jmax (10, bounds.getWidth() - labelWidth - valueWidth),
                                               barHeight);

    g.setFont (Theme::font (11.5f, true));
    g.setColour (Theme::textDim);
    g.drawText (label, 0, 0, labelWidth - 5, bounds.getHeight(), juce::Justification::centredLeft, false);

    g.setColour (Theme::field);
    g.fillRoundedRectangle (barArea.toFloat(), 3.5f);

    const float db = juce::Decibels::gainToDecibels (juce::jmax (level, 1.0e-6f), -60.0f);
    const float normalised = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

    if (normalised > 0.0f)
    {
        auto fill = barArea.toFloat().withWidth (barArea.getWidth() * normalised);
        juce::ColourGradient gradient (Theme::accent, (float) barArea.getX(), 0.0f,
                                       Theme::danger, (float) barArea.getRight(), 0.0f, false);
        gradient.addColour (0.7, Theme::accent);
        gradient.addColour (0.88, Theme::warning);
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (fill, 3.5f);
    }

    const float peakNormalised = juce::jlimit (0.0f, 1.0f,
        (juce::Decibels::gainToDecibels (juce::jmax (peak, 1.0e-6f), -60.0f) + 60.0f) / 60.0f);
    if (peakNormalised > 0.02f)
    {
        g.setColour (Theme::text.withAlpha (0.35f));
        const float x = (float) barArea.getX() + (float) barArea.getWidth() * peakNormalised;
        g.fillRect (juce::Rectangle<float> (x, (float) barY - 1.0f, 1.0f, (float) barHeight + 2.0f));
    }

    g.setFont (Theme::mono (11.5f));
    g.setColour (Theme::textDim);
    g.drawText (db <= -59.9f ? juce::String ("-inf") : juce::String (db, 1) + " dB",
                barArea.getRight() + 5, 0, valueWidth - 5, bounds.getHeight(),
                juce::Justification::centredRight, false);
}
