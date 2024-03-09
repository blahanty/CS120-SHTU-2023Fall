#pragma once

#include <JuceHeader.h>
#include "Protocol.h"
#include <fstream>
#include <vector>
#include <deque>
#include <numeric>
#include <algorithm>

using namespace juce;
using namespace std;

#define BIT_WIDTH 8
#define BYTE_NUM 52
#define FREQ 600
#define PREAMBLE_LENGTH 520
#define PREAMBLE_FREQ_BEGIN 5000
#define PREAMBLE_FREQ_END 10000
#define SUM_THRESHOLD 5
#define DELAY_BITS 0
#define NODE1_AETHERNET_IP 0xAC120102
#define NODE2_AETHERNET_IP 0xAC120101
#define WIFI_IP 0xC0A81F00
#define NODE2_WIFI_IP 0xC0A81F72
#define NODE2_WIFI_MAC 0x114514114514
#define DEVICE_NAME_WIFI "Network adapter 'Intel(R) Wi-Fi 6E AX211 160MHz' on local host"

class MainComponent : public juce::AudioAppComponent {
public:
	MainComponent();

	~MainComponent() override;

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;

	void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

	void releaseResources() override;

private:
	enum TransportState {
		Ready,
		AethernetSending, AethernetReceiving,
		AethernetReplySending, AethernetReplyReceiving,
		WifiSending, WifiReceiving
	};

	void changeState(TransportState newState) {
		if (state != newState) {
			state = newState;

			switch (state) {
			case Ready:
				hostButton.setEnabled(true);
				routerButton.setEnabled(true);
				break;

			case AethernetSending:
			case AethernetReceiving:
			case AethernetReplySending:
			case AethernetReplyReceiving:
			case WifiSending:
			case WifiReceiving:
				hostButton.setEnabled(false);
				routerButton.setEnabled(false);
				break;

			default:
				break;
			}
		}
	}

	void hostButtonClicked() {
		sentAethernetData.clear();
		receivedData.clear();
		originalData.clear();
		changeState(AethernetReceiving);
	}

	void routerButtonClicked() {
		sentAethernetData.clear();
		receivedData.clear();
		originalData.clear();

		pcap_if_t* alldevs;
		if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) != -1) {
			for (; alldevs; alldevs = alldevs->next)
				if (!strcmp(alldevs->description, DEVICE_NAME_WIFI))
					break;

			if (!alldevs)
				return;

			adhandle = pcap_open(alldevs->name, 65536, 1, PCAP_OPENFLAG_PROMISCUOUS, NULL, errbuf);
			pcap_freealldevs(alldevs);
			pcap_compile(adhandle, &fcode, "ip and icmp", 1, 0xff);
			pcap_setfilter(adhandle, &fcode);

			changeState(WifiReceiving);
		}
	}

	void setSampleBuffer(juce::AudioSampleBuffer* sampleBuffer) { this->sampleBuffer = sampleBuffer; }

	void setSampleRate(double sampleRate) { this->sampleRate = sampleRate; }

	void setReadPointer(int readPointer) { this->readPointer = readPointer; }

	void setWritePointer(int writePointer) { this->writePointer = writePointer; }

	uint32_t dequeToHex(int bits) {
		uint32_t result = 0;
		for (int i = 0; i < bits; ++i) {
			result <<= 1;
			if (originalData.front()) { ++result; }
			originalData.pop_front();
		}

		return result;
	}

	void calcAllChecksum() {
		icmph->setAllChecksum(0);
		uint32_t sum1 = 0;
		for (int i = 0; i < 2; ++i)
			sum1 += (icmph->getWord(i) >> 16) + (icmph->getWord(i) & 0xffff);
		for (int i = 0; i < (BYTE_NUM - 42) / 2; ++i)
			sum1 += ((uint16_t)sentEthernetData[42 + 2 * i] << 8) + (uint16_t)sentEthernetData[42 + 2 * i + 1];
		uint16_t sum2 = (uint16_t)((sum1 >> 16) + (sum1 & 0xffff));
		icmph->setAllChecksum(0xffff - sum2);
	}

	void dumpBit(int n) {
		originalData.erase(originalData.begin(), originalData.begin() + n);
	}

	uint8_t bitsToByte() {
		uint8_t byte = 0;
		for (int i = 0; i < 8; ++i) {
			byte <<= 1;
			byte += originalData.front();
			originalData.pop_front();
		}

		return byte;
	}

	void generateAethernetPreamble(bool isReversed) {
		sentAethernetData.clear();
		for (int i = 0; i < DELAY_BITS; ++i)
			sentAethernetData.push_back(0);

		if (isReversed)
			sentAethernetData.insert(sentAethernetData.end(), preambleWave2.begin(), preambleWave2.end());
		else
			sentAethernetData.insert(sentAethernetData.end(), preambleWave1.begin(), preambleWave1.end());
	}

	void generateAethernetIPHeader() {
		for (int i = 0; i < iph->getIHL(); ++i)
			for (int j = 0; j < 4; ++j)
				generateAethernetDataFromByte(iph->getByte(i, j));
	}

	void generateAethernetICMPHeader() {
		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 4; ++j)
				generateAethernetDataFromByte(icmph->getByte(i, j));
	}

	void generateAethernetDataFromByte(uint8_t byte) {
		for (int i = 7; i >= 0; --i)
			if (byte >> i & 1)
				sentAethernetData.insert(sentAethernetData.end(), carrierWave.begin(), carrierWave.end());
			else
				sentAethernetData.insert(sentAethernetData.end(), zeroWave.begin(), zeroWave.end());
	}

	void generateSentAethernetDataFromBuffer(int num) {
		for (int i = 0; i < num; ++i) {
			generateAethernetDataFromByte(dataBuffer.front());
			dataBuffer.pop_front();
		}
	}

	void generateSentAethernetData() {
		generateAethernetIPHeader();
		generateAethernetICMPHeader();
	}

	void generateEthernetICMPHeader() {
		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 4; ++j)
				sentEthernetData[34 + 4 * i + j] = icmph->getByte(i, j);
	}

	void generateSentEthernetData(uint64_t srcMac, uint64_t destMac) {
		for (int i = 0; i < 6; ++i) {
			sentEthernetData[i] = destMac >> ((5 - i) * 8) & 0xff;
			sentEthernetData[6 + i] = srcMac >> ((5 - i) * 8) & 0xff;
		}

		sentEthernetData[12] = 0x08;
		sentEthernetData[13] = 0x00;
		for (int i = 0; i < 5; ++i)
			for (int j = 0; j < 4; ++j)
				sentEthernetData[14 + 4 * i + j] = iph->getByte(i, j);		
	}

	TransportState state;
	TextButton hostButton, routerButton;
	AudioSampleBuffer* sampleBuffer = nullptr;
	double sampleRate;
	int readPointer = 0, writePointer = 0;

	vector<float> carrierWave, zeroWave;
	vector<float> preambleWave1, preambleWave2;
	vector<float> sentAethernetData;
	deque<float> receivedData, originalData;
	deque<u_char> dataBuffer;
	bool begin = false, preambleSuspected = false;
	float sum = 0, maxSum = SUM_THRESHOLD;
	int lowerTicks = 0;
	int bitsReceived = 0;

	IPHeader* iph;
	ICMPHeader* icmph;

	pcap_t* adhandle;
	struct bpf_program fcode;
	char errbuf[PCAP_ERRBUF_SIZE];
	u_char sentEthernetData[1500] = { 0 };
	uint32_t node4WifiIp = 0;
	uint64_t node4WifiMac = 0;
	int fragmentNum = 0, fragment = 0;
	int totalLen = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
