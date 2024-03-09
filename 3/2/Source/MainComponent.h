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
#define BYTE_NUM 28
#define FREQ 600
#define PREAMBLE_LENGTH 520
#define PREAMBLE_FREQ_BEGIN 5000
#define PREAMBLE_FREQ_END 10000
#define SUM_THRESHOLD 3
#define DELAY_BITS 0
#define NODE1_AETHERNET_IP 0xAC120102
#define NODE2_AETHERNET_IP 0xAC120101
#define HOTSPOT_IP 0xC0A88900
#define NODE2_HOTSPOT_IP 0xC0A80101
#define DEVICE_NAME "Network adapter 'Microsoft Wi-Fi Direct Virtual Adapter #2' on local host"
#define PACKET_LENGTH 98

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
		EthernetSending, EthernetReceiving
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
			case EthernetSending:
			case EthernetReceiving:
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
		if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) != -1)
		{
			for (; alldevs; alldevs = alldevs->next)
				if (!strcmp(alldevs->description, DEVICE_NAME))
					break;

			if (!alldevs)
				return;

			adhandle = pcap_open(alldevs->name, 65536, 1, PCAP_OPENFLAG_PROMISCUOUS, NULL, errbuf);
			pcap_freealldevs(alldevs);
			pcap_compile(adhandle, &fcode, "ip and icmp", 1, 0xffffff);
			pcap_setfilter(adhandle, &fcode);
		}

		changeState(EthernetReceiving);
	}

	void setSampleBuffer(juce::AudioSampleBuffer* sampleBuffer) { this->sampleBuffer = sampleBuffer; }

	void setSampleRate(double sampleRate) { this->sampleRate = sampleRate; }

	void setReadPointer(int readPointer) { this->readPointer = readPointer; }

	void setWritePointer(int writePointer) { this->writePointer = writePointer; }

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

	void calcAllChecksum(ICMPHeader* header) {
		header->setAllChecksum(0);
		uint32_t sum1 = 0;
		for (int i = 0; i < 2; ++i)
			sum1 += (header->getWord(i) >> 16) + (header->getWord(i) & 0xffff);
		for (int i = 0; i < (PACKET_LENGTH - 42) / 2; ++i)
			sum1 += ((uint16_t)sentEthernetData[42 + 2 * i] << 8) + (uint16_t)sentEthernetData[42 + 2 * i + 1];
		uint16_t sum2 = (uint16_t)((sum1 >> 16) + (sum1 & 0xffff));
		header->setAllChecksum(0xffff - sum2);
	}

	void generateAethernetDataFromByte(uint8_t byte) {
		for (int i = 7; i >= 0; --i)
			if (byte >> i & 1)
				sentAethernetData.insert(sentAethernetData.end(), carrierWave.begin(), carrierWave.end());
			else
				sentAethernetData.insert(sentAethernetData.end(), zeroWave.begin(), zeroWave.end());
	}

	void generateSentAethernetData(bool isPreambleReversed) {
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
				generateAethernetDataFromByte(icmph->getByte(i, j));
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

		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 4; ++j)
				sentEthernetData[34 + 4 * i + j] = icmph->getByte(i, j);
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
	bool begin = false, preambleSuspected = false;
	float sum = 0, maxSum = SUM_THRESHOLD;
	int lowerTicks = 0;
	int bitsReceived = 0;

	IPHeader* iph;
	ICMPHeader* icmph;

	pcap_t* adhandle;
	struct bpf_program fcode;
	char errbuf[PCAP_ERRBUF_SIZE];
	u_char sentEthernetData[PACKET_LENGTH];
	uint64_t node1MacAddr = 0, node3MacAddr = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
