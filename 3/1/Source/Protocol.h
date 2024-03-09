#pragma once

#include <iostream>
#include <cstring>

using namespace std;

class Command {
	string cmdName;
	int destPort;
	double echoInterval;
	int echoNum;

public:
	auto getCmdName() { return cmdName; }

	auto getDestPort() { return destPort; }

	auto getEchoInterval() { return echoInterval; }

	auto getEchoNum() { return echoNum; }

	auto setCmdName(string name) { cmdName = name; }

	auto setDestPort(int port) { destPort = port; }

	auto setEchoInterval(double interval) { echoInterval = interval; }

	auto setEchoNum(int num) { echoNum = num; }
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
			sum1 += getWord(i) >> 16 + getWord(i) & 0xffff;
		headerChecksum = temp;
		uint16_t sum2 = (uint16_t)(sum1 >> 16 + sum1 & 0xffff);
		return 0xffff - sum2;
	}

	IPHeader() = default;

	IPHeader(uint16_t len, uint16_t i, uint32_t src, uint32_t dest) {
		version = 4;
		IHL = 5;
		typeOfService = 0;
		totalLen = len;
		id = i;
		flags = 0;
		fragmentOffset = 0;
		TTL = 64;
		protocol = 1;
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
			sum1 += getWord(i) >> 16 + getWord(i) & 0xffff;
		checksum = temp;
		uint16_t sum2 = (uint16_t)(sum1 >> 16 + sum1 & 0xffff);
		return 0xffff - sum2;
	}

	ICMPHeader() = default;

	ICMPHeader(ICMPType t, Command cmd, uint16_t sn) {
		type = code = 0;
		id = cmd.getDestPort();
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