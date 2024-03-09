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
#define BYTE_NUM 512
#define FREQ 600
#define PREAMBLE_LENGTH 520
#define PREAMBLE_FREQ_BEGIN 5000
#define PREAMBLE_FREQ_END 10000
#define SUM_THRESHOLD 3
#define DELAY_BITS 0
#define NODE1_AETHERNET_IP 0xAC120102
#define NODE2_AETHERNET_IP 0xAC120101
#define NODE2_WIFI_IP 0xC0A81F72
#define DNS_WIFI_IP 0x01010101
#define NODE2_MAC_ADDR 0x114514114514
#define DNS_MAC_ADDR 0x114514114514
#define AETHERNET_IP 0xAC120100
#define HOTSPOT_IP 0xC0A80100
#define WIFI_IP 0x01000000
#define WIFI_DEVICE_NAME "Network adapter 'Intel(R) Wi-Fi 6E AX211 160MHz' on local host"

class MainComponent : public juce::AudioAppComponent {
public:
	MainComponent();

	~MainComponent() override;

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;

	void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

	void releaseResources() override;

private:
	enum TransportState {
		Ready, ToRun, Waiting,
		AethernetSending, AethernetReceiving,
		AethernetReplySending, AethernetReplyReceiving,
		EthernetSending, EthernetReceiving
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

			case Waiting:
			case AethernetSending:
			case AethernetReceiving:
			case AethernetReplySending:
			case AethernetReplyReceiving:
			case EthernetSending:
			case EthernetReceiving:
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
		sentAethernetData.clear();
		auto chooser = new juce::FileChooser("Select command ...", File::getSpecialLocation(File::SpecialLocationType::userDesktopDirectory), "*.txt");
		auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

		chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
			auto file = fc.getResult();
			int length = 0;
			if (file != juce::File{} && file.getFullPathName().contains(".txt"))
				length = parseArgs(file.getFullPathName().toStdString());
			if (length) {
				IPPacketLen = length + 28 + 12 + 5;
				for (int i = 0; i < length; ++i)
					IPPacketLen += cmd.getDestName(i).length();
				auto timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
				uint16_t id = timestamp & 0xffff;
				iph = new IPHeader(UDP, IPPacketLen, id, NODE1_AETHERNET_IP, NODE2_AETHERNET_IP);
				udph = new UDPHeader(0, 1, IPPacketLen - 20);
				generateSentAethernetData(UDP, false);
				changeState(ToRun);
			}
			});

	}

	void runButtonClicked() {
		ofstream outputFile(outputPath);
		outputFile.close();
		changeState(AethernetSending);
		startTime = high_resolution_clock::now();
	}

	void routerButtonClicked() {
		sentAethernetData.clear();
		receivedData.clear();
		originalData.clear();

		pcap_if_t* alldevs;
		if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) != -1)
		{
			for (; alldevs; alldevs = alldevs->next)
				if (!strcmp(alldevs->description, WIFI_DEVICE_NAME))
					break;

			if (!alldevs)
				return;

			adhandle = pcap_open(alldevs->name, 65536, 1, PCAP_OPENFLAG_PROMISCUOUS, NULL, errbuf);
			pcap_freealldevs(alldevs);
			pcap_compile(adhandle, &fcode, "icmp or udp and ip", 1, 0xff);
			pcap_setfilter(adhandle, &fcode);
		}

		changeState(AethernetReceiving);
	}

	void setSampleBuffer(juce::AudioSampleBuffer* sampleBuffer) { this->sampleBuffer = sampleBuffer; }

	void setSampleRate(double sampleRate) { this->sampleRate = sampleRate; }

	void setReadPointer(int readPointer) { this->readPointer = readPointer; }

	void setWritePointer(int writePointer) { this->writePointer = writePointer; }

	int parseArgs(string file) {
		ifstream inputfile(file);
		string token;
		inputfile >> token;
		if (token != "ping") {
			inputfile.close();
			return 0;
		}

		inputfile >> token;
		int len = cmd.setDestName(token);

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
				return 0;
			}
		}

		inputfile.close();
		return len;
	}

	string IPAddrToStr(uint32_t addr) {
		string IPStr = "";
		for (int i = 3; i > 0; --i)
			IPStr += (to_string((addr >> (i * 8) & 0xff)) + ".");
		IPStr += to_string(addr & 0xff);

		return IPStr;
	}

	void dumpBit(int n) {
		originalData.erase(originalData.begin(), originalData.begin() + n);
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

	void calcHeaderChecksum(ProtocolType p) {
		uint32_t sum1 = 0;
		uint16_t sum2 = 0;
		switch (p) {
		case ICMP:
			icmph->setDirectChecksum(0);
			for (int i = 0; i < 2; ++i)
				sum1 += (icmph->getWord(i) >> 16) + (icmph->getWord(i) & 0xffff);
			for (int i = 0; i < (IPPacketLen - 42) / 2; ++i)
				sum1 += ((uint16_t)sentEthernetData[42 + 2 * i] << 8) + (uint16_t)sentEthernetData[42 + 2 * i + 1];
			sum2 = (uint16_t)((sum1 >> 16) + (sum1 & 0xffff));
			icmph->setDirectChecksum(0xffff - sum2);
			break;

		case UDP:
			udph->setDirectChecksum(0);
			sum1 += (iph->getDestAddr() >> 16 & 0xffff) + (iph->getDestAddr() & 0xffff);
			sum1 += (iph->getSrcAddr() >> 16 & 0xffff) + (iph->getSrcAddr() & 0xffff);
			sum1 += 0x11;
			sum1 += udph->getLength();
			for (int i = 0; i < 2; ++i)
				sum1 += (udph->getWord(i) >> 16) + (udph->getWord(i) & 0xffff);
			for (int i = 0; i < (udph->getLength() - 7) / 2; ++i)
				sum1 += ((uint16_t)sentEthernetData[42 + 2 * i] << 8) + (uint16_t)sentEthernetData[42 + 2 * i + 1];
			sum2 = (uint16_t)((sum1 >> 16) + (sum1 & 0xffff));
			udph->setDirectChecksum(0xffff - sum2);
			break;

		default:
			break;
		}
	}

	void generateAethernetDataFromByte(uint8_t byte) {
		for (int i = 7; i >= 0; --i)
			if (byte >> i & 1)
				sentAethernetData.insert(sentAethernetData.end(), carrierWave.begin(), carrierWave.end());
			else
				sentAethernetData.insert(sentAethernetData.end(), zeroWave.begin(), zeroWave.end());
	}

	void generateSentAethernetData(ProtocolType p, bool isPreambleReversed) {
		sentAethernetData.clear();
		for (int i = 0; i < DELAY_BITS; ++i)
			sentAethernetData.push_back(0);

		if (isPreambleReversed)
			sentAethernetData.insert(sentAethernetData.end(), preambleWave2.begin(), preambleWave2.end());
		else
			sentAethernetData.insert(sentAethernetData.end(), preambleWave1.begin(), preambleWave1.end());

		for (int i = 0; i < iph->getIHL(); ++i)
			for (int j = 0; j < 4; ++j)
				generateAethernetDataFromByte(iph->getByte(i, j));

		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 4; ++j)
				switch (p) {
				case ICMP:
					generateAethernetDataFromByte(icmph->getByte(i, j));
					break;

				case UDP:
					generateAethernetDataFromByte(udph->getByte(i, j));
					break;

				default:
					break;
				}

		if (p == UDP) {
			generateAethernetDataFromByte((uint8_t)(iph->getId() & 0xff));
			generateAethernetDataFromByte((uint8_t)(iph->getId() >> 8 & 0xff));
			generateAethernetDataFromByte(0x01);
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x01);
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x00);
			for (int i = 0; i < cmd.getLength(); ++i) {
				string token = cmd.getDestName(i);
				uint8_t tokenLen = token.length();
				generateAethernetDataFromByte(tokenLen);
				for (int j = 0; j < tokenLen; ++j)
					generateAethernetDataFromByte(token.at(j));
			}
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x01);
			generateAethernetDataFromByte(0x00);
			generateAethernetDataFromByte(0x01);
		}
	}

	void generateSentEthernetData(ProtocolType p, uint64_t srcMac, uint64_t destMac) {
		for (int i = 0; i < 6; ++i) {
			sentEthernetData[i] = destMac >> ((5 - i) * 8) & 0xff;
			sentEthernetData[6 + i] = srcMac >> ((5 - i) * 8) & 0xff;
		}

		sentEthernetData[12] = 0x08;
		sentEthernetData[13] = 0x00;
		for (int i = 0; i < 5; ++i)
			for (int j = 0; j < 4; ++j)
				sentEthernetData[14 + 4 * i + j] = iph->getByte(i, j);

		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 4; ++j)
				switch (p) {
				case ICMP:
					sentEthernetData[34 + 4 * i + j] = icmph->getByte(i, j);
					break;

				case UDP:
					sentEthernetData[34 + 4 * i + j] = udph->getByte(i, j);
					break;

				default:
					break;
				}

	}

	TransportState state;
	TextButton hostButton, runButton, routerButton;
	AudioSampleBuffer* sampleBuffer = nullptr;
	double sampleRate;
	int readPointer = 0, writePointer = 0;

	vector<float> carrierWave, zeroWave;
	vector<float> preambleWave1, preambleWave2;
	vector<float> sentAethernetData;
	deque<float> receivedData, originalData;
	string outputPath = File::getSpecialLocation(File::SpecialLocationType::userDesktopDirectory).getFullPathName().toStdString() + "/OUTPUT.txt";;
	bool begin = false, preambleSuspected = false;
	float sum = 0, maxSum = SUM_THRESHOLD;
	int lowerTicks = 0;
	int bitsReceived = 0;

	Command cmd;
	IPHeader* iph;
	ICMPHeader* icmph;
	UDPHeader* udph;
	double echoInterval;
	int echoNum;
	int echoReceivedNum = 0;
	vector<int> rtt;
	int IPPacketLen = 0;

	time_point<high_resolution_clock> startTime, endTime;
	long long duration;

	pcap_t* adhandle;
	struct bpf_program fcode;
	char errbuf[PCAP_ERRBUF_SIZE];
	u_char sentEthernetData[BYTE_NUM];

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
