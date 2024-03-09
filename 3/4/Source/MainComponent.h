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
#define NODE2_HOTSPOT_IP 0xC0A88901
#define NODE3_HOTSPOT_IP 0xC0A889E0
#define NODE2_HOTSPOT_MAC 0x114514114514
#define NODE3_HOTSPOT_MAC 0x114514114514
#define WIFI_IP 0x0A000000
#define NODE2_WIFI_IP 0x0A14DB8A
#define NODE2_WIFI_MAC 0x114514114514
#define DEVICE_NAME_HOTSPOT "Network adapter 'Microsoft Wi-Fi Direct Virtual Adapter #2' on local host"
#define DEVICE_NAME_WIFI "Network adapter 'Intel(R) Wi-Fi 6E AX211 160MHz' on local host"
#define PACKET_LENGTH 62

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
		HotspotSending, HotspotReceiving,
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
			case HotspotSending:
			case HotspotReceiving:
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
		int devCount = 0;
		if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) != -1)
		{
			for (; alldevs; alldevs = alldevs->next) {
				if (!strcmp(alldevs->description, DEVICE_NAME_HOTSPOT)) {
					adhandleHotspot = pcap_open(alldevs->name, 65536, 1, PCAP_OPENFLAG_PROMISCUOUS, NULL, errbuf);
					++devCount;
				}
				else if (!strcmp(alldevs->description, DEVICE_NAME_WIFI)) {
					adhandleWifi = pcap_open(alldevs->name, 65536, 1, PCAP_OPENFLAG_PROMISCUOUS, NULL, errbuf);
					++devCount;
				}

				if (devCount == 2)
					break;
			}

			if (!alldevs)
				return;

			pcap_freealldevs(alldevs);
			pcap_compile(adhandleHotspot, &fcodeHotspot, "ip and icmp", 1, 0xffffff);
			pcap_setfilter(adhandleHotspot, &fcodeHotspot);
			pcap_compile(adhandleWifi, &fcodeWifi, "ip and icmp", 1, 0xff);
			pcap_setfilter(adhandleWifi, &fcodeWifi);
		}

		changeState(WifiReceiving);
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

	void calcAllChecksum() {
		icmph->setAllChecksum(0);
		uint32_t sum1 = 0;
		for (int i = 0; i < 2; ++i)
			sum1 += (icmph->getWord(i) >> 16) + (icmph->getWord(i) & 0xffff);
		for (int i = 0; i < (PACKET_LENGTH - 42) / 2; ++i)
			sum1 += ((uint16_t)sentEthernetData[42 + 2 * i] << 8) + (uint16_t)sentEthernetData[42 + 2 * i + 1];
		uint16_t sum2 = (uint16_t)((sum1 >> 16) + (sum1 & 0xffff));
		icmph->setAllChecksum(0xffff - sum2);
	}

	void dumpBit(int n) {
		originalData.erase(originalData.begin(), originalData.begin() + n);
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

	pcap_t* adhandleHotspot, * adhandleWifi;
	struct bpf_program fcodeHotspot, fcodeWifi;
	char errbuf[PCAP_ERRBUF_SIZE];
	u_char sentEthernetData[PACKET_LENGTH] = { 0 };
	uint32_t node4WifiIp = 0;
	uint64_t node3HotspotMac = 0, node4WifiMac = 0;
	u_char timestamp[PACKET_LENGTH - 46] = { 0 };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
