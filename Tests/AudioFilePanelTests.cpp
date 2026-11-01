#include "AudioFilePanel.h"
#include "StudioLookAndFeel.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>

class AudioFilePanelProbe
{
public:
    static void loadFile (AudioFilePanel& panel, const juce::File& file) { panel.loadFile (file); }
    static bool waveformLoaded (const AudioFilePanel& panel) { return panel.thumbnail.isFullyLoaded(); }
    static juce::File inputFile (const AudioFilePanel& panel) { return panel.inputFile; }
    static juce::File outputFile (const AudioFilePanel& panel) { return panel.outputFile; }
    static void startExport (AudioFilePanel& panel) { panel.startExport(); }
    static bool exportFinished (const AudioFilePanel& panel) { return panel.isRenderFinished(); }
    static juce::Rectangle<int> playBounds (const AudioFilePanel& panel) { return panel.playButton.getBounds(); }
    static juce::Rectangle<int> closeBounds (const AudioFilePanel& panel) { return panel.closeButton.getBounds(); }
    static juce::Rectangle<int> browseBounds (const AudioFilePanel& panel) { return panel.browseOutputButton.getBounds(); }
    static juce::Rectangle<int> exportBounds (const AudioFilePanel& panel) { return panel.exportButton.getBounds(); }
    static bool playVisible (const AudioFilePanel& panel) { return panel.playButton.isVisible(); }
    static bool previewHasFile (const AudioFilePanel& panel) { return panel.previewPlayer.hasFile(); }
    static void selectPreset (AudioFilePanel& panel, int id)
    {
        panel.presetCombo.setSelectedId (id, juce::sendNotificationSync);
    }
    static double correction (const AudioFilePanel& panel) { return panel.correctionSlider.getValue(); }
    static double retune (const AudioFilePanel& panel) { return panel.retuneSlider.getValue(); }
    static double humanize (const AudioFilePanel& panel) { return panel.humanizeSlider.getValue(); }
};

namespace
{
    bool writeTestFile (const juce::File& file)
    {
        auto fileStream = std::make_unique<juce::FileOutputStream> (file);
        if (! fileStream->openedOk())
            return false;

        std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (48000.0)
                                 .withNumChannels (2)
                                 .withBitsPerSample (24);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return false;

        juce::AudioBuffer<float> buffer (2, 512);
        const double increment = 2.0 * 3.14159265358979323846 * 220.0 / 48000.0;
        double phase = 0.0;

        for (int position = 0; position < 48000 * 2; position += 512)
        {
            const int count = std::min (512, 48000 * 2 - position);

            for (int i = 0; i < count; ++i)
            {
                const float sample = 0.35f * (float) std::sin (phase);
                phase += increment;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            const float* channels[2] = { buffer.getReadPointer (0), buffer.getReadPointer (1) };
            if (! writer->writeFromFloatArrays (channels, 2, count))
                return false;
        }

        return writer->flush();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    StudioLookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    Theme::uiScale = 1.3f;

    auto testFolder = juce::File::getCurrentWorkingDirectory().getChildFile ("Temp")
                          .getChildFile ("audio-file-panel-tests");
    testFolder.deleteRecursively();

    if (! testFolder.createDirectory())
    {
        std::cerr << "FAIL: could not create test folder\n";
        return 1;
    }

    const auto inputFile = testFolder.getChildFile ("input.wav");
    if (! writeTestFile (inputFile))
    {
        std::cerr << "FAIL: could not create test audio\n";
        return 1;
    }

    auto panelSettings = AudioFilePanel::InitialSettings{};
    panelSettings.prewarmPreviewAudio = false;
    AudioFilePanel panel (panelSettings);
    panel.setSize (900, 772);
    AudioFilePanelProbe::loadFile (panel, inputFile);

    for (int i = 0; i < 50 && ! AudioFilePanelProbe::waveformLoaded (panel); ++i)
        juce::Thread::sleep (100);

    const auto resolvedInput = AudioFilePanelProbe::inputFile (panel);
    const auto resolvedOutput = AudioFilePanelProbe::outputFile (panel);

    if (resolvedInput != inputFile)
    {
        std::cerr << "FAIL: input file was not accepted\n";
        return 1;
    }

    if (resolvedOutput.getFileName() != "input-tuned.wav")
    {
        std::cerr << "FAIL: automatic output name was not generated\n";
        return 1;
    }

    AudioFilePanelProbe::selectPreset (panel, 2);

    if (std::abs (AudioFilePanelProbe::correction (panel) - 85.0) > 0.01
        || std::abs (AudioFilePanelProbe::retune (panel) - 12.0) > 0.01
        || std::abs (AudioFilePanelProbe::humanize (panel) - 10.0) > 0.01)
    {
        std::cerr << "FAIL: preset values were not applied\n";
        return 1;
    }

    AudioFilePanelProbe::selectPreset (panel, 1);

    const auto browseBounds = AudioFilePanelProbe::browseBounds (panel);
    const auto exportBounds = AudioFilePanelProbe::exportBounds (panel);

    if (browseBounds.intersects (exportBounds))
    {
        std::cerr << "FAIL: browse and export buttons overlap\n";
        return 1;
    }

    if (AudioFilePanelProbe::playBounds (panel).getHeight() > 30
        || AudioFilePanelProbe::closeBounds (panel).getHeight() > 30
        || exportBounds.getHeight() > 30)
    {
        std::cerr << "FAIL: dialog buttons are too tall\n";
        return 1;
    }

    auto artifacts = juce::File::getCurrentWorkingDirectory().getChildFile ("Artifacts");
    artifacts.createDirectory();
    const auto imageFile = artifacts.getChildFile ("audio-file-panel-final.png");
    imageFile.deleteFile();
    const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);

    if (auto stream = imageFile.createOutputStream())
    {
        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, *stream))
        {
            std::cerr << "FAIL: could not write panel snapshot\n";
            return 1;
        }

        stream->flush();
    }
    else
    {
        std::cerr << "FAIL: could not save panel snapshot\n";
        return 1;
    }

    if (! AudioFilePanelProbe::previewHasFile (panel))
    {
        std::cerr << "FAIL: preview player did not load the file\n";
        return 1;
    }

    AudioFilePanelProbe::startExport (panel);

    for (int i = 0; i < 100 && ! AudioFilePanelProbe::exportFinished (panel); ++i)
        juce::Thread::sleep (100);

    if (! AudioFilePanelProbe::exportFinished (panel))
    {
        std::cerr << "FAIL: panel export did not finish\n";
        return 1;
    }

    if (! resolvedOutput.existsAsFile() || resolvedOutput.getSize() == 0)
    {
        std::cerr << "FAIL: panel export did not create an output file\n";
        return 1;
    }

    std::cout << "AudioFilePanel tests passed: waveform="
              << (AudioFilePanelProbe::waveformLoaded (panel) ? "loaded" : "pending")
              << " export_bytes=" << resolvedOutput.getSize()
              << " play_bounds=" << AudioFilePanelProbe::playBounds (panel).toString()
              << " play_visible=" << (AudioFilePanelProbe::playVisible (panel) ? "yes" : "no")
              << " close_bounds=" << AudioFilePanelProbe::closeBounds (panel).toString()
              << " snapshot=" << imageFile.getFullPathName() << "\n";
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    return 0;
}
