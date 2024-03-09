#include "MainComponent.h"

MainComponent::MainComponent() {
	setSize(192, 120);
	setAudioChannels(1, 1);
	recordButton.setButtonText("Record");
	recordButton.setSize(160, 24);
	recordButton.setTopLeftPosition(16, 12);
	recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
	recordButton.onClick = [this] { recordButtonClicked(); };
	addAndMakeVisible(recordButton);

	playButton.setButtonText("Play");
	playButton.setSize(160, 24);
	playButton.setTopLeftPosition(16, 48);
	playButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
	playButton.onClick = [this] { playButtonClicked(); };
	addAndMakeVisible(playButton);

	replayButton.setButtonText("Replay");
	replayButton.setSize(160, 24);
	replayButton.setTopLeftPosition(16, 84);
	replayButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
	replayButton.onClick = [this] { if (this->state == Recorded) { replayButtonClicked(); }};
	addAndMakeVisible(replayButton);

	changeState(Ready);
}

MainComponent::~MainComponent() { shutdownAudio(); }

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
	setSampleRate(sampleRate);
	setSampleBuffer(new juce::AudioSampleBuffer);
	sampleBuffer->setSize(1, sampleRate * 10);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
	auto* device = deviceManager.getCurrentAudioDevice();
	auto activeInputChannels = device->getActiveInputChannels();
	auto activeOutputChannels = device->getActiveOutputChannels();
	auto maxInputChannels = activeInputChannels.getHighestBit() + 1;
	auto maxOutputChannels = activeOutputChannels.getHighestBit() + 1;
	auto bufferSize = bufferToFill.buffer->getNumSamples();

	if (duration_cast<milliseconds>((high_resolution_clock::now() - startingTime)).count() > 10000) {
		switch (state) {
		case Playing:
		case Recording:
			changeState(Recorded);
			break;

		case Replaying:
			changeState(Ready);
			break;
		}
	}

	for (auto channel = 0; channel < maxOutputChannels; ++channel) {
		if ((!activeInputChannels[channel] || !activeOutputChannels[channel]) || maxInputChannels == 0) {
			bufferToFill.buffer->clear(channel, bufferToFill.startSample, bufferToFill.numSamples);
		}
		else {
			auto actualInputChannel = channel % maxInputChannels;

			auto* inBuffer = bufferToFill.buffer->getReadPointer(actualInputChannel, bufferToFill.startSample);
			auto* outBuffer = bufferToFill.buffer->getWritePointer(channel, bufferToFill.startSample);

			auto channelReadPointer = bufferToFill.buffer->getReadPointer(channel);
			auto channelWritePointer = bufferToFill.buffer->getWritePointer(channel);

			float freq = 523.2511306;
			float amp = 0.1;
			float dPhasePerSample = 2 * juce::MathConstants<double>::pi * (freq / (float)sampleRate);
			float initPhase = 0;

			switch (state) {
			case Playing:
				for (int i = 0; i < bufferSize; ++i) { outBuffer[i] += amp * sin(dPhasePerSample * i + initPhase); }

			case Recording:
				if (writePointer + bufferSize < sampleBuffer->getNumSamples()) {
					sampleBuffer->copyFrom(channel, writePointer, channelReadPointer, bufferSize);
					setWritePointer(writePointer + bufferSize);
				}
				else {
					sampleBuffer->copyFrom(channel, writePointer, channelReadPointer,
						sampleBuffer->getNumSamples() - writePointer);
					setWritePointer(0);
					bufferToFill.buffer->clear();
				}
				break;

			case Replaying:
				if (readPointer + bufferSize < sampleBuffer->getNumSamples()) {
					bufferToFill.buffer->addFrom(channel, 0,
						sampleBuffer->getReadPointer(channel, readPointer), bufferSize);
					setReadPointer(readPointer + bufferSize);
				}
				else {
					bufferToFill.buffer->addFrom(channel, 0,
						sampleBuffer->getReadPointer(channel, readPointer),
						sampleBuffer->getNumSamples() - readPointer);
					setReadPointer(0);
					bufferToFill.buffer->clear();
				}
				break;

			default:
				bufferToFill.buffer->clear();
				break;
			}
		}
	}
}

void MainComponent::releaseResources() { delete sampleBuffer; }
