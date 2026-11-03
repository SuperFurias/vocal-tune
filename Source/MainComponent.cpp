#include "MainComponent.h"
#include "AudioFilePanel.h"
#include "Platform.h"
#include "StudioLookAndFeel.h"
#include "Theme.h"
#include "Core/NoteQuantizer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace
{
    class ToolDialogWindow final : public juce::DialogWindow
    {
    public:
        ToolDialogWindow (const juce::String& title,
                          std::unique_ptr<juce::Component> content,
                          std::function<void()> onClosed)
            : juce::DialogWindow (title, Theme::panel, true, false),
              closeCallback (std::move (onClosed))
        {
            setContentOwned (content.release(), true);
            setResizable (false, false);
            setSize (juce::jmax (300, getContentComponent()->getWidth()),
                     juce::jmax (200, getContentComponent()->getHeight()));
        }

        void showModal (juce::Component* parent)
        {
            if (getPeer() == nullptr)
                addToDesktop();

            centreAroundComponent (parent, getWidth(), getHeight());
            setVisible (true);
            applyDarkTitleBar (*this);
            toFront (true);
            enterModalState (true, nullptr, false);
        }

        void closeButtonPressed() override
        {
            if (closing)
                return;

            closing = true;
            exitModalState (0);
            setVisible (false);

            if (closeCallback)
                closeCallback();
        }

    private:
        std::function<void()> closeCallback;
        bool closing = false;
    };

    class SettingsPanel final : public juce::Component
    {
    public:
        explicit SettingsPanel (AudioEngine& engineToUse)
            : engine (engineToUse),
              selector (engineToUse.getDeviceManager(), 0, 2, 0, 2, false, false, true, false)
        {
            setSize (700, 700);

            viewport.setViewedComponent (&selector, false);
            viewport.setScrollBarsShown (true, false);
            addAndMakeVisible (viewport);

            selector.setSize (660, 620);

            latencyCombo.addItem ("Low latency", 1);
            latencyCombo.addItem ("Balanced", 2);
            latencyCombo.addItem ("Smooth", 3);
            latencyCombo.setSelectedId (juce::jlimit (0, 2, engine.params.latencyMode.load()) + 1,
                                        juce::dontSendNotification);
            latencyCombo.onChange = [this]
            {
                engine.params.latencyMode.store (juce::jlimit (0, 2, latencyCombo.getSelectedId() - 1));
            };
            addAndMakeVisible (latencyCombo);

            setupSlider (a4Slider, 415.0, 466.0, 0.5, " Hz",
                         engine.params.tuningA4.load (std::memory_order_relaxed), 74);
            a4Slider.onValueChange = [this]
            {
                engine.params.tuningA4.store ((float) a4Slider.getValue());
            };
            addAndMakeVisible (a4Slider);

            setupSlider (gateSlider, -80.0, -30.0, 1.0, " dB",
                         engine.params.gateDb.load (std::memory_order_relaxed), 64);
            gateSlider.onValueChange = [this]
            {
                engine.params.gateDb.store ((float) gateSlider.getValue());
            };
            addAndMakeVisible (gateSlider);

            hint.setText ("For the lowest latency choose Windows Audio (Low Latency Mode), or Exclusive "
                          "Mode if nothing else needs the device, then pick the smallest buffer that runs "
                          "without crackles.",
                          juce::dontSendNotification);
            hint.setFont (Theme::font (10.0f));
            hint.setColour (juce::Label::textColourId, Theme::textDim);
            hint.setJustificationType (juce::Justification::topLeft);
            hint.setMinimumHorizontalScale (0.85f);
            addAndMakeVisible (hint);

            doneButton.getProperties().set ("accent", true);
            doneButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
            doneButton.setColour (juce::TextButton::textColourOffId, Theme::background);
            doneButton.setButtonText ("DONE");
            doneButton.onClick = [this]
            {
                if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
                    dialog->closeButtonPressed();
            };
            addAndMakeVisible (doneButton);
        }

        ~SettingsPanel() override
        {
            viewport.setViewedComponent (nullptr, false);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);

            g.setColour (Theme::text);
            g.setFont (Theme::display (19.0f));
            g.drawText ("AUDIO SETUP", 24, 18, getWidth() - 48, 26,
                        juce::Justification::centredLeft, false);

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (10.0f));
            g.drawText ("Device, buffer and voice engine", 24, 43, getWidth() - 48, 16,
                        juce::Justification::centredLeft, false);

            drawSectionHeading (g, deviceLabelBounds, "DEVICE");
            drawSectionHeading (g, engineLabelBounds, "VOICE ENGINE");

            g.setFont (Theme::font (8.5f, true));
            g.setColour (Theme::textDim);
            g.drawText ("PITCH ENGINE", latencyLabelBounds, juce::Justification::centredLeft, false);
            g.drawText ("TUNING (A4)", a4LabelBounds, juce::Justification::centredLeft, false);
            g.drawText ("NOISE GATE", gateLabelBounds, juce::Justification::centredLeft, false);

            g.setColour (Theme::strokeSoft);
            g.fillRect (24, doneButton.getY() - 16, getWidth() - 48, 1);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (24);
            area.removeFromTop (46);

            deviceLabelBounds = area.removeFromTop (20);
            area.removeFromTop (2);

            viewport.setBounds (area.removeFromTop (392));
            selector.setBounds (0, 0, juce::jmax (200, viewport.getWidth() - 12), 620);
            area.removeFromTop (14);

            engineLabelBounds = area.removeFromTop (20);
            area.removeFromTop (4);

            auto row = area.removeFromTop (52);
            auto pitchArea = row.removeFromLeft (190);
            latencyLabelBounds = pitchArea.removeFromTop (14);
            latencyCombo.setBounds (pitchArea.withHeight (30));

            row.removeFromLeft (18);
            auto a4Area = row.removeFromLeft (210);
            a4LabelBounds = a4Area.removeFromTop (14);
            a4Slider.setBounds (a4Area.withHeight (30));

            row.removeFromLeft (18);
            auto gateArea = row;
            gateLabelBounds = gateArea.removeFromTop (14);
            gateSlider.setBounds (gateArea.withHeight (30));

            area.removeFromTop (10);
            hint.setBounds (area.removeFromTop (40));

            doneButton.setBounds (getWidth() - 24 - 120, getHeight() - 24 - 34, 120, 34);
        }

    private:
        static void drawSectionHeading (juce::Graphics& g, juce::Rectangle<int> area,
                                        const juce::String& text)
        {
            g.setColour (Theme::strokeSoft);
            g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
            g.setColour (Theme::accent);
            g.fillRect (area.getX(), area.getY() + 1, 16, 2);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (8.5f, true));
            g.drawText (text, area.getX(), area.getY() + 5, area.getWidth(), area.getHeight() - 5,
                        juce::Justification::centredLeft, false);
        }

        static void setupSlider (juce::Slider& slider, double minValue, double maxValue,
                                 double interval, const juce::String& suffix, double value,
                                 int textBoxWidth)
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, textBoxWidth, 22);
            slider.setRange (minValue, maxValue, interval);
            slider.setTextValueSuffix (suffix);
            slider.setValue (value, juce::dontSendNotification);
            slider.setNumDecimalPlacesToDisplay (1);
            slider.setColour (juce::Slider::textBoxTextColourId, Theme::text);
            slider.setColour (juce::Slider::textBoxBackgroundColourId, Theme::field);
            slider.setColour (juce::Slider::textBoxOutlineColourId, Theme::stroke);
        }

        AudioEngine& engine;
        juce::AudioDeviceSelectorComponent selector;
        juce::Viewport viewport;
        juce::ComboBox latencyCombo;
        juce::Slider a4Slider;
        juce::Slider gateSlider;
        juce::Label hint;
        juce::TextButton doneButton;
        juce::Rectangle<int> deviceLabelBounds;
        juce::Rectangle<int> engineLabelBounds;
        juce::Rectangle<int> latencyLabelBounds;
        juce::Rectangle<int> a4LabelBounds;
        juce::Rectangle<int> gateLabelBounds;
    };

    class ObsPanel final : public juce::Component,
                           private juce::Timer
    {
    public:
        ObsPanel (juce::String overlayBaseUrl,
                  bool serverRunning,
                  const juce::String& serverStatus,
                  bool keyboardWasOn,
                  bool gridWasOn,
                  bool targetWasOn,
                  const juce::String& caption,
                  std::function<void (bool, bool, bool, const juce::String&)> changed)
            : baseUrl (std::move (overlayBaseUrl)),
              running (serverRunning),
              onChanged (std::move (changed)),
              keyboardToggle ("KEYBOARD"),
              gridToggle ("GRID"),
              targetToggle ("TARGET LINE")
        {
            setSize (660, 430);

            statusText = serverStatus;

            urlEditor.setMultiLine (false);
            urlEditor.setReadOnly (true);
            urlEditor.setFont (Theme::mono (11.0f));
            urlEditor.setColour (juce::TextEditor::backgroundColourId, Theme::field);
            urlEditor.setTextToShowWhenEmpty ("Overlay server offline", Theme::textFaint);
            urlEditor.setIndents (8, 8);
            addAndMakeVisible (urlEditor);

            addAndMakeVisible (keyboardToggle);
            addAndMakeVisible (gridToggle);
            addAndMakeVisible (targetToggle);
            keyboardToggle.setOn (keyboardWasOn);
            gridToggle.setOn (gridWasOn);
            targetToggle.setOn (targetWasOn);
            keyboardToggle.onToggle = [this] (bool) { updateUrl(); };
            gridToggle.onToggle = [this] (bool) { updateUrl(); };
            targetToggle.onToggle = [this] (bool) { updateUrl(); };

            captionEditor.setMultiLine (false);
            captionEditor.setFont (Theme::font (12.0f));
            captionEditor.setTextToShowWhenEmpty ("Optional caption, e.g. This is my voice.", Theme::textFaint);
            captionEditor.setText (caption, false);
            captionEditor.setIndents (8, 6);
            addAndMakeVisible (captionEditor);
            captionEditor.onTextChange = [this] { updateUrl(); };

            copyButton.getProperties().set ("accent", true);
            copyButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
            copyButton.setColour (juce::TextButton::textColourOffId, Theme::background);
            copyButton.setButtonText ("COPY LINK");
            copyButton.onClick = [this]
            {
                if (currentUrl.isNotEmpty())
                {
                    juce::SystemClipboard::copyTextToClipboard (currentUrl);
                    copiedLabel.setText ("Copied to clipboard", juce::dontSendNotification);
                    copiedLabel.setVisible (true);
                    startTimer (1400);
                }
            };
            addAndMakeVisible (copyButton);

            previewButton.setButtonText ("PREVIEW");
            previewButton.onClick = [this]
            {
                if (currentUrl.isNotEmpty())
                    juce::URL (currentUrl).launchInDefaultBrowser();
            };
            addAndMakeVisible (previewButton);

            demoButton.setButtonText ("DEMO PREVIEW");
            demoButton.onClick = [this]
            {
                if (currentUrl.isNotEmpty())
                    juce::URL (currentUrl).withParameter ("demo", "1").launchInDefaultBrowser();
            };
            addAndMakeVisible (demoButton);

            doneButton.setButtonText ("DONE");
            doneButton.onClick = [this]
            {
                if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
                    dialog->closeButtonPressed();
            };
            addAndMakeVisible (doneButton);

            copiedLabel.setFont (Theme::font (9.5f));
            copiedLabel.setColour (juce::Label::textColourId, Theme::accent);
            copiedLabel.setJustificationType (juce::Justification::centredRight);
            copiedLabel.setVisible (false);
            addAndMakeVisible (copiedLabel);

            updateUrl();

            if (! running)
            {
                copyButton.setEnabled (false);
                previewButton.setEnabled (false);
                demoButton.setEnabled (false);
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);

            g.setColour (Theme::text);
            g.setFont (Theme::display (19.0f));
            g.drawText ("OBS OVERLAY", 24, 18, getWidth() - 48, 26,
                        juce::Justification::centredLeft, false);

            g.setColour (running ? Theme::accent : Theme::danger);
            g.fillEllipse ((float) getWidth() - 34.0f, 29.0f, 7.0f, 7.0f);

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (10.0f));
            g.drawText (statusText, 24, 42, getWidth() - 48, 18,
                        juce::Justification::centredLeft, false);

            g.setFont (Theme::font (8.5f, true));
            g.setColour (Theme::textDim);
            g.drawText ("BROWSER SOURCE URL", urlLabelBounds, juce::Justification::centredLeft, false);
            g.drawText ("OPTIONS", optionLabelBounds, juce::Justification::centredLeft, false);
            g.drawText ("CAPTION", captionLabelBounds, juce::Justification::centredLeft, false);

            g.setColour (Theme::textFaint);
            g.setFont (Theme::font (10.0f));
            g.drawFittedText ("In OBS: Sources  >  +  >  Browser, paste the URL, set the size to 1920 x 1080 "
                              "and keep the transparent background. The overlay updates live from this app.",
                              helpBounds, juce::Justification::topLeft, 3, 0.9f);

            g.setColour (Theme::strokeSoft);
            g.fillRect (24, getHeight() - 58, getWidth() - 48, 1);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (24);
            area.removeFromTop (64);

            urlLabelBounds = area.removeFromTop (14);
            urlEditor.setBounds (area.removeFromTop (32));
            area.removeFromTop (12);

            optionLabelBounds = area.removeFromTop (14);
            auto toggleRow = area.removeFromTop (30);
            keyboardToggle.setBounds (toggleRow.removeFromLeft (118));
            toggleRow.removeFromLeft (8);
            gridToggle.setBounds (toggleRow.removeFromLeft (84));
            toggleRow.removeFromLeft (8);
            targetToggle.setBounds (toggleRow.removeFromLeft (128));
            area.removeFromTop (12);

            captionLabelBounds = area.removeFromTop (14);
            captionEditor.setBounds (area.removeFromTop (32));
            area.removeFromTop (12);

            helpBounds = area.removeFromTop (46);

            doneButton.setBounds (getWidth() - 24 - 88, getHeight() - 24 - 32, 88, 32);
            demoButton.setBounds (doneButton.getX() - 8 - 132, doneButton.getY(), 132, 32);
            previewButton.setBounds (demoButton.getX() - 8 - 92, doneButton.getY(), 92, 32);
            copyButton.setBounds (previewButton.getX() - 8 - 112, doneButton.getY(), 112, 32);
            copiedLabel.setBounds (24, doneButton.getY(), copyButton.getX() - 32, 32);
        }

        void updateUrl()
        {
            if (baseUrl.isEmpty() || ! running)
            {
                currentUrl.clear();
                urlEditor.setText ("", false);
                return;
            }

            juce::URL url (baseUrl);
            url = url.withParameter ("keyboard", keyboardToggle.isOn() ? "1" : "0");
            url = url.withParameter ("grid", gridToggle.isOn() ? "1" : "0");
            url = url.withParameter ("target", targetToggle.isOn() ? "1" : "0");
            url = url.withParameter ("history", "7");
            url = url.withParameter ("opacity", "0.9");

            const auto caption = captionEditor.getText().trim();
            if (caption.isNotEmpty())
                url = url.withParameter ("caption", caption);

            currentUrl = url.toString (true);
            urlEditor.setText (currentUrl, false);

            if (onChanged)
                onChanged (keyboardToggle.isOn(), gridToggle.isOn(), targetToggle.isOn(), caption);
        }

    private:
        void timerCallback() override
        {
            stopTimer();
            copiedLabel.setVisible (false);
        }

        juce::String baseUrl;
        bool running = false;
        juce::String statusText;
        std::function<void (bool, bool, bool, const juce::String&)> onChanged;
        juce::String currentUrl;

        juce::TextEditor urlEditor;
        PillToggle keyboardToggle;
        PillToggle gridToggle;
        PillToggle targetToggle;
        juce::TextEditor captionEditor;
        juce::TextButton copyButton;
        juce::TextButton previewButton;
        juce::TextButton demoButton;
        juce::TextButton doneButton;
        juce::Label copiedLabel;

        juce::Rectangle<int> urlLabelBounds;
        juce::Rectangle<int> optionLabelBounds;
        juce::Rectangle<int> captionLabelBounds;
        juce::Rectangle<int> helpBounds;
    };

}

const std::array<MainComponent::PresetDefinition, 5>& MainComponent::getPresets()
{
    static const std::array<PresetDefinition, 5> presets {{
        { "Natural",   0.62f, 35.0f, 0.22f },
        { "Pop",       0.85f, 12.0f, 0.10f },
        { "Tight",     0.95f,  4.0f, 0.00f },
        { "Hard Tune", 1.00f,  0.0f, 0.00f },
        { "Clean",     0.00f, 20.0f, 0.00f }
    }};
    return presets;
}

MainComponent::MainComponent()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    loadSettings();
    setupControls();
    applySettingsToControls();

    if (! overlay.start (settings.overlayPort))
    {
        setToast (overlay.getStatusText(), true);
    }
    else
    {
        settings.overlayPort = overlay.getPort();
        overlayStateDirty = true;
    }

    pitchView.setShowTarget (true);
    updateStatusTexts (engine.getSnapshot());
    setSize (1280, 860);
    startTimerHz (60);

    // The window is now on screen. Open the audio device after the first paint so
    // startup feels immediate and the loading state can be shown inside the app.
    juce::Timer::callAfterDelay (500, [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->initialiseAudio();
    });
}

void MainComponent::initialiseAudio()
{
    if (audioInitialised)
        return;

    audioStarting = true;
    repaint();

    std::unique_ptr<juce::XmlElement> deviceXml;
    if (getDeviceStateFile().existsAsFile())
        deviceXml = juce::XmlDocument::parse (getDeviceStateFile());

    const auto audioError = engine.initialise (deviceXml.get());
    audioInitialised = true;
    audioStarting = false;
    lastEngineError = engine.getLastError();

    if (audioError.isNotEmpty())
        setToast ("Audio: " + audioError, true);

    updateStatusTexts (engine.getSnapshot());
    overlayStateDirty = true;
    repaint();
}

MainComponent::~MainComponent()
{
    stopTimer();

    if (engine.isRecording())
        engine.stopRecording();

    saveSettings();

    settingsDialog.reset();
    obsDialog.reset();
    audioFileDialog.reset();

    overlay.stop();
    engine.shutdown();
}

void MainComponent::setupControls()
{
    addAndMakeVisible (pitchView);
    pitchView.setOnNoteClicked ([this] (int midi)
    {
        engine.playReferenceNote (midi);
    });

    dialCorrection.setOnChange ([this] (double value)
    {
        if (suppressCallbacks)
            return;
        settings.strength = (float) (value / 100.0);
        engine.params.strength.store (settings.strength);
        overlayStateDirty = true;
        markPresetCustom();
    });

    dialRetune.setOnChange ([this] (double value)
    {
        if (suppressCallbacks)
            return;
        settings.retuneMs = (float) value;
        engine.params.retuneMs.store (settings.retuneMs);
        markPresetCustom();
    });

    dialHumanize.setOnChange ([this] (double value)
    {
        if (suppressCallbacks)
            return;
        settings.humanize = (float) (value / 100.0);
        engine.params.humanize.store (settings.humanize);
        markPresetCustom();
    });

    addAndMakeVisible (dialCorrection);
    addAndMakeVisible (dialRetune);
    addAndMakeVisible (dialHumanize);

    addAndMakeVisible (meterIn);
    addAndMakeVisible (meterOut);

    for (int i = 0; i < 5; ++i)
        presetCombo.addItem (getPresets()[(size_t) i].name, i + 1);
    presetCombo.addItem ("Custom", 6);
    presetCombo.setSelectedId (6, juce::dontSendNotification);
    presetCombo.setTextWhenNothingSelected ("Custom");
    presetCombo.setTooltip ("Preset correction character. Moving the pitch controls selects Custom.");
    presetCombo.onChange = [this]
    {
        if (suppressCallbacks)
            return;

        const auto name = presetCombo.getText();
        settings.preset = name;

        if (name != "Custom")
            applyPreset (name);
    };
    addAndMakeVisible (presetCombo);

    keyCombo.addItem ("Chromatic", 1);
    for (int i = 0; i < 12; ++i)
        keyCombo.addItem (autotune::getNoteName (i), i + 2);
    keyCombo.setSelectedId (1, juce::dontSendNotification);
    keyCombo.setTooltip ("Key for scale snapping. Chromatic allows every note and disables the scale filter.");
    keyCombo.onChange = [this]
    {
        if (suppressCallbacks)
            return;

        const int id = keyCombo.getSelectedId();

        if (id == 1)
        {
            settings.keyIndex = -1;
            engine.params.scaleMask.store (0x0FFF);
            scaleCombo.setSelectedId (1, juce::dontSendNotification);
            scaleCombo.setEnabled (false);
            scaleCombo.setAlpha (0.55f);
        }
        else if (id >= 2)
        {
            settings.keyIndex = id - 2;
            engine.params.keyRoot.store (settings.keyIndex);
            scaleCombo.setEnabled (true);
            scaleCombo.setAlpha (1.0f);

            const auto& scales = autotune::getScales();
            settings.scaleIndex = juce::jlimit (0, (int) scales.size() - 1, settings.scaleIndex);
            scaleCombo.setSelectedId (settings.scaleIndex + 1, juce::dontSendNotification);
            engine.params.scaleMask.store (scales[(size_t) settings.scaleIndex].mask);
        }

        overlayStateDirty = true;
    };
    addAndMakeVisible (keyCombo);

    const auto& scales = autotune::getScales();
    for (int i = 0; i < (int) scales.size(); ++i)
        scaleCombo.addItem (scales[(size_t) i].name, i + 1);
    scaleCombo.setSelectedId (1, juce::dontSendNotification);
    scaleCombo.setTooltip ("Notes the correction is allowed to snap to.");
    scaleCombo.onChange = [this]
    {
        if (suppressCallbacks)
            return;

        const int index = juce::jlimit (0, (int) autotune::getScales().size() - 1,
                                       scaleCombo.getSelectedId() - 1);
        settings.scaleIndex = index;

        if (settings.keyIndex >= 0)
            engine.params.scaleMask.store (autotune::getScales()[(size_t) index].mask);

        overlayStateDirty = true;
    };
    addAndMakeVisible (scaleCombo);

    auto configureLabel = [this] (juce::Label& label, const juce::String& text,
                                  float height, juce::Colour colour, juce::Justification justification)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (Theme::font (height, true));
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (justification);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
    };

    configureLabel (keyLabel, "KEY", 13.5f, Theme::textDim, juce::Justification::centredLeft);
    configureLabel (scaleLabel, "SCALE", 13.5f, Theme::textDim, juce::Justification::centredLeft);
    configureLabel (transposeLabel, "TRANSPOSE", 13.5f, Theme::textDim, juce::Justification::centredLeft);
    configureLabel (gainInLabel, "IN GAIN", 13.5f, Theme::textDim, juce::Justification::centredLeft);
    configureLabel (gainOutLabel, "MONITOR VOL", 13.5f, Theme::textDim, juce::Justification::centredLeft);
    configureLabel (latencyLabel, "", 12.5f, Theme::textDim, juce::Justification::centredRight);
    latencyLabel.setFont (Theme::mono (12.5f));
    configureLabel (hintLabel, "Click keys to hear notes · headphones recommended", 12.0f, Theme::textDim,
                    juce::Justification::centredLeft);

    transposeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    transposeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
    transposeSlider.setRange (-12.0, 12.0, 1.0);
    transposeSlider.setNumDecimalPlacesToDisplay (0);
    transposeSlider.setTextValueSuffix (" st");
    transposeSlider.setValue (0.0, juce::dontSendNotification);
    transposeSlider.setTooltip ("Shift the corrected voice up or down in semitones.");
    transposeSlider.onValueChange = [this]
    {
        if (suppressCallbacks)
            return;
        settings.transpose = (int) std::lround (transposeSlider.getValue());
        engine.params.transpose.store (settings.transpose);
        overlayStateDirty = true;
    };
    addAndMakeVisible (transposeSlider);

    gainInSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainInSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
    gainInSlider.setRange (-24.0, 24.0, 0.5);
    gainInSlider.setNumDecimalPlacesToDisplay (1);
    gainInSlider.setTextValueSuffix (" dB");
    gainInSlider.setValue (0.0, juce::dontSendNotification);
    gainInSlider.setTooltip ("Microphone input gain.");
    gainInSlider.onValueChange = [this]
    {
        if (suppressCallbacks)
            return;
        settings.inputGainDb = (float) gainInSlider.getValue();
        engine.params.inputGainDb.store (settings.inputGainDb);
    };
    addAndMakeVisible (gainInSlider);

    gainOutSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainOutSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 22);
    gainOutSlider.setRange (0.0, 200.0, 1.0);
    gainOutSlider.setNumDecimalPlacesToDisplay (0);
    gainOutSlider.setTextValueSuffix ("%");
    gainOutSlider.setValue (100.0, juce::dontSendNotification);
    gainOutSlider.setTooltip ("Monitor and reference-note volume (100% = unity, up to 200%).");
    gainOutSlider.onValueChange = [this]
    {
        if (suppressCallbacks)
            return;

        const float linear = (float) juce::jlimit (0.0, 200.0, gainOutSlider.getValue()) / 100.0f;
        settings.outputGainDb = linear <= 0.0001f ? -100.0f
                                                   : juce::Decibels::gainToDecibels (linear);
        engine.params.outputGainDb.store (settings.outputGainDb);
    };
    addAndMakeVisible (gainOutSlider);

    monitorToggle.setTooltip ("Hear the corrected voice. Use headphones to avoid acoustic feedback. (M)");
    monitorToggle.onToggle = [this] (bool on)
    {
        settings.monitor = on;
        engine.params.monitor.store (on);
    };
    addAndMakeVisible (monitorToggle);

    bypassToggle.setTooltip ("Bypass pitch correction while keeping the visualizer alive. (B)");
    bypassToggle.onToggle = [this] (bool on)
    {
        settings.bypass = on;
        engine.params.bypass.store (on);
    };
    addAndMakeVisible (bypassToggle);

    recordButton.setTooltip ("Record the processed voice to a WAV file. (R)");
    recordButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (recordButton);

    fileButton.setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
    fileButton.setColour (juce::TextButton::textColourOffId, Theme::accent);
    fileButton.setTooltip ("Open the audio file workbench: preview a file, adjust the tuning, then export. (F3)");
    fileButton.onClick = [this] { showAudioFileDialog(); };
    addAndMakeVisible (fileButton);

    obsButton.getProperties().set ("accent", true);
    obsButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    obsButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    obsButton.setTooltip ("Copy the OBS Browser Source link. (F2)");
    obsButton.onClick = [this] { showObsDialog(); };
    addAndMakeVisible (obsButton);

    setupButton.setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
    setupButton.setColour (juce::TextButton::textColourOffId, Theme::text);
    setupButton.setTooltip ("Audio devices, buffer size, tuning and gate. (F1)");
    setupButton.onClick = [this] { showSettingsDialog(); };
    addAndMakeVisible (setupButton);
}

void MainComponent::applySettingsToControls()
{
    suppressCallbacks = true;

    dialCorrection.setValue (settings.strength * 100.0, juce::dontSendNotification);
    dialRetune.setValue (settings.retuneMs, juce::dontSendNotification);
    dialHumanize.setValue (settings.humanize * 100.0, juce::dontSendNotification);

    if (settings.keyIndex < 0)
    {
        keyCombo.setSelectedId (1, juce::dontSendNotification);
        scaleCombo.setSelectedId (1, juce::dontSendNotification);
        scaleCombo.setEnabled (false);
        scaleCombo.setAlpha (0.55f);
    }
    else
    {
        keyCombo.setSelectedId (juce::jlimit (0, 11, settings.keyIndex) + 2, juce::dontSendNotification);
        scaleCombo.setSelectedId (juce::jlimit (0, (int) autotune::getScales().size() - 1, settings.scaleIndex) + 1,
                                  juce::dontSendNotification);
        scaleCombo.setEnabled (true);
        scaleCombo.setAlpha (1.0f);
    }

    transposeSlider.setValue (settings.transpose, juce::dontSendNotification);
    gainInSlider.setValue (settings.inputGainDb, juce::dontSendNotification);

    const float outputLinear = juce::Decibels::decibelsToGain (settings.outputGainDb, -100.0f);
    gainOutSlider.setValue (juce::jlimit (0.0, 200.0, (double) outputLinear * 100.0),
                            juce::dontSendNotification);

    monitorToggle.setOn (settings.monitor);
    bypassToggle.setOn (settings.bypass);

    selectPresetItem (settings.preset);

    suppressCallbacks = false;

    applySettingsToEngine();
    overlayStateDirty = true;
}

void MainComponent::applySettingsToEngine()
{
    engine.params.strength.store (settings.strength);
    engine.params.retuneMs.store (settings.retuneMs);
    engine.params.humanize.store (settings.humanize);

    const auto& scales = autotune::getScales();
    const int scaleIndex = juce::jlimit (0, (int) scales.size() - 1, settings.scaleIndex);

    if (settings.keyIndex < 0)
    {
        engine.params.keyRoot.store (0);
        engine.params.scaleMask.store (0x0FFF);
    }
    else
    {
        engine.params.keyRoot.store (juce::jlimit (0, 11, settings.keyIndex));
        engine.params.scaleMask.store (scales[(size_t) scaleIndex].mask);
    }

    engine.params.transpose.store (juce::jlimit (-24, 24, settings.transpose));
    engine.params.tuningA4.store (juce::jlimit (415.0f, 466.0f, settings.tuningA4));
    engine.params.gateDb.store (juce::jlimit (-80.0f, -20.0f, settings.gateDb));
    engine.params.inputGainDb.store (juce::jlimit (-24.0f, 24.0f, settings.inputGainDb));
    settings.outputGainDb = juce::jlimit (-100.0f, 6.02f, settings.outputGainDb);
    engine.params.outputGainDb.store (settings.outputGainDb);
    engine.params.monitor.store (settings.monitor);
    engine.params.bypass.store (settings.bypass);
    engine.params.latencyMode.store (juce::jlimit (0, 2, settings.latencyMode));
}

void MainComponent::applyPreset (const juce::String& name)
{
    for (const auto& preset : getPresets())
    {
        if (name != preset.name)
            continue;

        suppressCallbacks = true;

        settings.strength = preset.strength;
        settings.retuneMs = preset.retuneMs;
        settings.humanize = preset.humanize;
        settings.preset = preset.name;

        dialCorrection.setValue (settings.strength * 100.0, juce::dontSendNotification);
        dialRetune.setValue (settings.retuneMs, juce::dontSendNotification);
        dialHumanize.setValue (settings.humanize * 100.0, juce::dontSendNotification);

        engine.params.strength.store (settings.strength);
        engine.params.retuneMs.store (settings.retuneMs);
        engine.params.humanize.store (settings.humanize);

        selectPresetItem (settings.preset);

        suppressCallbacks = false;
        overlayStateDirty = true;
        return;
    }
}

void MainComponent::markPresetCustom()
{
    if (suppressCallbacks)
        return;

    settings.preset = "Custom";
    selectPresetItem ("Custom");
}

void MainComponent::selectPresetItem (const juce::String& name)
{
    for (int i = 0; i < presetCombo.getNumItems(); ++i)
    {
        if (presetCombo.getItemText (i) == name)
        {
            presetCombo.setSelectedId (presetCombo.getItemId (i), juce::dontSendNotification);
            return;
        }
    }

    if (presetCombo.getNumItems() > 0)
        presetCombo.setSelectedId (presetCombo.getItemId (presetCombo.getNumItems() - 1),
                                   juce::dontSendNotification);
}

void MainComponent::setToast (const juce::String& message, bool isError)
{
    toastMessage = message;
    toastIsError = isError;
    toastUntilMs = juce::Time::getMillisecondCounterHiRes() + 4200.0;
    repaint();
}

void MainComponent::showSettingsDialog()
{
    if (settingsDialog != nullptr)
        settingsDialog->closeButtonPressed();

    settingsDialog.reset();

    auto content = std::make_unique<SettingsPanel> (engine);
    auto dialog = std::make_unique<ToolDialogWindow> (
        "Audio Setup", std::move (content),
        [this]
        {
            settings.tuningA4 = engine.params.tuningA4.load();
            settings.gateDb = engine.params.gateDb.load();
            settings.latencyMode = engine.params.latencyMode.load();
            saveSettings();
            updateStatusTexts (engine.getSnapshot());
        });
    dialog->showModal (this);
    settingsDialog = std::move (dialog);
}

void MainComponent::showObsDialog()
{
    if (obsDialog != nullptr)
        obsDialog->closeButtonPressed();

    obsDialog.reset();

    auto content = std::make_unique<ObsPanel> (
        overlay.getOverlayUrl(),
        overlay.isRunning(),
        overlay.isRunning() ? overlay.getStatusText() : "Overlay server offline - restart the app to retry.",
        settings.overlayKeyboard,
        settings.overlayGrid,
        settings.overlayTarget,
        settings.overlayCaption,
        [this] (bool keyboardOn, bool gridOn, bool targetOn, const juce::String& caption)
        {
            settings.overlayKeyboard = keyboardOn;
            settings.overlayGrid = gridOn;
            settings.overlayTarget = targetOn;
            settings.overlayCaption = caption;
        });

    auto dialog = std::make_unique<ToolDialogWindow> (
        "OBS Overlay", std::move (content),
        [this] { saveSettings(); });
    dialog->showModal (this);
    obsDialog = std::move (dialog);
}

void MainComponent::showAudioFileDialog()
{
    if (audioFileDialog != nullptr)
        audioFileDialog->closeButtonPressed();

    audioFileDialog.reset();

    AudioFilePanel::InitialSettings initial;
    initial.strength = engine.params.strength.load();
    initial.retuneMs = engine.params.retuneMs.load();
    initial.humanize = engine.params.humanize.load();
    initial.keyIndex = settings.keyIndex;
    initial.scaleIndex = settings.scaleIndex;
    initial.transpose = engine.params.transpose.load();
    initial.tuningA4 = engine.params.tuningA4.load();
    initial.gateDb = engine.params.gateDb.load();
    initial.inputGainDb = engine.params.inputGainDb.load();
    initial.presetName = settings.preset;

    auto content = std::make_unique<AudioFilePanel> (initial);
    auto dialog = std::make_unique<ToolDialogWindow> (
        "Tune Audio File", std::move (content), [] {});
    dialog->showModal (this);
    audioFileDialog = std::move (dialog);
}

void MainComponent::toggleRecording()
{
    if (engine.isRecording())
    {
        const auto path = engine.stopRecording();
        if (path.isNotEmpty())
            setToast ("Saved " + juce::File (path).getFileName() + " to the Recordings folder");
        else
            setToast ("Recording stopped");
    }
    else
    {
        if (engine.startRecording())
            setToast ("Recording processed audio to WAV");
        else
            setToast ("Could not start recording - is the audio device running?", true);
    }

    recordButton.setRecording (engine.isRecording(), engine.getRecordingSeconds());
}

void MainComponent::timerCallback()
{
    const auto snapshot = engine.getSnapshot();

    if (audioStarting)
        repaint();

    meterIn.setLevel (snapshot.inputPeak);
    meterOut.setLevel (snapshot.outputPeak);
    recordButton.setRecording (snapshot.recording, snapshot.recordingSeconds);

    updateStatusTexts (snapshot);

    const auto error = engine.getLastError();
    if (error != lastEngineError)
    {
        lastEngineError = error;
        if (error.isNotEmpty())
            setToast ("Audio: " + error, true);
    }

    if (toastUntilMs > 0.0 && juce::Time::getMillisecondCounterHiRes() > toastUntilMs)
    {
        toastUntilMs = 0.0;
        toastMessage.clear();
        repaint();
    }

    if (overlayStateDirty)
        publishOverlayState();

    if (++overlayPublishDivider >= 2)
    {
        overlayPublishDivider = 0;
        publishOverlayData();
    }
}

void MainComponent::updateStatusTexts (const AudioEngine::Snapshot& snapshot)
{
    auto shorten = [] (const juce::String& text, int maxChars)
    {
        return text.length() > maxChars ? text.substring (0, maxChars - 1) + "..." : text;
    };

    juce::String left;
    if (audioStarting)
    {
        left = "AUDIO STARTING  -  opening the microphone";
    }
    else if (snapshot.audioRunning)
    {
        left = shorten (snapshot.inputName, 34) + "  >  " + shorten (snapshot.outputName, 34);
    }
    else
    {
        left = "AUDIO OFFLINE";
        if (engine.getLastError().isNotEmpty())
            left += "  -  " + shorten (engine.getLastError(), 70);
    }

    const juce::String middle = audioStarting
                                    ? "Preparing the pitch engine"
                                    : juce::String (snapshot.sampleRate / 1000.0, 1) + " kHz  "
                                          + juce::String (snapshot.bufferSize) + " smp  "
                                          + juce::String (snapshot.totalLatencyMs, 1) + " ms total";

    juce::String right = overlay.isRunning() ? "OBS  " + overlay.getOverlayUrl() : "OBS OFFLINE";

    if (left != statusLeft)
    {
        statusLeft = left;
        repaint (bottomBarArea);
    }

    if (middle != statusMiddle)
    {
        statusMiddle = middle;
        repaint (bottomBarArea);
    }

    if (right != statusRight)
    {
        statusRight = right;
        repaint (bottomBarArea);
    }

    const auto latencyText = audioStarting
                                ? "loading audio..."
                                : "~" + juce::String (snapshot.algorithmLatencyMs, 1) + " ms engine  |  "
                                      + juce::String (snapshot.ioLatencyMs, 1) + " ms I/O";
    if (latencyLabel.getText() != latencyText)
        latencyLabel.setText (latencyText, juce::dontSendNotification);
}

void MainComponent::publishOverlayState()
{
    if (! overlay.isRunning())
        return;

    const int key = engine.params.keyRoot.load();
    const int transpose = engine.params.transpose.load();
    const float strength = engine.params.strength.load();
    const float tuning = engine.params.tuningA4.load();
    const uint32_t mask = engine.params.scaleMask.load();

    juce::String json;
    json << "{\"key\":" << key
         << ",\"mask\":" << (int) mask
         << ",\"strength\":" << juce::String (strength, 3)
         << ",\"transpose\":" << transpose
         << ",\"tuning\":" << juce::String (tuning, 1)
         << ",\"fmin\":82.4,\"fmax\":1046.5}";

    overlay.publishState (json);
    overlayStateDirty = false;
}

void MainComponent::publishOverlayData()
{
    if (! overlay.isRunning())
        return;

    std::vector<TracePoint> batch;
    uint64_t newestSequence = 0;
    engine.getPointsSince (lastOverlaySequence, batch, newestSequence);

    if (newestSequence < lastOverlaySequence)
    {
        lastOverlaySequence = 0;
        return;
    }

    if (batch.empty())
        return;

    juce::String json;
    json.preallocateBytes ((size_t) (batch.size() * 44 + 32));
    json << "{\"now\":" << juce::String (batch.back().timeMs, 1) << ",\"pts\":[";

    bool first = true;
    for (const auto& point : batch)
    {
        if (! first)
            json << ",";
        first = false;

        json << "[" << juce::String (point.timeMs, 1)
             << "," << juce::String (point.midi, 2)
             << "," << juce::String (point.targetMidi, 2)
             << "," << (point.voiced ? 1 : 0) << "]";
    }

    json << "]}";

    overlay.publishPoints (json);
    lastOverlaySequence = newestSequence;
}

juce::String MainComponent::buildOverlayUrl (bool demo) const
{
    const auto base = overlay.getOverlayUrl();
    if (base.isEmpty())
        return {};

    juce::URL url (base);
    url = url.withParameter ("keyboard", settings.overlayKeyboard ? "1" : "0");
    url = url.withParameter ("grid", settings.overlayGrid ? "1" : "0");
    url = url.withParameter ("target", settings.overlayTarget ? "1" : "0");
    url = url.withParameter ("history", "7");
    url = url.withParameter ("opacity", "0.9");

    if (settings.overlayCaption.trim().isNotEmpty())
        url = url.withParameter ("caption", settings.overlayCaption.trim());

    if (demo)
        url = url.withParameter ("demo", "1");

    return url.toString (true);
}

void MainComponent::loadSettings()
{
    const auto file = getSettingsFile();
    if (! file.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return;

    auto readNumber = [object] (const char* name, double fallback)
    {
        const auto value = object->getProperty (name);
        return (value.isInt() || value.isInt64() || value.isDouble()) ? (double) value : fallback;
    };

    auto readBool = [object] (const char* name, bool fallback)
    {
        const auto value = object->getProperty (name);
        return value.isBool() ? (bool) value : fallback;
    };

    auto readString = [object] (const char* name, const juce::String& fallback)
    {
        const auto value = object->getProperty (name);
        return value.isString() ? value.toString() : fallback;
    };

    settings.strength = juce::jlimit (0.0f, 1.0f, (float) readNumber ("strength", settings.strength));
    settings.retuneMs = juce::jlimit (0.0f, 250.0f, (float) readNumber ("retuneMs", settings.retuneMs));
    settings.humanize = juce::jlimit (0.0f, 1.0f, (float) readNumber ("humanize", settings.humanize));
    settings.keyIndex = juce::jlimit (-1, 11, (int) readNumber ("keyIndex", settings.keyIndex));
    settings.scaleIndex = juce::jlimit (0, (int) autotune::getScales().size() - 1,
                                        (int) readNumber ("scaleIndex", settings.scaleIndex));
    settings.transpose = juce::jlimit (-24, 24, (int) readNumber ("transpose", settings.transpose));
    settings.tuningA4 = juce::jlimit (415.0f, 466.0f, (float) readNumber ("tuningA4", settings.tuningA4));
    settings.gateDb = juce::jlimit (-80.0f, -20.0f, (float) readNumber ("gateDb", settings.gateDb));
    settings.inputGainDb = juce::jlimit (-24.0f, 24.0f, (float) readNumber ("inputGainDb", settings.inputGainDb));
    settings.outputGainDb = juce::jlimit (-100.0f, 6.02f, (float) readNumber ("outputGainDb", settings.outputGainDb));
    settings.monitor = readBool ("monitor", settings.monitor);
    settings.bypass = readBool ("bypass", settings.bypass);
    settings.latencyMode = juce::jlimit (0, 2, (int) readNumber ("latencyMode", settings.latencyMode));
    settings.preset = readString ("preset", settings.preset);
    settings.overlayPort = juce::jlimit (1024, 65535, (int) readNumber ("overlayPort", settings.overlayPort));
    settings.overlayKeyboard = readBool ("overlayKeyboard", settings.overlayKeyboard);
    settings.overlayGrid = readBool ("overlayGrid", settings.overlayGrid);
    settings.overlayTarget = readBool ("overlayTarget", settings.overlayTarget);
    settings.overlayCaption = readString ("overlayCaption", settings.overlayCaption);
}

void MainComponent::saveSettings()
{
    settings.strength = engine.params.strength.load();
    settings.retuneMs = engine.params.retuneMs.load();
    settings.humanize = engine.params.humanize.load();
    settings.transpose = engine.params.transpose.load();
    settings.tuningA4 = engine.params.tuningA4.load();
    settings.gateDb = engine.params.gateDb.load();
    settings.inputGainDb = engine.params.inputGainDb.load();
    settings.outputGainDb = engine.params.outputGainDb.load();
    settings.monitor = engine.params.monitor.load();
    settings.bypass = engine.params.bypass.load();
    settings.latencyMode = engine.params.latencyMode.load();

    if (overlay.getPort() > 0)
        settings.overlayPort = overlay.getPort();

    juce::var root (new juce::DynamicObject());
    auto* object = root.getDynamicObject();
    if (object == nullptr)
        return;

    object->setProperty ("strength", settings.strength);
    object->setProperty ("retuneMs", settings.retuneMs);
    object->setProperty ("humanize", settings.humanize);
    object->setProperty ("keyIndex", settings.keyIndex);
    object->setProperty ("scaleIndex", settings.scaleIndex);
    object->setProperty ("transpose", settings.transpose);
    object->setProperty ("tuningA4", settings.tuningA4);
    object->setProperty ("gateDb", settings.gateDb);
    object->setProperty ("inputGainDb", settings.inputGainDb);
    object->setProperty ("outputGainDb", settings.outputGainDb);
    object->setProperty ("monitor", settings.monitor);
    object->setProperty ("bypass", settings.bypass);
    object->setProperty ("latencyMode", settings.latencyMode);
    object->setProperty ("preset", settings.preset);
    object->setProperty ("overlayPort", settings.overlayPort);
    object->setProperty ("overlayKeyboard", settings.overlayKeyboard);
    object->setProperty ("overlayGrid", settings.overlayGrid);
    object->setProperty ("overlayTarget", settings.overlayTarget);
    object->setProperty ("overlayCaption", settings.overlayCaption);

    getSettingsFile().replaceWithText (juce::JSON::toString (root, false));

    if (auto xml = engine.getDeviceManager().createStateXml())
        xml->writeTo (getDeviceStateFile());
}

juce::File MainComponent::getAppDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
}

juce::File MainComponent::getSettingsFile() const
{
    return getAppDirectory().getChildFile ("settings.json");
}

juce::File MainComponent::getDeviceStateFile() const
{
    return getAppDirectory().getChildFile ("audio-device.xml");
}

void MainComponent::resized()
{
    Theme::uiScale = juce::jlimit (1.0f, 1.3f, (float) getHeight() / 900.0f);
    const float s = Theme::uiScale;
    const float compact = juce::jlimit (0.82f, 1.0f, ((float) getHeight() - 740.0f) / 120.0f);
    const float layoutScale = s * compact;

    auto scaled = [layoutScale] (int value) { return juce::roundToInt ((float) value * layoutScale); };

    keyLabel.setFont (Theme::font (13.5f, true));
    scaleLabel.setFont (Theme::font (13.5f, true));
    transposeLabel.setFont (Theme::font (13.5f, true));
    gainInLabel.setFont (Theme::font (13.5f, true));
    gainOutLabel.setFont (Theme::font (13.5f, true));
    hintLabel.setFont (Theme::font (12.0f));
    latencyLabel.setFont (Theme::mono (12.5f));

    auto bounds = getLocalBounds();
    topBarArea = bounds.removeFromTop (64);
    bottomBarArea = bounds.removeFromBottom (30);

    auto content = bounds.reduced (16, 16);
    const int sidebarWidth = juce::jlimit (350, 560, scaled (360));
    sidebarArea = content.removeFromRight (sidebarWidth);
    content.removeFromRight (16);
    viewArea = content;

    pitchView.setBounds (viewArea);

    brandArea = { 24, 10, 220, 44 };

    presetCombo.setBounds (250, 22, 206, 32);
    presetLabelArea = { 250, 4, 206, 16 };

    recordButton.setBounds (getWidth() - 16 - 98, 17, 98, 30);
    obsButton.setBounds (getWidth() - 16 - 98 - 6 - 112, 17, 112, 30);
    setupButton.setBounds (getWidth() - 16 - 98 - 6 - 112 - 6 - 82, 17, 82, 30);
    fileButton.setBounds (getWidth() - 16 - 98 - 6 - 112 - 6 - 82 - 6 - 112, 17, 112, 30);

    auto area = sidebarArea.reduced (scaled (20), scaled (18));
    const int x = area.getX();
    const int w = area.getWidth();
    int y = area.getY();

    const int dialWidth = juce::jlimit (200, 340, scaled (230));
    dialCorrection.setBounds (x + (w - dialWidth) / 2, y, dialWidth, scaled (184));
    y += scaled (192);

    const int smallWidth = (w - scaled (12)) / 2;
    dialRetune.setBounds (x, y, smallWidth, scaled (112));
    dialHumanize.setBounds (x + w - smallWidth, y, smallWidth, scaled (112));
    y += scaled (120);

    musicalHeadingArea = { x, y, w, scaled (18) };
    y += scaled (26);

    const int half = (w - scaled (10)) / 2;
    keyLabel.setBounds (x, y, half, scaled (17));
    scaleLabel.setBounds (x + half + scaled (10), y, half, scaled (17));
    y += scaled (19);
    keyCombo.setBounds (x, y, half, scaled (32));
    scaleCombo.setBounds (x + half + scaled (10), y, half, scaled (32));
    y += scaled (40);

    transposeLabel.setBounds (x, y, w, scaled (17));
    y += scaled (19);
    transposeSlider.setBounds (x, y, w, scaled (32));
    y += scaled (42);

    ioHeadingArea = { x, y, w, scaled (18) };
    y += scaled (26);

    const int toggleWidth = (w - scaled (8)) / 2;
    monitorToggle.setBounds (x, y, toggleWidth, scaled (32));
    bypassToggle.setBounds (x + w - toggleWidth, y, toggleWidth, scaled (32));
    y += scaled (40);

    meterIn.setBounds (x, y, w, scaled (22));
    y += scaled (26);
    meterOut.setBounds (x, y, w, scaled (22));
    y += scaled (28);

    gainInLabel.setBounds (x, y + scaled (2), scaled (56), scaled (26));
    gainInSlider.setBounds (x + scaled (58), y, w - scaled (58), scaled (30));
    y += scaled (34);
    gainOutLabel.setBounds (x, y + scaled (2), scaled (96), scaled (26));
    gainOutSlider.setBounds (x + scaled (98), y, w - scaled (98), scaled (30));
    y += scaled (38);

    hintLabel.setBounds (x, y, w, scaled (18));

    latencyLabel.setBounds (x, area.getBottom() - scaled (18), w, scaled (18));

    repaint();
}

void MainComponent::paint (juce::Graphics& g)
{
    juce::ColourGradient background (Theme::background2, 0.0f, 0.0f,
                                     Theme::background, 0.0f, (float) getHeight(), false);
    g.setGradientFill (background);
    g.fillRect (getLocalBounds());

    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRect (topBarArea);

    // Logo mark
    {
        auto logo = juce::Rectangle<float> (24.0f, 17.0f, 30.0f, 30.0f);
        g.setColour (Theme::accent.withAlpha (0.10f));
        g.fillRoundedRectangle (logo, 8.0f);
        g.setColour (Theme::accent.withAlpha (0.55f));
        g.drawRoundedRectangle (logo.reduced (0.5f), 8.0f, 1.0f);

        g.setColour (Theme::accent);
        const auto c = logo.getCentre();
        g.fillRoundedRectangle (c.x - 7.0f, c.y - 3.0f, 2.2f, 9.0f, 1.1f);
        g.fillRoundedRectangle (c.x - 3.0f, c.y - 8.0f, 2.2f, 16.0f, 1.1f);
        g.fillRoundedRectangle (c.x + 1.0f, c.y - 5.0f, 2.2f, 11.0f, 1.1f);
        g.fillRoundedRectangle (c.x + 5.0f, c.y - 1.0f, 2.2f, 6.0f, 1.1f);
    }

    g.setColour (Theme::text);
    g.setFont (Theme::display (19.0f));
    g.drawText ("VOCAL TUNE", 64, 7, 220, 28, juce::Justification::centredLeft, false);

    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (11.5f, true));
    g.drawText ("REAL-TIME PITCH", 65, 32, 220, 15, juce::Justification::centredLeft, false);

    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (11.5f, true));
    g.drawText ("PRESET", presetLabelArea, juce::Justification::centredLeft, false);

    g.setColour (Theme::strokeSoft);
    g.fillRect (0, topBarArea.getBottom() - 1, getWidth(), 1);

    // Sidebar card
    {
        juce::Path card;
        card.addRoundedRectangle (sidebarArea.toFloat(), 16.0f);
        g.setColour (Theme::panel);
        g.fillPath (card);

        juce::ColourGradient sheen (Theme::panelRaised.withAlpha (0.35f), (float) sidebarArea.getX(),
                                    (float) sidebarArea.getY(), Theme::panel.withAlpha (0.0f),
                                    (float) sidebarArea.getX(), (float) sidebarArea.getY() + 180.0f, false);
        g.setGradientFill (sheen);
        g.fillPath (card);

        g.setColour (Theme::stroke);
        g.strokePath (card, juce::PathStrokeType (1.0f));
    }

    auto drawHeading = [] (juce::Graphics& graphics, juce::Rectangle<int> heading, const juce::String& text)
    {
        if (heading.isEmpty())
            return;

        graphics.setColour (Theme::strokeSoft);
        graphics.fillRect (heading.getX(), heading.getY(), heading.getWidth(), 1);
        graphics.setColour (Theme::accent);
        graphics.fillRect (heading.getX(), heading.getY() + 1, 18, 2);
        graphics.setColour (Theme::textFaint);
        graphics.setFont (Theme::font (12.5f, true));
        graphics.drawText (text, heading.getX(), heading.getY() + 3, heading.getWidth(),
                           heading.getHeight() - 5, juce::Justification::centredLeft, false);
    };

    drawHeading (g, musicalHeadingArea, "MUSICAL");
    drawHeading (g, ioHeadingArea, "I/O & MONITORING");

    if (latencyLabel.getBounds().getHeight() > 0)
    {
        g.setColour (Theme::strokeSoft);
        g.fillRect (latencyLabel.getX(), latencyLabel.getY() - 9, latencyLabel.getWidth(), 1);
    }

    // Bottom status bar
    g.setColour (Theme::background.withAlpha (0.55f));
    g.fillRect (bottomBarArea);
    g.setColour (Theme::strokeSoft);
    g.fillRect (0, bottomBarArea.getY(), getWidth(), 1);

    auto bottom = bottomBarArea.reduced (20, 0);
    const int leftWidth = (int) (getWidth() * 0.42);
    auto leftArea = bottom.removeFromLeft (leftWidth);
    auto rightArea = bottom.removeFromRight (juce::jmin (430, bottom.getWidth() / 2));
    auto middleArea = bottom;

    g.setFont (Theme::font (11.5f));
    g.setColour (Theme::textFaint);
    g.drawFittedText (statusLeft, leftArea, juce::Justification::centredLeft, 1, 0.82f);
    g.drawFittedText (statusMiddle, middleArea.reduced (6, 0), juce::Justification::centred, 1, 0.8f);

    const bool online = overlay.isRunning();
    g.setColour (online ? Theme::accent : Theme::danger);
    g.fillEllipse ((float) rightArea.getX(), (float) rightArea.getCentreY() - 2.0f, 4.0f, 4.0f);
    g.setColour (online ? Theme::textDim : Theme::danger.withAlpha (0.8f));
    g.drawFittedText (statusRight, rightArea.withTrimmedLeft (10), juce::Justification::centredRight, 1, 0.78f);
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (audioStarting)
    {
        const auto overlayArea = viewArea.toFloat().reduced (0.5f);
        g.setColour (Theme::background.withAlpha (0.82f));
        g.fillRoundedRectangle (overlayArea, 14.0f);

        auto card = juce::Rectangle<float> (0.0f, 0.0f, 410.0f, 188.0f)
                        .withCentre (viewArea.getCentre().toFloat());
        g.setColour (Theme::panel.withAlpha (0.98f));
        g.fillRoundedRectangle (card, 16.0f);
        g.setColour (Theme::stroke);
        g.drawRoundedRectangle (card.reduced (0.5f), 16.0f, 1.0f);

        const auto logo = juce::Rectangle<float> (card.getCentreX() - 15.0f, card.getY() + 26.0f, 30.0f, 30.0f);
        g.setColour (Theme::accent.withAlpha (0.10f));
        g.fillRoundedRectangle (logo, 8.0f);
        g.setColour (Theme::accent.withAlpha (0.55f));
        g.drawRoundedRectangle (logo.reduced (0.5f), 8.0f, 1.0f);

        g.setColour (Theme::accent);
        const auto c = logo.getCentre();
        g.fillRoundedRectangle (c.x - 7.0f, c.y - 3.0f, 2.2f, 9.0f, 1.1f);
        g.fillRoundedRectangle (c.x - 3.0f, c.y - 8.0f, 2.2f, 16.0f, 1.1f);
        g.fillRoundedRectangle (c.x + 1.0f, c.y - 5.0f, 2.2f, 11.0f, 1.1f);
        g.fillRoundedRectangle (c.x + 5.0f, c.y - 1.0f, 2.2f, 6.0f, 1.1f);

        g.setFont (Theme::display (19.0f));
        g.setColour (Theme::text);
        g.drawText ("STARTING AUDIO", card.withTrimmedTop (70.0f).withHeight (26.0f).toNearestInt(),
                    juce::Justification::centred, false);

        g.setFont (Theme::font (11.0f));
        g.setColour (Theme::textDim);
        g.drawText ("Opening the microphone and preparing the pitch engine",
                    card.withTrimmedTop (100.0f).withHeight (18.0f).toNearestInt(),
                    juce::Justification::centred, false);

        const bool obsReady = overlay.isRunning();
        g.setColour (obsReady ? Theme::accent : Theme::danger);
        g.fillEllipse (card.getX() + 126.0f, card.getY() + 130.0f, 5.0f, 5.0f);
        g.setFont (Theme::font (9.5f, true));
        g.setColour (obsReady ? Theme::accent : Theme::danger);
        g.drawText (obsReady ? "OBS OVERLAY READY" : "OBS OVERLAY OFFLINE",
                    card.withTrimmedTop (123.0f).withHeight (18.0f).toNearestInt(),
                    juce::Justification::centred, false);

        juce::Rectangle<float> progress (card.getX() + 96.0f, card.getBottom() - 42.0f,
                                         card.getWidth() - 192.0f, 2.0f);
        g.setColour (Theme::stroke);
        g.fillRoundedRectangle (progress, 1.0f);
        g.setColour (Theme::accent.withAlpha (0.8f));
        g.fillRoundedRectangle (progress.withWidth (progress.getWidth() * 0.38f), 1.0f);
    }

    if (toastMessage.isEmpty() || juce::Time::getMillisecondCounterHiRes() > toastUntilMs)
        return;

    const int toastWidth = juce::jmin (viewArea.getWidth() - 50, 640);
    const int toastHeight = 32;
    juce::Rectangle<int> toastArea (viewArea.getX() + (viewArea.getWidth() - toastWidth) / 2,
                                    viewArea.getY() + 18, toastWidth, toastHeight);

    g.setColour (Theme::background.withAlpha (0.9f));
    g.fillRoundedRectangle (toastArea.toFloat(), 9.0f);
    g.setColour (toastIsError ? Theme::danger.withAlpha (0.65f) : Theme::accent.withAlpha (0.5f));
    g.drawRoundedRectangle (toastArea.toFloat().reduced (0.5f), 9.0f, 1.0f);

    g.setFont (Theme::font (12.0f, true));
    g.setColour (toastIsError ? Theme::danger : Theme::text);
    g.drawFittedText (toastMessage, toastArea.reduced (12, 0), juce::Justification::centred, 1, 0.82f);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::F1Key)
    {
        showSettingsDialog();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F2Key)
    {
        showObsDialog();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F3Key)
    {
        showAudioFileDialog();
        return true;
    }

    const auto character = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (character == 'm')
    {
        settings.monitor = ! settings.monitor;
        engine.params.monitor.store (settings.monitor);
        monitorToggle.setOn (settings.monitor);
        return true;
    }

    if (character == 'b')
    {
        settings.bypass = ! settings.bypass;
        engine.params.bypass.store (settings.bypass);
        bypassToggle.setOn (settings.bypass);
        return true;
    }

    if (character == 'r')
    {
        toggleRecording();
        return true;
    }

    return false;
}
