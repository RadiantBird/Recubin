#include <Network/NetworkManager.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <cstring>

NetworkManager& NetworkManager::get() {
    static NetworkManager instance;
    return instance;
}

NetworkManager::~NetworkManager() {
    shutdown();
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

    m_role = NetworkRole::Host;
    m_peers.clear();
    RCBN_LOG("NetworkManager: Host started on port " << port);
    return true;
}

bool NetworkManager::connect(const std::string& address, uint16_t port) {
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

    m_role = NetworkRole::Client;
    m_peers.clear(); // ハンドシェイク完了(ENET_EVENT_TYPE_CONNECT)時にhandleEventで追加される
    RCBN_LOG("NetworkManager: connecting to " << address << ":" << port << " ...");
    return true;
}

void NetworkManager::shutdown() {
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_peers.clear();
    m_role = NetworkRole::Offline;
}

void NetworkManager::poll() {
    if (!m_host) return;

    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        handleEvent(event);
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
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            m_peers.erase(std::remove(m_peers.begin(), m_peers.end(), event.peer), m_peers.end());
            RCBN_LOG("NetworkManager: peer disconnected");
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            if (event.packet->dataLength >= 1) {
                auto type = static_cast<MessageType>(event.packet->data[0]);
                const uint8_t* payload = event.packet->data + 1;
                size_t payloadLen = event.packet->dataLength - 1;

                if (type == MessageType::Chat) {
                    std::string text(reinterpret_cast<const char*>(payload), payloadLen);
                    RCBN_LOG("NetworkManager: [chat] " << text);
                } else if (type == MessageType::DummyPosition && payloadLen >= sizeof(float) * 3) {
                    Vector3 pos;
                    std::memcpy(&pos.x, payload, sizeof(float) * 3);
                    RCBN_LOG("NetworkManager: [dummy pos] (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
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

void NetworkManager::sendDummyPosition(const Vector3& pos) {
    uint8_t data[1 + sizeof(float) * 3];
    data[0] = static_cast<uint8_t>(MessageType::DummyPosition);
    std::memcpy(data + 1, &pos.x, sizeof(float) * 3);

    ENetPacket* packet = enet_packet_create(data, sizeof(data), 0 /* unreliable */);
    broadcastPacket(packet, NetworkChannel::Unreliable);
}
