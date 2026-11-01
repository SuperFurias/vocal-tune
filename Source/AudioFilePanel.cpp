#include "AudioFilePanel.h"
#include "Core/NoteQuantizer.h"
#include "Theme.h"

#include <algorithm>
#include <atomic>
#include <cmath>

class AudioFilePanel::RenderJob final : public juce::Thread
{
public:
    RenderJob (juce::File inputToUse, juce::File outputToUse,
               OfflineRenderer::Settings settingsToUse)
        : juce::Thread ("Audio file export"),
          inputFile (std::move (inputToUse)),
          outputFile (std::move (outputToUse)),
          settings (settingsToUse)
    {
    }

    void run() override
    {
        result = OfflineRenderer::renderFile (inputFile, outputFile, settings,
                                              [this] (double amount)
        {
            progress.store (amount, std::memory_order_relaxed);
            return ! threadShouldExit();
        });

        finished.store (true, std::memory_order_release);
    }

    std::atomic<double> progress { 0.0 };
    std::atomic<bool> finished { false };
    OfflineRenderer::Result result;

private:
    juce::File inputFile;
    juce::File outputFile;
    OfflineRenderer::Settings settings;
};

namespace
{
    constexpr const char* audioFilePatterns = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";

    struct PanelPreset
    {
        const char* name;
        float strength;
        float retuneMs;
        float humanize;
    };

    constexpr PanelPreset panelPresets[] = {
        { "Natural",   0.62f, 35.0f, 0.22f },
        { "Pop",       0.85f, 12.0f, 0.10f },
        { "Tight",     0.95f,  4.0f, 0.00f },
        { "Hard Tune", 1.00f,  0.0f, 0.00f },
        { "Clean",     0.00f, 20.0f, 0.00f }
    };

    bool isSupportedAudioFile (const juce::File& file)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        return extension == ".wav" || extension == ".aif" || extension == ".aiff"
               || extension == ".flac" || extension == ".ogg" || extension == ".mp3";
    }
}

AudioFilePanel::AudioFilePanel (InitialSettings settingsToUse)
    : liveSettings (settingsToUse),
      prewarmPreviewAudio (settingsToUse.prewarmPreviewAudio)
{
    setOpaque (true);
    setSize (900, 800);
    setWantsKeyboardFocus (true);

    formatManager.registerBasicFormats();

    auto configureLabel = [this] (juce::Label& label, const juce::String& text,
                                  float height, juce::Colour colour, bool bold)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (Theme::font (height, bold));
        label.setColour (juce::Label::textColourId, colour);
        label.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setInterceptsMouseClicks (false, false);
        label.setMinimumHorizontalScale (0.78f);
        addAndMakeVisible (label);
    };

    configureLabel (presetLabel, "PRESET", 8.5f, Theme::textDim, true);
    configureLabel (keyLabel, "KEY", 8.5f, Theme::textDim, true);
    configureLabel (scaleLabel, "SCALE", 8.5f, Theme::textDim, true);
    configureLabel (transposeLabel, "TRANSPOSE", 8.5f, Theme::textDim, true);
    configureLabel (correctionLabel, "CORRECTION", 8.5f, Theme::textDim, true);
    configureLabel (retuneLabel, "RETUNE", 8.5f, Theme::textDim, true);
    configureLabel (humanizeLabel, "HUMANIZE", 8.5f, Theme::textDim, true);
    configureLabel (tuningLabel, "TUNING A4", 8.5f, Theme::textDim, true);
    configureLabel (inputGainLabel, "INPUT GAIN", 8.5f, Theme::textDim, true);

    fileLabel.setFont (Theme::font (13.0f, true));
    fileLabel.setColour (juce::Label::textColourId, Theme::text);
    fileLabel.setJustificationType (juce::Justification::centredLeft);
    fileLabel.setInterceptsMouseClicks (false, false);
    fileLabel.setMinimumHorizontalScale (0.7f);
    fileLabel.setVisible (false);
    addAndMakeVisible (fileLabel);

    infoLabel.setFont (Theme::mono (9.5f));
    infoLabel.setColour (juce::Label::textColourId, Theme::textDim);
    infoLabel.setJustificationType (juce::Justification::centredLeft);
    infoLabel.setInterceptsMouseClicks (false, false);
    infoLabel.setMinimumHorizontalScale (0.72f);
    infoLabel.setVisible (false);
    addAndMakeVisible (infoLabel);

    outputLabel.setFont (Theme::mono (10.0f));
    outputLabel.setColour (juce::Label::textColourId, Theme::textDim);
    outputLabel.setJustificationType (juce::Justification::centredLeft);
    outputLabel.setInterceptsMouseClicks (false, false);
    outputLabel.setMinimumHorizontalScale (0.68f);
    outputLabel.setText ("Choose a source file first.", juce::dontSendNotification);
    addAndMakeVisible (outputLabel);

    statusLabel.setFont (Theme::font (10.0f));
    statusLabel.setColour (juce::Label::textColourId, Theme::textDim);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setInterceptsMouseClicks (false, false);
    statusLabel.setMinimumHorizontalScale (0.72f);
    addAndMakeVisible (statusLabel);

    for (int i = 0; i < (int) (sizeof (panelPresets) / sizeof (panelPresets[0])); ++i)
        presetCombo.addItem (panelPresets[i].name, i + 1);
    presetCombo.addItem ("Custom", (int) (sizeof (panelPresets) / sizeof (panelPresets[0])) + 1);
    presetCombo.setTooltip ("Load a correction, retune and humanize preset.");
    presetCombo.onChange = [this] { applyPresetSelection(); };
    addAndMakeVisible (presetCombo);

    keyCombo.addItem ("Chromatic", 1);
    for (int i = 0; i < 12; ++i)
        keyCombo.addItem (autotune::getNoteName (i), i + 2);
    keyCombo.setTooltip ("Key used to choose the scale notes for preview and export.");
    keyCombo.onChange = [this]
    {
        updateKeyScaleState();
        updateControls();
        notifyPreviewSettings();
    };
    addAndMakeVisible (keyCombo);

    for (int i = 0; i < (int) autotune::getScales().size(); ++i)
        scaleCombo.addItem (autotune::getScales()[(size_t) i].name, i + 1);
    scaleCombo.setTooltip ("Scale notes allowed in preview and export.");
    scaleCombo.onChange = [this] { notifyPreviewSettings(); };
    addAndMakeVisible (scaleCombo);

    auto setupSlider = [] (juce::Slider& slider, double minimum, double maximum, double interval,
                           const juce::String& suffix, double value, int textBoxWidth)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, textBoxWidth, 22);
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);
        slider.setValue (value, juce::dontSendNotification);
        slider.setNumDecimalPlacesToDisplay (1);
        slider.setColour (juce::Slider::textBoxTextColourId, Theme::text);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, Theme::field);
        slider.setColour (juce::Slider::textBoxOutlineColourId, Theme::stroke);
    };

    setupSlider (correctionSlider, 0.0, 100.0, 1.0, "%", settingsToUse.strength * 100.0, 52);
    setupSlider (retuneSlider, 0.0, 250.0, 1.0, " ms", settingsToUse.retuneMs, 58);
    setupSlider (humanizeSlider, 0.0, 100.0, 1.0, "%", settingsToUse.humanize * 100.0, 52);
    setupSlider (transposeSlider, -12.0, 12.0, 1.0, " st", settingsToUse.transpose, 50);
    setupSlider (tuningSlider, 415.0, 466.0, 0.1, " Hz", settingsToUse.tuningA4, 64);
    setupSlider (inputGainSlider, -24.0, 24.0, 0.5, " dB", settingsToUse.inputGainDb, 58);

    addAndMakeVisible (correctionSlider);
    addAndMakeVisible (retuneSlider);
    addAndMakeVisible (humanizeSlider);
    addAndMakeVisible (transposeSlider);
    addAndMakeVisible (tuningSlider);
    addAndMakeVisible (inputGainSlider);

    for (juce::Slider* slider : { &correctionSlider, &retuneSlider, &humanizeSlider })
        slider->onValueChange = [this]
        {
            setPresetToCustom();
            notifyPreviewSettings();
        };

    for (juce::Slider* slider : { &transposeSlider, &tuningSlider, &inputGainSlider })
        slider->onValueChange = [this] { notifyPreviewSettings(); };

    playButton.getProperties().set ("accent", true);
    playButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    playButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    playButton.setColour (juce::TextButton::textColourOnId, Theme::background);
    playButton.setTooltip ("Play or pause the loaded file through the preview engine.");
    playButton.onClick = [this] { togglePreviewPlayback(); };
    addAndMakeVisible (playButton);

    for (auto* button : { &rewindButton, &forwardButton })
    {
        button->setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
        button->setColour (juce::TextButton::textColourOffId, Theme::textDim);
        button->setColour (juce::TextButton::textColourOnId, Theme::text);
    }

    rewindButton.setTooltip ("Jump back 10 seconds.");
    rewindButton.onClick = [this] { seekRelative (-10.0); };
    forwardButton.setTooltip ("Jump forward 10 seconds.");
    forwardButton.onClick = [this] { seekRelative (10.0); };
    addAndMakeVisible (rewindButton);
    addAndMakeVisible (forwardButton);

    auto configureModeButton = [] (juce::TextButton& button, const juce::String& tooltip)
    {
        button.setClickingTogglesState (false);
        button.setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
        button.setColour (juce::TextButton::textColourOffId, Theme::textDim);
        button.setColour (juce::TextButton::textColourOnId, Theme::background);
        button.setTooltip (tooltip);
    };

    configureModeButton (originalButton, "Hear the untouched source audio.");
    configureModeButton (tunedButton, "Hear the source processed with the settings below.");
    originalButton.onClick = [this] { setPreviewTuned (false); };
    tunedButton.onClick = [this] { setPreviewTuned (true); };
    addAndMakeVisible (originalButton);
    addAndMakeVisible (tunedButton);

    chooseButton.setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
    chooseButton.setColour (juce::TextButton::textColourOffId, Theme::accent);
    chooseButton.setTooltip ("Choose a source audio file. You can also drop one onto the waveform.");
    chooseButton.onClick = [this] { chooseAudioFile(); };
    addAndMakeVisible (chooseButton);

    browseOutputButton.setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
    browseOutputButton.setColour (juce::TextButton::textColourOffId, Theme::text);
    browseOutputButton.setTooltip ("Choose a different destination for the tuned WAV.");
    browseOutputButton.onClick = [this] { chooseOutputFile(); };
    addAndMakeVisible (browseOutputButton);

    matchLiveButton.setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
    matchLiveButton.setColour (juce::TextButton::textColourOffId, Theme::textDim);
    matchLiveButton.setTooltip ("Reset these export controls to the values currently used live.");
    matchLiveButton.onClick = [this] { resetToLiveSettings(); };
    addAndMakeVisible (matchLiveButton);

    closeButton.setColour (juce::TextButton::buttonColourId, Theme::panelRaised);
    closeButton.setColour (juce::TextButton::textColourOffId, Theme::text);
    closeButton.onClick = [this]
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->closeButtonPressed();
    };
    addAndMakeVisible (closeButton);

    exportButton.getProperties().set ("accent", true);
    exportButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    exportButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    exportButton.setTooltip ("Render the source file with these settings.");
    exportButton.onClick = [this]
    {
        if (exporting)
            cancelExport();
        else
            startExport();
    };
    addAndMakeVisible (exportButton);

    resetToLiveSettings();
    notifyPreviewSettings();
    previewPlayer.setTuned (previewTuned);
    updatePreviewButtons();
    updateControls();
    setStatus ("Choose an audio file or drop one here.");
    startTimerHz (15);
}

AudioFilePanel::~AudioFilePanel()
{
    stopTimer();
    previewPlayer.stop();
    previewPlayer.release();

    if (renderJob != nullptr)
    {
        renderJob->signalThreadShouldExit();
        renderJob->stopThread (3000);
        renderJob.reset();
    }
}

void AudioFilePanel::paint (juce::Graphics& g)
{
    juce::ColourGradient background (Theme::panel, 0.0f, 0.0f,
                                     Theme::background, 0.0f, (float) getHeight(), false);
    g.setGradientFill (background);
    g.fillRect (getLocalBounds());

    const int headerTextWidth = headerArea.getWidth();

    g.setColour (Theme::text);
    g.setFont (Theme::display (19.0f));
    g.drawText ("TUNE AUDIO FILE", headerArea.getX(), headerArea.getY(),
                headerTextWidth, 26, juce::Justification::centredLeft, false);

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (10.0f));
    g.drawText ("Review the waveform, shape the tuning, then export a tuned 24-bit WAV.",
                headerArea.getX(), headerArea.getY() + 26, headerTextWidth, 16,
                juce::Justification::centredLeft, false);

    auto sourceCard = sourceArea.toFloat();
    g.setColour (Theme::panelRaised);
    g.fillRoundedRectangle (sourceCard, 14.0f);
    g.setColour (dragOver ? Theme::accent : Theme::stroke);
    g.drawRoundedRectangle (sourceCard.reduced (0.5f), 14.0f, dragOver ? 1.5f : 1.0f);

    auto waveCard = waveformArea.toFloat();
    g.setColour (Theme::field);
    g.fillRoundedRectangle (waveCard, 9.0f);
    g.setColour (Theme::strokeSoft);
    g.drawRoundedRectangle (waveCard.reduced (0.5f), 9.0f, 1.0f);

    if (inputFile.existsAsFile())
    {
        auto wave = waveformArea.reduced (10, 16);

        if (thumbnail.getTotalLength() > 0.0)
        {
            g.setColour (Theme::accent.withAlpha (0.68f));
            thumbnail.drawChannels (g, wave, 0.0, thumbnail.getTotalLength(), 0.88f);
        }

        g.setColour (Theme::strokeSoft);
        g.fillRect (juce::Rectangle<float> ((float) wave.getX(), (float) wave.getCentreY(),
                                            (float) wave.getWidth(), 1.0f));

        if (durationSeconds > 0.0)
        {
            const double position = juce::jlimit (0.0, durationSeconds,
                                                  previewPlayer.getPositionSeconds());
            if (position > 0.0)
            {
                const float x = (float) wave.getX()
                                + (float) wave.getWidth() * (float) (position / durationSeconds);
                g.setColour (Theme::accent.withAlpha (0.85f));
                g.fillRect (x - 1.0f, (float) wave.getY(), 2.0f, (float) wave.getHeight());
                g.fillEllipse (x - 4.0f, (float) wave.getY() - 3.0f, 8.0f, 8.0f);
            }
        }

        if (! thumbnail.isFullyLoaded())
        {
            g.setColour (Theme::textFaint);
            g.setFont (Theme::font (8.5f, true));
            g.drawText ("READING WAVEFORM...", waveformArea.reduced (12, 8),
                        juce::Justification::topRight, false);
        }
    }
    else
    {
        const auto centre = waveformArea.getCentre();

        g.setColour (Theme::accent.withAlpha (0.24f));
        for (int i = 0; i < 17; ++i)
        {
            const float x = (float) centre.x - 48.0f + (float) i * 6.0f;
            const float height = 8.0f + 28.0f * std::abs (std::sin ((float) i * 0.9f));
            g.fillRoundedRectangle (x, (float) centre.y - 28.0f - height * 0.5f, 2.0f, height, 1.0f);
        }

        g.setColour (Theme::text);
        g.setFont (Theme::display (16.0f));
        g.drawText ("DROP AUDIO HERE", waveformArea.getX(), centre.y + 6,
                    waveformArea.getWidth(), 22, juce::Justification::centred, false);

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (9.5f));
        g.drawText ("or click the waveform to choose a file  ·  WAV / AIFF / FLAC / OGG / MP3",
                    waveformArea.getX() + 10, centre.y + 28, waveformArea.getWidth() - 20, 16,
                    juce::Justification::centred, false);
    }

    if (inputFile.existsAsFile() && durationSeconds > 0.0)
    {
        const double position = juce::jlimit (0.0, durationSeconds,
                                              previewPlayer.getPositionSeconds());
        const float progress = (float) (position / durationSeconds);

        g.setColour (Theme::field);
        g.fillRoundedRectangle (transportProgressArea.toFloat(), 2.0f);
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (transportProgressArea.toFloat()
                                    .withWidth ((float) transportProgressArea.getWidth() * progress), 2.0f);

        const float handleX = (float) transportProgressArea.getX()
                              + (float) transportProgressArea.getWidth() * progress;
        g.fillEllipse (handleX - 4.0f, (float) transportProgressArea.getCentreY() - 4.0f, 8.0f, 8.0f);

        g.setColour (Theme::textDim);
        g.setFont (Theme::mono (9.0f));
        g.drawText (formatDuration (position) + " / " + formatDuration (durationSeconds),
                    transportTimeArea, juce::Justification::centredRight, false);
    }

    drawSectionHeading (g, settingsHeadingArea, "TUNE SETTINGS");

    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (8.5f));
    g.drawText ("These settings affect this export only.",
                settingsHeadingArea.getX(), settingsHeadingArea.getY() + 5,
                settingsHeadingArea.getWidth(), settingsHeadingArea.getHeight() - 5,
                juce::Justification::centredRight, false);

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (8.5f, true));
    g.drawText ("EXPORT AS", outputArea.getX(), outputArea.getY(),
                outputArea.getWidth(), outputArea.getHeight(),
                juce::Justification::centredLeft, false);

    g.setColour (Theme::field);
    g.fillRoundedRectangle (outputLabel.getBounds().toFloat(), 8.0f);
    g.setColour (Theme::strokeSoft);
    g.drawRoundedRectangle (outputLabel.getBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);

    g.setColour (Theme::strokeSoft);
    g.fillRect (footerArea.getX(), footerArea.getY() - 12, footerArea.getWidth(), 1);

    if (exporting)
    {
        g.setColour (Theme::field);
        g.fillRoundedRectangle (progressArea.toFloat(), 2.0f);
        const float filled = (float) progressArea.getWidth() * (float) juce::jlimit (0.0, 1.0, exportProgress);
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (progressArea.toFloat().withWidth (filled), 2.0f);
    }
}

void AudioFilePanel::resized()
{
    auto outer = getLocalBounds().reduced (24);

    footerArea = outer.removeFromBottom (52);
    outer.removeFromBottom (10);

    headerArea = outer.removeFromTop (50);
    outer.removeFromTop (8);

    constexpr int fixedBelowSource = 18 + 2 + 48 + 46 + 46 + 10 + 42;
    constexpr int sourceGap = 12;
    const int sourceHeight = juce::jmax (220, outer.getHeight() - fixedBelowSource - sourceGap);
    sourceArea = outer.removeFromTop (sourceHeight);
    outer.removeFromTop (sourceGap);

    settingsHeadingArea = outer.removeFromTop (18);
    outer.removeFromTop (2);

    keyRowArea = outer.removeFromTop (48);
    firstSliderRow = outer.removeFromTop (46);
    secondSliderRow = outer.removeFromTop (46);
    outer.removeFromTop (10);
    outputArea = outer.removeFromTop (42);

    fileLabel.setBounds (sourceArea.getX() + 18, sourceArea.getY() + 12,
                         sourceArea.getWidth() - 18 - 120 - 30, 28);
    chooseButton.setBounds (sourceArea.getRight() - 18 - 120, sourceArea.getY() + 12, 120, 28);

    auto sourceContent = sourceArea.reduced (18, 14);
    sourceContent.removeFromTop (38);
    metadataArea = sourceContent.removeFromBottom (20);
    sourceContent.removeFromBottom (4);
    transportArea = sourceContent.removeFromBottom (32);
    sourceContent.removeFromBottom (6);
    waveformArea = sourceContent;
    infoLabel.setBounds (metadataArea);

    const int skipWidth = 50;
    const int playWidth = 70;
    const int modeWidth = 74;
    const int transportButtonY = transportArea.getY() + 2;
    int transportX = transportArea.getX();

    rewindButton.setBounds (transportX, transportButtonY + 1, skipWidth, 26);
    transportX += skipWidth + 6;
    playButton.setBounds (transportX, transportButtonY, playWidth, 28);
    transportX += playWidth + 6;
    forwardButton.setBounds (transportX, transportButtonY + 1, skipWidth, 26);
    transportX += skipWidth + 8;
    originalButton.setBounds (transportX, transportButtonY + 1, modeWidth, 26);
    transportX += modeWidth + 6;
    tunedButton.setBounds (transportX, transportButtonY + 1, modeWidth, 26);
    transportX += modeWidth + 10;

    transportTimeArea = { transportArea.getRight() - 112, transportArea.getY() + 6, 112, 22 };
    transportProgressArea = { transportX, transportArea.getY() + 15,
                              juce::jmax (20, transportTimeArea.getX() - transportX - 10), 4 };

    auto keyRow = keyRowArea;
    auto keyBlock = keyRow.removeFromLeft (130);
    keyRow.removeFromLeft (10);
    auto presetBlock = keyRow.removeFromLeft (170);
    keyRow.removeFromLeft (10);
    auto scaleBlock = keyRow.removeFromLeft (180);
    keyRow.removeFromLeft (10);
    auto matchBlock = keyRow.removeFromRight (100);
    keyRow.removeFromRight (10);
    auto transposeBlock = keyRow;

    auto layoutCombo = [] (juce::Rectangle<int> block, juce::Label& label, juce::ComboBox& combo)
    {
        label.setBounds (block.removeFromTop (12));
        block.removeFromTop (2);
        combo.setBounds (block.withHeight (30));
    };

    layoutCombo (keyBlock, keyLabel, keyCombo);
    layoutCombo (presetBlock, presetLabel, presetCombo);
    layoutCombo (scaleBlock, scaleLabel, scaleCombo);
    transposeLabel.setBounds (transposeBlock.removeFromTop (12));
    transposeBlock.removeFromTop (2);
    transposeSlider.setBounds (transposeBlock.withHeight (28));
    matchLiveButton.setBounds (matchBlock.withTrimmedTop (16).withHeight (28));

    auto layoutSlider = [] (juce::Rectangle<int> block, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (block.removeFromTop (12));
        block.removeFromTop (2);
        slider.setBounds (block.withHeight (28));
    };

    auto firstRow = firstSliderRow;
    const int firstWidth = (firstRow.getWidth() - 24) / 3;
    layoutSlider (firstRow.removeFromLeft (firstWidth), correctionLabel, correctionSlider);
    firstRow.removeFromLeft (12);
    layoutSlider (firstRow.removeFromLeft (firstWidth), retuneLabel, retuneSlider);
    firstRow.removeFromLeft (12);
    layoutSlider (firstRow, humanizeLabel, humanizeSlider);

    auto secondRow = secondSliderRow;
    const int secondWidth = (secondRow.getWidth() - 12) / 2;
    layoutSlider (secondRow.removeFromLeft (secondWidth), tuningLabel, tuningSlider);
    secondRow.removeFromLeft (12);
    layoutSlider (secondRow, inputGainLabel, inputGainSlider);

    browseOutputButton.setBounds (outputArea.getRight() - 88, outputArea.getY() + 7, 88, 28);
    outputLabel.setBounds (outputArea.getX() + 90, outputArea.getY() + 7,
                           outputArea.getWidth() - 90 - 98, 28);

    const int footerButtonHeight = 30;
    const int footerButtonY = footerArea.getY() + 11;
    closeButton.setBounds (footerArea.getX(), footerButtonY, 82, footerButtonHeight);
    exportButton.setBounds (footerArea.getRight() - 140, footerButtonY, 140, footerButtonHeight);

    const int statusX = closeButton.getRight() + 12;
    const int statusWidth = exportButton.getX() - statusX - 14;
    statusLabel.setBounds (statusX, footerArea.getY(), statusWidth, 18);
    progressArea = { statusX, footerArea.getY() + 24, statusWidth, 3 };

    repaint();
}

void AudioFilePanel::mouseDown (const juce::MouseEvent& event)
{
    if (exporting)
        return;

    const auto position = event.getPosition();
    const auto timelineHit = transportProgressArea.expanded (0, 10);

    if (timelineHit.contains (position))
    {
        seekTarget = SeekTarget::timeline;
        seekToPosition (position.toFloat());
        return;
    }

    if (! waveformArea.contains (position))
        return;

    if (! inputFile.existsAsFile())
    {
        chooseAudioFile();
        return;
    }

    seekTarget = SeekTarget::waveform;
    seekToPosition (position.toFloat());
}

void AudioFilePanel::mouseDrag (const juce::MouseEvent& event)
{
    if (seekTarget != SeekTarget::none && ! exporting)
        seekToPosition (event.position);
}

void AudioFilePanel::mouseUp (const juce::MouseEvent&)
{
    seekTarget = SeekTarget::none;
}

void AudioFilePanel::mouseMove (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();
    const bool overWaveform = waveformArea.contains (position);
    const bool overTimeline = transportProgressArea.expanded (0, 10).contains (position);

    setMouseCursor ((overWaveform || overTimeline) && ! exporting
                        ? juce::MouseCursor::PointingHandCursor
                        : juce::MouseCursor::NormalCursor);
}

void AudioFilePanel::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void AudioFilePanel::timerCallback()
{
    const bool nowPlaying = previewPlayer.isPlaying();

    if (nowPlaying != playbackWasPlaying)
    {
        playbackWasPlaying = nowPlaying;
        updatePreviewButtons();

        if (! nowPlaying && durationSeconds > 0.0
            && previewPlayer.getPositionSeconds() >= durationSeconds - 0.08)
        {
            setStatus ("Playback finished.");
        }

        repaint (sourceArea);
    }

    if (nowPlaying)
        repaint (sourceArea);

    if (exporting && renderJob != nullptr)
    {
        exportProgress = renderJob->progress.load (std::memory_order_relaxed);

        if (renderJob->finished.load (std::memory_order_acquire))
        {
            renderJob->stopThread (2000);
            const auto result = renderJob->result;
            renderJob.reset();

            exportProgress = 0.0;
            setExporting (false);

            if (result.cancelled)
            {
                setStatus ("Export cancelled.");
            }
            else if (! result.ok)
            {
                setStatus ("Export failed: " + result.error, true);
            }
            else
            {
                setStatus ("Exported " + outputFile.getFileName()
                           + "  ·  " + formatDuration (durationSeconds));
            }

            repaint();
        }
        else
        {
            statusLabel.setText ("Rendering... " + juce::String (juce::roundToInt (exportProgress * 100.0)) + "%",
                                 juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, Theme::textDim);
            repaint (progressArea);
        }
    }
    else if (inputFile.existsAsFile() && ! thumbnail.isFullyLoaded())
    {
        repaint (waveformArea);
    }
}

bool AudioFilePanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isSupportedAudioFile (juce::File (path)))
            return true;

    return false;
}

void AudioFilePanel::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint (sourceArea);
}

void AudioFilePanel::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint (sourceArea);
}

void AudioFilePanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint (sourceArea);

    if (! files.isEmpty())
        loadFile (juce::File (files[0]));
}

void AudioFilePanel::chooseAudioFile()
{
    if (exporting)
        return;

    auto folder = inputFile.existsAsFile()
                      ? inputFile.getParentDirectory()
                      : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

    const auto safeThis = juce::Component::SafePointer<AudioFilePanel> (this);
    openChooser = std::make_unique<juce::FileChooser> ("Choose audio to tune", folder,
                                                       audioFilePatterns, true, false, this);
    openChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (auto* panel = safeThis.getComponent())
        {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                panel->loadFile (file);
        }
    });
}

void AudioFilePanel::chooseOutputFile()
{
    if (exporting || ! inputFile.existsAsFile())
        return;

    const auto safeThis = juce::Component::SafePointer<AudioFilePanel> (this);
    saveChooser = std::make_unique<juce::FileChooser> ("Export tuned WAV",
                                                       outputFile.exists() ? outputFile
                                                                           : inputFile.getSiblingFile (inputFile.getFileNameWithoutExtension() + "-tuned.wav"),
                                                       "*.wav", true, false, this);
    saveChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        auto* panel = safeThis.getComponent();
        if (panel == nullptr)
            return;

        const auto chosen = chooser.getResult();
        if (chosen == juce::File{})
            return;

        const auto file = chosen.withFileExtension ("wav");
        if (file.getFullPathName().equalsIgnoreCase (panel->inputFile.getFullPathName()))
        {
            panel->setStatus ("Choose an output file different from the source.", true);
            return;
        }

        panel->outputFile = file;
        panel->outputLabel.setText (file.getFullPathName(), juce::dontSendNotification);
        panel->outputLabel.setTooltip (file.getFullPathName());
        panel->setStatus ("Ready to export.");
        panel->updateControls();
    });
}

void AudioFilePanel::loadFile (const juce::File& file)
{
    if (exporting || ! file.existsAsFile())
        return;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        setStatus ("That file could not be read. Use WAV, AIFF, FLAC, OGG or MP3.", true);
        return;
    }

    if (reader->numChannels < 1 || reader->numChannels > 2)
    {
        setStatus ("Only mono and stereo files are supported in this workbench.", true);
        return;
    }

    juce::String previewError;
    if (! previewPlayer.loadFile (file, previewError))
    {
        setStatus ("Preview unavailable: " + previewError, true);
        return;
    }

    playbackWasPlaying = false;
    playButton.setButtonText ("PLAY");
    previewPlayer.setTuned (previewTuned);

    inputFile = file;
    durationSeconds = reader->sampleRate > 0.0
                          ? (double) reader->lengthInSamples / reader->sampleRate
                          : 0.0;
    sampleRate = reader->sampleRate;
    numChannels = (int) reader->numChannels;
    bitsPerSample = reader->bitsPerSample;
    formatName = file.getFileExtension().trimCharactersAtStart (".").toUpperCase();

    thumbnail.clear();
    thumbnail.setSource (new juce::FileInputSource (file));

    fileLabel.setText (file.getFileName(), juce::dontSendNotification);
    fileLabel.setTooltip (file.getFullPathName());
    fileLabel.setVisible (true);
    infoLabel.setText (buildInfoText(), juce::dontSendNotification);
    infoLabel.setVisible (true);
    chooseButton.setButtonText ("CHANGE AUDIO");

    auto candidate = file.getSiblingFile (file.getFileNameWithoutExtension() + "-tuned.wav");
    int suffix = 2;
    while (candidate.existsAsFile())
        candidate = file.getSiblingFile (file.getFileNameWithoutExtension()
                                         + "-tuned-" + juce::String (suffix++) + ".wav");

    outputFile = candidate;
    outputLabel.setText (outputFile.getFullPathName(), juce::dontSendNotification);
    outputLabel.setTooltip (outputFile.getFullPathName());

    setStatus ("Ready to export. Adjust the settings or choose a different output.");
    updateControls();

    if (prewarmPreviewAudio)
    {
        const auto previewAudioError = previewPlayer.prepareForPlayback();
        if (previewAudioError.isNotEmpty())
            setStatus ("Ready to export. Preview unavailable: " + previewAudioError, true);
    }

    repaint();
}

void AudioFilePanel::resetToLiveSettings()
{
    suppressPresetChange = true;

    keyCombo.setSelectedId (liveSettings.keyIndex < 0 ? 1 : liveSettings.keyIndex + 2,
                            juce::dontSendNotification);

    const auto& scales = autotune::getScales();
    scaleCombo.setSelectedId (juce::jlimit (0, (int) scales.size() - 1, liveSettings.scaleIndex) + 1,
                              juce::dontSendNotification);

    correctionSlider.setValue (liveSettings.strength * 100.0, juce::dontSendNotification);
    retuneSlider.setValue (liveSettings.retuneMs, juce::dontSendNotification);
    humanizeSlider.setValue (liveSettings.humanize * 100.0, juce::dontSendNotification);
    transposeSlider.setValue (liveSettings.transpose, juce::dontSendNotification);
    tuningSlider.setValue (liveSettings.tuningA4, juce::dontSendNotification);
    inputGainSlider.setValue (liveSettings.inputGainDb, juce::dontSendNotification);

    int presetId = (int) (sizeof (panelPresets) / sizeof (panelPresets[0])) + 1;

    for (int i = 0; i < (int) (sizeof (panelPresets) / sizeof (panelPresets[0])); ++i)
        if (liveSettings.presetName.equalsIgnoreCase (panelPresets[i].name))
            presetId = i + 1;

    presetCombo.setSelectedId (presetId, juce::dontSendNotification);
    suppressPresetChange = false;

    updateKeyScaleState();
    updateControls();
    notifyPreviewSettings();

    if (inputFile.existsAsFile())
        setStatus ("Settings reset to the live values.");
}

void AudioFilePanel::applyPresetSelection()
{
    const int presetId = presetCombo.getSelectedId();
    const int presetCount = (int) (sizeof (panelPresets) / sizeof (panelPresets[0]));

    if (presetId < 1 || presetId > presetCount)
        return;

    const auto& preset = panelPresets[presetId - 1];
    suppressPresetChange = true;
    correctionSlider.setValue (preset.strength * 100.0, juce::dontSendNotification);
    retuneSlider.setValue (preset.retuneMs, juce::dontSendNotification);
    humanizeSlider.setValue (preset.humanize * 100.0, juce::dontSendNotification);
    suppressPresetChange = false;
    notifyPreviewSettings();
}

void AudioFilePanel::setPresetToCustom()
{
    if (suppressPresetChange)
        return;

    const int customId = (int) (sizeof (panelPresets) / sizeof (panelPresets[0])) + 1;
    if (presetCombo.getSelectedId() != customId)
        presetCombo.setSelectedId (customId, juce::dontSendNotification);
}

void AudioFilePanel::startExport()
{
    if (exporting)
        return;

    if (! inputFile.existsAsFile())
    {
        setStatus ("Choose a source audio file first.", true);
        return;
    }

    if (outputFile == juce::File{})
    {
        setStatus ("Choose an output file first.", true);
        return;
    }

    if (outputFile.getFullPathName().equalsIgnoreCase (inputFile.getFullPathName()))
    {
        setStatus ("Choose an output file different from the source.", true);
        return;
    }

    if (previewPlayer.isPlaying())
    {
        previewPlayer.pause();
        playbackWasPlaying = false;
        playButton.setButtonText ("PLAY");
    }

    setStatus ("Rendering tuned WAV...");
    exportProgress = 0.0;
    setExporting (true);
    renderJob = std::make_unique<RenderJob> (inputFile, outputFile, makeExportSettings());
    renderJob->startThread (juce::Thread::Priority::normal);
}

void AudioFilePanel::cancelExport()
{
    if (renderJob != nullptr)
    {
        renderJob->signalThreadShouldExit();
        setStatus ("Cancelling export...");
    }
}

void AudioFilePanel::setExporting (bool shouldBeExporting)
{
    exporting = shouldBeExporting;
    exportButton.setButtonText (exporting ? "CANCEL" : "EXPORT WAV");
    updateControls();
    repaint();
}

bool AudioFilePanel::isRenderFinished() const
{
    return renderJob != nullptr && renderJob->finished.load (std::memory_order_acquire);
}

void AudioFilePanel::updateKeyScaleState()
{
    const bool chromatic = keyCombo.getSelectedId() <= 1;
    scaleCombo.setEnabled (! exporting && ! chromatic);
    scaleCombo.setAlpha (chromatic ? 0.55f : 1.0f);
}

void AudioFilePanel::updateControls()
{
    const bool canEdit = ! exporting;
    const bool hasFile = inputFile.existsAsFile();

    presetCombo.setEnabled (canEdit);
    keyCombo.setEnabled (canEdit);
    correctionSlider.setEnabled (canEdit);
    retuneSlider.setEnabled (canEdit);
    humanizeSlider.setEnabled (canEdit);
    transposeSlider.setEnabled (canEdit);
    tuningSlider.setEnabled (canEdit);
    inputGainSlider.setEnabled (canEdit);

    chooseButton.setEnabled (canEdit);
    chooseButton.setButtonText (hasFile ? "CHANGE AUDIO" : "CHOOSE AUDIO");
    browseOutputButton.setEnabled (canEdit && hasFile);
    matchLiveButton.setEnabled (canEdit);
    exportButton.setEnabled (exporting || (hasFile && outputFile != juce::File{}));
    closeButton.setEnabled (true);

    playButton.setEnabled (canEdit && hasFile);
    rewindButton.setEnabled (canEdit && hasFile);
    forwardButton.setEnabled (canEdit && hasFile);
    originalButton.setEnabled (canEdit && hasFile);
    tunedButton.setEnabled (canEdit && hasFile);

    scaleCombo.setEnabled (canEdit && keyCombo.getSelectedId() > 1);
    scaleCombo.setAlpha (keyCombo.getSelectedId() > 1 ? 1.0f : 0.55f);
    outputLabel.setAlpha (hasFile ? 1.0f : 0.55f);

    updatePreviewButtons();
}

void AudioFilePanel::updatePreviewButtons()
{
    const bool playing = previewPlayer.isPlaying();
    playButton.setButtonText (playing ? "PAUSE" : "PLAY");

    auto styleModeButton = [] (juce::TextButton& button, bool selected)
    {
        button.setToggleState (selected, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonColourId,
                          selected ? Theme::accent : Theme::panelRaised);
        button.setColour (juce::TextButton::buttonOnColourId,
                          selected ? Theme::accent : Theme::panelRaised);
        button.setColour (juce::TextButton::textColourOffId,
                          selected ? Theme::background : Theme::textDim);
        button.setColour (juce::TextButton::textColourOnId,
                          selected ? Theme::background : Theme::textDim);
        button.repaint();
    };

    styleModeButton (originalButton, ! previewTuned);
    styleModeButton (tunedButton, previewTuned);
}

void AudioFilePanel::togglePreviewPlayback()
{
    if (! inputFile.existsAsFile())
        return;

    if (previewPlayer.isPlaying())
    {
        previewPlayer.pause();
        playbackWasPlaying = false;
        updatePreviewButtons();
        setStatus ("Preview paused.");
        repaint (sourceArea);
        return;
    }

    const auto error = previewPlayer.start();

    if (error.isNotEmpty())
    {
        playbackWasPlaying = false;
        updatePreviewButtons();
        setStatus ("Preview: " + error, true);
        return;
    }

    playbackWasPlaying = true;
    updatePreviewButtons();
    setStatus (previewTuned ? "Playing tuned preview." : "Playing original audio.");
    repaint (sourceArea);
}

void AudioFilePanel::setPreviewTuned (bool shouldBeTuned)
{
    previewTuned = shouldBeTuned;
    previewPlayer.setTuned (previewTuned);
    updatePreviewButtons();

    if (previewPlayer.isPlaying())
        setStatus (previewTuned ? "Playing tuned preview." : "Playing original audio.");
}

void AudioFilePanel::seekRelative (double seconds)
{
    if (! inputFile.existsAsFile() || durationSeconds <= 0.0)
        return;

    previewPlayer.seekToSeconds (juce::jlimit (0.0, durationSeconds,
                                               previewPlayer.getPositionSeconds() + seconds));
    playbackWasPlaying = previewPlayer.isPlaying();
    repaint (sourceArea);
}

void AudioFilePanel::seekToPosition (juce::Point<float> position)
{
    if (! inputFile.existsAsFile() || durationSeconds <= 0.0)
        return;

    const auto bounds = seekTarget == SeekTarget::timeline
                            ? transportProgressArea.toFloat()
                            : waveformArea.reduced (10, 16).toFloat();

    if (bounds.getWidth() <= 0.0f)
        return;

    const float fraction = juce::jlimit (0.0f, 1.0f,
                                         (position.x - bounds.getX()) / bounds.getWidth());
    previewPlayer.seekToSeconds ((double) fraction * durationSeconds);
    playbackWasPlaying = previewPlayer.isPlaying();
    repaint (sourceArea);
}

void AudioFilePanel::notifyPreviewSettings()
{
    previewPlayer.setSettings (makeExportSettings());
}

void AudioFilePanel::setStatus (const juce::String& message, bool isError)
{
    statusLabel.setText (message, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? Theme::danger : Theme::textDim);
    repaint (footerArea);
}

OfflineRenderer::Settings AudioFilePanel::makeExportSettings() const
{
    OfflineRenderer::Settings settings;
    settings.strength = (float) (correctionSlider.getValue() / 100.0);
    settings.retuneMs = (float) retuneSlider.getValue();
    settings.humanize = (float) (humanizeSlider.getValue() / 100.0);
    settings.transpose = (int) std::lround (transposeSlider.getValue());
    settings.tuningA4 = (float) tuningSlider.getValue();
    settings.inputGainDb = (float) inputGainSlider.getValue();
    settings.gateDb = liveSettings.gateDb;

    const int keyId = keyCombo.getSelectedId();

    if (keyId <= 1)
    {
        settings.keyRoot = 0;
        settings.scaleMask = 0x0FFFu;
    }
    else
    {
        settings.keyRoot = juce::jlimit (0, 11, keyId - 2);
        const auto& scales = autotune::getScales();
        const int scaleIndex = juce::jlimit (0, (int) scales.size() - 1,
                                             scaleCombo.getSelectedId() - 1);
        settings.scaleMask = scales[(size_t) scaleIndex].mask;
    }

    return settings;
}

juce::String AudioFilePanel::formatDuration (double seconds) const
{
    if (! std::isfinite (seconds) || seconds <= 0.0)
        return "0:00";

    const int total = (int) std::lround (seconds);
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

juce::String AudioFilePanel::buildInfoText() const
{
    juce::String info;
    info << formatDuration (durationSeconds)
         << "  ·  " << juce::String (sampleRate / 1000.0, 1) << " kHz"
         << "  ·  " << (numChannels == 1 ? "Mono" : "Stereo");

    if (bitsPerSample > 0)
        info << "  ·  " << (int) bitsPerSample << "-bit";

    if (formatName.isNotEmpty())
        info << "  ·  " << formatName;

    return info;
}

void AudioFilePanel::drawSectionHeading (juce::Graphics& g, juce::Rectangle<int> area,
                                         const juce::String& text) const
{
    g.setColour (Theme::strokeSoft);
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
    g.setColour (Theme::accent);
    g.fillRect (area.getX(), area.getY() + 1, 16, 2);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (8.5f, true));
    g.drawText (text, area.getX(), area.getY() + 6, area.getWidth(), area.getHeight() - 6,
                juce::Justification::centredLeft, false);
}
