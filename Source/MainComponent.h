#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "OverlayServer.h"
#include "PitchView.h"
#include "UiComponents.h"

#include <memory>
#include <array>
#include <cmath>

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct AppSettings
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
        float outputGainDb = 0.0f;
        bool monitor = true;
        bool bypass = false;
        int latencyMode = 1;
        juce::String preset = "Natural";
        int overlayPort = 39871;
        bool overlayKeyboard = true;
        bool overlayGrid = true;
        bool overlayTarget = true;
        juce::String overlayCaption;
    };

    struct PresetDefinition
    {
        const char* name;
        float strength;
        float retuneMs;
        float humanize;
    };

    void timerCallback() override;
    void setupControls();
    void initialiseAudio();
    void applySettingsToControls();
    void applySettingsToEngine();
    void loadSettings();
    void saveSettings();
    void showSettingsDialog();
    void showObsDialog();
    void showAudioFileDialog();
    void toggleRecording();
    void applyPreset (const juce::String& name);
    void markPresetCustom();
    void selectPresetItem (const juce::String& name);
    void setToast (const juce::String& message, bool isError = false);
    void updateStatusTexts (const AudioEngine::Snapshot& snapshot);
    void publishOverlayData();
    void publishOverlayState();
    juce::String buildOverlayUrl (bool demo = false) const;
    juce::File getAppDirectory() const;
    juce::File getSettingsFile() const;
    juce::File getDeviceStateFile() const;

    static const std::array<PresetDefinition, 5>& getPresets();

    AudioEngine engine;
    OverlayServer overlay;

    PitchView pitchView { engine };
    Dial dialCorrection { "CORRECTION", { 0.0, 100.0 }, 85.0, 85.0,
                          [] (double v) { return juce::String ((int) std::lround (v)) + "%"; }, true };
    Dial dialRetune { "RETUNE", { 0.0, 250.0 }, 12.0, 12.0,
                      [] (double v) { return juce::String ((int) std::lround (v)) + " ms"; }, false };
    Dial dialHumanize { "HUMANIZE", { 0.0, 100.0 }, 10.0, 10.0,
                        [] (double v) { return juce::String ((int) std::lround (v)) + "%"; }, false };

    Meter meterIn { "IN" };
    Meter meterOut { "OUT" };

    juce::ComboBox presetCombo;
    juce::ComboBox keyCombo;
    juce::ComboBox scaleCombo;

    juce::Slider transposeSlider;
    juce::Slider gainInSlider;
    juce::Slider gainOutSlider;

    PillToggle monitorToggle { "MONITOR" };
    PillToggle bypassToggle { "BYPASS" };

    RecordButton recordButton;
    juce::TextButton fileButton { "TUNE FILE" };
    juce::TextButton obsButton { "OBS LINK" };
    juce::TextButton setupButton { "SETUP" };

    juce::Label keyLabel;
    juce::Label scaleLabel;
    juce::Label transposeLabel;
    juce::Label gainInLabel;
    juce::Label gainOutLabel;
    juce::Label latencyLabel;
    juce::Label hintLabel;

    juce::TooltipWindow tooltipWindow { this, 700 };

    std::unique_ptr<juce::DialogWindow> settingsDialog;
    std::unique_ptr<juce::DialogWindow> obsDialog;
    std::unique_ptr<juce::DialogWindow> audioFileDialog;

    AppSettings settings;
    bool suppressCallbacks = false;
    bool audioStarting = true;
    bool audioInitialised = false;
    bool overlayStateDirty = true;
    uint64_t lastOverlaySequence = 0;
    int overlayPublishDivider = 0;

    juce::String statusLeft;
    juce::String statusMiddle;
    juce::String statusRight;
    juce::String lastEngineError;
    juce::String toastMessage;
    double toastUntilMs = 0.0;
    bool toastIsError = false;

    juce::Rectangle<int> viewArea;
    juce::Rectangle<int> sidebarArea;
    juce::Rectangle<int> topBarArea;
    juce::Rectangle<int> bottomBarArea;
    juce::Rectangle<int> brandArea;
    juce::Rectangle<int> presetLabelArea;
    juce::Rectangle<int> musicalHeadingArea;
    juce::Rectangle<int> ioHeadingArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
