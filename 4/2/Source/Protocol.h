#pragma once

#include <iostream>
#include <cstring>
#include <WinSock2.h>
#include <Windows.h>
#include <pcap.h>

#pragma comment(lib, "packet.lib")
#pragma comment(lib, "wpcap.lib")

using namespace std;

class Command {
	vector<string> destName;
	double echoInterval;
	int echoNum;
	int length;

public:
	auto setDestName(string name) {
		destName.clear();
		char* name_ = name.data() + '\0';
		for (char* token = strtok(name_, "."); token; token = strtok(NULL, "."))
			destName.push_back((string)token);

		length = destName.size();
		return length;
	}

	auto setEchoInterval(double interval) { echoInterval = interval; }

	auto setEchoNum(int num) { echoNum = num; }

	auto getDestName(int index) { return destName[index]; }

	auto getEchoInterval() { return echoInterval; }

	auto getEchoNum() { return echoNum; }

	auto getLength() { return length; }
};

enum ProtocolType {
	ICMP, UDP, TCP
};

class IPHeader {
	uint8_t version = 4, IHL = 5, typeOfService = 0;
	uint16_t totalLen = 0, id = 0;
	uint8_t flags = 0;
	uint16_t fragmentOffset = 0;
	uint8_t TTL = 0, protocol = 0;
	uint16_t headerChecksum = 0;
	uint32_t srcAddr = 0, destAddr = 0;

public:
	auto setVersion(uint8_t v) { version = v; }

	auto setIHL(uint8_t i) { IHL = i; }

	auto setTypeOfService(uint8_t type) { typeOfService = type; }

	auto setTotalLen(uint16_t len) { totalLen = len; }

	auto setId(uint16_t identification) { id = identification; }

	auto setFlags(uint8_t f) { flags = f; }

	auto setFragmentOffset(uint16_t offset) { fragmentOffset = offset; }

	auto setTTL(uint8_t t) { TTL = t; }

	auto setProtocol(uint8_t p) { protocol = p; }

	auto setHeaderChecksum() { headerChecksum = calcChecksum(); }

	auto setSrcAddr(uint32_t addr) { srcAddr = addr; }

	auto setDestAddr(uint32_t addr) { destAddr = addr; }

	auto getVersion() { return version; }

	auto getIHL() { return IHL; }

	auto getTypeOfService() { return typeOfService; }

	auto getTotalLen() { return totalLen; }

	auto getId() { return id; }

	auto getFlags() { return flags; }

	auto getFragmentOffset() { return fragmentOffset; }

	auto getTTL() { return TTL; }

	auto getProtocol() { return protocol; }

	auto getHeaderChecksum() { return headerChecksum; }

	auto getSrcAddr() { return srcAddr; }

	auto getDestAddr() { return destAddr; }

	uint32_t getWord(int index) {
		uint32_t word = 0;
		switch (index) {
		case 0:
			word += (uint32_t)version << 28;
			word += (uint32_t)IHL << 24;
			word += (uint32_t)typeOfService << 16;
			word += (uint32_t)totalLen;
			break;

		case 1:
			word += (uint32_t)id << 16;
			word += (uint32_t)flags << 13;
			word += (uint32_t)fragmentOffset;
			break;

		case 2:
			word += (uint32_t)TTL << 24;
			word += (uint32_t)protocol << 16;
			word += (uint32_t)headerChecksum;
			break;

		case 3:
			word += srcAddr;
			break;

		case 4:
			word += destAddr;
			break;

		default:
			break;
		}

		return word;
	}

	uint8_t getByte(int wordIndex, int byteIndex) {
		auto word = getWord(wordIndex);
		int bitShift = (8 * (3 - byteIndex));
		return (uint8_t)(word >> bitShift & 0xff);
	}

	uint16_t calcChecksum() {
		auto temp = headerChecksum;
		headerChecksum = 0;
		uint32_t sum1 = 0;
		for (int i = 0; i < 5; ++i)
			sum1 += (getWord(i) >> 16) + (getWord(i) & 0xffff);
		headerChecksum = temp;
		uint16_t sum2 = (uint16_t)((sum1 >> 16) + (sum1 & 0xffff));
		return 0xffff - sum2;
	}

	IPHeader() = default;

	IPHeader(ProtocolType pt, uint16_t len, uint16_t i, uint32_t src, uint32_t dest) {
		version = 4;
		IHL = 5;
		typeOfService = 0;
		totalLen = len;
		id = i;
		flags = 0;
		fragmentOffset = 0;
		TTL = 64;
		switch (pt) {
		case ICMP:
			protocol = 1;
			break;
		case UDP:
			protocol = 17;
			break;
		default:
			break;
		}
		srcAddr = src;
		destAddr = dest;
		setHeaderChecksum();
	}
};

enum ICMPType {
	Echo, EchoReply
};

class ICMPHeader {
	uint8_t type = 0, code = 0;
	uint16_t checksum = 0;
	uint16_t id = 0, seqNum = 0;

public:
	auto setType(uint8_t t) { type = t; }

	auto setCode(uint8_t c) { code = c; }

	auto setChecksum() { checksum = calcChecksum(); }

	auto setDirectChecksum(uint16_t c) { checksum = c; }

	auto getType() { return type; }

	auto getCode() { return code; }

	auto getChecksum() { return checksum; }

	auto setId(uint16_t i) { id = i; }

	auto setSeqNum(uint16_t num) { seqNum = num; }

	auto getId() { return id; }

	auto getSeqNum() { return seqNum; }

	uint32_t getWord(int index) {
		uint32_t word = 0;
		switch (index) {
		case 0:
			word += (uint32_t)type << 24;
			word += (uint32_t)code << 16;
			word += (uint32_t)checksum;
			break;

		case 1:
			word += (uint32_t)id << 16;
			word += (uint32_t)seqNum;
			break;

		default:
			break;
		}

		return word;
	}

	uint8_t getByte(int wordIndex, int byteIndex) {
		auto word = getWord(wordIndex);
		int bitShift = (8 * (3 - byteIndex));
		return (uint8_t)(word >> bitShift & 0xff);
	}

	uint16_t calcChecksum() {
		auto temp = checksum;
		checksum = 0;
		uint32_t sum1 = 0;
		for (int i = 0; i < 2; ++i)
			sum1 += (getWord(i) >> 16) + (getWord(i) & 0xffff);
		checksum = temp;
		uint16_t sum2 = (uint16_t)((sum1 >> 16) + (sum1 & 0xffff));
		return 0xffff - sum2;
	}

	ICMPHeader() = default;

	ICMPHeader(ICMPType t, uint16_t sn) {
		type = code = 0;
		id = 0x0001;
		seqNum = sn;
		switch (t) {
		case Echo:
			type = 8;
			break;

		case EchoReply:
			type = 0;
			break;

		default:
			break;
		}
		setChecksum();
	}
};

class UDPHeader {
	uint16_t srcPort, destPort;
	uint16_t length;
	uint16_t checksum;

public:
	auto setSrcPort(uint16_t src) { srcPort = src; }

	auto setDestPort(uint16_t dest) { destPort = dest; }

	auto setLength(uint16_t len) { length = len; }

	auto setChecksum(uint16_t c) { checksum = c; }

	auto getSrcPort() { return srcPort; }

	auto getDestPort() { return destPort; }

	auto getLength() { return length; }

	auto getChecksum() { return checksum; }

	uint32_t getWord(int index) {
		uint32_t word = 0;
		switch (index) {
		case 0:
			word += (uint32_t)srcPort << 16;
			word += (uint32_t)destPort;
			break;

		case 1:
			word += (uint32_t)length << 16;
			word += (uint32_t)checksum;
			break;

		default:
			break;
		}

		return word;
	}

	uint8_t getByte(int wordIndex, int byteIndex) {
		auto word = getWord(wordIndex);
		int bitShift = (8 * (3 - byteIndex));
		return (uint8_t)(word >> bitShift & 0xff);
	}

	UDPHeader() = default;

	UDPHeader(uint16_t src, uint16_t dest, uint16_t len) {
		srcPort = src;
		destPort = dest;
		length = len;
		checksum = 0;
	}
};

class TCPHeader {
	uint16_t srcPort, destPort;
	uint32_t seqNum, ackNum;
	uint8_t dataOffset;
	uint16_t control;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgentPointer;

public:
	auto setSrcPort(uint16_t src) { srcPort = src; }

	auto setDestPort(uint16_t dest) { destPort = dest; }

	auto setSeqNum(uint32_t num) { seqNum = num; }

	auto setAckNum(uint32_t num) { ackNum = num; }

	auto setDataOffset(uint16_t offset) { dataOffset = offset; }

	auto setControl(uint16_t ctrl) { control = ctrl; }

	auto setWindow(uint16_t win) { window = win; }

	auto setChecksum(uint16_t c) { checksum = c; }

	auto setUrgentPointer(uint16_t ptr) { urgentPointer = ptr; }

	auto getSrcPort() { return srcPort; }

	auto getDestPort() { return destPort; }

	auto getSeqNum() { return seqNum; }

	auto getAckNum() { return ackNum; }

	auto getDataOffset() { return dataOffset; }

	auto getControl() { return control; }

	auto getWindow() { return window; }

	auto getChecksum() { return checksum; }

	auto getUrgentPointer() { return urgentPointer; }

	uint32_t getWord(int index) {
		uint32_t word = 0;
		switch (index) {
		case 0:
			word += (uint32_t)srcPort << 16;
			word += (uint32_t)destPort;
			break;

		case 1:
			word += seqNum;
			break;

		case 2:
			word += ackNum;
			break;
		
		case 3:
			word += (uint32_t)dataOffset << 28;
			word += (uint32_t)control << 16;
			word += (uint32_t)window;
			break;

		case 4:
			word += (uint32_t)checksum << 16;
			word += (uint32_t)urgentPointer;
			break;

		default:
			break;
		}

		return word;
	}

	uint8_t getByte(int wordIndex, int byteIndex) {
		auto word = getWord(wordIndex);
		int bitShift = (8 * (3 - byteIndex));
		return (uint8_t)(word >> bitShift & 0xff);
	}

	TCPHeader() = default;
};