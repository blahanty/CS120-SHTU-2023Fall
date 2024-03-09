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
					if (++samplesReceived == BYTE_NUM * 8 * BIT_WIDTH) {
						maxSum = SUM_THRESHOLD;
						samplesReceived = 0;
						begin = false;
						for (int i = 0; i < BYTE_NUM * 8; ++i) {
							sum = 0;
							for (int j = 0; j < BIT_WIDTH; ++j) {
								sum += originalData.front();
								originalData.pop_front();
							}
							if (sum > 1) {
								originalData.push_back(1);
								border = -0.18;
							}
							else {
								originalData.push_back(0);
								border = 3;
							}
						}

						iph = new IPHeader();
						iph->setVersion(dequeToHex(4) & 0xf);
						iph->setIHL(dequeToHex(4) & 0xf);
						iph->setTypeOfService(dequeToHex(8) & 0xff);
						dumpBit(16);
						iph->setId(dequeToHex(16) & 0xffff);
						iph->setFlags(dequeToHex(3) & 0x7);
						iph->setFragmentOffset(dequeToHex(13) & 0x1fff);
						iph->setTTL((dequeToHex(8) & 0xff) - 1);
						iph->setProtocol(dequeToHex(8) & 0xff);
						dumpBit(80);
						iph->setSrcAddr(NODE2_WIFI_IP);
						iph->setDestAddr(DNS_WIFI_IP);

						udph = new UDPHeader();
						udph->setSrcPort(3);
						udph->setDestPort(53);
						dumpBit(32);
						udph->setLength((dequeToHex(16) & 0xffff));
						dumpBit(16);

						iph->setTotalLen(udph->getLength() + 20);
						iph->setHeaderChecksum();
						udph->setChecksum(0);

						for (int i = 42; i < 34 + udph->getLength(); ++i)
							sentEthernetData[i] = (uint8_t)dequeToHex(8);

						for (int i = 34 + udph->getLength(); i < BYTE_NUM; ++i)
							sentEthernetData[i] = 0;

						calcHeaderChecksum(UDP);

						int cnt = sentEthernetData[54], ptr = 55;
						while ("CS120 is the best!") {
							domainName.append((char*)(sentEthernetData + ptr++), 1);
							if (!--cnt) {
								cnt = sentEthernetData[ptr++];
								if (cnt)
									domainName.append(".");
								else
									break;
							}
						}

						originalData.clear();
						receivedData.clear();
						generateSentEthernetData(UDP, NODE2_MAC_ADDR, DNS_MAC_ADDR);
						changeState(DNSSending);
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

				while (!begin && receivedData.size() > PREAMBLE_LENGTH) {
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
						sum = 0;
						break;
					}
					receivedData.pop_front();
				}

				while (begin&&receivedData.size() >= PACKET_BYTES * 8 * BIT_WIDTH) {
						maxSum = SUM_THRESHOLD;
						samplesReceived = 0;
						begin = false;
						if (!IPPacketLen) {
							for (int i = 0; i < 16; ++i) {
								sum = 0;
								IPPacketLen <<= 1;
								for (int j = 0; j < BIT_WIDTH; ++j) {
									sum += receivedData[0];
									receivedData.pop_front();
								}

								if (sum > 0.8)
									IPPacketLen++;
							}
							bitsReceived += 16;
						}
						while (receivedData.size() >= BIT_WIDTH ) {
							sum = 0;
							for (int i = 0; i < BIT_WIDTH; ++i) {
								sum += receivedData[0];
								receivedData.pop_front();
							}

							if (sum > 0)
								originalData.push_back(1);
							else
								originalData.push_back(0);

							if (originalData.size() == IPPacketLen * 8) {
								ofstream outputFile(outputPath, ios::app);
								for (int i = 0; i < IPPacketLen; ++i)
									outputFile << (char)dequeToHex(8);

								outputFile.close();
								originalData.clear();
								receivedData.clear();
								changeState(Ready);
								break;
							}
							if (++bitsReceived == PACKET_BYTES * 8) {
								bitsReceived = 0; 
								break;
							}
						}
						
						break;
					}
					
				bufferToFill.buffer->clear();
				break;

			case DNSSending:
				pcap_sendpacket(adhandle, sentEthernetData, 14 + 20 + udph->getLength());
				changeState(DNSReceiving);
				break;

			case DNSReceiving:
			{
				pcap_pkthdr* pktHeader;
				const u_char* pktData;
				int res;
				if ((res = pcap_next_ex(adhandle, &pktHeader, &pktData))) {
					if (res > 0) {
						iph = new IPHeader();
						iph->setVersion((*((uint8_t*)(pktData + 14))) >> 4 & 0xf);
						iph->setIHL((*((uint8_t*)(pktData + 14))) & 0xf);
						iph->setTypeOfService(*((uint8_t*)(pktData + 15)));
						uint16_t totalLen = ((*((uint16_t*)(pktData + 16)) & 0xff) << 8) + (*((uint16_t*)(pktData + 17)) & 0xff);
						iph->setTotalLen(52);
						iph->setId(((*((uint16_t*)(pktData + 18)) & 0xff) << 8) + (*((uint16_t*)(pktData + 19)) & 0xff) + 10);
						iph->setFlags((*((uint8_t*)(pktData + 20))) >> 5 & 0x7);
						iph->setFragmentOffset(((*((uint16_t*)(pktData + 20)) & 0x1f) << 8) + (*((uint16_t*)(pktData + 21)) & 0xff));
						iph->setTTL((*((uint8_t*)(pktData + 22))) - 1);
						iph->setProtocol(6);
						uint32_t src = 0, dest = 0;
						for (int i = 0; i < 4; ++i) {
							int bits = (3 - i) * 8;
							src += (*((uint32_t*)(pktData + 26 + i)) & 0xff) << bits;
							dest += (*((uint32_t*)(pktData + totalLen + 10 + i)) & 0xff) << bits;
						}
						iph->setSrcAddr(NODE2_WIFI_IP);
						iph->setDestAddr(dest);
						iph->setHeaderChecksum();

						udph = new UDPHeader();
						udph->setSrcPort(((*((uint16_t*)(pktData + 34)) & 0xff) << 8) + (*((uint16_t*)(pktData + 35)) & 0xff));
						udph->setDestPort(((*((uint16_t*)(pktData + 36)) & 0xff) << 8) + (*((uint16_t*)(pktData + 37)) & 0xff));
						udph->setLength(((*((uint16_t*)(pktData + 38)) & 0xff) << 8) + (*((uint16_t*)(pktData + 39)) & 0xff));

						if ((src & 0xffffff00) == WIFI_IP && udph->getDestPort() == 3) {
							tcph = new TCPHeader;
							tcph->setSrcPort(PORT);
							tcph->setDestPort(80);
							tcph->setSeqNum(INIT_SEQ_NUM);
							tcph->setAckNum(0);
							tcph->setDataOffset(8);
							tcph->setControl(2);
							tcph->setWindow(0xfaf0);
							tcph->setUrgentPointer(0);

							sentEthernetData[54] = 0x02;
							sentEthernetData[55] = 0x04;
							sentEthernetData[56] = 0x05;
							sentEthernetData[57] = 0xb4;
							sentEthernetData[58] = 0x01;
							sentEthernetData[59] = 0x03;
							sentEthernetData[60] = 0x03;
							sentEthernetData[61] = 0x08;
							sentEthernetData[62] = 0x01;
							sentEthernetData[63] = 0x01;
							sentEthernetData[64] = 0x04;
							sentEthernetData[65] = 0x02;
							for (int i = 66; i < BYTE_NUM; ++i)
								sentEthernetData[i] = 0;

							calcHeaderChecksum(TCP);
							generateSentEthernetData(TCP, NODE2_MAC_ADDR, ROUTER_WIFI_MAC);
							changeState(TCPSynSending);
						}
					}
				}

				bufferToFill.buffer->clear();
				break;
			}

			case TCPSynSending:
				pcap_sendpacket(adhandle, sentEthernetData, 66);
				changeState(TCPSynReceiving);
				break;

			case TCPSynReceiving:
			{
				pcap_pkthdr* pktHeader;
				const u_char* pktData;
				int res;
				if ((res = pcap_next_ex(adhandle, &pktHeader, &pktData)) >= 0) {
					if (res > 0) {
						iph->setTotalLen(40);
						iph->setTTL((*((uint8_t*)(pktData + 22))) - 1);
						iph->setProtocol(*((uint8_t*)(pktData + 23)));
						uint32_t src = 0, dest = 0;
						for (int i = 0; i < 4; ++i) {
							int bits = (3 - i) * 8;
							src += (*((uint32_t*)(pktData + 26 + i)) & 0xff) << bits;
							dest += (*((uint32_t*)(pktData + 30 + i)) & 0xff) << bits;
						}
						iph->setSrcAddr(dest);
						iph->setDestAddr(src);

						tcph = new TCPHeader;
						tcph->setSrcPort(((*((uint16_t*)(pktData + 36)) & 0xff) << 8) + (*((uint16_t*)(pktData + 37)) & 0xff));
						tcph->setDestPort(((*((uint16_t*)(pktData + 34)) & 0xff) << 8) + (*((uint16_t*)(pktData + 35)) & 0xff));

						if (tcph->getSrcPort() == PORT) {
							uint32_t seq = 0;
							for (int i = 0; i < 4; ++i) {
								int bits = (3 - i) * 8;
								seq += (*((uint32_t*)(pktData + 38 + i)) & 0xff) << bits;
							}
							tcph->setSeqNum(INIT_SEQ_NUM + 1);
							tcph->setAckNum(seq + 1);
							tcph->setDataOffset(5);
							tcph->setControl(0x010);
							tcph->setWindow(0x0402);
							tcph->setUrgentPointer(0);
							for (int i = 54; i < BYTE_NUM; ++i)
								sentEthernetData[i] = 0;

							calcHeaderChecksum(TCP);
							iph->setId(iph->getId() + 1);
							iph->setHeaderChecksum();
							generateSentEthernetData(TCP, NODE2_MAC_ADDR, ROUTER_WIFI_MAC);
							changeState(TCPAckSending);
						}
					}
				}

				bufferToFill.buffer->clear();
				break;
			}

			case TCPAckSending:
				pcap_sendpacket(adhandle, sentEthernetData, 54);

				iph->setId(iph->getId() + 1);
				iph->setTotalLen(domainName.length() + 103);
				iph->setHeaderChecksum();

				tcph->setControl(0x018);

				sentEthernetData[54] = 0x47;
				sentEthernetData[55] = 0x45;
				sentEthernetData[56] = 0x54;
				sentEthernetData[57] = 0x20;
				sentEthernetData[58] = 0x2f;
				sentEthernetData[59] = 0x20;
				sentEthernetData[60] = 0x48;
				sentEthernetData[61] = 0x54;
				sentEthernetData[62] = 0x54;
				sentEthernetData[63] = 0x50;
				sentEthernetData[64] = 0x2f;
				sentEthernetData[65] = 0x31;
				sentEthernetData[66] = 0x2e;
				sentEthernetData[67] = 0x31;
				sentEthernetData[68] = 0x0d;
				sentEthernetData[69] = 0x0a;
				sentEthernetData[70] = 0x48;
				sentEthernetData[71] = 0x6f;
				sentEthernetData[72] = 0x73;
				sentEthernetData[73] = 0x74;
				sentEthernetData[74] = 0x3a;
				sentEthernetData[75] = 0x20;
				for (int i = 0; i < domainName.length(); ++i)
					sentEthernetData[76 + i] = domainName.at(i);

				sentEthernetData[domainName.length() + 76] = 0x0d;
				sentEthernetData[domainName.length() + 77] = 0x0a;
				sentEthernetData[domainName.length() + 78] = 0x55;
				sentEthernetData[domainName.length() + 79] = 0x73;
				sentEthernetData[domainName.length() + 80] = 0x65;
				sentEthernetData[domainName.length() + 81] = 0x72;
				sentEthernetData[domainName.length() + 82] = 0x2d;
				sentEthernetData[domainName.length() + 83] = 0x41;
				sentEthernetData[domainName.length() + 84] = 0x67;
				sentEthernetData[domainName.length() + 85] = 0x65;
				sentEthernetData[domainName.length() + 86] = 0x6e;
				sentEthernetData[domainName.length() + 87] = 0x74;
				sentEthernetData[domainName.length() + 88] = 0x3a;
				sentEthernetData[domainName.length() + 89] = 0x20;
				sentEthernetData[domainName.length() + 90] = 0x63;
				sentEthernetData[domainName.length() + 91] = 0x75;
				sentEthernetData[domainName.length() + 92] = 0x72;
				sentEthernetData[domainName.length() + 93] = 0x6c;
				sentEthernetData[domainName.length() + 94] = 0x2f;
				sentEthernetData[domainName.length() + 95] = 0x38;
				sentEthernetData[domainName.length() + 96] = 0x2e;
				sentEthernetData[domainName.length() + 97] = 0x34;
				sentEthernetData[domainName.length() + 98] = 0x2e;
				sentEthernetData[domainName.length() + 99] = 0x30;
				sentEthernetData[domainName.length() + 100] = 0x0d;
				sentEthernetData[domainName.length() + 101] = 0x0a;
				sentEthernetData[domainName.length() + 102] = 0x41;
				sentEthernetData[domainName.length() + 103] = 0x63;
				sentEthernetData[domainName.length() + 104] = 0x63;
				sentEthernetData[domainName.length() + 105] = 0x65;
				sentEthernetData[domainName.length() + 106] = 0x70;
				sentEthernetData[domainName.length() + 107] = 0x74;
				sentEthernetData[domainName.length() + 108] = 0x3a;
				sentEthernetData[domainName.length() + 109] = 0x20;
				sentEthernetData[domainName.length() + 110] = 0x2a;
				sentEthernetData[domainName.length() + 111] = 0x2f;
				sentEthernetData[domainName.length() + 112] = 0x2a;
				sentEthernetData[domainName.length() + 113] = 0x0d;
				sentEthernetData[domainName.length() + 114] = 0x0a;
				sentEthernetData[domainName.length() + 115] = 0x0d;
				sentEthernetData[domainName.length() + 116] = 0x0a;
				for (int i = domainName.length() + 117; i < BYTE_NUM; ++i)
					sentEthernetData[i] = 0;

				calcHeaderChecksum(TCP);
				generateSentEthernetData(TCP, NODE2_MAC_ADDR, ROUTER_WIFI_MAC);
				changeState(TCPGetSending);
				break;

			case TCPGetSending:
				pcap_sendpacket(adhandle, sentEthernetData, domainName.length() + 117);
				changeState(TCPAckReceivng);
				break;

			case TCPAckReceivng:
			{
				pcap_pkthdr* pktHeader;
				const u_char* pktData;
				int res;
				if ((res = pcap_next_ex(adhandle, &pktHeader, &pktData)) >= 0) {
					if (res > 0) {
						iph->setId(((*((uint16_t*)(pktData + 18)) & 0xff) << 8) + (*((uint16_t*)(pktData + 19)) & 0xff) + 1);
						uint32_t src = 0;
						for (int i = 0; i < 4; ++i) {
							int bits = (3 - i) * 8;
							src += (*((uint32_t*)(pktData + 26 + i)) & 0xff) << bits;
						}

						uint16_t dest = ((*((uint16_t*)(pktData + 36)) & 0xff) << 8) + (*((uint16_t*)(pktData + 37)) & 0xff);

						if (dest == PORT)
							changeState(TCPDataReceiving);
					}
				}

				HTMLData.clear();
				bufferToFill.buffer->clear();
				break;
			}

			case TCPDataReceiving:
			{
				pcap_pkthdr* pktHeader;
				const u_char* pktData;
				int res;
				if ((res = pcap_next_ex(adhandle, &pktHeader, &pktData)) >= 0) {
					if (res > 0) {
						uint16_t len = ((*((uint16_t*)(pktData + 16)) & 0xff) << 8) + (*((uint16_t*)(pktData + 17)) & 0xff);
						uint32_t src = 0, seq = 0;
						for (int i = 0; i < 4; ++i) {
							int bits = (3 - i) * 8;
							src += (*((uint32_t*)(pktData + 26 + i)) & 0xff) << bits;
							seq += (*((uint32_t*)(pktData + 38 + i)) & 0xff) << bits;
						}

						uint16_t dest = ((*((uint16_t*)(pktData + 36)) & 0xff) << 8) + (*((uint16_t*)(pktData + 37)) & 0xff);
						bool push = (*((uint8_t*)(pktData + 47))) >> 3 & 1;

						if (dest == PORT) {
							uint32_t temp = 0;
							int beginPtr = 14 + len;
							if (((*((uint16_t*)(pktData + 18)) & 0xff) << 8) + (*((uint16_t*)(pktData + 19)) & 0xff) == iph->getId()) {
								for (int i = 54; i < 14 + len; ++i) {
									temp = 0;
									for (int j = 0; j < 4; ++j) {
										int bits = (3 - j) * 8;
										temp += (*((uint32_t*)(pktData + i + j)) & 0xff) << bits;
									}
									if (temp == 0x0d0a0d0a) {
										beginPtr = i + 4;
										break;
									}
								}

								for (int i = beginPtr; i < 14 + len; ++i)
									HTMLData.push_back(*((uint8_t*)(pktData + i)));
							}
							else {
								for (int i = 54; i < 14 + len; ++i)
									HTMLData.push_back(*((uint8_t*)(pktData + i)));
							}

							if (push) {
								len = HTMLData.size();
								HTMLData.push_front((uint8_t)(len & 0xff));
								HTMLData.push_front((uint8_t)(len >> 8 & 0xff));
								int packetByteNum = 0;
								sentAethernetData.insert(sentAethernetData.end(), preambleWave2.begin(), preambleWave2.end());
								while (HTMLData.size()) {
									generateAethernetDataFromByte(HTMLData.front());
									HTMLData.pop_front();
									if (++packetByteNum == PACKET_BYTES) {
										packetByteNum = 0;
										for (int i = 0; i < DELAY_BITS; ++i)
											sentAethernetData.push_back(0);

										sentAethernetData.insert(sentAethernetData.end(), preambleWave2.begin(), preambleWave2.end());
									}
								}
								changeState(AethernetReplySending);
								delete tcph;

							}
						}
					}
				}

				bufferToFill.buffer->clear();
				break;
			}
			break;

			default:
				bufferToFill.buffer->clear();
				break;
			}
		}
	}
}

void MainComponent::releaseResources() { delete sampleBuffer; }
