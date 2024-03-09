#pragma once

#include <JuceHeader.h>
#include <chrono>

using namespace std::chrono;

class MainComponent : public juce::AudioAppComponent {
public:
    MainComponent();

    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    void releaseResources() override;

private:
    enum TransportState {
        Ready, Recording, Playing, Recorded, Replaying
    };

    void changeState(TransportState newState) {
        if (state != newState) {
            state = newState;

            switch (state) {
            case Ready:
                recordButton.setEnabled(true);
                playButton.setEnabled(true);
                replayButton.setEnabled(false);
                break;

            case Recording:
            case Playing:
            case Replaying:
                recordButton.setEnabled(false);
                playButton.setEnabled(false);
                replayButton.setEnabled(false);
                break;

            case Recorded:
                recordButton.setEnabled(true);
                playButton.setEnabled(true);
                replayButton.setEnabled(true);
                break;
            }
        }
    }

    void recordButtonClicked() {
        changeState(Recording);
        setStartingTime(high_resolution_clock::now());
    }

    void playButtonClicked() {
        changeState(Playing);
        setStartingTime(high_resolution_clock::now());
    }

    void replayButtonClicked() {
        changeState(Replaying);
        setStartingTime(high_resolution_clock::now());
    }

    void setSampleBuffer(juce::AudioSampleBuffer* sampleBuffer) { this->sampleBuffer = sampleBuffer; }

    void setSampleRate(double sampleRate) { this->sampleRate = sampleRate; }

    void setReadPointer(int readPointer) { this->readPointer = readPointer; }

    void setWritePointer(int writePointer) { this->writePointer = writePointer; }

    void setStartingTime(high_resolution_clock::time_point startingTime) { this->startingTime = startingTime; }

    TransportState state;
    juce::TextButton recordButton, playButton, replayButton;
    juce::AudioSampleBuffer* sampleBuffer = nullptr;
    double sampleRate;
    int readPointer = 0, writePointer = 0;
    high_resolution_clock::time_point startingTime;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
