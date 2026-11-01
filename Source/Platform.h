#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <dwmapi.h>
 #ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
  #define DWMWA_USE_IMMERSIVE_DARK_MODE 20
 #endif
 #ifndef DWMWA_CAPTION_COLOR
  #define DWMWA_CAPTION_COLOR 35
 #endif
 #ifndef DWMWA_TEXT_COLOR
  #define DWMWA_TEXT_COLOR 36
 #endif

inline void applyDarkTitleBar (juce::Component& component)
{
    if (auto* peer = component.getPeer())
    {
        if (auto hwnd = (HWND) peer->getNativeHandle())
        {
            const BOOL dark = TRUE;
            DwmSetWindowAttribute (hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof (dark));

            const COLORREF caption = RGB (10, 11, 16);
            DwmSetWindowAttribute (hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof (caption));

            const COLORREF text = RGB (242, 244, 247);
            DwmSetWindowAttribute (hwnd, DWMWA_TEXT_COLOR, &text, sizeof (text));
        }
    }
}
#else
inline void applyDarkTitleBar (juce::Component&) {}
#endif
