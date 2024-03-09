#pragma once

#include <JuceHeader.h>
#include "Protocol.h"
#include <fstream>
#include <vector>
#include <deque>

using namespace juce;
using namespace std;

#define BIT_WIDTH 8
#define BYTE_NUM (PACKET_LENGTH - 14)
#define FREQ 600
#define PREAMBLE_LENGTH 520
#define PREAMBLE_FREQ_BEGIN 5000
#define PREAMBLE_FREQ_END 10000
#define SUM_THRESHOLD 3
#define DELAY_BITS 0
#define NODE1_IP 0xAC12010C
#define NODE2_IP 0xAC12010D
#define NODE1_MAC_ADDR 0x114514114514
#define NODE2_MAC_ADDR 0x114514114514
#define DEVICE_NAME_AETHERNET "Network adapter 'Microsoft KM-TEST ����������' on local host"
#define PACKET_LENGTH 74
#define ARP_PACKET_LENGTH 42

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
		EthernetNode1ARPReceiving, AethernetNode1ARPSending,
		AethernetNode2ARPReceiving, EthernetNode2ARPSending,
		EthernetNode2ARPReplySending, AethernetNode2ARPReplySending,
		AethernetNode1ARPReplyReceiving, EthernetNode1ARPReplySending,
		EthernetNode1ICMPReceiving, AethernetNode1ICMPSending,
		AethernetNode2ICMPReceiving, EthernetNode2ICMPSending,
		EthernetNode2ICMPReplySending, AethernetNode2ICMPReplySending,
		AethernetNode1ICMPReplyReceiving, EthernetNode1ICMPReplySending,
	};

	void changeState(TransportState newState) {
		if (state != newState) {
			state = newState;

			switch (state) {
			case Ready:
				hostButton.setEnabled(true);
				routerButton.setEnabled(true);
				break;

			case EthernetNode1ARPReceiving:
			case AethernetNode1ARPSending:
			case AethernetNode2ARPReceiving:
			case EthernetNode2ARPSending:
			case EthernetNode2ARPReplySending:
			case AethernetNode2ARPReplySending:
			case AethernetNode1ARPReplyReceiving:
			case EthernetNode1ARPReplySending:
			case EthernetNode1ICMPReceiving:
			case AethernetNode1ICMPSending:
			case AethernetNode2ICMPReceiving:
			case EthernetNode2ICMPSending:
			case EthernetNode2ICMPReplySending:
			case AethernetNode2ICMPReplySending:
			case AethernetNode1ICMPReplyReceiving:
			case EthernetNode1ICMPReplySending:
				hostButton.setEnabled(false);
				routerButton.setEnabled(false);
				break;

			default:
				break;
			}
		}
	}

	void hostButtonClicked() {
		sentData.clear();
		pcap_if_t* alldevs;
		if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) != -1)
		{
			for (; alldevs; alldevs = alldevs->next)
				if (!strcmp(alldevs->description, DEVICE_NAME_AETHERNET))
					break;

			if (!alldevs)
				return;

			adhandle = pcap_open(alldevs->name, 65536, 1, PCAP_OPENFLAG_PROMISCUOUS, NULL, errbuf);
			pcap_freealldevs(alldevs);
			pcap_compile(adhandle, &fcode, "ip and icmp or arp", 1, 0xffff);
			pcap_setfilter(adhandle, &fcode);
		}

		changeState(EthernetNode1ARPReceiving);
	}


	void routerButtonClicked() {
		receivedData.clear();
		originalData.clear();
		pcap_if_t* alldevs;
		if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) != -1)
		{
			for (; alldevs; alldevs = alldevs->next)
				if (!strcmp(alldevs->description, DEVICE_NAME_AETHERNET))
					break;

			if (!alldevs)
				return;

			adhandle = pcap_open(alldevs->name, 65536, 1, PCAP_OPENFLAG_PROMISCUOUS, NULL, errbuf);
			pcap_freealldevs(alldevs);
			pcap_compile(adhandle, &fcode, "ip and icmp", 1, 0xffff);
			pcap_setfilter(adhandle, &fcode);
		}

		changeState(AethernetNode2ARPReceiving);
	}

	void setSampleBuffer(juce::AudioSampleBuffer* sampleBuffer) { this->sampleBuffer = sampleBuffer; }

	void setSampleRate(double sampleRate) { this->sampleRate = sampleRate; }

	void setReadPointer(int readPointer) { this->readPointer = readPointer; }

	void setWritePointer(int writePointer) { this->writePointer = writePointer; }

	enum ProtocolType {
		ICMP, ARP
	};

	void generateSentAethernetDataFromByte(uint8_t byte) {
		for (int i = 7; i >= 0; --i)
			if (byte >> i & 1)
				sentData.insert(sentData.end(), carrierWave.begin(), carrierWave.end());
			else
				sentData.insert(sentData.end(), zeroWave.begin(), zeroWave.end());
	}

	void generateSentAethernetData(bool isPreambleReversed) {
		sentData.clear();
		for (int i = 0; i < DELAY_BITS; ++i)
			sentData.push_back(0);

		if (isPreambleReversed)
			sentData.insert(sentData.end(), preambleWave2.begin(), preambleWave2.end());
		else
			sentData.insert(sentData.end(), preambleWave1.begin(), preambleWave1.end());

		for (int i = 0; i < iph->getIHL(); ++i)
			for (int j = 0; j < 4; ++j)
				generateSentAethernetDataFromByte(iph->getByte(i, j));

		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 4; ++j)
				generateSentAethernetDataFromByte(icmph->getByte(i, j));
	}

	void generateSentEthernetData(ProtocolType p, uint64_t srcMac, uint64_t destMac) {
		for (int i = 0; i < 6; ++i) {
			sentEthernetData[i] = destMac >> ((5 - i) * 8) & 0xff;
			sentEthernetData[6 + i] = srcMac >> ((5 - i) * 8) & 0xff;
		}

		sentEthernetData[12] = 0x08;
		switch (p) {
		case ICMP:
			sentEthernetData[13] = 0x00;
			for (int i = 0; i < 5; ++i)
				for (int j = 0; j < 4; ++j)
					sentEthernetData[14 + 4 * i + j] = iph->getByte(i, j);

			for (int i = 0; i < 2; ++i)
				for (int j = 0; j < 4; ++j)
					sentEthernetData[34 + 4 * i + j] = icmph->getByte(i, j);
			break;

		case ARP:
			sentEthernetData[13] = 0x06;
			for (int i = 0; i < 7; ++i)
				for (int j = 0; j < 4; ++j)
					sentEthernetData[14 + 4 * i + j] = arph->getByte(i, j);
			break;

		default:
			break;
		}
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

	void calcICMPChecksum() {
		uint32_t sum1 = 0;
		uint16_t sum2 = 0;
		icmph->setChecksum(0);
		for (int i = 0; i < 2; ++i)
			sum1 += (icmph->getWord(i) >> 16) + (icmph->getWord(i) & 0xffff);
		for (int i = 0; i < (PACKET_LENGTH - 42) / 2; ++i)
			sum1 += ((uint16_t)sentEthernetData[42 + 2 * i] << 8) + (uint16_t)sentEthernetData[42 + 2 * i + 1];
		sum2 = (uint16_t)((sum1 >> 16) + (sum1 & 0xffff));
		icmph->setChecksum(0xffff - sum2);
	}

	TransportState state;
	TextButton hostButton, routerButton;
	AudioSampleBuffer* sampleBuffer = nullptr;
	double sampleRate;
	int readPointer = 0, writePointer = 0;

	vector<float> carrierWave, zeroWave;
	vector<float> preambleWave1, preambleWave2;
	vector<float> sentData;
	deque<float> receivedData, originalData;
	bool begin = false, preambleSuspected = false;
	float sum = 0, maxSum = SUM_THRESHOLD;
	int lowerTicks = 0;
	int bitsReceived = 0;

	Command cmd;
	IPHeader* iph = nullptr;
	ICMPHeader* icmph = nullptr;
	ARPHeader* arph = nullptr;

	pcap_t* adhandle;
	struct bpf_program fcode;
	char errbuf[PCAP_ERRBUF_SIZE];
	u_char sentEthernetData[PACKET_LENGTH];

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
