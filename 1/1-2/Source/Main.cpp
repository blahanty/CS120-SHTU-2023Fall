#include <JuceHeader.h>
#include "MainComponent.h"

class CS120_1_1_2 : public juce::JUCEApplication {
public:
    CS120_1_1_2() = default;

    const juce::String getApplicationName() override { return "CS120_1_1_2"; }

    const juce::String getApplicationVersion() override { return "1.14.514"; }

    void initialise(const juce::String& commandLine) override {
        mainWindow.reset(new MainWindow(getApplicationName(), new MainComponent, *this));
    }

    void shutdown() override {
        mainWindow = nullptr;
    }

    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String& name, juce::Component* c, JUCEApplication& a) :
            DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel()
                .findColour(ResizableWindow::backgroundColourId),
                juce::DocumentWindow::allButtons), app(a) {
            setUsingNativeTitleBar(true);
            setContentOwned(c, true);
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override {
            app.systemRequestedQuit();
        }

    private:
        JUCEApplication& app;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr <MainWindow> mainWindow;
};

START_JUCE_APPLICATION(CS120_1_1_2)
