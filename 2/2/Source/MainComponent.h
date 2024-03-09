#pragma once

#include <JuceHeader.h>
#include <fstream>
#include <vector>
#include <deque>

using namespace std;
using namespace juce;

#define BIT_WIDTH 8
#define PREAMBLE_LENGTH 80
#define PREAMBLE_FREQ_BEGIN 5000
#define PREAMBLE_FREQ_END 10000
#define BIT_NUM 50000
#define SUM_THRESHOLD 15
#define FREQ 600
#define PACKAGE_BITS 400
#define PACKAGE_LENGTH ((PACKAGE_BITS*BIT_WIDTH)+PREAMBLE_LENGTH)
#define DELAY_BITS 1200


class MainComponent : public juce::AudioAppComponent {
public:
	MainComponent();

	~MainComponent() override;

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;

	void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

	void releaseResources() override;

private:
	enum TransportState {
		Ready, ToSend, Sending, ToReceive, Receiving, Received, AckSending, AckReceiving, Timeout
	};

	void changeState(TransportState newState) {
		if (state != newState) {
			switch (newState) {
			case Ready:
				openButton.setEnabled(true);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(false);
				receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
				receiveButton.setButtonText("Receive");
				saveButton.setEnabled(false);
				break;

			case ToSend:
				openButton.setEnabled(true);
				sendButton.setEnabled(true);
				sendButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
				sendButton.setButtonText("Send");
				receiveButton.setEnabled(false);
				saveButton.setEnabled(false);
				break;

			case ToReceive:
				openButton.setEnabled(true);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(true);
				saveButton.setEnabled(false);
				break;

			case Sending:
				openButton.setEnabled(false);
				sendButton.setEnabled(false);
				if (state == Ready)
					receiveButton.setEnabled(false);
				saveButton.setEnabled(false);
				break;

			case Receiving:
				openButton.setEnabled(false);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(true);
				saveButton.setEnabled(false);
				break;

			case Received:
				openButton.setEnabled(false);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(false);
				saveButton.setEnabled(true);
				break;

			case Timeout:
				openButton.setEnabled(true);
				sendButton.setEnabled(false);
				sendButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
				sendButton.setButtonText("Timeout");
				receiveButton.setEnabled(false);
				receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
				receiveButton.setButtonText("Receive");
				saveButton.setEnabled(false);
				break;

			case AckSending:
			case AckReceiving:
			default:
				break;
			}
			state = newState;
		}
	}

	void openButtonClicked() {
		changeState(Ready);
		sentData.clear();
		auto chooser = new juce::FileChooser("Select input file or output directory ...", File::getSpecialLocation(File::SpecialLocationType::userDesktopDirectory));
		auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories;

		chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
			auto file = fc.getResult();
			int count = 0;
			if (file != juce::File{}) {
				auto path = fc.getResult().getFullPathName();

				if (path.contains(".bin")) {
					char c;
					ifstream inputfile(path.toStdString(), ios::in | ios::binary);
					char input[BIT_NUM / 8];
					sentData.clear();
					for (int i = 0; i < BIT_NUM / 8; ++i) {
						if (i % (PACKAGE_BITS / 8) == 0) {
							for (int j = 0; j < DELAY_BITS; ++j)
								sentData.push_back(0);
							sentData.insert(sentData.end(), preambleWave.begin(), preambleWave.end());
						}
						inputfile.get(c);
						for (int j = 7; j >= 0; --j) {
							if (c >> j & 1)
								sentData.insert(sentData.end(), carrierWave.begin(), carrierWave.end());
							else
								sentData.insert(sentData.end(), zeroWave.begin(), zeroWave.end());
						}
					}
					inputfile.close();
					changeState(ToSend);
				}
				else if (!path.contains(".")) {
					outpath = path.toStdString() + "/OUTPUT.bin";
					changeState(ToReceive);
				}
			}});
	}

	void sendButtonClicked() {
		isReceiving = false;
		changeState(Sending);
	}

	void receiveButtonClicked() {
		isReceiving = true;
		outputData.clear();
		switch (state)
		{
		case ToReceive:
		{
			receivedData.clear();
			receiveButton.setButtonText("Stop");
			receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
			changeState(Receiving);
			break;
		}

		case Receiving:
			receiveButton.setButtonText("Receive");
			receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
			changeState(Ready);
			break;
		}
	}

	void saveButtonClicked() {
		char out = 0;
		int bits = 0;
		float sum = 0;
		ofstream outputFile(outpath, ios::out | ios::binary);
		for (int p = 0; p < BIT_NUM; p++) {
			sum = 0;
			out <<= 1;
			for (int i = 0; i < BIT_WIDTH; ++i)
				sum += outputData[p * BIT_WIDTH + i];
			out += sum > 0;
			if (++bits % 8 == 0) {
				output += out;
				out = 0;
			}
		}
		outputFile << output;
		changeState(Ready);
	}

	void setSampleBuffer(juce::AudioSampleBuffer* sampleBuffer) { this->sampleBuffer = sampleBuffer; }

	void setSampleRate(double sampleRate) { this->sampleRate = sampleRate; }

	void setReadPointer(int readPointer) { this->readPointer = readPointer; }

	void setWritePointer(int writePointer) { this->writePointer = writePointer; }

	TransportState state;
	TextButton openButton, sendButton, receiveButton, saveButton;
	AudioSampleBuffer* sampleBuffer = nullptr;
	double sampleRate;
	int readPointer = 0, writePointer = 0;

	vector<float> carrierWave, zeroWave;
	vector<float> sentData, outputData;
	deque<float> receivedData;
	float sum = 0, maxSum = SUM_THRESHOLD;
	int lowerTicks = 0, timeoutTicks = 0;
	int bitsReceived = 0, packagesReceived = 0;
	bool begin = false, preambleSuspected = false;
	vector<float> preambleWave, ackWave;
	bool isReceiving;
	string outpath, output = "";

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
