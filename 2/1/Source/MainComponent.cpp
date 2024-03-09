#include "MainComponent.h"

MainComponent::MainComponent() {
	setSize(192, 156);
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

	saveButton.setButtonText("Save");
	saveButton.setSize(160, 24);
	saveButton.setTopLeftPosition(16, 120);
	saveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
	saveButton.onClick = [this] { saveButtonClicked(); };
	addAndMakeVisible(saveButton);

	changeState(Ready);
}

MainComponent::~MainComponent() { shutdownAudio(); }

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
	carrierWave.clear();
	zeroWave.clear();
	for (int i = 0; i < BIT_WIDTH; ++i) {
		carrierWave.push_back(0.5 * cos(2 * juce::MathConstants<double>::pi * FREQ * i / 48000));
		zeroWave.push_back(0.5 * cos(2 * juce::MathConstants<double>::pi * FREQ * i / 48000 + juce::MathConstants<double>::pi));
	}
	preambleWave.clear();
	float phase = 0;
	for (int j = 0; j < PREAMBLE_LENGTH; ++j) {
		preambleWave.push_back(cos(phase));
		phase += juce::MathConstants<double>::pi * 2 * (PREAMBLE_FREQ_BEGIN + j * (PREAMBLE_FREQ_END - PREAMBLE_FREQ_BEGIN) / (PREAMBLE_LENGTH - 1)) / 48000;
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
			char out = 0;
			switch (state) {
			case Sending:
				bufferToFill.buffer->clear();
				for (int i = 0; i < bufferSize; ++i, ++readPointer) {
					if (readPointer < sentData.size())
						bufferToFill.buffer->addSample(channel, i, sentData[readPointer]);
					else {
						setReadPointer(0);
						changeState(Ready);
						break;
					}
				}
				break;

			case Receiving:
				for (int i = 0; i < bufferSize; i++)
					receivedData.push_back(bufferToFill.buffer->getSample(channel, i));
				while (!begin && receivedData.size() >= PREAMBLE_LENGTH) {
					sum = 0;
					for (int j = 0; j < PREAMBLE_LENGTH; ++j)
						sum += receivedData[j] * preambleWave[j];
					if (sum > maxSum) {
						maxSum = sum;
						lowerTicks = 0;
						preambleSuspected = true;
					}
					else if (preambleSuspected && ++lowerTicks >= PREAMBLE_LENGTH) {
						begin = true;
						preambleSuspected = false;
						break;
					}
					receivedData.pop_front();
				}
				while (begin && receivedData.size()) {
					outputData.push_back(receivedData.front());
					receivedData.pop_front();
					if (++bitsReceived == PACKAGE_BITS * BIT_WIDTH) {
						maxSum = SUM_THRESHOLD;
						bitsReceived = 0;
						begin = false;
						if (++packagesReceived == BIT_NUM / PACKAGE_BITS)
							changeState(Received);
						break;
					}
				}
				bufferToFill.buffer->clear();
				break;

			default:
				bufferToFill.buffer->clear();
				break;
			}
		}
	}
}

void MainComponent::releaseResources() { delete sampleBuffer; }