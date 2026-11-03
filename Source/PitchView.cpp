#include "PitchView.h"
#include "Core/NoteQuantizer.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool isBlackKey (int pitchClass)
    {
        switch (pitchClass)
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: return false;
        }
    }

    juce::String keyName (int midi, autotune::NoteLabelStyle style, bool useFlats)
    {
        return juce::String::fromUTF8 (autotune::formatNoteLabel (midi, style, useFlats).c_str());
    }
}

PitchView::PitchView (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    setInterceptsMouseClicks (true, false);
    setTooltip ("Click or drag any labeled piano key to hear a reference note.");
    startTimerHz (60);
}

void PitchView::setShowTarget (bool shouldShow)
{
    showTarget = shouldShow;
    repaint();
}

void PitchView::setNoteLabelStyle (NoteLabelStyle style)
{
    noteLabelStyle = style;
    repaint();
}

void PitchView::setUseFlats (bool shouldUseFlats)
{
    useFlats = shouldUseFlats;
    repaint();
}

juce::Rectangle<float> PitchView::getPianoBounds() const
{
    const float keyboardWidth = juce::jlimit (190.0f, 360.0f, (float) getWidth() * 0.20f);
    return { 0.0f, 0.0f, keyboardWidth, (float) getHeight() };
}

int PitchView::whiteKeyIndexForMidi (int midi) const
{
    if (midi < lowMidi)
        return -1;

    int index = -1;
    for (int note = lowMidi; note <= juce::jmin (midi, highMidi); ++note)
        if (! isBlackKey (((note % 12) + 12) % 12))
            ++index;

    return index;
}

int PitchView::whiteKeyCount() const
{
    return whiteKeyIndexForMidi (highMidi) + 1;
}

int PitchView::whiteMidiForIndex (int index) const
{
    int count = -1;
    for (int midi = lowMidi; midi <= highMidi; ++midi)
        if (! isBlackKey (((midi % 12) + 12) % 12))
        {
            ++count;
            if (count == index)
                return midi;
        }

    return -1;
}

juce::Rectangle<float> PitchView::getBlackKeyBounds (int midi,
                                                     juce::Rectangle<float> piano) const
{
    const int whiteCount = whiteKeyCount();
    const float whiteHeight = piano.getHeight() / (float) whiteCount;
    const int previousWhiteIndex = whiteKeyIndexForMidi (midi - 1);
    const float boundaryY = piano.getBottom() - ((float) previousWhiteIndex + 1.0f) * whiteHeight;
    const float keyWidth = piano.getWidth() * 0.62f;
    const float keyHeight = whiteHeight * 0.62f;

    return { piano.getX(), boundaryY - keyHeight * 0.5f, keyWidth, keyHeight };
}

int PitchView::midiAtMouse (juce::Point<float> position) const
{
    const auto piano = getPianoBounds();

    if (! piano.contains (position))
        return -1;

    for (int midi = lowMidi; midi <= highMidi; ++midi)
        if (isBlackKey (((midi % 12) + 12) % 12)
            && getBlackKeyBounds (midi, piano).contains (position))
            return midi;

    const int whiteCount = whiteKeyCount();
    const float whiteHeight = piano.getHeight() / (float) whiteCount;
    const int rowFromTop = juce::jlimit (0, whiteCount - 1,
                                         (int) std::floor ((position.y - piano.getY()) / whiteHeight));
    return whiteMidiForIndex (whiteCount - 1 - rowFromTop);
}

void PitchView::triggerNoteAt (juce::Point<float> position)
{
    const int midi = midiAtMouse (position);

    if (midi < 0)
    {
        lastTriggeredMidi = -1;
        return;
    }

    if (midi == lastTriggeredMidi)
        return;

    lastTriggeredMidi = midi;
    flashMidi = midi;
    flashUntilMs = juce::Time::getMillisecondCounterHiRes() + 260.0;

    if (onNoteClicked)
        onNoteClicked (midi);

    repaint();
}

void PitchView::mouseDown (const juce::MouseEvent& event)
{
    lastTriggeredMidi = -1;
    triggerNoteAt (event.position);
}

void PitchView::mouseDrag (const juce::MouseEvent& event)
{
    triggerNoteAt (event.position);
}

void PitchView::mouseUp (const juce::MouseEvent&)
{
    lastTriggeredMidi = -1;
}

void PitchView::mouseMove (const juce::MouseEvent& event)
{
    const int midi = midiAtMouse (event.position);
    if (midi == hoverMidi)
        return;

    hoverMidi = midi;
    setMouseCursor (midi >= 0 ? juce::MouseCursor::PointingHandCursor
                              : juce::MouseCursor::NormalCursor);
    repaint();
}

void PitchView::mouseExit (const juce::MouseEvent&)
{
    hoverMidi = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void PitchView::timerCallback()
{
    engine.getTrace (points, historySeconds);
    snapshot = engine.getSnapshot();

    if (! points.empty())
        lastFrameTimeMs = points.back().timeMs;

    repaint();
}

float PitchView::noteCentreY (int midi) const
{
    const auto piano = getPianoBounds();

    if (isBlackKey (((midi % 12) + 12) % 12))
        return getBlackKeyBounds (midi, piano).getCentreY();

    const int index = whiteKeyIndexForMidi (midi);
    const float whiteHeight = piano.getHeight() / (float) whiteKeyCount();
    return piano.getBottom() - ((float) index + 0.5f) * whiteHeight;
}

float PitchView::yForMidi (float midi) const
{
    const float clamped = juce::jlimit ((float) lowMidi, (float) highMidi, midi);
    const int lower = (int) std::floor (clamped);
    const int upper = juce::jmin (highMidi, lower + 1);
    const float fraction = clamped - (float) lower;

    const float lowerY = noteCentreY (lower);
    const float upperY = noteCentreY (upper);
    return lowerY + fraction * (upperY - lowerY);
}

juce::Colour PitchView::colourForNote (int midi, bool inScale) const
{
    const int pc = ((midi % 12) + 12) % 12;
    const float hue = (float) pc / 12.0f;
    const bool black = isBlackKey (pc);

    float saturation = inScale ? 0.36f : 0.20f;
    float lightness = (black ? 0.125f : 0.185f) + (inScale ? 0.010f : -0.012f);

    const int octave = midi / 12;
    lightness += (octave % 2 == 0) ? 0.006f : -0.004f;

    return juce::Colour::fromHSL (hue, saturation, juce::jlimit (0.06f, 0.5f, lightness), 1.0f);
}

void PitchView::paint (juce::Graphics& g)
{
    const auto fullBounds = getLocalBounds().toFloat();
    const int width = getWidth();
    const int height = getHeight();

    if (width <= 4 || height <= 4)
        return;

    juce::Path framePath;
    framePath.addRoundedRectangle (fullBounds.reduced (0.5f), 14.0f);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (framePath);

        g.setGradientFill (juce::ColourGradient (Theme::panelRaised, 0.0f, 0.0f,
                                                 Theme::panel, 0.0f, (float) height, false));
        g.fillRect (fullBounds);

        const auto piano = getPianoBounds();
        const float plotLeft = piano.getRight() + 1.0f;
        const float plotRight = (float) width - 4.0f;
        const float plotWidth = juce::jmax (40.0f, plotRight - plotLeft);
        const float plotBottom = (float) height;
        const int root = snapshot.keyRoot;
        const uint32_t mask = snapshot.scaleMask == 0 ? 0x0FFFu : snapshot.scaleMask;

        const double newestTime = ! points.empty() ? points.back().timeMs
                                                   : juce::Time::getMillisecondCounterHiRes();
        const double historyMs = historySeconds * 1000.0;

        auto xForTime = [&] (double timeMs)
        {
            const double age = newestTime - timeMs;
            const double fraction = juce::jlimit (0.0, 1.0, 1.0 - age / historyMs);
            return plotLeft + (float) (fraction * (double) plotWidth) - 4.0f;
        };

        // Pitch bands follow the exact white-key rows and black-key bars.
        {
            const int whiteCount = whiteKeyCount();
            const float whiteHeight = piano.getHeight() / (float) whiteCount;

            for (int index = 0; index < whiteCount; ++index)
            {
                const int midi = whiteMidiForIndex (index);
                const int pc = ((midi - root) % 12 + 12) % 12;
                const bool inScale = (mask & (1u << pc)) != 0;
                const float top = piano.getBottom() - ((float) index + 1.0f) * whiteHeight;

                const auto base = colourForNote (midi, inScale);
                juce::ColourGradient band (base.brighter (0.035f), 0.0f, top,
                                           base.darker (0.045f), 0.0f, top + whiteHeight, false);
                g.setGradientFill (band);
                g.fillRect (juce::Rectangle<float> (plotLeft, top, plotWidth, whiteHeight + 0.5f));

                g.setColour (juce::Colours::black.withAlpha (0.08f));
                g.fillRect (juce::Rectangle<float> (plotLeft, top, plotWidth, 1.0f));
            }

            for (int midi = lowMidi; midi <= highMidi; ++midi)
            {
                if (! isBlackKey (((midi % 12) + 12) % 12))
                    continue;

                const int pc = ((midi - root) % 12 + 12) % 12;
                const bool inScale = (mask & (1u << pc)) != 0;
                const auto key = getBlackKeyBounds (midi, piano);
                const auto base = colourForNote (midi, inScale);

                g.setColour (juce::Colours::black.withAlpha (0.24f));
                g.fillRect (juce::Rectangle<float> (plotLeft, key.getY() - 1.0f, plotWidth,
                                                    key.getHeight() + 2.0f));

                juce::ColourGradient band (base.brighter (0.07f), 0.0f, key.getY(),
                                           base.darker (0.03f), 0.0f, key.getBottom(), false);
                g.setGradientFill (band);
                g.fillRect (juce::Rectangle<float> (plotLeft, key.getY(), plotWidth,
                                                    key.getHeight() + 0.5f));

                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.fillRect (juce::Rectangle<float> (plotLeft, key.getY(), plotWidth, 1.0f));
            }
        }

        // Highlight the target key.
        if (snapshot.voiced && snapshot.targetMidi >= (float) lowMidi - 0.5f
            && snapshot.targetMidi <= (float) highMidi + 0.5f)
        {
            const int targetNote = juce::jlimit (lowMidi, highMidi,
                                                 (int) std::lround (snapshot.targetMidi));
            float targetTop = 0.0f;
            float targetBottom = 0.0f;

            if (isBlackKey (((targetNote % 12) + 12) % 12))
            {
                const auto key = getBlackKeyBounds (targetNote, piano);
                targetTop = key.getY();
                targetBottom = key.getBottom();
            }
            else
            {
                const int index = whiteKeyIndexForMidi (targetNote);
                const float whiteHeight = piano.getHeight() / (float) whiteKeyCount();
                targetTop = piano.getBottom() - ((float) index + 1.0f) * whiteHeight;
                targetBottom = targetTop + whiteHeight;
            }

            g.setColour (Theme::accent.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float> (plotLeft, targetTop, plotWidth,
                                                juce::jmax (1.0f, targetBottom - targetTop)));
        }

        // Grid
        {
            g.setColour (Theme::line.withAlpha (0.035f));
            for (int second = 0; second <= (int) std::ceil (historySeconds); ++second)
            {
                const float x = xForTime (newestTime - second * 1000.0);
                if (x >= plotLeft && x <= plotRight)
                    g.drawVerticalLine ((int) std::lround (x), 0.0f, plotBottom);
            }

            g.setColour (juce::Colours::white.withAlpha (0.045f));
            for (int midi = lowMidi; midi <= highMidi; ++midi)
                if (midi % 12 == 0)
                {
                    const float y = yForMidi ((float) midi);
                    g.drawHorizontalLine ((int) std::lround (y), plotLeft, plotRight);
                }
        }

        // Piano keyboard
        {
            const int whiteCount = whiteKeyCount();
            const float whiteHeight = piano.getHeight() / (float) whiteCount;
            const float blackKeyWidth = piano.getWidth() * 0.62f;
            const float whiteLabelFont = juce::jlimit (7.0f, 12.5f, whiteHeight * 0.47f)
                                             / juce::jmax (0.1f, Theme::uiScale);
            const float blackLabelFont = juce::jlimit (6.5f, 10.5f, whiteHeight * 0.35f)
                                             / juce::jmax (0.1f, Theme::uiScale);

            auto whiteKeyBounds = [&] (int midi)
            {
                const int index = whiteKeyIndexForMidi (midi);
                const float top = piano.getBottom() - ((float) index + 1.0f) * whiteHeight;
                return juce::Rectangle<float> (piano.getX(), top, piano.getWidth(), whiteHeight);
            };

            for (int index = 0; index < whiteCount; ++index)
            {
                const int midi = whiteMidiForIndex (index);
                const auto key = whiteKeyBounds (midi);

                juce::ColourGradient face (Theme::keyWhite.darker (0.06f), key.getX(), 0.0f,
                                           Theme::keyWhite.brighter (0.05f), key.getRight(), 0.0f, false);
                g.setGradientFill (face);
                g.fillRect (key);

                g.setColour (Theme::keyLine);
                g.fillRect (juce::Rectangle<float> (key.getX(), key.getY(), key.getWidth(), 1.0f));
            }

            const double nowMs = juce::Time::getMillisecondCounterHiRes();

            auto isBlackNote = [] (int midi)
            {
                return isBlackKey (((midi % 12) + 12) % 12);
            };

            auto drawKeyHighlight = [&] (int midi, juce::Colour colour, float alpha)
            {
                if (midi < lowMidi || midi > highMidi)
                    return;

                if (isBlackNote (midi))
                {
                    const auto key = getBlackKeyBounds (midi, piano);
                    g.setColour (colour.withAlpha (alpha));
                    g.fillRoundedRectangle (key, 3.0f);
                    g.setColour (colour.withAlpha (juce::jmin (1.0f, alpha * 1.7f)));
                    g.drawRoundedRectangle (key.reduced (0.5f), 2.5f, 1.0f);
                }
                else
                {
                    const auto key = whiteKeyBounds (midi);
                    g.setColour (colour.withAlpha (alpha * 0.62f));
                    g.fillRect (key);
                    g.setColour (colour.withAlpha (juce::jmin (1.0f, alpha * 1.5f)));
                    g.fillRect (key.getRight() - 3.0f, key.getY(), 2.0f, key.getHeight());
                }
            };

            if (hoverMidi >= 0 && ! isBlackNote (hoverMidi))
                drawKeyHighlight (hoverMidi, Theme::accent, 0.20f);

            if (flashMidi >= 0 && nowMs < flashUntilMs && ! isBlackNote (flashMidi))
                drawKeyHighlight (flashMidi, Theme::accent, 0.42f);

            for (int midi = lowMidi; midi <= highMidi; ++midi)
            {
                if (! isBlackNote (midi))
                    continue;

                const auto key = getBlackKeyBounds (midi, piano);

                g.setColour (juce::Colours::black.withAlpha (0.38f));
                g.fillRoundedRectangle (key.translated (2.4f, 1.2f), 3.0f);

                juce::ColourGradient face (Theme::keyBlack.darker (0.04f), key.getX(), 0.0f,
                                           Theme::keyBlack.brighter (0.11f), key.getRight(), 0.0f, false);
                g.setGradientFill (face);
                g.fillRoundedRectangle (key, 3.0f);

                g.setColour (juce::Colours::white.withAlpha (0.15f));
                g.drawRoundedRectangle (key.reduced (0.5f), 2.5f, 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float> (key.getX() + 2.0f, key.getY() + 1.0f,
                                                    key.getWidth() - 5.0f, 1.0f));
                g.setColour (juce::Colours::black.withAlpha (0.32f));
                g.fillRoundedRectangle (juce::Rectangle<float> (key.getRight() - 4.0f, key.getY() + 1.0f,
                                                                3.0f, key.getHeight() - 2.0f), 1.5f);
            }

            if (hoverMidi >= 0 && isBlackNote (hoverMidi))
                drawKeyHighlight (hoverMidi, Theme::accent, 0.20f);

            if (flashMidi >= 0 && nowMs < flashUntilMs && isBlackNote (flashMidi))
                drawKeyHighlight (flashMidi, Theme::accent, 0.42f);

            for (int midi = lowMidi; midi <= highMidi; ++midi)
            {
                const int pc = ((midi % 12) + 12) % 12;

                const auto label = keyName (midi, noteLabelStyle, useFlats);

                if (isBlackKey (pc))
                {
                    const auto key = getBlackKeyBounds (midi, piano);
                    const auto labelArea = key.reduced (6.0f, 0.0f);
                    g.setFont (Theme::font (blackLabelFont));
                    g.setColour (Theme::text.withAlpha (0.92f));
                    g.drawText (label, labelArea.toNearestInt(),
                                juce::Justification::centredRight, false);
                }
                else
                {
                    const auto key = whiteKeyBounds (midi);
                    const auto labelArea = juce::Rectangle<float> (blackKeyWidth + 3.0f,
                                                                   key.getY(),
                                                                   piano.getWidth() - blackKeyWidth - 11.0f,
                                                                   key.getHeight());
                    g.setFont (Theme::font (whiteLabelFont, pc == 0));
                    g.setColour (juce::Colour (0xff1b1f27).withAlpha (pc == 0 ? 1.0f : 0.86f));
                    g.drawText (label, labelArea.toNearestInt(),
                                juce::Justification::centredRight, false);
                }
            }

            g.setColour (Theme::stroke);
            g.drawRect (piano, 1.0f);
        }

        // Trace
        {
            juce::Graphics::ScopedSaveState traceSave (g);
            g.reduceClipRegion (juce::Rectangle<int> ((int) std::floor (plotLeft), 0,
                                                      (int) std::ceil (plotWidth) + 12,
                                                      (int) std::ceil (plotBottom)));

            auto buildPath = [&] (bool targetPath)
            {
                juce::Path path;
                bool started = false;
                double lastTime = 0.0;

                for (const auto& point : points)
                {
                    if (point.timeMs < newestTime - historyMs)
                        continue;

                    const float midi = targetPath ? point.targetMidi : point.midi;
                    const bool usable = point.voiced && (! targetPath || point.targetMidi > 0.0f);

                    if (! usable)
                    {
                        started = false;
                        continue;
                    }

                    if (started && point.timeMs - lastTime > 150.0)
                        started = false;

                    const float x = xForTime (point.timeMs);
                    const float y = yForMidi (midi);

                    if (! started)
                    {
                        path.startNewSubPath (x, y);
                        started = true;
                    }
                    else
                    {
                        path.lineTo (x, y);
                    }

                    lastTime = point.timeMs;
                }

                return path;
            };

            if (showTarget && snapshot.strength > 0.02f)
            {
                const auto targetPath = buildPath (true);

                if (! targetPath.isEmpty())
                {
                    g.setColour (Theme::accent.withAlpha (0.24f));
                    g.strokePath (targetPath, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved,
                                                                    juce::PathStrokeType::rounded));
                }
            }

            const auto pitchPath = buildPath (false);
            if (! pitchPath.isEmpty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.035f));
                g.strokePath (pitchPath, juce::PathStrokeType (14.0f, juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.strokePath (pitchPath, juce::PathStrokeType (7.5f, juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
                g.setColour (juce::Colours::white.withAlpha (0.16f));
                g.strokePath (pitchPath, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
                g.setColour (juce::Colours::white.withAlpha (0.94f));
                g.strokePath (pitchPath, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
            }

            // Glowing head
            if (snapshot.voiced && snapshot.midi > 0.0f)
            {
                float headX = plotRight - 8.0f;
                float headY = yForMidi (snapshot.midi);

                if (! points.empty() && points.back().voiced)
                {
                    headX = xForTime (points.back().timeMs);
                    headY = yForMidi (points.back().midi);
                }

                g.setColour (Theme::accent.withAlpha (0.05f));
                g.fillEllipse (headX - 16.0f, headY - 16.0f, 32.0f, 32.0f);
                g.setColour (juce::Colours::white.withAlpha (0.10f));
                g.fillEllipse (headX - 8.0f, headY - 8.0f, 16.0f, 16.0f);
                g.setColour (juce::Colours::white);
                g.fillEllipse (headX - 2.8f, headY - 2.8f, 5.6f, 5.6f);
            }
        }

        // Time ruler
        {
            const float tickBottom = plotBottom - 1.0f;
            const float tickTop = tickBottom - 5.0f;
            const float labelTop = tickBottom - 17.0f;

            g.setColour (Theme::stroke.withAlpha (0.45f));
            g.drawHorizontalLine ((int) tickBottom, plotLeft, plotRight);

            g.setFont (Theme::mono (10.0f));

            for (int second = 0; second <= (int) historySeconds; ++second)
            {
                float x = xForTime (newestTime - second * 1000.0);
                if (second == (int) historySeconds && x < plotLeft)
                    x = plotLeft + 2.0f;

                if (x < plotLeft - 1.0f || x > plotRight)
                    continue;

                g.setColour (Theme::text.withAlpha (second == 0 ? 0.90f : 0.62f));
                g.drawVerticalLine ((int) std::lround (x), tickTop, tickBottom);

                const auto label = second == 0 ? juce::String ("now")
                                               : "-" + juce::String (second) + "s";
                const float labelCentre = juce::jlimit (plotLeft + 20.0f, plotRight - 20.0f, x);

                g.setColour (juce::Colours::black.withAlpha (0.42f));
                g.drawText (label,
                            juce::Rectangle<float> (labelCentre - 24.0f, labelTop + 1.0f, 48.0f, 12.0f),
                            juce::Justification::centred, false);
                g.setColour (Theme::text.withAlpha (second == 0 ? 0.98f : 0.75f));
                g.drawText (label,
                            juce::Rectangle<float> (labelCentre - 24.0f, labelTop, 48.0f, 12.0f),
                            juce::Justification::centred, false);
            }
        }

        // Note readout
        {
            const float ui = Theme::uiScale;
            const int pillWidth = juce::roundToInt ((snapshot.voiced ? 200.0f : 126.0f) * ui);
            const int pillHeight = juce::roundToInt (30.0f * ui);
            const int inset = juce::roundToInt (12.0f * ui);
            const auto pillArea = juce::Rectangle<int> (width - pillWidth - juce::roundToInt (14.0f * ui),
                                                        juce::roundToInt (10.0f * ui),
                                                        pillWidth, pillHeight);
            g.setColour (juce::Colours::black.withAlpha (0.34f));
            g.fillRoundedRectangle (pillArea.toFloat(), 9.0f);
            g.setColour (Theme::stroke);
            g.drawRoundedRectangle (pillArea.toFloat().reduced (0.5f), 9.0f, 1.0f);

            if (snapshot.voiced && snapshot.midi > 0.0f)
            {
                const int rounded = (int) std::lround (snapshot.midi);
                const int cents = (int) std::lround ((snapshot.midi - (float) rounded) * 100.0f);
                const auto noteName = autotune::midiToName ((float) rounded);
                const auto centsText = (cents >= 0 ? "+" : "") + juce::String (cents) + "c";

                const auto content = pillArea.reduced (inset, 0);

                g.setFont (Theme::font (14.0f, true));
                g.setColour (Theme::text);
                g.drawText (noteName, content.withWidth (juce::roundToInt (46.0f * ui)),
                            juce::Justification::centredLeft, false);

                g.setFont (Theme::mono (10.5f));
                g.setColour (Theme::textDim);
                g.drawText (centsText,
                            content.withTrimmedLeft (juce::roundToInt (42.0f * ui))
                                   .withWidth (juce::roundToInt (54.0f * ui)),
                            juce::Justification::centredLeft, false);

                if (snapshot.targetMidi > 0.0f)
                {
                    g.setFont (Theme::font (10.5f, true));
                    g.setColour (Theme::accent.withAlpha (0.9f));
                    g.drawText ("-> " + autotune::midiToName (snapshot.targetMidi),
                                content.withTrimmedLeft (juce::roundToInt (98.0f * ui)),
                                juce::Justification::centredRight, false);
                }
            }
            else
            {
                g.setColour (Theme::textDim);
                g.fillEllipse ((float) pillArea.getX() + 12.0f * ui,
                               (float) pillArea.getCentreY() - 2.0f,
                               4.0f, 4.0f);
                g.setFont (Theme::font (10.0f, true));
                g.drawText ("LISTENING",
                            pillArea.reduced (inset, 0).withTrimmedLeft (juce::roundToInt (16.0f * ui)),
                            juce::Justification::centredLeft, false);
            }
        }

        if (points.empty())
        {
            g.setFont (Theme::font (10.5f, true));
            g.setColour (Theme::text.withAlpha (0.26f));
            g.drawText ("SING OR SPEAK INTO YOUR MICROPHONE",
                        juce::Rectangle<int> ((int) std::floor (plotLeft),
                                              (int) std::floor (plotBottom) - 34,
                                              (int) std::ceil (plotWidth), 20),
                        juce::Justification::centred, false);
        }
    }

    g.setColour (Theme::stroke);
    g.strokePath (framePath, juce::PathStrokeType (1.0f));
}
