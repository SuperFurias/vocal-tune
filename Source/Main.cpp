#include "MainComponent.h"
#include "Platform.h"
#include "StudioLookAndFeel.h"
#include "Theme.h"

class VocalTuneApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Vocal Tune"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        lookAndFeel = std::make_unique<StudioLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : juce::DocumentWindow (name, Theme::background, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (980, 780, 3840, 2160);
            centreWithSize (1280, 860);
            setVisible (true);
            applyDarkTitleBar (*this);
        }

        void closeButtonPressed() override
        {
            if (auto* app = JUCEApplication::getInstance())
                app->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<StudioLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (VocalTuneApplication)
