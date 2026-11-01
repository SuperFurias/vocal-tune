#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    inline float uiScale = 1.0f;

    inline const juce::Colour background   { 0xff07080c };
    inline const juce::Colour background2  { 0xff0a0c11 };
    inline const juce::Colour panel        { 0xff0c0e14 };
    inline const juce::Colour panelRaised  { 0xff11141c };
    inline const juce::Colour field        { 0xff0a0c11 };
    inline const juce::Colour stroke       { 0x18ffffff };
    inline const juce::Colour strokeSoft   { 0x0dffffff };
    inline const juce::Colour text         { 0xfff2f4f7 };
    inline const juce::Colour textDim      { 0xff8a919f };
    inline const juce::Colour textFaint    { 0xff565d6b };
    inline const juce::Colour accent       { 0xff8ef0c8 };
    inline const juce::Colour accentSoft   { 0x408ef0c8 };
    inline const juce::Colour accentDark   { 0x1f8ef0c8 };
    inline const juce::Colour warning      { 0xffffc66d };
    inline const juce::Colour danger       { 0xffff5f6d };
    inline const juce::Colour recordRed    { 0xffff4d5e };
    inline const juce::Colour keyWhite     { 0xffe9ecf2 };
    inline const juce::Colour keyBlack     { 0xff101218 };
    inline const juce::Colour keyLine      { 0x30000000 };
    inline const juce::Colour meterTrack   { 0xff1b2029 };
    inline const juce::Colour line         { 0xffffffff };

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", height * uiScale,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font mono (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Consolas", height * uiScale,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font display (float height)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", height * uiScale, juce::Font::bold));
    }
}
