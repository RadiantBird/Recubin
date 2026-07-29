#include <Network/NatProtocol.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace NatProtocol {
namespace {

constexpr std::array<uint8_t, 4> RENDEZVOUS_MAGIC = {'R', 'C', 'B', 'N'};
constexpr std::array<uint8_t, 4> PUNCH_MAGIC = {'R', 'C', 'P', 'N'};
constexpr size_t RENDEZVOUS_HEADER_SIZE = 12;
constexpr size_t PUNCH_PACKET_SIZE = 40;

uint16_t readU16(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                                 static_cast<uint16_t>(data[1]));
}

uint32_t readU32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

uint64_t readU64(const uint8_t* data) {
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(value); ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

void writeU16(std::vector<uint8_t>& output, uint16_t value) {
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

void writeU32(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>(value >> 24));
    output.push_back(static_cast<uint8_t>(value >> 16));
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

void writeU64(std::vector<uint8_t>& output, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<uint8_t>(value >> shift));
    }
}

uint32_t readIpv4(const uint8_t* data) {
    uint32_t host = 0;
    std::memcpy(&host, data, sizeof(host));
    return host;
}

void writeIpv4(std::vector<uint8_t>& output, uint32_t host) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&host);
    output.insert(output.end(), bytes, bytes + sizeof(host));
}

bool isRendezvousType(uint8_t value) {
    return value >= static_cast<uint8_t>(RendezvousMessageType::CookieRequest) &&
           value <= static_cast<uint8_t>(RendezvousMessageType::Error);
}

bool isCandidateType(uint8_t value) {
    return value <= static_cast<uint8_t>(CandidateType::PeerReflexive);
}

DecodeResult validateCandidateList(const uint8_t* data, size_t size) {
    std::vector<NetworkCandidate> candidates;
    return decodeCandidates(data, size, candidates);
}

DecodeResult validateRendezvousPayload(RendezvousMessageType type,
                                       const uint8_t* data,
                                       size_t size) {
    switch (type) {
        case RendezvousMessageType::CookieRequest:
        case RendezvousMessageType::CookieChallenge:
            return size == 16 ? DecodeResult::Ok : DecodeResult::InvalidLength;
        case RendezvousMessageType::Create:
            return size >= 17 ? validateCandidateList(data + 16, size - 16)
                              : DecodeResult::InvalidLength;
        case RendezvousMessageType::Created:
            return size == 32 ? DecodeResult::Ok : DecodeResult::InvalidLength;
        case RendezvousMessageType::Join:
            return size >= 25 ? validateCandidateList(data + 24, size - 24)
                              : DecodeResult::InvalidLength;
        case RendezvousMessageType::Joined:
            return size >= 29 ? validateCandidateList(data + 28, size - 28)
                              : DecodeResult::InvalidLength;
        case RendezvousMessageType::PeerJoined:
            return size >= 21 ? validateCandidateList(data + 20, size - 20)
                              : DecodeResult::InvalidLength;
        case RendezvousMessageType::CandidateUpdate:
            if (size == 4) return DecodeResult::Ok;
            return size >= 33 ? validateCandidateList(data + 32, size - 32)
                              : DecodeResult::InvalidLength;
        case RendezvousMessageType::Refresh:
            return (size == 4 || size == 32) ? DecodeResult::Ok
                                             : DecodeResult::InvalidLength;
        case RendezvousMessageType::Promote:
            return (size == 4 || size == 36) ? DecodeResult::Ok
                                             : DecodeResult::InvalidLength;
        case RendezvousMessageType::Leave:
            return (size == 0 || size == 32) ? DecodeResult::Ok
                                             : DecodeResult::InvalidLength;
        case RendezvousMessageType::Error:
            return size == 1 ? DecodeResult::Ok : DecodeResult::InvalidLength;
    }
    return DecodeResult::InvalidType;
}

} // namespace

std::vector<uint8_t> encodeStunBindingRequest(const StunTransactionId& transactionId) {
    std::vector<uint8_t> output;
    output.reserve(20);
    writeU16(output, 0x0001);
    writeU16(output, 0);
    writeU32(output, STUN_MAGIC_COOKIE);
    output.insert(output.end(), transactionId.begin(), transactionId.end());
    return output;
}

DecodeResult decodeStunBindingResponse(const uint8_t* data,
                                       size_t size,
                                       const StunTransactionId& expectedTransactionId,
                                       NetworkCandidate& mappedAddress) {
    if (data == nullptr || size < 20) return DecodeResult::Truncated;
    if (readU16(data) != 0x0101) return DecodeResult::InvalidType;

    const uint16_t messageLength = readU16(data + 2);
    if ((messageLength & 3u) != 0 || static_cast<size_t>(messageLength) + 20 != size) {
        return DecodeResult::InvalidLength;
    }
    if (readU32(data + 4) != STUN_MAGIC_COOKIE) return DecodeResult::InvalidMagic;
    if (!std::equal(expectedTransactionId.begin(),
                    expectedTransactionId.end(),
                    data + 8)) {
        return DecodeResult::WrongTransaction;
    }

    bool foundMappedAddress = false;
    NetworkCandidate decoded;
    size_t offset = 20;
    while (offset < size) {
        if (size - offset < 4) return DecodeResult::Truncated;
        const uint16_t attributeType = readU16(data + offset);
        const uint16_t attributeLength = readU16(data + offset + 2);
        offset += 4;
        if (attributeLength > size - offset) return DecodeResult::Truncated;

        if (attributeType == 0x0020) {
            if (attributeLength < 4) return DecodeResult::InvalidLength;
            if (data[offset] != 0) return DecodeResult::InvalidLength;
            const uint8_t family = data[offset + 1];
            if (family != 0x01) return DecodeResult::UnsupportedAddressFamily;
            if (attributeLength != 8) return DecodeResult::InvalidLength;

            decoded.type = CandidateType::ServerReflexive;
            decoded.port = static_cast<uint16_t>(
                readU16(data + offset + 2) ^ static_cast<uint16_t>(STUN_MAGIC_COOKIE >> 16));
            std::array<uint8_t, 4> addressBytes{};
            addressBytes[0] = data[offset + 4] ^ 0x21u;
            addressBytes[1] = data[offset + 5] ^ 0x12u;
            addressBytes[2] = data[offset + 6] ^ 0xA4u;
            addressBytes[3] = data[offset + 7] ^ 0x42u;
            decoded.host = readIpv4(addressBytes.data());
            foundMappedAddress = true;
        }

        const size_t paddedLength = (static_cast<size_t>(attributeLength) + 3u) & ~size_t{3u};
        if (paddedLength > size - offset) return DecodeResult::Truncated;
        offset += paddedLength;
    }

    if (!foundMappedAddress) return DecodeResult::MissingAttribute;
    mappedAddress = decoded;
    return DecodeResult::Ok;
}

bool encodeRendezvousPacket(const RendezvousPacket& packet, std::vector<uint8_t>& output) {
    const uint8_t rawType = static_cast<uint8_t>(packet.type);
    if (!isRendezvousType(rawType) ||
        packet.payload.size() > MAX_DATAGRAM_SIZE - RENDEZVOUS_HEADER_SIZE ||
        packet.payload.size() > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    if (validateRendezvousPayload(packet.type,
                                  packet.payload.data(),
                                  packet.payload.size()) != DecodeResult::Ok) {
        return false;
    }

    output.clear();
    output.reserve(RENDEZVOUS_HEADER_SIZE + packet.payload.size());
    output.insert(output.end(), RENDEZVOUS_MAGIC.begin(), RENDEZVOUS_MAGIC.end());
    output.push_back(RENDEZVOUS_VERSION);
    output.push_back(rawType);
    writeU16(output, static_cast<uint16_t>(packet.payload.size()));
    writeU32(output, packet.transactionId);
    output.insert(output.end(), packet.payload.begin(), packet.payload.end());
    return true;
}

DecodeResult decodeRendezvousPacket(const uint8_t* data, size_t size, RendezvousPacket& packet) {
    if (data == nullptr || size < RENDEZVOUS_HEADER_SIZE) return DecodeResult::Truncated;
    if (size > MAX_DATAGRAM_SIZE) return DecodeResult::InvalidLength;
    if (!std::equal(RENDEZVOUS_MAGIC.begin(), RENDEZVOUS_MAGIC.end(), data)) {
        return DecodeResult::InvalidMagic;
    }
    if (data[4] != RENDEZVOUS_VERSION) return DecodeResult::UnsupportedVersion;
    if (!isRendezvousType(data[5])) return DecodeResult::InvalidType;

    const uint16_t payloadLength = readU16(data + 6);
    if (static_cast<size_t>(payloadLength) + RENDEZVOUS_HEADER_SIZE != size) {
        return DecodeResult::InvalidLength;
    }

    const auto type = static_cast<RendezvousMessageType>(data[5]);
    const uint8_t* payload = data + RENDEZVOUS_HEADER_SIZE;
    const DecodeResult payloadResult = validateRendezvousPayload(type, payload, payloadLength);
    if (payloadResult != DecodeResult::Ok) return payloadResult;

    packet.type = type;
    packet.transactionId = readU32(data + 8);
    packet.payload.assign(payload, data + size);
    return DecodeResult::Ok;
}

bool encodeCandidates(const std::vector<NetworkCandidate>& candidates,
                      std::vector<uint8_t>& output) {
    if (candidates.size() > MAX_CANDIDATES) return false;
    for (const NetworkCandidate& candidate : candidates) {
        if (!isCandidateType(static_cast<uint8_t>(candidate.type)) || candidate.port == 0) {
            return false;
        }
    }

    output.clear();
    output.reserve(1 + candidates.size() * 7);
    output.push_back(static_cast<uint8_t>(candidates.size()));
    for (const NetworkCandidate& candidate : candidates) {
        output.push_back(static_cast<uint8_t>(candidate.type));
        writeIpv4(output, candidate.host);
        writeU16(output, candidate.port);
    }
    return true;
}

DecodeResult decodeCandidates(const uint8_t* data,
                              size_t size,
                              std::vector<NetworkCandidate>& candidates) {
    if (data == nullptr || size < 1) return DecodeResult::Truncated;
    const size_t count = data[0];
    if (count > MAX_CANDIDATES) return DecodeResult::TooManyCandidates;
    if (size != 1 + count * 7) return DecodeResult::InvalidLength;

    std::vector<NetworkCandidate> decoded;
    decoded.reserve(count);
    size_t offset = 1;
    for (size_t i = 0; i < count; ++i) {
        if (!isCandidateType(data[offset])) return DecodeResult::InvalidType;
        NetworkCandidate candidate;
        candidate.type = static_cast<CandidateType>(data[offset]);
        candidate.host = readIpv4(data + offset + 1);
        candidate.port = readU16(data + offset + 5);
        if (candidate.port == 0) return DecodeResult::InvalidLength;
        decoded.push_back(candidate);
        offset += 7;
    }
    candidates = std::move(decoded);
    return DecodeResult::Ok;
}

bool encodePunchPacket(const PunchPacket& packet, std::vector<uint8_t>& output) {
    const uint8_t rawType = static_cast<uint8_t>(packet.type);
    if (rawType < static_cast<uint8_t>(PunchMessageType::Probe) ||
        rawType > static_cast<uint8_t>(PunchMessageType::Acknowledge)) {
        return false;
    }

    output.clear();
    output.reserve(PUNCH_PACKET_SIZE);
    output.insert(output.end(), PUNCH_MAGIC.begin(), PUNCH_MAGIC.end());
    output.push_back(PUNCH_VERSION);
    output.push_back(rawType);
    writeU16(output, 0);
    writeU64(output, packet.nonce);
    output.insert(output.end(), packet.token.begin(), packet.token.end());
    writeU32(output, packet.senderPeerId);
    writeU32(output, packet.roomEpoch);
    return true;
}

DecodeResult decodePunchPacket(const uint8_t* data, size_t size, PunchPacket& packet) {
    if (data == nullptr || size < PUNCH_PACKET_SIZE) return DecodeResult::Truncated;
    if (size != PUNCH_PACKET_SIZE) return DecodeResult::InvalidLength;
    if (!std::equal(PUNCH_MAGIC.begin(), PUNCH_MAGIC.end(), data)) {
        return DecodeResult::InvalidMagic;
    }
    if (data[4] != PUNCH_VERSION) return DecodeResult::UnsupportedVersion;
    if (data[5] < static_cast<uint8_t>(PunchMessageType::Probe) ||
        data[5] > static_cast<uint8_t>(PunchMessageType::Acknowledge)) {
        return DecodeResult::InvalidType;
    }
    if (readU16(data + 6) != 0) return DecodeResult::InvalidLength;

    PunchPacket decoded;
    decoded.type = static_cast<PunchMessageType>(data[5]);
    decoded.nonce = readU64(data + 8);
    std::copy_n(data + 16, decoded.token.size(), decoded.token.begin());
    decoded.senderPeerId = readU32(data + 32);
    decoded.roomEpoch = readU32(data + 36);
    packet = decoded;
    return DecodeResult::Ok;
}

} // namespace NatProtocol
