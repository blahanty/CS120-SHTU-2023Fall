#include "MainComponent.h"

MainComponent::MainComponent() {
	setSize(192, 120);
	setAudioChannels(1, 1);

	hostButton.setButtonText("Host");
	hostButton.setSize(160, 24);
	hostButton.setTopLeftPosition(16, 12);
	hostButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
	hostButton.onClick = [this] { hostButtonClicked(); };
	addAndMakeVisible(hostButton);

	routerButton.setButtonText("Router");
	routerButton.setSize(160, 24);
	routerButton.setTopLeftPosition(16, 48);
	routerButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
	routerButton.onClick = [this] { routerButtonClicked(); };
	addAndMakeVisible(routerButton);

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

	preambleWave1.clear();
	float phase = 0;
	for (int i = 0; i < PREAMBLE_LENGTH; ++i) {
		preambleWave1.push_back(0.5 * cos(phase));
		phase += juce::MathConstants<double>::pi * 2 *
			(PREAMBLE_FREQ_BEGIN + i * (PREAMBLE_FREQ_END - PREAMBLE_FREQ_BEGIN) / (PREAMBLE_LENGTH - 1)) / 48000;
	}

	preambleWave2.clear();
	for (int i = 0; i < PREAMBLE_LENGTH; ++i)
		preambleWave2.push_back(preambleWave1[PREAMBLE_LENGTH - i - 1]);

	receivedData.clear();
	originalData.clear();

	WSADATA wd;
	WSAStartup(MAKEWORD(2, 2), &wd);
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
			case AethernetSending:
				bufferToFill.buffer->clear();
				for (int i = 0; i < bufferSize; ++i, ++readPointer) {
					if (readPointer < sentAethernetData.size())
						bufferToFill.buffer->addSample(channel, i, sentAethernetData[readPointer]);
					else {
						setReadPointer(0);
						receivedData.clear();
						originalData.clear();
						changeState(AethernetReplyReceiving);
						break;
					}
				}
				break;

			case AethernetReceiving:
				for (int i = 0; i < bufferSize; ++i)
					receivedData.push_back(bufferToFill.buffer->getSample(channel, i));

				while (!begin && receivedData.size() >= PREAMBLE_LENGTH) {
					sum = 0;
					for (int i = 0; i < PREAMBLE_LENGTH; ++i)
						sum += receivedData[i] * preambleWave1[i];
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
					originalData.push_back(receivedData.front());
					receivedData.pop_front();
					if (++bitsReceived == BYTE_NUM * 8 * BIT_WIDTH) {
						maxSum = SUM_THRESHOLD;
						bitsReceived = 0;
						begin = false;
						for (int i = 0; i < BYTE_NUM * 8; ++i) {
							sum = 0;
							for (int j = 0; j < BIT_WIDTH; ++j) {
								sum += originalData.front();
								originalData.pop_front();
							}
							if (sum > 0.8)
								originalData.push_back(1);
							else
								originalData.push_back(0);
						}

						iph = new IPHeader();
						iph->setVersion(dequeToHex(4) & 0xf);
						iph->setIHL(dequeToHex(4) & 0xf);
						iph->setTypeOfService(dequeToHex(8) & 0xff);
						iph->setTotalLen(dequeToHex(16) & 0xffff);
						iph->setId(dequeToHex(16) & 0xffff);
						iph->setFlags(dequeToHex(3) & 0x7);
						iph->setFragmentOffset(dequeToHex(13) & 0x1fff);
						iph->setTTL((dequeToHex(8) & 0xff) - 1);
						iph->setProtocol(dequeToHex(8) & 0xff);
						dumpBit(16);
						iph->setDestAddr(dequeToHex(32));
						iph->setSrcAddr(dequeToHex(32));
						iph->setHeaderChecksum();

						generateAethernetPreamble(true);
						generateAethernetIPHeader();
						while (originalData.size())
						{
							if (originalData.front())
								sentAethernetData.insert(sentAethernetData.end(), carrierWave.begin(), carrierWave.end());
							else
								sentAethernetData.insert(sentAethernetData.end(), zeroWave.begin(), zeroWave.end());

							originalData.pop_front();
						}
						setReadPointer(0);
						originalData.clear();
						receivedData.clear();
						delete iph;
						changeState(AethernetReplySending);
						break;
					}
				}

				bufferToFill.buffer->clear();
				break;

			case AethernetReplySending:
				bufferToFill.buffer->clear();
				for (int i = 0; i < bufferSize; ++i, ++readPointer) {
					if (readPointer < sentAethernetData.size())
						bufferToFill.buffer->addSample(channel, i, sentAethernetData[readPointer]);
					else {
						setReadPointer(0);
						receivedData.clear();
						originalData.clear();
						changeState(AethernetReceiving);
						break;
					}
				}
				break;

			case AethernetReplyReceiving:
				for (int i = 0; i < bufferSize; ++i)
					receivedData.push_back(bufferToFill.buffer->getSample(channel, i));

				while (!begin && receivedData.size() >= PREAMBLE_LENGTH) {
					sum = 0;
					for (int i = 0; i < PREAMBLE_LENGTH; ++i)
						sum += receivedData[i] * preambleWave2[i];

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
					originalData.push_back(receivedData[0]);
					receivedData.pop_front();
					if (++bitsReceived == BYTE_NUM * 8 * BIT_WIDTH) {
						maxSum = SUM_THRESHOLD;
						bitsReceived = 0;
						begin = false;
						for (int i = 0; i < BYTE_NUM * 8; ++i) {
							sum = 0;
							for (int j = 0; j < BIT_WIDTH; ++j) {
								sum += originalData[0];
								originalData.pop_front();
							}
							if (sum > 0.7)
								originalData.push_back(1);
							else
								originalData.push_back(0);
						}

						iph = new IPHeader();
						iph->setVersion(dequeToHex(4) & 0xf);
						iph->setIHL(dequeToHex(4) & 0xf);
						iph->setTypeOfService(dequeToHex(8) & 0xff);
						int totalLen = dequeToHex(16) & 0xffff;
						iph->setTotalLen(totalLen);
						iph->setId(dequeToHex(16) & 0xffff);
						iph->setFlags(dequeToHex(3) & 0x7);
						iph->setFragmentOffset(dequeToHex(13) & 0x1fff);
						iph->setTTL((dequeToHex(8) & 0xff) - 1);
						iph->setProtocol(dequeToHex(8) & 0xff);
						dumpBit(80);
						iph->setSrcAddr(NODE2_WIFI_IP);
						iph->setDestAddr(node4WifiIp);
						iph->setHeaderChecksum();

						for (int i = 0; i < totalLen - 20; ++i)
							sentEthernetData[34 + i] = bitsToByte();

						for (int i = 14 + totalLen; i < 1500; ++i)
							sentEthernetData[i] = 0;

						originalData.clear();
						receivedData.clear();
						generateSentEthernetData(NODE2_WIFI_MAC, node4WifiMac);
						changeState(WifiSending);
						break;
					}
				}

				bufferToFill.buffer->clear();
				break;

			case WifiSending:
				pcap_sendpacket(adhandle, sentEthernetData, BYTE_NUM + 14);
				if (++fragment == fragmentNum) {
					delete iph;
					delete icmph;
					dataBuffer.clear();
					fragmentNum = fragment = 0;
					for (int i = 0; i < 1500; ++i)
						sentEthernetData[i] = 0;

					changeState(WifiReceiving);
				}
				else {
					if (fragment + 1 == fragmentNum)
						iph->setFlags(0);
					int len = dataBuffer.size() + 20 > BYTE_NUM ? BYTE_NUM : dataBuffer.size() + 20;
					iph->setTotalLen(len);
					iph->setFragmentOffset(fragment * (BYTE_NUM - 20) / 8);
					iph->setHeaderChecksum();

					setReadPointer(0);
					generateAethernetPreamble(false);
					generateAethernetIPHeader();
					generateSentAethernetDataFromBuffer(len - 20);
					changeState(AethernetSending);
				}

				bufferToFill.buffer->clear();
				break;

			case WifiReceiving:
			{
				pcap_pkthdr* pktHeader;
				const u_char* pktData;
				int res;
				if ((res = pcap_next_ex(adhandle, &pktHeader, &pktData)) >= 0) {
					if (res > 0 && *((uint8_t*)(pktData + 34)) == 8) {
						iph = new IPHeader();
						iph->setVersion((*((uint8_t*)(pktData + 14))) >> 4 & 0xf);
						iph->setIHL((*((uint8_t*)(pktData + 14))) & 0xf);
						iph->setTypeOfService(*((uint8_t*)(pktData + 15)));
						totalLen = ((*((uint16_t*)(pktData + 16)) & 0xff) << 8) + (*((uint16_t*)(pktData + 17)) & 0xff);
						iph->setTotalLen(totalLen);
						iph->setId(((*((uint16_t*)(pktData + 18)) & 0xff) << 8) + (*((uint16_t*)(pktData + 19)) & 0xff));
						iph->setFlags(1);
						iph->setFragmentOffset(0);
						iph->setTTL((*((uint8_t*)(pktData + 22))) - 1);
						iph->setProtocol(*((uint8_t*)(pktData + 23)));

						node4WifiIp = 0;
						uint32_t dest = 0;
						for (int i = 0; i < 4; ++i) {
							int bits = (3 - i) * 8;
							node4WifiIp += (*((uint32_t*)(pktData + 26 + i)) & 0xff) << bits;
							dest += (*((uint32_t*)(pktData + 58 + i)) & 0xff) << bits;
						}

						dataBuffer.clear();
						for (int i = 0; i < totalLen - 20; ++i)
							dataBuffer.push_back(*(pktData + 42 + i));

						int len = 0;
						if (dest == NODE1_AETHERNET_IP) {
							len = totalLen > BYTE_NUM ? BYTE_NUM : totalLen;
							iph->setTotalLen(len);
							iph->setSrcAddr(NODE2_AETHERNET_IP);
						}
						else {
							delete iph;
							break;
						}

						iph->setDestAddr(dest);
						iph->setHeaderChecksum();

						icmph = new ICMPHeader();
						icmph->setType(0);
						icmph->setCode(*((uint8_t*)(pktData + 35)));
						icmph->setId(((*((uint16_t*)(pktData + 38)) & 0xff) << 8) + (*((uint16_t*)(pktData + 39)) & 0xff));
						icmph->setSeqNum(((*((uint16_t*)(pktData + 40)) & 0xff) << 8) + (*((uint16_t*)(pktData + 41)) & 0xff));
						for (int i = 0; i < totalLen - 28; ++i)
							sentEthernetData[42 + i] = dataBuffer[i];
						calcAllChecksum();

						node4WifiMac = 0;
						for (int i = 0; i < 6; ++i) {
							int bits = (5 - i) * 8;
							node4WifiMac += (*((uint64_t*)(pktData + 6 + i)) & 0xff) << bits;
						}

						fragmentNum = (dataBuffer.size() + (BYTE_NUM - 21)) / (BYTE_NUM - 20);
						fragment = 0;
						setReadPointer(0);
						generateAethernetPreamble(false);
						generateSentAethernetData();
						generateSentAethernetDataFromBuffer(len - 28);
						changeState(AethernetSending);

						delete iph;
						delete icmph;
					}
				}

				bufferToFill.buffer->clear();
				break; }

			default:
				bufferToFill.buffer->clear();
				break;
			}
		}
	}
}

void MainComponent::releaseResources() { delete sampleBuffer; }
