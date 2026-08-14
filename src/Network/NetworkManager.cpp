#include <Network/NetworkManager.hpp>
#include <Network/ByteStream.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <unistd.h>
#endif

namespace {
constexpr size_t kMaxChatBytes = 512;
constexpr uint8_t kGameProtocolVersion = 2;
constexpr float kDiscoveryTimeout = 8.0f;
constexpr float kPunchTimeout = 8.0f;
constexpr float kEnetTimeout = 8.0f;

uint32_t readBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

void appendBe32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void appendToken(std::vector<uint8_t>& out, const AdmissionToken& token) {
    out.insert(out.end(), token.begin(), token.end());
}

uint64_t randomU64() {
    std::random_device rd;
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(value); i += 2) {
        value = (value << 16) ^ static_cast<uint64_t>(rd());
    }
    return value;
}

bool validateChatText(const uint8_t* data, size_t len, std::string& out) {
    if (!data || len == 0 || len > kMaxChatBytes) return false;
    out.assign(reinterpret_cast<const char*>(data), len);
    for (unsigned char c : out) {
        // UTF-8の非ASCII byteは保持し、端末制御に使えるASCII制御文字だけを拒否する。
        if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

std::vector<uint8_t> makeChatPayload(PeerId senderId, const std::string& text) {
    ByteWriter w;
    w.data.reserve(1 + sizeof(uint32_t) + text.size());
    w.writeU8(static_cast<uint8_t>(MessageType::Chat));
    w.writeU32(static_cast<uint32_t>(senderId));
    w.data.insert(w.data.end(), text.begin(), text.end());
    return std::move(w.data);
}
}

NetworkManager& NetworkManager::get() {
    static NetworkManager instance;
    return instance;
}

NetworkManager::~NetworkManager() {
    shutdown();
}

const char* NetworkManager::roleToString(NetworkRole role) {
    switch (role) {
        case NetworkRole::Host:   return "Host";
        case NetworkRole::Client: return "Client";
        case NetworkRole::Offline:
        default:
            return "Offline";
    }
}

void NetworkManager::changeRole(NetworkRole newRole) {
    if (m_role == newRole) return;
    NetworkRole old = m_role;
    m_role = newRole;
    if (onRoleChanged) onRoleChanged(old, newRole);
}

float NetworkManager::measureCpuScore() {
    // 固定計算量のマイクロベンチ。xorshift32と浮動小数演算を混ぜたループを固定回数回して
    // 経過時間を計測し、回数/経過ミリ秒 をスコアとする。最適化で消えないよう volatile で読み出す。
    constexpr uint64_t kIterations = 40'000'000ULL;

    auto start = std::chrono::steady_clock::now();

    uint32_t state = 2463534242u;
    float acc = 1.0f;
    for (uint64_t i = 0; i < kIterations; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        acc += static_cast<float>(state & 0xFFFF) * 0.0001f;
        if (acc > 1.0e6f) acc = 1.0f;
    }

    auto end = std::chrono::steady_clock::now();
    volatile float result = acc; // 最適化での消去防止
    (void)result;

    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    if (elapsedMs < 0.001) elapsedMs = 0.001;

    return static_cast<float>(static_cast<double>(kIterations) / elapsedMs);
}

bool NetworkManager::initializeBoundHost(uint16_t port) {
    if (!m_enetInitialized) {
        if (enet_initialize() != 0) {
            RCBN_ERROR("ENet: enet_initialize failed");
            return false;
        }
        m_enetInitialized = true;
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    m_host = enet_host_create(&address, 32, static_cast<size_t>(NetworkChannel::Count), 0, 0);
    if (!m_host) {
        RCBN_ERROR("ENet: failed to start host on port " << port);
        return false;
    }
    m_host->intercept = &NetworkManager::interceptPacket;
    m_listenPort = m_host->address.port;
    return true;
}

float NetworkManager::getHostPeerRttMs() const {
    if (m_role != NetworkRole::Client || m_peers.empty() || !m_peers.front())
        return 0.0f;
    return static_cast<float>(m_peers.front()->roundTripTime);
}

bool NetworkManager::startHost(uint16_t port, bool isPlayerHost) {
    shutdown();
    if (!initializeBoundHost(port)) return false;

    m_localIsPlayer = isPlayerHost;
    changeRole(NetworkRole::Host);
    m_connectionState = ConnectionState::Connected;
    m_peers.clear();

    if (m_localCpuScore == 0.0f) {
        m_localCpuScore = measureCpuScore();
        RCBN_LOG("NetworkManager: local CPU score = " << m_localCpuScore);
    }
    m_localPeerId = 1;
    m_nextPeerId = 2;
    m_roster.clear();
    PeerInfo self;
    self.id = 1;
    self.endpoint.host = 0;
    self.endpoint.listenPort = m_listenPort;
    self.cpuScore = m_localCpuScore;
    self.latencyMs = 0.0f;
    self.isHost = true;
    self.isPlayer = m_localIsPlayer;
    m_roster.push_back(self);
    m_peerIds.clear();
    m_rosterDirty = true;

    RCBN_LOG("NetworkManager: Host started on port " << m_listenPort);
    return true;
}

bool NetworkManager::connect(const std::string& address, uint16_t port, uint16_t listenPort) {
    shutdown();

    if (!initializeBoundHost(listenPort)) return false;

    ENetAddress enetAddress;
    enet_address_set_host(&enetAddress, address.c_str());
    enetAddress.port = port;

    m_expectedPeer = enet_host_connect(m_host, &enetAddress, static_cast<size_t>(NetworkChannel::Count), 0);
    if (!m_expectedPeer) {
        RCBN_ERROR("ENet: no available peers for connect() to " << address << ":" << port);
        enet_host_destroy(m_host);
        m_host = nullptr;
        return false;
    }

    m_localIsPlayer = true;
    changeRole(NetworkRole::Client);
    m_connectionState = ConnectionState::Connecting;
    m_peers.clear(); // ハンドシェイク完了(ENET_EVENT_TYPE_CONNECT)時にhandleEventで追加される

    if (m_localCpuScore == 0.0f) {
        m_localCpuScore = measureCpuScore();
        RCBN_LOG("NetworkManager: local CPU score = " << m_localCpuScore);
    }
    // m_localPeerId は 0 のまま(Welcome待ち。再接続時は既存値保持だがそれはPhase 2)
    m_roster.clear();
    m_peerIds.clear();

    RCBN_LOG("NetworkManager: connecting to " << address << ":" << port << " ...");
    return true;
}

bool NetworkManager::createRoom(const NatConfig& config) {
    return beginNat(config, true, {});
}

bool NetworkManager::joinRoom(const std::string& roomCode, const NatConfig& config) {
    if (roomCode.size() != 8) {
        shutdown();
        failConnection(ConnectionError::RoomNotFound, "room code must contain 8 characters");
        return false;
    }
    return beginNat(config, false, roomCode);
}

void NetworkManager::shutdown() {
    if (m_natMode && m_haveCookie && m_host && m_admissionToken != AdmissionToken{}) {
        std::vector<uint8_t> payload(m_cookie.begin(), m_cookie.end());
        appendToken(payload, m_admissionToken);
        sendRendezvous(NatProtocol::RendezvousMessageType::Leave, payload, false);
    }
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_peers.clear();
    m_expectedPeer = nullptr;
    changeRole(NetworkRole::Offline);

    m_peerIds.clear();
    m_pendingAdmissions.clear();
    m_roster.clear();
    m_localPeerId = 0;
    m_localIsPlayer = true;
    m_resourceReportTimer = 0.0f;
    m_rosterBroadcastTimer = 0.0f;
    m_rosterDirty = false;

    m_migrationState = MigrationState::None;
    m_migrationRetryTimer = 0.0f;
    m_migrationElapsed = 0.0f;
    m_natMode = false;
    m_connectionState = ConnectionState::Offline;
    m_connectionError = ConnectionError::None;
    m_roomCode.clear();
    m_roomEpoch = 0;
    m_admissionToken = {};
    m_peerTokens.clear();
    m_pendingPunches.clear();
    m_pendingPunchTimer = 0.0f;
    m_localCandidates.clear();
    m_haveCookie = false;
    m_recoveringCookie = false;
    m_promotePending = false;
    m_promoteRetryTimer = 0.0f;
    m_promoteRetryElapsed = 0.0f;
    m_retryDatagram.clear();
    m_stateElapsed = 0.0f;
    m_retryTimer = 0.0f;
    m_retryDelay = 0.25f;
    m_refreshTimer = 0.0f;
    m_stunRefreshTimer = 0.0f;
}

bool NetworkManager::resolveAddress(const std::string& host, uint16_t port, ENetAddress& address) const {
    address.port = port;
    return !host.empty() && enet_address_set_host(&address, host.c_str()) == 0;
}

void NetworkManager::failConnection(ConnectionError error, const char* detail) {
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_peers.clear();
    m_expectedPeer = nullptr;
    m_peerIds.clear();
    m_pendingAdmissions.clear();
    changeRole(NetworkRole::Offline);
    m_connectionState = ConnectionState::Failed;
    m_connectionError = error;
    RCBN_ERROR("NetworkManager: " << connectionErrorToString(error) << ": " << detail);
}

bool NetworkManager::beginNat(const NatConfig& config, bool creating, const std::string& roomCode) {
    shutdown();
    if (config.stunHost.empty() || config.rendezvousHost.empty()) {
        failConnection(ConnectionError::MissingConfig, "StunServer and RendezvousServer are required");
        return false;
    }
    if (!resolveAddress(config.stunHost, config.stunPort, m_stunAddress) ||
        !resolveAddress(config.rendezvousHost, config.rendezvousPort, m_rendezvousAddress)) {
        failConnection(ConnectionError::MissingConfig, "failed to resolve STUN or rendezvous server");
        return false;
    }
    if (!initializeBoundHost(config.listenPort)) {
        failConnection(ConnectionError::EnetTimeout, "failed to create bound UDP host");
        return false;
    }

    m_natConfig = config;
    m_natConfig.listenPort = m_listenPort;
    m_natMode = true;
    m_creatingRoom = creating;
    m_roomCode = roomCode;
    m_connectionState = ConnectionState::Discovering;
    m_connectionError = ConnectionError::None;
    m_stateElapsed = 0.0f;
    m_retryTimer = 0.0f;
    m_retryDelay = 0.25f;
    m_transactionId = static_cast<uint32_t>(randomU64());
    for (auto& byte : m_stunTransaction) byte = static_cast<uint8_t>(randomU64());
    gatherLocalCandidates();
    sendStunRequest();
    return true;
}

void NetworkManager::addCandidate(std::vector<NetworkCandidate>& candidates,
                                  const NetworkCandidate& candidate) {
    if (candidate.port == 0 ||
        std::find(candidates.begin(), candidates.end(), candidate) != candidates.end()) return;
    candidates.push_back(candidate);
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const NetworkCandidate& a, const NetworkCandidate& b) {
            return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
        });
}

void NetworkManager::gatherLocalCandidates() {
    m_localCandidates.clear();
    char hostname[256] = {};
    if (gethostname(hostname, static_cast<int>(sizeof(hostname) - 1)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* addresses = nullptr;
        if (getaddrinfo(hostname, nullptr, &hints, &addresses) == 0) {
            for (addrinfo* it = addresses; it; it = it->ai_next) {
                if (!it->ai_addr || it->ai_addrlen < sizeof(sockaddr_in)) continue;
                const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
                if (ipv4->sin_addr.s_addr != ENET_HOST_ANY) {
                    addCandidate(m_localCandidates,
                                 {CandidateType::Local, ipv4->sin_addr.s_addr, m_listenPort});
                }
            }
            freeaddrinfo(addresses);
        }
    }
    ENetAddress loopback{};
    if (enet_address_set_host(&loopback, "127.0.0.1") == 0) {
        addCandidate(m_localCandidates, {CandidateType::Local, loopback.host, m_listenPort});
    }
}

bool NetworkManager::sendRaw(const ENetAddress& address, const std::vector<uint8_t>& bytes) {
    if (!m_host || bytes.empty() || bytes.size() > NatProtocol::MAX_DATAGRAM_SIZE) return false;
    ENetBuffer buffer;
    buffer.data = const_cast<uint8_t*>(bytes.data());
    buffer.dataLength = bytes.size();
    return enet_socket_send(m_host->socket, &address, &buffer, 1) == static_cast<int>(bytes.size());
}

void NetworkManager::sendStunRequest() {
    sendRaw(m_stunAddress, NatProtocol::encodeStunBindingRequest(m_stunTransaction));
    m_retryTimer = m_retryDelay;
}

void NetworkManager::sendRendezvous(NatProtocol::RendezvousMessageType type,
                                    const std::vector<uint8_t>& payload,
                                    bool rememberForRetry) {
    NatProtocol::RendezvousPacket packet;
    packet.type = type;
    packet.transactionId = ++m_transactionId;
    packet.payload = payload;
    std::vector<uint8_t> datagram;
    if (!NatProtocol::encodeRendezvousPacket(packet, datagram)) return;
    sendRaw(m_rendezvousAddress, datagram);
    if (rememberForRetry) {
        m_retryDatagram = datagram;
        m_retryTimer = 0.5f;
        m_stateElapsed = 0.0f;
    }
}

void NetworkManager::sendCookieRequest() {
    std::vector<uint8_t> nonce(16);
    for (auto& byte : nonce) byte = static_cast<uint8_t>(randomU64());
    if (m_connectionState == ConnectionState::Discovering) {
        m_connectionState = m_creatingRoom
            ? ConnectionState::CreatingRoom
            : ConnectionState::JoiningRoom;
    }
    sendRendezvous(NatProtocol::RendezvousMessageType::CookieRequest, nonce, true);
}

void NetworkManager::sendRoomRequest() {
    std::vector<uint8_t> encodedCandidates;
    if (!NatProtocol::encodeCandidates(m_localCandidates, encodedCandidates)) return;
    std::vector<uint8_t> payload(m_cookie.begin(), m_cookie.end());
    if (!m_creatingRoom) payload.insert(payload.end(), m_roomCode.begin(), m_roomCode.end());
    payload.insert(payload.end(), encodedCandidates.begin(), encodedCandidates.end());
    m_connectionState = m_creatingRoom ? ConnectionState::CreatingRoom : ConnectionState::JoiningRoom;
    sendRendezvous(m_creatingRoom ? NatProtocol::RendezvousMessageType::Create
                                  : NatProtocol::RendezvousMessageType::Join,
                   payload, true);
}

void NetworkManager::sendCandidateUpdate() {
    if (!m_haveCookie || m_admissionToken == AdmissionToken{}) return;
    std::vector<uint8_t> candidates;
    if (!NatProtocol::encodeCandidates(m_localCandidates, candidates)) return;
    std::vector<uint8_t> payload(m_cookie.begin(), m_cookie.end());
    appendToken(payload, m_admissionToken);
    payload.insert(payload.end(), candidates.begin(), candidates.end());
    sendRendezvous(NatProtocol::RendezvousMessageType::CandidateUpdate, payload, false);
}

void NetworkManager::sendPromoteRequest() {
    if (!m_haveCookie || m_admissionToken == AdmissionToken{}) return;
    std::vector<uint8_t> payload(m_cookie.begin(), m_cookie.end());
    appendToken(payload, m_admissionToken);
    appendBe32(payload, m_roomEpoch);
    sendRendezvous(NatProtocol::RendezvousMessageType::Promote, payload, false);
    m_promoteRetryTimer = 1.0f;
}

int NetworkManager::interceptPacket(ENetHost*, ENetEvent*) {
    return NetworkManager::get().handleIntercept(NetworkManager::get().m_host);
}

int NetworkManager::handleIntercept(ENetHost* host) {
    if (!m_natMode || host != m_host || !host->receivedData) return 0;
    const uint8_t* data = host->receivedData;
    const size_t size = host->receivedDataLength;
    const ENetAddress source = host->receivedAddress;

    if (source.host == m_stunAddress.host && source.port == m_stunAddress.port) {
        NetworkCandidate mapped;
        if (NatProtocol::decodeStunBindingResponse(data, size, m_stunTransaction, mapped) ==
            NatProtocol::DecodeResult::Ok) {
            const size_t oldSize = m_localCandidates.size();
            addCandidate(m_localCandidates, mapped);
            m_stunRefreshTimer = 0.0f;
            if (m_connectionState == ConnectionState::Discovering) sendCookieRequest();
            else if (m_localCandidates.size() != oldSize) {
                for (auto& info : m_roster) {
                    if (info.id == m_localPeerId) {
                        info.endpoint.candidates = m_localCandidates;
                        info.endpoint.host = m_localCandidates.front().host;
                        info.endpoint.listenPort = m_listenPort;
                        m_rosterDirty = true;
                        break;
                    }
                }
                sendCandidateUpdate();
            }
        }
        return 1;
    }
    if (source.host == m_rendezvousAddress.host && source.port == m_rendezvousAddress.port) {
        NatProtocol::RendezvousPacket packet;
        if (NatProtocol::decodeRendezvousPacket(data, size, packet) == NatProtocol::DecodeResult::Ok) {
            handleRendezvous(packet);
        }
        return 1;
    }
    NatProtocol::PunchPacket punch;
    if (NatProtocol::decodePunchPacket(data, size, punch) == NatProtocol::DecodeResult::Ok) {
        handlePunch(punch, source);
        return 1;
    }
    return 0;
}

void NetworkManager::handleRendezvous(const NatProtocol::RendezvousPacket& packet) {
    using Type = NatProtocol::RendezvousMessageType;
    const auto& payload = packet.payload;
    if (packet.type == Type::CookieChallenge) {
        if (payload.size() != m_cookie.size()) return;
        std::copy(payload.begin(), payload.end(), m_cookie.begin());
        m_haveCookie = true;
        if (m_recoveringCookie && m_admissionToken != AdmissionToken{}) {
            m_recoveringCookie = false;
            sendCandidateUpdate();
        } else {
            sendRoomRequest();
        }
        return;
    }
    if (packet.type == Type::Error) {
        if (payload.size() != 1) return;
        if (m_promotePending) {
            // The old host can remain fresh in rendezvous briefly after ENet has
            // detected its loss.  Promotion registration is advisory for peers
            // with cached candidates, so a conflict/rate limit/server rejection
            // must not tear down the newly promoted host.
            RCBN_WARN("NetworkManager: rendezvous deferred host promotion (error "
                      << static_cast<unsigned>(payload[0]) << ")");
            return;
        }
        switch (payload[0]) {
            case 2:
            case 3:
                m_haveCookie = false;
                m_recoveringCookie = true;
                sendCookieRequest();
                break;
            case 4: failConnection(ConnectionError::RoomNotFound, "room not found"); break;
            case 5: failConnection(ConnectionError::RoomFull, "room is full"); break;
            case 6: failConnection(ConnectionError::AdmissionRejected, "admission rejected"); break;
            default: failConnection(ConnectionError::RendezvousTimeout, "rendezvous rejected request"); break;
        }
        return;
    }
    if (packet.type == Type::Created) {
        if (payload.size() != 32 || m_connectionState != ConnectionState::CreatingRoom) return;
        m_roomCode.assign(reinterpret_cast<const char*>(payload.data()), 8);
        std::copy_n(payload.data() + 8, m_admissionToken.size(), m_admissionToken.begin());
        m_localPeerId = static_cast<PeerId>(readBe32(payload.data() + 24));
        m_roomEpoch = readBe32(payload.data() + 28);
        m_peerTokens[m_localPeerId] = m_admissionToken;
        changeRole(NetworkRole::Host);
        m_connectionState = ConnectionState::Connected;
        m_stateElapsed = 0.0f;
        if (m_localCpuScore == 0.0f) m_localCpuScore = measureCpuScore();
        m_nextPeerId = 2;
        m_roster.clear();
        PeerInfo self;
        self.id = m_localPeerId;
        self.endpoint.listenPort = m_listenPort;
        self.endpoint.candidates = m_localCandidates;
        if (!m_localCandidates.empty()) {
            self.endpoint.host = m_localCandidates.front().host;
        }
        self.cpuScore = m_localCpuScore;
        self.isHost = true;
        self.isPlayer = true;
        m_roster.push_back(self);
        m_rosterDirty = true;
        RCBN_LOG("NetworkManager: room " << m_roomCode << " created");
        return;
    }
    if (packet.type == Type::Joined) {
        if (payload.size() < 29 ||
            (m_connectionState != ConnectionState::JoiningRoom &&
             m_role != NetworkRole::Client)) return;
        std::copy_n(payload.data(), m_admissionToken.size(), m_admissionToken.begin());
        m_localPeerId = static_cast<PeerId>(readBe32(payload.data() + 16));
        m_roomEpoch = readBe32(payload.data() + 20);
        const PeerId hostPeerId = static_cast<PeerId>(readBe32(payload.data() + 24));
        std::vector<NetworkCandidate> candidates;
        if (NatProtocol::decodeCandidates(payload.data() + 28, payload.size() - 28, candidates) !=
            NatProtocol::DecodeResult::Ok || candidates.empty()) return;
        m_peerTokens[m_localPeerId] = m_admissionToken;
        changeRole(NetworkRole::Client);
        PeerEndpoint endpoint;
        endpoint.candidates = std::move(candidates);
        endpoint.host = endpoint.candidates.front().host;
        endpoint.listenPort = endpoint.candidates.front().port;
        bool foundHost = false;
        for (auto& info : m_roster) {
            if (info.isHost) info.isHost = false;
            if (info.id == hostPeerId) {
                info.endpoint = endpoint;
                info.isHost = true;
                foundHost = true;
            }
        }
        if (!foundHost) {
            PeerInfo hostInfo;
            hostInfo.id = hostPeerId;
            hostInfo.endpoint = endpoint;
            hostInfo.isHost = true;
            m_roster.push_back(hostInfo);
        }
        if (m_connectionState == ConnectionState::JoiningRoom ||
            m_connectionState == ConnectionState::Migrating ||
            m_peers.empty()) {
            startPunching(endpoint, m_admissionToken, hostPeerId);
        }
        return;
    }
    if (packet.type == Type::PeerJoined) {
        if (payload.size() < 21) return;
        const PeerId peerId = static_cast<PeerId>(readBe32(payload.data()));
        AdmissionToken token{};
        std::copy_n(payload.data() + 4, token.size(), token.begin());
        std::vector<NetworkCandidate> candidates;
        if (NatProtocol::decodeCandidates(payload.data() + 20, payload.size() - 20, candidates) !=
            NatProtocol::DecodeResult::Ok || candidates.empty()) return;
        m_peerTokens[peerId] = token;
        PeerEndpoint pending;
        pending.candidates = std::move(candidates);
        pending.host = pending.candidates.front().host;
        pending.listenPort = pending.candidates.front().port;
        bool found = false;
        for (auto& info : m_roster) {
            if (info.id == peerId) {
                info.endpoint = pending;
                found = true;
                break;
            }
        }
        if (!found && peerId != m_localPeerId) {
            PeerInfo info;
            info.id = peerId;
            info.endpoint = pending;
            m_roster.push_back(info);
        }
        if (m_role == NetworkRole::Host) {
            bool alreadyAdmitted = false;
            for (const auto& [connectedPeer, connectedId] : m_peerIds) {
                (void)connectedPeer;
                if (connectedId == peerId) {
                    alreadyAdmitted = true;
                    break;
                }
            }
            if (alreadyAdmitted) {
                m_rosterDirty = true;
                return;
            }
            m_pendingPunches[peerId] = pending;
            m_punchTarget = pending;
            m_punchToken = token;
            m_punchPeerId = peerId;
            m_punchNonce = randomU64();
            m_retryTimer = 0.0f;
            sendPunchProbes();
        }
        return;
    }
    if (packet.type == Type::Promote && payload.size() == 4) {
        m_roomEpoch = readBe32(payload.data());
        m_promotePending = false;
        m_promoteRetryTimer = 0.0f;
        m_promoteRetryElapsed = 0.0f;
        m_rosterDirty = true;
        RCBN_LOG("NetworkManager: rendezvous accepted host promotion at epoch "
                 << m_roomEpoch);
    }
}

void NetworkManager::startPunching(const PeerEndpoint& endpoint,
                                   const AdmissionToken& token,
                                   PeerId peerId) {
    m_punchTarget = endpoint;
    m_punchToken = token;
    m_punchPeerId = peerId;
    m_punchNonce = randomU64();
    m_connectionState = ConnectionState::Punching;
    m_stateElapsed = 0.0f;
    m_retryTimer = 0.0f;
    sendPunchProbes();
}

void NetworkManager::sendPunchProbes() {
    NatProtocol::PunchPacket packet;
    packet.type = NatProtocol::PunchMessageType::Probe;
    packet.nonce = m_punchNonce;
    packet.token = m_punchToken;
    packet.senderPeerId = m_localPeerId;
    packet.roomEpoch = m_roomEpoch;
    std::vector<uint8_t> datagram;
    if (!NatProtocol::encodePunchPacket(packet, datagram)) return;
    for (const auto& candidate : m_punchTarget.candidates) {
        ENetAddress address{candidate.host, candidate.port};
        sendRaw(address, datagram);
    }
    m_retryTimer = 0.2f;
}

void NetworkManager::handlePunch(const NatProtocol::PunchPacket& packet,
                                 const ENetAddress& source) {
    if (packet.roomEpoch != m_roomEpoch) return;
    AdmissionToken expected{};
    if (m_role == NetworkRole::Host) {
        auto it = m_peerTokens.find(packet.senderPeerId);
        if (it == m_peerTokens.end()) return;
        expected = it->second;
    } else {
        expected = m_punchToken;
    }
    if (packet.token != expected) return;

    NetworkCandidate reflexive{CandidateType::PeerReflexive, source.host, source.port};
    if (m_role == NetworkRole::Host) {
        auto pending = m_pendingPunches.find(packet.senderPeerId);
        if (pending != m_pendingPunches.end()) addCandidate(pending->second.candidates, reflexive);
    } else {
        addCandidate(m_punchTarget.candidates, reflexive);
    }

    if (packet.type == NatProtocol::PunchMessageType::Probe) {
        NatProtocol::PunchPacket ack = packet;
        ack.type = NatProtocol::PunchMessageType::Acknowledge;
        ack.senderPeerId = m_localPeerId;
        std::vector<uint8_t> datagram;
        if (NatProtocol::encodePunchPacket(ack, datagram)) sendRaw(source, datagram);
    }
    if (m_role == NetworkRole::Client &&
        (packet.type == NatProtocol::PunchMessageType::Acknowledge ||
         packet.type == NatProtocol::PunchMessageType::Probe) &&
        m_connectionState == ConnectionState::Punching) {
        if (connectToCandidate(reflexive)) {
            m_connectionState = ConnectionState::Connecting;
            m_stateElapsed = 0.0f;
        }
    }
}

bool NetworkManager::connectToCandidate(const NetworkCandidate& candidate) {
    if (!m_host) return false;
    ENetAddress address{candidate.host, candidate.port};
    m_expectedPeer = enet_host_connect(m_host, &address,
                                       static_cast<size_t>(NetworkChannel::Count), 0);
    if (!m_expectedPeer) return false;
    RCBN_LOG("NetworkManager: ENet connecting to punched candidate "
             << candidate.host << ":" << candidate.port);
    return true;
}

void NetworkManager::updateNat(float dt) {
    if (!m_natMode || m_connectionState == ConnectionState::Offline ||
        m_connectionState == ConnectionState::Failed) return;
    m_stateElapsed += dt;
    m_retryTimer -= dt;
    m_refreshTimer += dt;
    m_stunRefreshTimer += dt;
    m_pendingPunchTimer -= dt;
    if (m_promotePending) {
        m_promoteRetryTimer -= dt;
        m_promoteRetryElapsed += dt;
        if (m_promoteRetryElapsed <= 30.0f && m_promoteRetryTimer <= 0.0f) {
            sendPromoteRequest();
        } else if (m_promoteRetryElapsed > 30.0f) {
            m_promotePending = false;
            RCBN_WARN("NetworkManager: rendezvous promotion registration timed out; "
                      "continuing with cached peer candidates");
        }
    }

    if (m_role == NetworkRole::Host && !m_pendingPunches.empty() && m_pendingPunchTimer <= 0.0f) {
        for (const auto& [peerId, endpoint] : m_pendingPunches) {
            auto token = m_peerTokens.find(peerId);
            if (token == m_peerTokens.end()) continue;
            NatProtocol::PunchPacket packet;
            packet.type = NatProtocol::PunchMessageType::Probe;
            packet.nonce = randomU64();
            packet.token = token->second;
            packet.senderPeerId = m_localPeerId;
            packet.roomEpoch = m_roomEpoch;
            std::vector<uint8_t> datagram;
            if (!NatProtocol::encodePunchPacket(packet, datagram)) continue;
            for (const auto& candidate : endpoint.candidates) {
                sendRaw({candidate.host, candidate.port}, datagram);
            }
        }
        m_pendingPunchTimer = 0.2f;
    }

    if (m_connectionState == ConnectionState::Discovering) {
        if (m_stateElapsed >= kDiscoveryTimeout) {
            failConnection(ConnectionError::StunTimeout, "STUN binding timed out");
            return;
        }
        if (m_retryTimer <= 0.0f) {
            m_retryDelay = (std::min)(m_retryDelay * 2.0f, 4.0f);
            sendStunRequest();
        }
    } else if (m_connectionState == ConnectionState::CreatingRoom ||
               m_connectionState == ConnectionState::JoiningRoom) {
        if (m_stateElapsed >= kDiscoveryTimeout) {
            failConnection(ConnectionError::RendezvousTimeout, "rendezvous request timed out");
            return;
        }
        if (m_retryTimer <= 0.0f && !m_retryDatagram.empty()) {
            sendRaw(m_rendezvousAddress, m_retryDatagram);
            m_retryTimer = 0.5f;
        }
    } else if (m_connectionState == ConnectionState::Punching) {
        if (m_stateElapsed >= kPunchTimeout) {
            failConnection(ConnectionError::PunchTimeout, "no direct candidate succeeded");
            return;
        }
        if (m_retryTimer <= 0.0f) sendPunchProbes();
    } else if (m_connectionState == ConnectionState::Connecting &&
               m_stateElapsed >= kEnetTimeout) {
        failConnection(ConnectionError::EnetTimeout, "ENet handshake timed out");
        return;
    }

    if (m_haveCookie && m_admissionToken != AdmissionToken{} && m_refreshTimer >= 5.0f) {
        std::vector<uint8_t> payload(m_cookie.begin(), m_cookie.end());
        appendToken(payload, m_admissionToken);
        sendRendezvous(NatProtocol::RendezvousMessageType::Refresh, payload, false);
        m_refreshTimer = 0.0f;
    }
    if (m_stunRefreshTimer >= 15.0f) {
        for (auto& byte : m_stunTransaction) byte = static_cast<uint8_t>(randomU64());
        sendStunRequest();
        m_stunRefreshTimer = 0.0f;
    }
}

void NetworkManager::poll() {
    if (!m_host) return;

    ENetEvent event;
    while (m_host && enet_host_service(m_host, &event, 0) > 0) {
        handleEvent(event);
    }
}

void NetworkManager::update(float dt) {
    poll();
    updateNat(dt);
    if (m_connectionState == ConnectionState::Failed) return;

    if (m_migrationState != MigrationState::None) {
        updateMigration(dt);
        return; // 移行中は通常の周期送信をしない
    }
    if (!m_host) return;

    if (m_role == NetworkRole::Host) {
        for (auto it = m_pendingAdmissions.begin(); it != m_pendingAdmissions.end();) {
            it->second += dt;
            if (it->second >= 2.0f) {
                RCBN_WARN("NetworkManager: rejected peer that did not complete Hello admission");
                enet_peer_disconnect_now(it->first, 0);
                it = m_pendingAdmissions.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (m_role == NetworkRole::Client) {
        m_resourceReportTimer += dt;
        if (m_resourceReportTimer >= 0.5f) {
            m_resourceReportTimer = 0.0f;
            ByteWriter w;
            w.writeU8(static_cast<uint8_t>(MessageType::ResourceReport));
            w.writeF32(m_localCpuScore);
            ENetPacket* packet = enet_packet_create(w.data.data(), w.data.size(), ENET_PACKET_FLAG_RELIABLE);
            broadcastPacket(packet, NetworkChannel::Reliable);
        }
    } else if (m_role == NetworkRole::Host) {
        for (auto& [peer, id] : m_peerIds) {
            float rtt = static_cast<float>(peer->roundTripTime);
            for (auto& info : m_roster) {
                if (info.id == id) {
                    if (std::abs(info.latencyMs - rtt) >= 1.0f) {
                        info.latencyMs = rtt;
                        m_rosterDirty = true;
                    }
                    break;
                }
            }
        }
        for (auto& info : m_roster) {
            if (info.id == m_localPeerId) {
                info.latencyMs = 0.0f;
                break;
            }
        }

        m_rosterBroadcastTimer += dt;
        if (m_rosterDirty || m_rosterBroadcastTimer >= 1.0f) {
            broadcastRoster();
        }
    }
}

void NetworkManager::updateMigration(float dt) {
    m_migrationElapsed += dt;
    if (m_migrationElapsed > 10.0f) {
        failConnection(m_natMode ? ConnectionError::PunchTimeout : ConnectionError::EnetTimeout,
                       "host migration timed out");
        return;
    }

    switch (m_migrationState) {
        case MigrationState::Electing: {
            PeerInfo winner = electNewHost();
            if (winner.id == 0) {
                failConnection(ConnectionError::EnetTimeout, "no host migration candidates");
                return;
            }
            RCBN_LOG("NetworkManager: migration winner id=" << winner.id << " cpuScore=" << winner.cpuScore);

            if (winner.id == m_localPeerId) {
                m_migrationState = MigrationState::Rehosting;
            } else {
                m_migrationTarget = winner.endpoint;
                // 勝者のバインド完了を待つため初回は0.5秒待ってから接続する
                m_migrationRetryTimer = 0.5f;
                m_migrationState = MigrationState::Reconnecting;
            }
            break;
        }
        case MigrationState::Rehosting: {
            if (promoteToHost()) {
                m_migrationState = MigrationState::None;
            } else {
                shutdown();
            }
            break;
        }
        case MigrationState::Reconnecting: {
            m_migrationRetryTimer -= dt;
            if (!m_peers.empty()) {
                // CONNECT成立(handleEventでpush済み、sendHelloも送信済み)
                m_migrationState = MigrationState::None;
                RCBN_LOG("NetworkManager: reconnected to new host");
                return;
            }
            if (m_natMode && m_migrationRetryTimer <= 0.0f &&
                m_connectionState == ConnectionState::Migrating) {
                auto token = m_peerTokens.find(m_localPeerId);
                if (token == m_peerTokens.end() || m_migrationTarget.candidates.empty()) {
                    failConnection(ConnectionError::AdmissionRejected,
                                   "missing cached migration admission data");
                    return;
                }
                startPunching(m_migrationTarget, token->second, 0);
            } else if (!m_natMode && m_migrationRetryTimer <= 0.0f) {
                connectToEndpoint(m_migrationTarget); // 失敗してもリトライ継続
                m_migrationRetryTimer = 1.5f; // 次の試行まで(接続試行自体の猶予を含む)
            }
            break;
        }
        case MigrationState::None:
            break;
    }
}

PeerInfo NetworkManager::electNewHost() const {
    PeerInfo winner;
    bool found = false;
    for (const auto& info : m_roster) {
        if (info.isHost || !info.isPlayer) continue;
        if (!found) {
            winner = info;
            found = true;
            continue;
        }
        if (info.cpuScore - winner.cpuScore > 0.001f) {
            winner = info;
        } else if (std::abs(info.cpuScore - winner.cpuScore) < 0.001f) {
            if (info.latencyMs < winner.latencyMs) {
                winner = info;
            } else if (info.latencyMs == winner.latencyMs && info.id < winner.id) {
                winner = info;
            }
        }
    }
    if (!found) return PeerInfo{};
    return winner;
}

bool NetworkManager::promoteToHost() {
    if (!m_host) return false;
    for (ENetPeer* peer : m_peers) enet_peer_reset(peer);
    m_peers.clear();
    m_expectedPeer = nullptr;
    m_peerIds.clear();
    m_pendingAdmissions.clear();

    changeRole(NetworkRole::Host);
    m_connectionState = ConnectionState::Connected;

    m_roster.erase(std::remove_if(m_roster.begin(), m_roster.end(),
        [](const PeerInfo& info) { return info.isHost; }), m_roster.end());

    bool selfFound = false;
    for (auto& info : m_roster) {
        if (info.id == m_localPeerId) {
            info.isHost = true;
            info.isPlayer = true;
            info.latencyMs = 0.0f;
            info.endpoint.host = m_localCandidates.empty() ? 0 : m_localCandidates.front().host;
            info.endpoint.listenPort = m_listenPort;
            info.endpoint.candidates = m_localCandidates;
            info.cpuScore = m_localCpuScore;
            selfFound = true;
            break;
        }
    }
    if (!selfFound) {
        PeerInfo self;
        self.id = m_localPeerId;
        self.endpoint.host = m_localCandidates.empty() ? 0 : m_localCandidates.front().host;
        self.endpoint.listenPort = m_listenPort;
        self.endpoint.candidates = m_localCandidates;
        self.cpuScore = m_localCpuScore;
        self.latencyMs = 0.0f;
        self.isHost = true;
        self.isPlayer = true;
        m_roster.push_back(self);
    }

    m_rosterDirty = true;
    m_resourceReportTimer = 0.0f;
    m_rosterBroadcastTimer = 0.0f;

    // ランデブーが停止していても、Rosterでキャッシュ済みの候補と参加tokenを使って
    // 残存ピアへ双方向パンチを開始する。Promote応答のsnapshotはこの情報を後から更新する。
    m_pendingPunches.clear();
    for (const auto& info : m_roster) {
        if (info.id == m_localPeerId || info.endpoint.candidates.empty()) continue;
        auto token = m_peerTokens.find(info.id);
        if (token != m_peerTokens.end() && token->second != AdmissionToken{}) {
            m_pendingPunches[info.id] = info.endpoint;
        }
    }
    m_pendingPunchTimer = 0.0f;

    if (m_natMode && m_haveCookie) {
        m_promotePending = true;
        m_promoteRetryTimer = 0.0f;
        m_promoteRetryElapsed = 0.0f;
        sendPromoteRequest();
    }

    RCBN_LOG("NetworkManager: promoted to host using existing UDP socket on port " << m_listenPort);
    return true;
}

bool NetworkManager::connectToEndpoint(const PeerEndpoint& endpoint) {
    if (!m_host) return false;
    for (ENetPeer* peer : m_peers) enet_peer_reset(peer);
    m_peers.clear();
    m_expectedPeer = nullptr;

    changeRole(NetworkRole::Client);
    if (m_natMode && !endpoint.candidates.empty()) {
        m_punchTarget = endpoint;
        auto tokenIt = m_peerTokens.find(m_localPeerId);
        if (tokenIt != m_peerTokens.end()) m_punchToken = tokenIt->second;
        m_punchPeerId = 0;
        m_punchNonce = randomU64();
        sendPunchProbes();
        for (const auto& candidate : endpoint.candidates) {
            if (connectToCandidate(candidate)) return true;
        }
        return false;
    }

    NetworkCandidate candidate{CandidateType::Local, endpoint.host, endpoint.listenPort};
    if (!connectToCandidate(candidate)) {
        RCBN_ERROR("ENet: no available peers for migration reconnect");
        return false;
    }

    RCBN_LOG("NetworkManager: reconnecting to new host " << endpoint.host << ":" << endpoint.listenPort << " ...");
    return true;
}

void NetworkManager::sendHello() {
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(MessageType::Hello));
    w.writeU8(kGameProtocolVersion);
    w.writeU32(m_roomEpoch);
    w.data.insert(w.data.end(), m_admissionToken.begin(), m_admissionToken.end());
    w.writeU16(m_listenPort);
    w.writeU32(static_cast<uint32_t>(m_localPeerId));
    std::vector<uint8_t> encodedCandidates;
    if (!NatProtocol::encodeCandidates(m_localCandidates, encodedCandidates)) return;
    w.data.insert(w.data.end(), encodedCandidates.begin(), encodedCandidates.end());
    ENetPacket* packet = enet_packet_create(w.data.data(), w.data.size(), ENET_PACKET_FLAG_RELIABLE);
    broadcastPacket(packet, NetworkChannel::Reliable);
}

void NetworkManager::broadcastRoster() {
    if (!m_host || m_role != NetworkRole::Host) return;

    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(MessageType::Roster));
    w.writeU32(static_cast<uint32_t>(m_roster.size()));
    for (const auto& info : m_roster) {
        w.writeU32(static_cast<uint32_t>(info.id));
        w.writeU32(info.endpoint.host);
        w.writeU16(info.endpoint.listenPort);
        std::vector<uint8_t> encodedCandidates;
        if (!NatProtocol::encodeCandidates(info.endpoint.candidates, encodedCandidates)) return;
        w.data.insert(w.data.end(), encodedCandidates.begin(), encodedCandidates.end());
        auto token = m_peerTokens.find(info.id);
        if (token != m_peerTokens.end()) {
            w.data.insert(w.data.end(), token->second.begin(), token->second.end());
        } else {
            const AdmissionToken emptyToken{};
            w.data.insert(w.data.end(), emptyToken.begin(), emptyToken.end());
        }
        w.writeF32(info.cpuScore);
        w.writeF32(info.latencyMs);
        w.writeU8(info.isHost ? 1 : 0);
        w.writeU8(info.isPlayer ? 1 : 0);
    }
    w.writeU32(m_nextPeerId);

    ENetPacket* packet = enet_packet_create(w.data.data(), w.data.size(), ENET_PACKET_FLAG_RELIABLE);
    broadcastPacket(packet, NetworkChannel::Reliable);

    bool wasDirty = m_rosterDirty;
    m_rosterDirty = false;
    m_rosterBroadcastTimer = 0.0f;

    if (wasDirty) {
        logRoster();
    }
}

void NetworkManager::logRoster() const {
    // RCBN_LOG("NetworkManager: roster (" << m_roster.size() << " peers)");
    // for (const auto& info : m_roster) {
    //     RCBN_LOG("  peer id=" << info.id
    //               << " cpuScore=" << info.cpuScore
    //               << " latencyMs=" << info.latencyMs
    //               << " isHost=" << (info.isHost ? "true" : "false")
    //               << " isPlayer=" << (info.isPlayer ? "true" : "false"));
    // }
    return;
}

void NetworkManager::handleEvent(const ENetEvent& event) {
    switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            // Host/Client どちらの視点でも、ハンドシェイク完了(state==CONNECTED)を
            // ここで初めて確認できる。connect()呼び出し直後の時点ではまだ送信できないため、
            // m_peersへの追加はここに一本化する。
            if (m_role == NetworkRole::Client && event.peer != m_expectedPeer) {
                RCBN_WARN("NetworkManager: rejected unexpected incoming ENet peer");
                enet_peer_disconnect_now(event.peer, 0);
                break;
            }
            RCBN_LOG("NetworkManager: peer connected (" << event.peer->address.host << ":" << event.peer->address.port << ")");

            if (m_role == NetworkRole::Client) {
                m_peers.push_back(event.peer);
                sendHello();
            } else if (m_role == NetworkRole::Host) {
                m_pendingAdmissions[event.peer] = 0.0f;
            }
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            m_peers.erase(std::remove(m_peers.begin(), m_peers.end(), event.peer), m_peers.end());
            m_pendingAdmissions.erase(event.peer);
            RCBN_LOG("NetworkManager: peer disconnected");

            if (m_role == NetworkRole::Host) {
                auto it = m_peerIds.find(event.peer);
                if (it != m_peerIds.end()) {
                    PeerId id = it->second;
                    m_peerIds.erase(it);
                    m_roster.erase(std::remove_if(m_roster.begin(), m_roster.end(),
                        [id](const PeerInfo& info) { return info.id == id; }), m_roster.end());
                    m_rosterDirty = true;
                }
            } else if (m_role == NetworkRole::Client &&
                       m_connectionState == ConnectionState::Connected &&
                       m_migrationState == MigrationState::None) {
                // Clientのm_peersはHost1件のみ→切断相手は常にHost
                m_migrationState = MigrationState::Electing;
                m_connectionState = ConnectionState::Migrating;
                m_migrationElapsed = 0.0f;
                RCBN_LOG("NetworkManager: host disconnected, starting migration");
            }
            // Reconnecting中のDISCONNECT(新Hostへの接続試行失敗)はここでは何もしない(updateMigrationがリトライする)。
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            if (event.packet->dataLength >= 1) {
                auto type = static_cast<MessageType>(event.packet->data[0]);
                const uint8_t* payload = event.packet->data + 1;
                size_t payloadLen = event.packet->dataLength - 1;
                ByteReader reader{payload, payloadLen};

                if (type == MessageType::Chat) {
                    uint32_t claimedSender = 0;
                    if (reader.readU32(claimedSender)) {
                        std::string text;
                        if (validateChatText(reader.p, reader.remaining, text)) {
                            if (m_role == NetworkRole::Host) {
                                // Clientの申告senderIdは信用せず、Hello完了済みPeerの割当IDを使う。
                                auto it = m_peerIds.find(event.peer);
                                if (it != m_peerIds.end()) {
                                    PeerId senderId = it->second;
                                    if (onChatMessage) onChatMessage(senderId, text);
                                    auto canonical = makeChatPayload(senderId, text);
                                    ENetPacket* chatPacket = enet_packet_create(
                                        canonical.data(), canonical.size(), ENET_PACKET_FLAG_RELIABLE);
                                    broadcastPacket(chatPacket, NetworkChannel::Reliable);
                                }
                            } else if (m_role == NetworkRole::Client && claimedSender != 0) {
                                // ClientがChatを受ける相手は常に現Host。payloadはHostが正規化済み。
                                if (onChatMessage) onChatMessage(static_cast<PeerId>(claimedSender), text);
                            }
                        }
                    }
                } else if (type == MessageType::Hello && m_role == NetworkRole::Host) {
                    uint8_t protocolVersion = 0;
                    uint32_t roomEpoch = 0;
                    AdmissionToken token{};
                    uint16_t listenPort = 0;
                    uint32_t previousPeerId = 0;
                    bool headerOk = reader.readU8(protocolVersion) && reader.readU32(roomEpoch);
                    if (headerOk && reader.remaining >= token.size()) {
                        std::copy_n(reader.p, token.size(), token.begin());
                        reader.p += token.size();
                        reader.remaining -= token.size();
                    } else {
                        headerOk = false;
                    }
                    if (headerOk && reader.readU16(listenPort) && reader.readU32(previousPeerId)) {
                        std::vector<NetworkCandidate> helloCandidates;
                        const bool candidatesOk =
                            NatProtocol::decodeCandidates(reader.p, reader.remaining, helloCandidates) ==
                            NatProtocol::DecodeResult::Ok;
                        auto expectedToken = m_peerTokens.find(static_cast<PeerId>(previousPeerId));
                        const bool admitted = protocolVersion == kGameProtocolVersion &&
                            (!m_natMode ||
                             (roomEpoch == m_roomEpoch &&
                             expectedToken != m_peerTokens.end() &&
                             expectedToken->second == token));
                        if (!candidatesOk || !admitted) {
                            RCBN_WARN("NetworkManager: rejected unauthorised Hello");
                            enet_peer_disconnect_now(event.peer, 0);
                            m_pendingAdmissions.erase(event.peer);
                            m_peers.erase(std::remove(m_peers.begin(), m_peers.end(), event.peer), m_peers.end());
                            enet_packet_destroy(event.packet);
                            break;
                        }
                        // 「ロスターに存在するか」ではなく「現在接続中のPeerが実際にそのIDを
                        // 使っているか」で判定する。ホスト移行後の再接続では新Hostが旧ロスターを
                        // 引き継いでおり、再接続Clientの前回IDは必ずロスターに載っているため。
                        bool activelyInUse = false;
                        for (const auto& [p, usedId] : m_peerIds) {
                            if (usedId == previousPeerId) { activelyInUse = true; break; }
                        }

                        PeerId id = (previousPeerId != 0 && !activelyInUse)
                            ? static_cast<PeerId>(previousPeerId)
                            : static_cast<PeerId>(m_nextPeerId++);

                        m_peerIds[event.peer] = id;
                        m_pendingAdmissions.erase(event.peer);
                        m_peers.push_back(event.peer);
                        m_pendingPunches.erase(id);

                        PeerInfo info;
                        info.id = id;
                        info.endpoint.host = event.peer->address.host;
                        info.endpoint.listenPort = listenPort;
                        info.endpoint.candidates = std::move(helloCandidates);
                        addCandidate(info.endpoint.candidates,
                                     {CandidateType::PeerReflexive,
                                      event.peer->address.host, event.peer->address.port});
                        info.cpuScore = 0.0f;
                        info.latencyMs = 0.0f;
                        info.isHost = false;
                        info.isPlayer = true;

                        bool replaced = false;
                        for (auto& existing : m_roster) {
                            if (existing.id == id) {
                                info.cpuScore = existing.cpuScore; // 引き継いだ集計値を保持
                                info.isPlayer = existing.isPlayer;
                                existing = info;
                                replaced = true;
                                break;
                            }
                        }
                        if (!replaced) m_roster.push_back(info);

                        ByteWriter w;
                        w.writeU8(static_cast<uint8_t>(MessageType::Welcome));
                        w.writeU32(static_cast<uint32_t>(id));
                        ENetPacket* welcomePacket = enet_packet_create(w.data.data(), w.data.size(), ENET_PACKET_FLAG_RELIABLE);
                        if (enet_peer_send(event.peer, static_cast<enet_uint8>(NetworkChannel::Reliable), welcomePacket) != 0) {
                            enet_packet_destroy(welcomePacket);
                        }

                        m_rosterDirty = true;
                        RCBN_LOG("NetworkManager: [hello] assigned peer id=" << id << " listenPort=" << listenPort);
                    }
                } else if (type == MessageType::Welcome && m_role == NetworkRole::Client) {
                    uint32_t id = 0;
                    if (reader.readU32(id)) {
                        m_localPeerId = static_cast<PeerId>(id);
                        m_connectionState = ConnectionState::Connected;
                        m_connectionError = ConnectionError::None;
                        m_stateElapsed = 0.0f;
                        RCBN_LOG("NetworkManager: [welcome] assigned peer id=" << m_localPeerId);
                    }
                } else if (type == MessageType::Roster && m_role == NetworkRole::Client) {
                    uint32_t count = 0;
                    if (reader.readU32(count) && count <= 32) {
                        std::vector<PeerInfo> newRoster;
                        newRoster.reserve(count);
                        bool ok = true;
                        for (uint32_t i = 0; i < count && ok; ++i) {
                            uint32_t id = 0, endpointHost = 0;
                            uint16_t listenPort = 0;
                            float cpuScore = 0.0f, latencyMs = 0.0f;
                            uint8_t isHost = 0, isPlayer = 0;
                            AdmissionToken token{};
                            ok = reader.readU32(id) && reader.readU32(endpointHost) &&
                                 reader.readU16(listenPort);
                            std::vector<NetworkCandidate> candidates;
                            if (ok && reader.remaining >= 1) {
                                const size_t candidateBytes = 1 + static_cast<size_t>(reader.p[0]) * 7;
                                if (candidateBytes > reader.remaining ||
                                    NatProtocol::decodeCandidates(reader.p, candidateBytes, candidates) !=
                                    NatProtocol::DecodeResult::Ok) {
                                    ok = false;
                                } else {
                                    reader.p += candidateBytes;
                                    reader.remaining -= candidateBytes;
                                }
                            } else {
                                ok = false;
                            }
                            if (ok && reader.remaining >= token.size()) {
                                std::copy_n(reader.p, token.size(), token.begin());
                                reader.p += token.size();
                                reader.remaining -= token.size();
                            } else {
                                ok = false;
                            }
                            ok = ok && reader.readF32(cpuScore) &&
                                 reader.readF32(latencyMs) && reader.readU8(isHost) &&
                                 reader.readU8(isPlayer);
                            if (!ok) break;

                            PeerInfo info;
                            info.id = static_cast<PeerId>(id);
                            info.endpoint.host = endpointHost;
                            info.endpoint.listenPort = listenPort;
                            info.endpoint.candidates = std::move(candidates);
                            info.cpuScore = cpuScore;
                            info.latencyMs = latencyMs;
                            info.isHost = (isHost != 0);
                            info.isPlayer = (isPlayer != 0);
                            newRoster.push_back(info);
                            m_peerTokens[info.id] = token;
                        }
                        uint32_t nextPeerId = 0;
                        if (ok && reader.readU32(nextPeerId)) {
                            bool changed = (newRoster.size() != m_roster.size());
                            if (!changed) {
                                for (size_t i = 0; i < newRoster.size(); ++i) {
                                    const auto& a = newRoster[i];
                                    const auto& b = m_roster[i];
                                    if (a.id != b.id || a.endpoint.host != b.endpoint.host ||
                                        a.endpoint.listenPort != b.endpoint.listenPort ||
                                        a.cpuScore != b.cpuScore || a.latencyMs != b.latencyMs ||
                                        a.isHost != b.isHost || a.isPlayer != b.isPlayer) {
                                        changed = true;
                                        break;
                                    }
                                }
                            }

                            m_roster = std::move(newRoster);
                            m_nextPeerId = nextPeerId;

                            if (changed) logRoster();
                        }
                    }
                } else if (type == MessageType::ResourceReport && m_role == NetworkRole::Host) {
                    float cpuScore = 0.0f;
                    if (reader.readF32(cpuScore)) {
                        auto it = m_peerIds.find(event.peer);
                        if (it != m_peerIds.end()) {
                            PeerId id = it->second;
                            for (auto& info : m_roster) {
                                if (info.id == id) {
                                    if (std::abs(info.cpuScore - cpuScore) >= 0.01f) {
                                        info.cpuScore = cpuScore;
                                        m_rosterDirty = true;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                } else if (type >= MessageType::AvatarState && type <= MessageType::SimulationClock) {
                    // レプリケーション系メッセージはゲーム層(ReplicationManager)へ委譲する。
                    // packet破棄前の同期呼び出しなのでpayloadポインタをそのまま渡してよい。
                    if (onGameMessage) {
                        PeerId senderId = 0;
                        if (m_role == NetworkRole::Host) {
                            auto it = m_peerIds.find(event.peer);
                            if (it != m_peerIds.end()) senderId = it->second;
                        } else {
                            senderId = 1; // Client視点の相手は常にHost(表示上のID。実IDはメッセージ内で運ぶ)
                        }
                        if (senderId != 0 || m_role == NetworkRole::Client) {
                            onGameMessage(static_cast<uint8_t>(type), payload, payloadLen, senderId);
                        }
                    }
                }
            }
            enet_packet_destroy(event.packet);
            break;
        }
        default:
            break;
    }
}

void NetworkManager::broadcastPacket(ENetPacket* packet, NetworkChannel channel) {
    if (!m_host) {
        enet_packet_destroy(packet);
        return;
    }

    if (m_role == NetworkRole::Host) {
        bool queued = false;
        for (const auto& [peer, id] : m_peerIds) {
            (void)id;
            if (peer->state == ENET_PEER_STATE_CONNECTED &&
                enet_peer_send(peer, static_cast<enet_uint8>(channel), packet) == 0) {
                queued = true;
            }
        }
        if (!queued) enet_packet_destroy(packet);
    } else if (m_role == NetworkRole::Client && !m_peers.empty()) {
        // enet_peer_send はPeerがCONNECTED状態でない等の理由で失敗しても
        // packetを破棄しない(-1を返すだけ)ため、失敗時は呼び出し側で破棄する
        if (enet_peer_send(m_peers.front(), static_cast<enet_uint8>(channel), packet) != 0) {
            enet_packet_destroy(packet);
        }
    } else {
        enet_packet_destroy(packet);
        return;
    }
    enet_host_flush(m_host);
}

void NetworkManager::sendChatMessage(const std::string& text) {
    std::string validated;
    if (!validateChatText(reinterpret_cast<const uint8_t*>(text.data()), text.size(), validated)) {
        RCBN_WARN("NetworkManager: rejected empty, oversized, or control-character chat message");
        return;
    }
    if (!m_host) return;

    // Client申告のIDはHostで上書きされる。Host自身の発言はここでローカル通知する。
    PeerId senderId = m_localPeerId;
    if (senderId == 0) return;
    auto data = makeChatPayload(senderId, validated);

    if (m_role == NetworkRole::Host && onChatMessage) onChatMessage(senderId, validated);
    if (m_role == NetworkRole::Client && (m_peers.empty() || senderId == 0)) return;

    ENetPacket* packet = enet_packet_create(data.data(), data.size(), ENET_PACKET_FLAG_RELIABLE);
    broadcastPacket(packet, NetworkChannel::Reliable);
}

const char* NetworkManager::connectionErrorToString(ConnectionError error) {
    switch (error) {
        case ConnectionError::None: return "None";
        case ConnectionError::MissingConfig: return "MissingConfig";
        case ConnectionError::StunTimeout: return "StunTimeout";
        case ConnectionError::RoomNotFound: return "RoomNotFound";
        case ConnectionError::RoomFull: return "RoomFull";
        case ConnectionError::RendezvousTimeout: return "RendezvousTimeout";
        case ConnectionError::PunchTimeout: return "PunchTimeout";
        case ConnectionError::AdmissionRejected: return "AdmissionRejected";
        case ConnectionError::EnetTimeout: return "EnetTimeout";
        default: return "Unknown";
    }
}

bool NetworkManager::sendBytes(const std::vector<uint8_t>& payload, NetworkChannel channel) {
    if (!m_host || m_peers.empty() || payload.empty()) return false;
    uint32_t flags = (channel == NetworkChannel::Reliable) ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* packet = enet_packet_create(payload.data(), payload.size(), flags);
    if (!packet) return false;
    broadcastPacket(packet, channel);
    return true;
}
