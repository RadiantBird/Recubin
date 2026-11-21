#pragma once

#include <Network/NetworkTypes.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace NatProtocol {

constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442u;
constexpr size_t STUN_TRANSACTION_ID_SIZE = 12;
constexpr size_t MAX_DATAGRAM_SIZE = 1200;
constexpr size_t MAX_CANDIDATES = 32;
constexpr uint8_t RENDEZVOUS_VERSION = 1;
constexpr uint8_t PUNCH_VERSION = 1;

using StunTransactionId = std::array<uint8_t, STUN_TRANSACTION_ID_SIZE>;

enum class DecodeResult : uint8_t {
    Ok,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidType,
    InvalidLength,
    WrongTransaction,
    UnsupportedAddressFamily,
    MissingAttribute,
    TooManyCandidates
};

std::vector<uint8_t> encodeStunBindingRequest(const StunTransactionId& transactionId);
DecodeResult decodeStunBindingResponse(const uint8_t* data,
                                       size_t size,
                                       const StunTransactionId& expectedTransactionId,
                                       NetworkCandidate& mappedAddress);

enum class RendezvousMessageType : uint8_t {
    CookieRequest   = 1,
    CookieChallenge = 2,
    Create          = 3,
    Created         = 4,
    Join            = 5,
    Joined          = 6,
    PeerJoined      = 7,
    CandidateUpdate = 8,
    Refresh         = 9,
    Promote         = 10,
    Leave           = 11,
    Error           = 12
};

// Envelopeは RCBN | version:u8 | type:u8 | payloadSize:u16be | txId:u32be。
// payload:
// CookieRequest=random16, CookieChallenge=cookie16,
// Create=cookie16+candidates, Created=room8+token16+peerId:u32be+epoch:u32be,
// Join=cookie16+room8+candidates,
// Joined=token16+peerId:u32be+epoch:u32be+hostPeerId:u32be+candidates,
// PeerJoined=peerId:u32be+token16+candidates,
// CandidateUpdate=cookie16+token16+candidates (responseはepoch:u32be),
// Refresh=cookie16+token16 (responseはepoch:u32be),
// Promote=cookie16+token16+expectedEpoch:u32be (responseはnewEpoch:u32be),
// Leave=cookie16+token16 (responseは空), Error=code:u8。
struct RendezvousPacket {
    RendezvousMessageType type = RendezvousMessageType::Error;
    uint32_t transactionId = 0;
    std::vector<uint8_t> payload;
};

bool encodeRendezvousPacket(const RendezvousPacket& packet, std::vector<uint8_t>& output);
DecodeResult decodeRendezvousPacket(const uint8_t* data, size_t size, RendezvousPacket& packet);

bool encodeCandidates(const std::vector<NetworkCandidate>& candidates,
                      std::vector<uint8_t>& output);
DecodeResult decodeCandidates(const uint8_t* data,
                              size_t size,
                              std::vector<NetworkCandidate>& candidates);

enum class PunchMessageType : uint8_t {
    Probe = 1,
    Acknowledge = 2
};

struct PunchPacket {
    PunchMessageType type = PunchMessageType::Probe;
    uint64_t nonce = 0;
    AdmissionToken token{};
    PeerId senderPeerId = 0;
    uint32_t roomEpoch = 0;
};

bool encodePunchPacket(const PunchPacket& packet, std::vector<uint8_t>& output);
DecodeResult decodePunchPacket(const uint8_t* data, size_t size, PunchPacket& packet);

} // namespace NatProtocol
