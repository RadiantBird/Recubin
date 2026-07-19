#include <Network/NetworkManager.hpp>
#include <Network/ByteStream.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

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

bool NetworkManager::startHost(uint16_t port) {
    shutdown();

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

    changeRole(NetworkRole::Host);
    m_peers.clear();

    m_listenPort = port;
    if (m_localCpuScore == 0.0f) {
        m_localCpuScore = measureCpuScore();
        RCBN_LOG("NetworkManager: local CPU score = " << m_localCpuScore);
    }
    m_localPeerId = 1;
    m_nextPeerId = 2;
    m_roster.clear();
    PeerInfo self;
    self.id = 1;
    self.endpoint = PeerEndpoint{0, port};
    self.cpuScore = m_localCpuScore;
    self.latencyMs = 0.0f;
    self.isHost = true;
    m_roster.push_back(self);
    m_peerIds.clear();
    m_rosterDirty = true;

    RCBN_LOG("NetworkManager: Host started on port " << port);
    return true;
}

bool NetworkManager::connect(const std::string& address, uint16_t port, uint16_t listenPort) {
    shutdown();

    if (!m_enetInitialized) {
        if (enet_initialize() != 0) {
            RCBN_ERROR("ENet: enet_initialize failed");
            return false;
        }
        m_enetInitialized = true;
    }

    m_host = enet_host_create(nullptr, 1, static_cast<size_t>(NetworkChannel::Count), 0, 0);
    if (!m_host) {
        RCBN_ERROR("ENet: failed to create client host");
        return false;
    }

    ENetAddress enetAddress;
    enet_address_set_host(&enetAddress, address.c_str());
    enetAddress.port = port;

    ENetPeer* peer = enet_host_connect(m_host, &enetAddress, static_cast<size_t>(NetworkChannel::Count), 0);
    if (!peer) {
        RCBN_ERROR("ENet: no available peers for connect() to " << address << ":" << port);
        enet_host_destroy(m_host);
        m_host = nullptr;
        return false;
    }

    changeRole(NetworkRole::Client);
    m_peers.clear(); // ハンドシェイク完了(ENET_EVENT_TYPE_CONNECT)時にhandleEventで追加される

    m_listenPort = listenPort;
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

void NetworkManager::shutdown() {
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_peers.clear();
    changeRole(NetworkRole::Offline);

    m_peerIds.clear();
    m_roster.clear();
    m_localPeerId = 0;
    m_resourceReportTimer = 0.0f;
    m_rosterBroadcastTimer = 0.0f;
    m_rosterDirty = false;

    m_migrationState = MigrationState::None;
    m_migrationRetryTimer = 0.0f;
    m_migrationElapsed = 0.0f;
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

    if (m_migrationState != MigrationState::None) {
        updateMigration(dt);
        return; // 移行中は通常の周期送信をしない
    }
    if (!m_host) return;

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
        RCBN_ERROR("NetworkManager: migration timed out");
        shutdown();
        return;
    }

    switch (m_migrationState) {
        case MigrationState::Electing: {
            PeerInfo winner = electNewHost();
            if (winner.id == 0) {
                RCBN_ERROR("NetworkManager: no migration candidates");
                shutdown();
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
            if (m_migrationRetryTimer <= 0.0f) {
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
        if (info.isHost) continue;
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
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_peers.clear();
    m_peerIds.clear();

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = m_listenPort;

    m_host = enet_host_create(&address, 32, static_cast<size_t>(NetworkChannel::Count), 0, 0);
    if (!m_host) {
        RCBN_ERROR("NetworkManager: failed to promote to host on port " << m_listenPort);
        return false;
    }

    changeRole(NetworkRole::Host);

    m_roster.erase(std::remove_if(m_roster.begin(), m_roster.end(),
        [](const PeerInfo& info) { return info.isHost; }), m_roster.end());

    bool selfFound = false;
    for (auto& info : m_roster) {
        if (info.id == m_localPeerId) {
            info.isHost = true;
            info.latencyMs = 0.0f;
            info.endpoint = PeerEndpoint{0, m_listenPort};
            info.cpuScore = m_localCpuScore;
            selfFound = true;
            break;
        }
    }
    if (!selfFound) {
        PeerInfo self;
        self.id = m_localPeerId;
        self.endpoint = PeerEndpoint{0, m_listenPort};
        self.cpuScore = m_localCpuScore;
        self.latencyMs = 0.0f;
        self.isHost = true;
        m_roster.push_back(self);
    }

    m_rosterDirty = true;
    m_resourceReportTimer = 0.0f;
    m_rosterBroadcastTimer = 0.0f;

    RCBN_LOG("NetworkManager: promoted to host on port " << m_listenPort);
    return true;
}

bool NetworkManager::connectToEndpoint(const PeerEndpoint& endpoint) {
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_peers.clear();

    m_host = enet_host_create(nullptr, 1, static_cast<size_t>(NetworkChannel::Count), 0, 0);
    if (!m_host) {
        RCBN_ERROR("ENet: failed to create client host for migration reconnect");
        return false;
    }

    ENetAddress addr;
    addr.host = endpoint.host;
    addr.port = endpoint.listenPort;

    ENetPeer* peer = enet_host_connect(m_host, &addr, static_cast<size_t>(NetworkChannel::Count), 0);
    if (!peer) {
        RCBN_ERROR("ENet: no available peers for migration reconnect");
        enet_host_destroy(m_host);
        m_host = nullptr;
        return false;
    }

    changeRole(NetworkRole::Client);

    RCBN_LOG("NetworkManager: reconnecting to new host " << endpoint.host << ":" << endpoint.listenPort << " ...");
    return true;
}

void NetworkManager::sendHello() {
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(MessageType::Hello));
    w.writeU16(m_listenPort);
    w.writeU32(static_cast<uint32_t>(m_localPeerId));
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
        w.writeF32(info.cpuScore);
        w.writeF32(info.latencyMs);
        w.writeU8(info.isHost ? 1 : 0);
    }
    w.writeU32(m_nextPeerId);

    ENetPacket* packet = enet_packet_create(w.data.data(), w.data.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(m_host, static_cast<enet_uint8>(NetworkChannel::Reliable), packet);
    enet_host_flush(m_host);

    bool wasDirty = m_rosterDirty;
    m_rosterDirty = false;
    m_rosterBroadcastTimer = 0.0f;

    if (wasDirty) {
        logRoster();
    }
}

void NetworkManager::logRoster() const {
    RCBN_LOG("NetworkManager: roster (" << m_roster.size() << " peers)");
    for (const auto& info : m_roster) {
        RCBN_LOG("  peer id=" << info.id
                  << " cpuScore=" << info.cpuScore
                  << " latencyMs=" << info.latencyMs
                  << " isHost=" << (info.isHost ? "true" : "false"));
    }
}

void NetworkManager::handleEvent(const ENetEvent& event) {
    switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            // Host/Client どちらの視点でも、ハンドシェイク完了(state==CONNECTED)を
            // ここで初めて確認できる。connect()呼び出し直後の時点ではまだ送信できないため、
            // m_peersへの追加はここに一本化する。
            m_peers.push_back(event.peer);
            RCBN_LOG("NetworkManager: peer connected (" << event.peer->address.host << ":" << event.peer->address.port << ")");

            if (m_role == NetworkRole::Client) {
                sendHello();
            }
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            m_peers.erase(std::remove(m_peers.begin(), m_peers.end(), event.peer), m_peers.end());
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
            } else if (m_role == NetworkRole::Client && m_migrationState == MigrationState::None) {
                // Clientのm_peersはHost1件のみ→切断相手は常にHost
                m_migrationState = MigrationState::Electing;
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
                    std::string text(reinterpret_cast<const char*>(payload), payloadLen);
                    RCBN_LOG("NetworkManager: [chat] " << text);
                } else if (type == MessageType::Hello && m_role == NetworkRole::Host) {
                    uint16_t listenPort = 0;
                    uint32_t previousPeerId = 0;
                    if (reader.readU16(listenPort) && reader.readU32(previousPeerId)) {
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

                        PeerInfo info;
                        info.id = id;
                        info.endpoint = PeerEndpoint{event.peer->address.host, listenPort};
                        info.cpuScore = 0.0f;
                        info.latencyMs = 0.0f;
                        info.isHost = false;

                        bool replaced = false;
                        for (auto& existing : m_roster) {
                            if (existing.id == id) {
                                info.cpuScore = existing.cpuScore; // 引き継いだ集計値を保持
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
                        RCBN_LOG("NetworkManager: [welcome] assigned peer id=" << m_localPeerId);
                    }
                } else if (type == MessageType::Roster && m_role == NetworkRole::Client) {
                    uint32_t count = 0;
                    if (reader.readU32(count)) {
                        std::vector<PeerInfo> newRoster;
                        newRoster.reserve(count);
                        bool ok = true;
                        for (uint32_t i = 0; i < count && ok; ++i) {
                            uint32_t id = 0, endpointHost = 0;
                            uint16_t listenPort = 0;
                            float cpuScore = 0.0f, latencyMs = 0.0f;
                            uint8_t isHost = 0;
                            ok = reader.readU32(id) && reader.readU32(endpointHost) &&
                                 reader.readU16(listenPort) && reader.readF32(cpuScore) &&
                                 reader.readF32(latencyMs) && reader.readU8(isHost);
                            if (!ok) break;

                            PeerInfo info;
                            info.id = static_cast<PeerId>(id);
                            info.endpoint = PeerEndpoint{endpointHost, listenPort};
                            info.cpuScore = cpuScore;
                            info.latencyMs = latencyMs;
                            info.isHost = (isHost != 0);
                            newRoster.push_back(info);
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
                                        a.isHost != b.isHost) {
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
                } else if (type >= MessageType::AvatarState && type <= MessageType::WorldTransforms) {
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
        // enet_host_broadcast は非CONNECTEDなPeerを自動的にスキップし、
        // 誰にも送れなかった場合はpacket自体を内部で破棄してくれる
        enet_host_broadcast(m_host, static_cast<enet_uint8>(channel), packet);
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
    std::vector<uint8_t> data;
    data.reserve(1 + text.size());
    data.push_back(static_cast<uint8_t>(MessageType::Chat));
    data.insert(data.end(), text.begin(), text.end());

    ENetPacket* packet = enet_packet_create(data.data(), data.size(), ENET_PACKET_FLAG_RELIABLE);
    broadcastPacket(packet, NetworkChannel::Reliable);
}

bool NetworkManager::sendBytes(const std::vector<uint8_t>& payload, NetworkChannel channel) {
    if (!m_host || m_peers.empty() || payload.empty()) return false;
    uint32_t flags = (channel == NetworkChannel::Reliable) ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* packet = enet_packet_create(payload.data(), payload.size(), flags);
    if (!packet) return false;
    broadcastPacket(packet, channel);
    return true;
}
