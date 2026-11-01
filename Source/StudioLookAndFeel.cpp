#include "StudioLookAndFeel.h"
#include "Theme.h"

#include <cmath>

StudioLookAndFeel::StudioLookAndFeel()
{
    setColourScheme (getDarkColourScheme());

    setColour (juce::ResizableWindow::backgroundColourId, Theme::panel);
    setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    setColour (juce::TextButton::textColourOffId, Theme::text);
    setColour (juce::TextButton::textColourOnId, Theme::background);
    setColour (juce::ComboBox::backgroundColourId, Theme::panelRaised);
    setColour (juce::ComboBox::textColourId, Theme::text);
    setColour (juce::ComboBox::outlineColourId, Theme::stroke);
    setColour (juce::ComboBox::arrowColourId, Theme::textDim);
    setColour (juce::ComboBox::focusedOutlineColourId, Theme::accent.withAlpha (0.55f));
    setColour (juce::PopupMenu::backgroundColourId, Theme::panelRaised);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, Theme::background);
    setColour (juce::TextEditor::backgroundColourId, Theme::field);
    setColour (juce::TextEditor::textColourId, Theme::text);
    setColour (juce::TextEditor::outlineColourId, Theme::stroke);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent.withAlpha (0.55f));
    setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.25f));
    setColour (juce::TextEditor::highlightedTextColourId, Theme::text);
    setColour (juce::Label::textColourId, Theme::text);
    setColour (juce::Slider::textBoxTextColourId, Theme::text);
    setColour (juce::Slider::textBoxBackgroundColourId, Theme::field);
    setColour (juce::Slider::textBoxOutlineColourId, Theme::stroke);
    setColour (juce::Slider::rotarySliderFillColourId, Theme::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, Theme::panelRaised);
    setColour (juce::Slider::thumbColourId, Theme::accent);
    setColour (juce::Slider::trackColourId, Theme::accent);
    setColour (juce::TooltipWindow::backgroundColourId, Theme::panelRaised);
    setColour (juce::TooltipWindow::textColourId, Theme::text);
    setColour (juce::TooltipWindow::outlineColourId, Theme::stroke);
    setColour (juce::ToggleButton::textColourId, Theme::text);
    setColour (juce::ToggleButton::tickColourId, Theme::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Theme::textDim);
    setColour (juce::ScrollBar::thumbColourId, Theme::textFaint);
    setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
    setColour (juce::AlertWindow::textColourId, Theme::text);
    setColour (juce::AlertWindow::outlineColourId, Theme::stroke);
    setColour (juce::ListBox::backgroundColourId, Theme::panel);
    setColour (juce::ListBox::textColourId, Theme::text);
    setColour (juce::ListBox::outlineColourId, Theme::stroke);
}

void StudioLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 7.0f;
    const float startAngle = juce::MathConstants<float>::pi * 1.25f;
    const float sweep = juce::MathConstants<float>::pi * 1.5f;
    const float endAngle = startAngle + sweep;
    const float valueAngle = startAngle + sweep * juce::jlimit (0.0f, 1.0f, sliderPos);
    const float thickness = height > 118 ? 5.0f : 3.5f;

    auto makeArc = [&] (float from, float to)
    {
        juce::Path path;
        const int steps = 72;
        for (int i = 0; i <= steps; ++i)
        {
            const float a = from + (to - from) * (float) i / (float) steps;
            const float px = centre.x + radius * std::sin (a);
            const float py = centre.y - radius * std::cos (a);
            if (i == 0)
                path.startNewSubPath (px, py);
            else
                path.lineTo (px, py);
        }
        return path;
    };

    const auto trackPath = makeArc (startAngle, endAngle);
    g.setColour (Theme::stroke);
    g.strokePath (trackPath, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    const auto valuePath = makeArc (startAngle, valueAngle);
    g.setColour (Theme::accent.withAlpha (0.10f));
    g.strokePath (valuePath, juce::PathStrokeType (thickness * 3.2f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (valuePath, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    // Tick marks around the ring
    for (int i = 0; i <= 4; ++i)
    {
        const float a = startAngle + sweep * (float) i / 4.0f;
        const float inner = radius + thickness * 0.9f;
        const float outer = radius + thickness * 2.2f;
        g.setColour (Theme::stroke.withMultipliedAlpha (i == 0 || i == 4 ? 1.4f : 0.8f));
        g.drawLine (centre.x + inner * std::sin (a), centre.y - inner * std::cos (a),
                    centre.x + outer * std::sin (a), centre.y - outer * std::cos (a),
                    1.0f);
    }

    const float dotX = centre.x + radius * std::sin (valueAngle);
    const float dotY = centre.y - radius * std::cos (valueAngle);
    g.setColour (Theme::accent.withAlpha (0.25f));
    g.fillEllipse (dotX - 7.0f, dotY - 7.0f, 14.0f, 14.0f);
    g.setColour (Theme::accent);
    g.fillEllipse (dotX - 3.2f, dotY - 3.2f, 6.4f, 6.4f);
    g.setColour (Theme::background.withAlpha (0.5f));
    g.drawEllipse (dotX - 3.2f, dotY - 3.2f, 6.4f, 6.4f, 1.0f);
}

void StudioLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool horizontal = style == juce::Slider::LinearHorizontal
                            || style == juce::Slider::LinearBar
                            || style == juce::Slider::LinearBarVertical;

    juce::Rectangle<float> track;

    if (horizontal)
        track = { (float) x + 2.0f, (float) y + (float) height * 0.5f - 2.5f, (float) width - 4.0f, 5.0f };
    else
        track = { (float) x + (float) width * 0.5f - 2.5f, (float) y + 2.0f, 5.0f, (float) height - 4.0f };

    g.setColour (Theme::field);
    g.fillRoundedRectangle (track, 2.5f);

    if (horizontal)
    {
        const float position = juce::jlimit (track.getX(), track.getRight(), sliderPos);
        juce::Rectangle<float> fill (track.getX(), track.getY(), position - track.getX(), track.getHeight());
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (fill, 2.5f);

        const float thumbX = position;
        const float thumbY = track.getCentreY();
        g.setColour (Theme::accent.withAlpha (0.16f));
        g.fillEllipse (thumbX - 9.0f, thumbY - 9.0f, 18.0f, 18.0f);
        g.setColour (Theme::text);
        g.fillEllipse (thumbX - 5.5f, thumbY - 5.5f, 11.0f, 11.0f);
        g.setColour (Theme::accentDark);
        g.drawEllipse (thumbX - 5.5f, thumbY - 5.5f, 11.0f, 11.0f, 1.0f);
    }
    else
    {
        const float position = juce::jlimit (track.getY(), track.getBottom(), sliderPos);
        juce::Rectangle<float> fill (track.getX(), position, track.getWidth(), track.getBottom() - position);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (fill, 2.5f);

        const float thumbY = position;
        g.setColour (Theme::text);
        g.fillEllipse (track.getCentreX() - 5.5f, thumbY - 5.5f, 11.0f, 11.0f);
    }
}

void StudioLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                              const juce::Colour& backgroundColour,
                                              bool highlighted, bool down)
{
    const bool accent = (bool) button.getProperties().getWithDefault ("accent", false);
    auto base = accent ? Theme::accent : backgroundColour;

    if (accent)
        base = base.withMultipliedBrightness (down ? 0.88f : (highlighted ? 1.04f : 1.0f));
    else
        base = base.withMultipliedBrightness (down ? 0.82f : (highlighted ? 1.25f : 1.0f));

    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = juce::jmin (10.0f, bounds.getHeight() * 0.5f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, corner);

    if (! accent)
    {
        g.setColour (Theme::stroke);
        g.drawRoundedRectangle (bounds, corner, 1.0f);
    }

    if (button.getToggleState())
    {
        g.setColour (accent ? Theme::background.withAlpha (0.10f) : Theme::accent.withAlpha (0.16f));
        g.fillRoundedRectangle (bounds.reduced (2.0f), corner - 1.0f);
    }
}

void StudioLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const float corner = juce::jmin (8.0f, bounds.getHeight() * 0.5f);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (box.hasKeyboardFocus (true)
                     ? box.findColour (juce::ComboBox::focusedOutlineColourId)
                     : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    const float cx = (float) width - 15.0f;
    const float cy = (float) height * 0.5f;

    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void StudioLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, Theme::text);
}

juce::Font StudioLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return Theme::font (17.0f);
}

juce::Font StudioLookAndFeel::getPopupMenuFont()
{
    return Theme::font (17.0f);
}

juce::Font StudioLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return Theme::font (juce::jlimit (12.0f, 14.5f, (float) buttonHeight * 0.38f), true);
}

juce::Font StudioLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

void StudioLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (Theme::panelRaised);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (Theme::stroke);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

void StudioLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                           bool isSeparator, bool isActive, bool isHighlighted,
                                           bool isTicked, bool hasSubMenu, const juce::String& text,
                                           const juce::String&, const juce::Drawable*,
                                           const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (Theme::stroke);
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    const auto item = area.reduced (5, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (item, 6.0f);
    }

    juce::Colour colour = textColour != nullptr ? *textColour : Theme::text;
    if (isHighlighted && isActive)
        colour = Theme::background;
    else if (! isActive)
        colour = Theme::textFaint;

    g.setColour (colour);
    g.setFont (getPopupMenuFont());
    g.drawText (text, area.reduced (14, 0), juce::Justification::centredLeft, true);

    if (isTicked)
    {
        g.setColour (isHighlighted ? Theme::background : Theme::accent);
        g.fillEllipse ((float) area.getRight() - 18.0f, (float) area.getCentreY() - 2.5f, 5.0f, 5.0f);
    }

    if (hasSubMenu)
    {
        juce::Path arrow;
        const float x = (float) area.getRight() - 16.0f;
        const float y = (float) area.getCentreY();
        arrow.startNewSubPath (x - 2.0f, y - 4.0f);
        arrow.lineTo (x + 2.0f, y);
        arrow.lineTo (x - 2.0f, y + 4.0f);
        g.setColour (colour.withAlpha (0.8f));
        g.strokePath (arrow, juce::PathStrokeType (1.3f));
    }
}

void StudioLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (Theme::panelRaised);
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (Theme::stroke);
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::font (12.5f));
    g.drawFittedText (text, bounds.toNearestInt().reduced (9, 3), juce::Justification::centredLeft, 2);
}

void StudioLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                                  juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 7.0f);
}

void StudioLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                               juce::TextEditor& editor)
{
    auto bounds = juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
    g.setColour (editor.hasKeyboardFocus (true)
                     ? editor.findColour (juce::TextEditor::focusedOutlineColourId)
                     : editor.findColour (juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
}
