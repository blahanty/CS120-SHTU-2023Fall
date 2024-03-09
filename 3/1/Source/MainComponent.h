#pragma once

#include <JuceHeader.h>
#include "Protocol.h"
#include <fstream>
#include <vector>
#include <deque>
#include <chrono>
#include <numeric>
#include <algorithm>

using namespace juce;
using namespace std;
using namespace std::chrono;

#define BIT_WIDTH 8
#define BYTE_NUM 28
#define FREQ 600
#define PREAMBLE_LENGTH 520
#define PREAMBLE_FREQ_BEGIN 5000
#define PREAMBLE_FREQ_END 10000
#define SUM_THRESHOLD 3
#define DELAY_BITS 0
#define NODE1_IP 0xAC120102
#define NODE2_IP 0xAC120101
#define NODE2_IP_STR "172.18.1.1"

class MainComponent : public juce::AudioAppComponent {
public:
	MainComponent();

	~MainComponent() override;

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;

	void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

	void releaseResources() override;

private:
	enum TransportState {
		Ready, ToRun, Sending, Receiving, ReplySending, ReplyReceiving, Waiting
	};

	void changeState(TransportState newState) {
		if (state != newState) {
			state = newState;

			switch (state) {
			case Ready:
				hostButton.setEnabled(true);
				runButton.setEnabled(false);
				routerButton.setEnabled(true);
				break;

			case ToRun:
				hostButton.setEnabled(true);
				runButton.setEnabled(true);
				routerButton.setEnabled(false);
				break;

			case Sending:
			case Receiving:
			case ReplySending:
			case ReplyReceiving:
			case Waiting:
				hostButton.setEnabled(false);
				runButton.setEnabled(false);
				routerButton.setEnabled(false);
				break;

			default:
				break;
			}
		}
	}

	void hostButtonClicked() {
		changeState(Ready);
		sentData.clear();
		auto chooser = new juce::FileChooser("Select command ...", File::getSpecialLocation(File::SpecialLocationType::userDesktopDirectory), "*.txt");
		auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

		chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
			auto file = fc.getResult();
			bool valid = false;
			if (file != juce::File{} && file.getFullPathName().contains(".txt"))
				valid = parseArgs(file.getFullPathName().toStdString());
			if (valid) {
				auto timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
				uint16_t id = timestamp & 0xffff;
				iph = new IPHeader(BYTE_NUM, id, NODE1_IP, NODE2_IP);
				icmph = new ICMPHeader(Echo, cmd, 1);
				generateSentData(false);
				changeState(ToRun);
			}
			});
	}

	void runButtonClicked() {
		ofstream outputFile(outputPath);
		outputFile.close();
		changeState(Sending);
		startTime = high_resolution_clock::now();
	}

	void routerButtonClicked() {
		sentData.clear();
		receivedData.clear();
		originalData.clear();
		changeState(Receiving);
	}

	void setSampleBuffer(juce::AudioSampleBuffer* sampleBuffer) { this->sampleBuffer = sampleBuffer; }

	void setSampleRate(double sampleRate) { this->sampleRate = sampleRate; }

	void setReadPointer(int readPointer) { this->readPointer = readPointer; }

	void setWritePointer(int writePointer) { this->writePointer = writePointer; }

	bool parseArgs(string file) {
		ifstream inputfile(file);
		string token;
		inputfile >> token;
		if (token != "ping") {
			inputfile.close();
			return false;
		}
		cmd.setCmdName("ping");

		inputfile >> token;
		if (token != NODE2_IP_STR) {
			inputfile.close();
			return false;
		}
		cmd.setDestPort(3);

		for (int i = 0; i < 2; ++i) {
			inputfile >> token;
			if (token == "-i") {
				inputfile >> token;
				echoInterval = stod(token);
				cmd.setEchoInterval(echoInterval);
			}
			else if (token == "-n") {
				inputfile >> token;
				echoNum = stoi(token);
				cmd.setEchoNum(echoNum);
			}
			else {
				inputfile.close();
				return false;
			}
		}

		inputfile.close();
		return true;
	}

	void generateSentDataFromByte(uint8_t byte) {
		for (int i = 7; i >= 0; --i)
			if (byte >> i & 1)
				sentData.insert(sentData.end(), carrierWave.begin(), carrierWave.end());
			else
				sentData.insert(sentData.end(), zeroWave.begin(), zeroWave.end());
	}

	void generateSentData(bool isPreambleReversed) {
		sentData.clear();
		for (int i = 0; i < DELAY_BITS; ++i)
			sentData.push_back(0);

		if (isPreambleReversed)
			sentData.insert(sentData.end(), preambleWave2.begin(), preambleWave2.end());
		else
			sentData.insert(sentData.end(), preambleWave1.begin(), preambleWave1.end());

		for (int i = 0; i < iph->getIHL(); ++i)
			for (int j = 0; j < 4; ++j)
				generateSentDataFromByte(iph->getByte(i, j));

		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 4; ++j)
				generateSentDataFromByte(icmph->getByte(i, j));
	}

	string IPAddrToStr(uint32_t addr) {
		string IPStr = "";
		for (int i = 3; i > 0; --i)
			IPStr += (to_string((addr >> (i * 8) & 0xff)) + ".");
		IPStr += to_string(addr & 0xff);
		return IPStr;
	}

	uint32_t dequeToHex(int bits) {
		uint32_t result = 0;
		for (int i = 0; i < bits; ++i) {
			result <<= 1;
			if (originalData.front()) { ++result; }
			originalData.pop_front();
		}

		return result;
	}

	TransportState state;
	TextButton hostButton, runButton, routerButton;
	AudioSampleBuffer* sampleBuffer = nullptr;
	double sampleRate;
	int readPointer = 0, writePointer = 0;

	vector<float> carrierWave, zeroWave;
	vector<float> preambleWave1, preambleWave2;
	vector<float> sentData;
	deque<float> receivedData, originalData;
	string outputPath = File::getSpecialLocation(File::SpecialLocationType::userDesktopDirectory).getFullPathName().toStdString() + "/OUTPUT.txt";;
	bool begin = false, preambleSuspected = false;
	float sum = 0, maxSum = SUM_THRESHOLD;
	int lowerTicks = 0;
	int bitsReceived = 0;

	Command cmd;
	IPHeader* iph = nullptr;
	ICMPHeader* icmph = nullptr;
	double echoInterval;
	int echoNum;
	int echoReceivedNum = 0;
	vector<int> rtt;

	time_point<high_resolution_clock> startTime, endTime;
	long long duration;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
