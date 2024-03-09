#include "MainComponent.h"

MainComponent::MainComponent() {
	setSize(192, 120);
	setAudioChannels(1, 1);

	openButton.setButtonText("Open...");
	openButton.setSize(160, 24);
	openButton.setTopLeftPosition(16, 12);
	openButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
	openButton.onClick = [this] { openButtonClicked(); };
	addAndMakeVisible(openButton);

	sendButton.setButtonText("Send");
	sendButton.setSize(160, 24);
	sendButton.setTopLeftPosition(16, 48);
	sendButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
	sendButton.onClick = [this] { sendButtonClicked(); };
	addAndMakeVisible(sendButton);

	receiveButton.setButtonText("Receive");
	receiveButton.setSize(160, 24);
	receiveButton.setTopLeftPosition(16, 84);
	receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
	receiveButton.onClick = [this] { receiveButtonClicked(); };
	addAndMakeVisible(receiveButton);

	changeState(Ready);
}

MainComponent::~MainComponent() { shutdownAudio(); }

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
	carrierWave.clear();
	zeroWave.clear();
	for (int i = 0; i < BIT_WIDTH; ++i) {
		carrierWave.push_back(cos(2 * juce::MathConstants<double>::pi * FREQ * i / 48000));
		zeroWave.push_back(cos(2 * juce::MathConstants<double>::pi * FREQ * i / 48000 + juce::MathConstants<double>::pi));
	}
	receivedData.clear();
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
	auto* device = deviceManager.getCurrentAudioDevice();
	auto activeInputChannels = device->getActiveInputChannels();
	auto activeOutputChannels = device->getActiveOutputChannels();
	auto maxInputChannels = activeInputChannels.getHighestBit() + 1;
	auto maxOutputChannels = activeOutputChannels.getHighestBit() + 1;
	auto bufferSize = bufferToFill.buffer->getNumSamples();

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

			switch (state) {
			case Sending:
				bufferToFill.buffer->clear();
				for (int i = 0; i < bufferSize; ++i, ++readPointer) {
					if (readPointer < sentData.size())
						bufferToFill.buffer->addSample(channel, i, sentData[readPointer]);
					else {
						setReadPointer(0);
						changeState(Ready);
					}
				}
				break;

			case Receiving:
				for (int i = 0; i < bufferSize; i++) {
					receivedData.push_back(bufferToFill.buffer->getSample(channel, i));
				}
				while (receivedData.size() >= PREAMBLE_LENGTH && !begin) {
					sum = 0;
					for (int j = 0; j < PREAMBLE_LENGTH; j++) {
						sum += receivedData[j] * preambleWave[j];
					}
					if (sum>maxSum) {
						maxSum = sum;
						lowerTicks = 0;
						preambleSuspected = true;
					}
					else if (preambleSuspected && ++lowerTicks >= 480) {
						begin = true;
					}

					receivedData.pop_front();
				}
				while (begin && receivedData.size() >= BIT_WIDTH) {
					sum = 0;
					for (int i = 0; i < BIT_WIDTH; i++) {
						sum += receivedData[0] * carrierWave[i];
						receivedData.pop_front();
					}
					outputFile.appendText(sum < 0 ? "0" : "1");
					if (++bitsReceived == BIT_NUM) {
						changeState(Ready);
						begin = false;
						break;
					}
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
