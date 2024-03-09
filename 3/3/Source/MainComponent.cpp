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

	runButton.setButtonText("Run");
	runButton.setSize(160, 24);
	runButton.setTopLeftPosition(16, 48);
	runButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
	runButton.onClick = [this] { runButtonClicked(); };
	addAndMakeVisible(runButton);

	routerButton.setButtonText("Router");
	routerButton.setSize(160, 24);
	routerButton.setTopLeftPosition(16, 84);
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
		preambleWave1.push_back(cos(phase));
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
							if (sum > 0)
								originalData.push_back(1);
							else
								originalData.push_back(0);
						}

						iph = new IPHeader();
						iph->setVersion(dequeToHex(4) & 0xf);
						iph->setIHL(dequeToHex(4) & 0xf);
						iph->setTypeOfService(dequeToHex(8) & 0xff);
						originalData.erase(originalData.begin(), originalData.begin() + 16);
						iph->setTotalLen(42);
						iph->setId(dequeToHex(16) & 0xffff);
						iph->setFlags(dequeToHex(3) & 0x7);
						iph->setFragmentOffset(dequeToHex(13) & 0x1fff);
						iph->setTTL((dequeToHex(8) & 0xff) - 1);
						iph->setProtocol(dequeToHex(8) & 0xff);
						originalData.erase(originalData.begin(), originalData.begin() + 48);
						iph->setSrcAddr(NODE2_WIFI_IP);
						iph->setDestAddr(dequeToHex(32));
						iph->setHeaderChecksum();

						icmph = new ICMPHeader();
						icmph->setType(8);
						originalData.erase(originalData.begin(), originalData.begin() + 8);
						icmph->setCode(dequeToHex(8) & 0xff);
						originalData.erase(originalData.begin(), originalData.begin() + 16);
						icmph->setId(dequeToHex(16) & 0xffff);
						icmph->setSeqNum(dequeToHex(16) & 0xffff);
						icmph->setAllChecksum(0);

						for (int i = 42; i < PACKET_LENGTH; ++i)
							sentEthernetData[i] = 0;

						calcAllChecksum(icmph);

						originalData.clear();
						receivedData.clear();
						generateSentEthernetData(NODE2_MAC_ADDR, NODE4_MAC_ADDR);
						changeState(EthernetSending);
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

				while (receivedData.size() >= PREAMBLE_LENGTH) {
					sum = 0;
					for (int i = 0; i < PREAMBLE_LENGTH; ++i)
						sum += receivedData[i] * preambleWave2[i];

					if (sum > maxSum) {
						maxSum = sum;
						lowerTicks = 0;
						preambleSuspected = true;
					}
					else if (preambleSuspected && ++lowerTicks >= PREAMBLE_LENGTH) {
						endTime = high_resolution_clock::now();
						duration = duration_cast<milliseconds>(endTime - startTime).count();
						rtt.push_back(duration);
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
							if (sum > 0)
								originalData.push_back(1);
							else
								originalData.push_back(0);
						}

						IPHeader* iphReceived = new IPHeader();
						iphReceived->setVersion(dequeToHex(4) & 0xf);
						iphReceived->setIHL(dequeToHex(4) & 0xf);
						iphReceived->setTypeOfService(dequeToHex(8) & 0xff);
						iphReceived->setTotalLen(dequeToHex(16) & 0xffff);
						iphReceived->setId(dequeToHex(16) & 0xffff);
						iphReceived->setFlags(dequeToHex(3) & 0x7);
						iphReceived->setFragmentOffset(dequeToHex(13) & 0x1fff);
						iphReceived->setTTL(dequeToHex(8) & 0xff);
						iphReceived->setProtocol(dequeToHex(8) & 0xff);
						int checkSumReceived = dequeToHex(16) & 0xffff;
						iphReceived->setSrcAddr(dequeToHex(32));
						iphReceived->setDestAddr(dequeToHex(32));
						iphReceived->setHeaderChecksum();

						ICMPHeader* icmphReceived = new ICMPHeader();
						icmphReceived->setType(dequeToHex(8) & 0xff);
						icmphReceived->setCode(dequeToHex(8) & 0xff);
						checkSumReceived = dequeToHex(16) & 0xffff;
						icmphReceived->setId(dequeToHex(16) & 0xffff);
						icmphReceived->setSeqNum(dequeToHex(16) & 0xffff);
						icmphReceived->setChecksum();

						if (iphReceived->getSrcAddr() != NODE4_WIFI_IP) {
							delete iphReceived;
							delete icmphReceived;
							changeState(AethernetReceiving);
							break;
						}

						uint16_t bytes = iphReceived->getTotalLen() - iphReceived->getIHL() * 4;
						uint8_t ttl = iphReceived->getTTL();
						uint32_t src = iphReceived->getSrcAddr();

						originalData.clear();
						receivedData.clear();
						ofstream outputFile(outputPath, ios::app);
						outputFile << "Reply from ";
						outputFile << IPAddrToStr(src) << ": ";
						outputFile << "bytes=" << (int)bytes << " ";
						outputFile << "time=" << duration << "ms ";
						outputFile << "TTL=" << (int)ttl << "\n";
						if (++echoReceivedNum == cmd.getEchoNum()) {
							outputFile << "Ping statistics for ";
							outputFile << IPAddrToStr(src) << ":\n";
							outputFile << "Packets: Sent = " << cmd.getEchoNum() << ", ";
							outputFile << "Received = " << echoReceivedNum << ", ";
							outputFile << "Lost = " << "0" << " (" << "0" << "% loss),\n";
							outputFile << "Approximate round trip times in milli-seconds:\n";
							auto minRtt = *min_element(rtt.begin(), rtt.end());
							outputFile << "Minimum = " << minRtt << "ms, ";
							auto maxRtt = *max_element(rtt.begin(), rtt.end());
							outputFile << "Maximum = " << maxRtt << "ms, ";
							int aveRtt = (int)((double)(accumulate(rtt.begin(), rtt.end(), 0) / cmd.getEchoNum()) + 0.5);
							outputFile << "Average = " << aveRtt << "ms";
							echoReceivedNum = 0;
						}
						outputFile.close();
						--echoNum;
						delete iphReceived;
						delete icmphReceived;
						changeState(Waiting);
						break;
					}
				}

				bufferToFill.buffer->clear();
				break;

			case Waiting:
				if (!echoNum) {
					delete iph;
					delete icmph;
					sentAethernetData.clear();
					receivedData.clear();
					originalData.clear();
					rtt.clear();
					changeState(Ready);
				}
				else {
					auto currentTime = high_resolution_clock::now();
					auto currentInterval = duration_cast<milliseconds>(currentTime - startTime).count();
					icmph->setSeqNum(echoReceivedNum + 1);
					icmph->setChecksum();
					generateSentAethernetData(false);
					if (currentInterval >= 1000 * cmd.getEchoInterval()) {
						startTime = high_resolution_clock::now();
						changeState(AethernetSending);
					}
				}
				bufferToFill.buffer->clear();
				break;

			case EthernetSending:
				pcap_sendpacket(adhandle, sentEthernetData, sizeof(sentEthernetData));
				changeState(EthernetReceiving);
				break;

			case EthernetReceiving:
			{
				pcap_pkthdr* pktHeader;
				const u_char* pktData;
				int res;
				if ((res = pcap_next_ex(adhandle, &pktHeader, &pktData)) >= 0) {
					if (res > 0 && *((uint8_t*)(pktData + 34)) == 0) {
						iph = new IPHeader();
						iph->setVersion((*((uint8_t*)(pktData + 14))) >> 4 & 0xf);
						iph->setIHL((*((uint8_t*)(pktData + 14))) & 0xf);
						iph->setTypeOfService(*((uint8_t*)(pktData + 15)));
						iph->setTotalLen(28);
						iph->setId(((*((uint16_t*)(pktData + 18)) & 0xff) << 8) + (*((uint16_t*)(pktData + 19)) & 0xff));
						iph->setFlags((*((uint8_t*)(pktData + 20))) >> 5 & 0x7);
						iph->setFragmentOffset(((*((uint16_t*)(pktData + 20)) & 0x1f) << 8) + (*((uint16_t*)(pktData + 21)) & 0xff));
						iph->setTTL((*((uint8_t*)(pktData + 22))) - 1);
						iph->setProtocol(*((uint8_t*)(pktData + 23)));
						uint32_t src = 0, dest = 0;
						for (int i = 0; i < 4; ++i) {
							int bits = (3 - i) * 8;
							src += (*((uint32_t*)(pktData + 26 + i)) & 0xff) << bits;
							dest += (*((uint32_t*)(pktData + 30 + i)) & 0xff) << bits;
						}
						iph->setSrcAddr(src);
						iph->setDestAddr(dest);
						iph->setHeaderChecksum();

						icmph = new ICMPHeader();
						icmph->setType(0);
						icmph->setCode(*((uint8_t*)(pktData + 35)));
						icmph->setId(((*((uint16_t*)(pktData + 38)) & 0xff) << 8) + (*((uint16_t*)(pktData + 39)) & 0xff));
						icmph->setSeqNum(((*((uint16_t*)(pktData + 40)) & 0xff) << 8) + (*((uint16_t*)(pktData + 41)) & 0xff));
						icmph->setChecksum();

						if ((iph->getSrcAddr() & 0xff000000) == WIFI_IP && (icmph->getId() >> 8) == 1) {
							iph->setDestAddr(NODE1_AETHERNET_IP);
							iph->setHeaderChecksum();
							generateSentAethernetData(true);
							setReadPointer(0);
							originalData.clear();
							receivedData.clear();
							changeState(AethernetReplySending);
						}

						delete iph;
						delete icmph;
					}
				}

				bufferToFill.buffer->clear();
				break;
			}

			default:
				bufferToFill.buffer->clear();
				break;
			}
		}
	}
}

void MainComponent::releaseResources() { delete sampleBuffer; }
