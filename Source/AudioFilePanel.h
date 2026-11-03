#pragma once

#include "AudioPreviewPlayer.h"
#include "OfflineRenderer.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

class AudioFilePanel : public juce::Component,
                       private juce::Timer,
                       private juce::FileDragAndDropTarget
{
public:
    struct InitialSettings
    {
        float strength = 0.62f;
        float retuneMs = 35.0f;
        float humanize = 0.22f;
        int keyIndex = -1;
        int scaleIndex = 0;
        int transpose = 0;
        float tuningA4 = 440.0f;
        float gateDb = -55.0f;
        float inputGainDb = 0.0f;
        juce::String presetName = "Natural";
        bool prewarmPreviewAudio = true;
    };

    explicit AudioFilePanel (InitialSettings settingsToUse);
    ~AudioFilePanel() override;

    InitialSettings getCurrentSettings() const;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    class RenderJob;

    void timerCallback() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void chooseAudioFile();
    void chooseOutputFile();
    void loadFile (const juce::File& file);
    void applySettings (const InitialSettings& settingsToApply);
    void resetToDefaults();
    void applyPresetSelection();
    void setPresetToCustom();
    void startExport();
    void cancelExport();
    void setExporting (bool shouldBeExporting);
    bool isRenderFinished() const;
    void updateKeyScaleState();
    void updateControls();
    void updatePreviewButtons();
    void togglePreviewPlayback();
    void setPreviewTuned (bool shouldBeTuned);
    void seekRelative (double seconds);
    void seekToPosition (juce::Point<float> position);
    void notifyPreviewSettings();
    void setStatus (const juce::String& message, bool isError = false);
    OfflineRenderer::Settings makeExportSettings() const;
    juce::String formatDuration (double seconds) const;
    juce::String buildInfoText() const;
    void drawSectionHeading (juce::Graphics&, juce::Rectangle<int>, const juce::String&) const;

    InitialSettings initialSettings;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 8 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };

    juce::File inputFile;
    juce::File outputFile;
    juce::String formatName;
    double durationSeconds = 0.0;
    double sampleRate = 0.0;
    int numChannels = 0;
    unsigned int bitsPerSample = 0;

    juce::ComboBox presetCombo;
    juce::ComboBox keyCombo;
    juce::ComboBox scaleCombo;
    juce::Slider correctionSlider;
    juce::Slider retuneSlider;
    juce::Slider humanizeSlider;
    juce::Slider transposeSlider;
    juce::Slider tuningSlider;
    juce::Slider inputGainSlider;

    juce::Label presetLabel;
    juce::Label keyLabel;
    juce::Label scaleLabel;
    juce::Label transposeLabel;
    juce::Label correctionLabel;
    juce::Label retuneLabel;
    juce::Label humanizeLabel;
    juce::Label tuningLabel;
    juce::Label inputGainLabel;
    juce::Label fileLabel;
    juce::Label infoLabel;
    juce::Label outputLabel;
    juce::Label statusLabel;

    juce::TextButton rewindButton { "-10s" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton forwardButton { "+10s" };
    juce::TextButton originalButton { "ORIGINAL" };
    juce::TextButton tunedButton { "TUNED" };
    juce::TextButton chooseButton { "CHOOSE AUDIO" };
    juce::TextButton browseOutputButton { "BROWSE" };
    juce::TextButton resetButton { "RESET" };
    juce::TextButton closeButton { "CLOSE" };
    juce::TextButton exportButton { "EXPORT WAV" };

    std::unique_ptr<juce::FileChooser> openChooser;
    std::unique_ptr<juce::FileChooser> saveChooser;
    std::unique_ptr<RenderJob> renderJob;
    AudioPreviewPlayer previewPlayer;

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> sourceArea;
    juce::Rectangle<int> waveformArea;
    juce::Rectangle<int> transportArea;
    juce::Rectangle<int> transportProgressArea;
    juce::Rectangle<int> transportTimeArea;
    juce::Rectangle<int> metadataArea;
    juce::Rectangle<int> settingsHeadingArea;
    juce::Rectangle<int> keyRowArea;
    juce::Rectangle<int> firstSliderRow;
    juce::Rectangle<int> secondSliderRow;
    juce::Rectangle<int> outputArea;
    juce::Rectangle<int> footerArea;
    juce::Rectangle<int> progressArea;

    bool exporting = false;
    bool dragOver = false;
    bool previewTuned = true;
    bool playbackWasPlaying = false;
    enum class SeekTarget { none, waveform, timeline };

    bool suppressPresetChange = false;
    bool prewarmPreviewAudio = true;
    SeekTarget seekTarget = SeekTarget::none;
    double exportProgress = 0.0;

    juce::TooltipWindow tooltipWindow { this, 700 };

    friend class AudioFilePanelProbe;
};
