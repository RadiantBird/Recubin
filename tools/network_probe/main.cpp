#include <Network/NetworkManager.hpp>

#include <charconv>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

volatile std::sig_atomic_t g_stopRequested = 0;

struct LaunchOptions {
    bool host = false;
    bool client = false;
    bool directHost = false;
    bool directClient = false;
    bool chatEnabled = true;
    std::string roomCode;
    std::string directAddress;
    std::string stun;
    std::string rendezvous;
    uint16_t directPort = 0;
    uint16_t listenPort = 0;
    uint32_t durationSeconds = 0;
    uint32_t messageIntervalSeconds = 5;
    uint32_t expectedPeers = 0;
};

void handleSignal(int) {
    g_stopRequested = 1;
}

const char* stateName(ConnectionState state) {
    switch (state) {
        case ConnectionState::Offline: return "Offline";
        case ConnectionState::Discovering: return "Discovering";
        case ConnectionState::CreatingRoom: return "CreatingRoom";
        case ConnectionState::JoiningRoom: return "JoiningRoom";
        case ConnectionState::Punching: return "Punching";
        case ConnectionState::Connecting: return "Connecting";
        case ConnectionState::Connected: return "Connected";
        case ConnectionState::Migrating: return "Migrating";
        case ConnectionState::Failed: return "Failed";
        default: return "Unknown";
    }
}

const char* candidateTypeName(CandidateType type) {
    switch (type) {
        case CandidateType::Local: return "local";
        case CandidateType::ServerReflexive: return "server-reflexive";
        case CandidateType::PeerReflexive: return "peer-reflexive";
        default: return "unknown";
    }
}

bool parseUnsigned(std::string_view value, uint32_t maximum, uint32_t& output) {
    if (value.empty()) return false;
    uint32_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed > maximum) {
        return false;
    }
    output = parsed;
    return true;
}

bool parseEndpoint(const std::string& value,
                   uint16_t defaultPort,
                   std::string& host,
                   uint16_t& port) {
    if (value.empty()) return false;
    const size_t separator = value.rfind(':');
    if (separator == std::string::npos) {
        host = value;
        port = defaultPort;
        return true;
    }
    if (separator == 0 || separator + 1 >= value.size()) return false;
    uint32_t parsedPort = 0;
    if (!parseUnsigned(std::string_view(value).substr(separator + 1), 65535, parsedPort) ||
        parsedPort == 0) {
        return false;
    }
    host = value.substr(0, separator);
    port = static_cast<uint16_t>(parsedPort);
    return true;
}

bool isValidDirectHost(const std::string& host) {
    if (host.empty() || host.size() > 253 ||
        host.front() == '.' || host.back() == '.') {
        return false;
    }
    size_t labelStart = 0;
    while (labelStart < host.size()) {
        const size_t labelEnd = host.find('.', labelStart);
        const size_t end = labelEnd == std::string::npos ? host.size() : labelEnd;
        if (end == labelStart || end - labelStart > 63 ||
            host[labelStart] == '-' || host[end - 1] == '-') {
            return false;
        }
        for (size_t index = labelStart; index < end; ++index) {
            const unsigned char c = static_cast<unsigned char>(host[index]);
            if (!std::isalnum(c) && c != '-') return false;
        }
        if (labelEnd == std::string::npos) break;
        labelStart = labelEnd + 1;
    }

    bool allNumericOrDots = true;
    for (const unsigned char c : host) {
        if (!std::isdigit(c) && c != '.') {
            allNumericOrDots = false;
            break;
        }
    }
    if (!allNumericOrDots || host.find('.') == std::string::npos) return true;

    size_t start = 0;
    int octets = 0;
    while (start < host.size()) {
        const size_t dot = host.find('.', start);
        const size_t end = dot == std::string::npos ? host.size() : dot;
        uint32_t octet = 0;
        const std::string_view value(host.data() + start, end - start);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), octet);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
            octet > 255 || ++octets > 4) {
            return false;
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return octets == 4;
}

bool parseDirectEndpoint(const std::string& value,
                         std::string& host,
                         uint16_t& port) {
    const size_t separator = value.find(':');
    if (separator == std::string::npos || separator != value.rfind(':') ||
        separator == 0 || separator + 1 >= value.size()) {
        return false;
    }
    uint32_t parsedPort = 0;
    if (!parseUnsigned(std::string_view(value).substr(separator + 1), 65535, parsedPort) ||
        parsedPort == 0) {
        return false;
    }
    host = value.substr(0, separator);
    if (!isValidDirectHost(host)) return false;
    port = static_cast<uint16_t>(parsedPort);
    return true;
}

void printUsage(const char* executable) {
    std::cout
        << "Usage:\n"
        << "  " << executable
        << " --host --stun <host[:port]> --rendezvous <host[:port]> [options]\n"
        << "  " << executable
        << " --connect <room-code> --stun <host[:port]> --rendezvous <host[:port]> [options]\n"
        << "  " << executable << " --direct-host <port> [options]\n"
        << "  " << executable << " --direct-connect <host:port> [options]\n"
        << "\nOptions:\n"
        << "  --listen-port <port>       Local UDP port; 0 lets the OS choose\n"
        << "  --duration <seconds>       Stop after this duration; 0 waits for Ctrl-C\n"
        << "  --message-interval <sec>   Automatic chat probe interval (default 5)\n"
        << "  --no-chat                  Disable automatic chat probes\n"
        << "  --expect-peers <count>     Print READY when this roster size is reached\n";
}

bool parseArguments(int argc, char** argv, LaunchOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        const auto takeValue = [&](std::string& output) {
            if (index + 1 >= argc) return false;
            output = argv[++index];
            return true;
        };
        const auto takeNumber = [&](uint32_t maximum, uint32_t& output) {
            if (index + 1 >= argc) return false;
            return parseUnsigned(argv[++index], maximum, output);
        };

        if (argument == "--host") {
            options.host = true;
        } else if (argument == "--connect") {
            options.client = true;
            if (!takeValue(options.roomCode)) return false;
        } else if (argument == "--direct-host") {
            options.directHost = true;
            uint32_t value = 0;
            if (!takeNumber(65535, value) || value == 0) return false;
            options.directPort = static_cast<uint16_t>(value);
        } else if (argument == "--direct-connect") {
            options.directClient = true;
            std::string endpoint;
            if (!takeValue(endpoint) ||
                !parseDirectEndpoint(endpoint, options.directAddress, options.directPort)) {
                return false;
            }
        } else if (argument == "--stun") {
            if (!takeValue(options.stun)) return false;
        } else if (argument == "--rendezvous") {
            if (!takeValue(options.rendezvous)) return false;
        } else if (argument == "--listen-port") {
            uint32_t value = 0;
            if (!takeNumber(65535, value)) return false;
            options.listenPort = static_cast<uint16_t>(value);
        } else if (argument == "--duration") {
            if (!takeNumber(86400, options.durationSeconds)) return false;
        } else if (argument == "--message-interval") {
            if (!takeNumber(3600, options.messageIntervalSeconds) ||
                options.messageIntervalSeconds == 0) {
                return false;
            }
        } else if (argument == "--expect-peers") {
            if (!takeNumber(32, options.expectedPeers)) return false;
        } else if (argument == "--no-chat") {
            options.chatEnabled = false;
        } else if (argument == "--help" || argument == "-h") {
            return false;
        } else {
            return false;
        }
    }
    const int modeCount =
        static_cast<int>(options.host) + static_cast<int>(options.client) +
        static_cast<int>(options.directHost) + static_cast<int>(options.directClient);
    if (modeCount != 1) return false;
    if (options.directHost || options.directClient) return true;
    return !options.stun.empty() && !options.rendezvous.empty() &&
           (!options.client || options.roomCode.size() == 8);
}

std::string candidateAddress(const NetworkCandidate& candidate) {
    ENetAddress address{candidate.host, candidate.port};
    char ip[64] = {};
    if (enet_address_get_host_ip(&address, ip, sizeof(ip)) != 0) {
        return "<invalid>:" + std::to_string(candidate.port);
    }
    return std::string(ip) + ":" + std::to_string(candidate.port);
}

void printCandidates(const std::vector<NetworkCandidate>& candidates,
                     const std::string& prefix) {
    for (const auto& candidate : candidates) {
        std::cout << prefix << candidateTypeName(candidate.type) << " "
                  << candidateAddress(candidate) << '\n';
    }
}

std::string rosterFingerprint(const std::vector<PeerInfo>& roster) {
    std::string result;
    for (const auto& peer : roster) {
        result += std::to_string(peer.id);
        result += peer.isHost ? "H" : "C";
        result += ":";
        result += std::to_string(peer.endpoint.listenPort);
        result += ":";
        result += std::to_string(peer.endpoint.candidates.size());
        result += ";";
    }
    return result;
}

void printRoster(const NetworkManager& manager) {
    const auto& roster = manager.getRoster();
    std::cout << "[probe] roster peers=" << roster.size() << '\n';
    for (const auto& peer : roster) {
        std::cout << "[probe] peer=" << peer.id
                  << " role=" << (peer.isHost ? "host" : "client")
                  << " listen-port=" << peer.endpoint.listenPort
                  << " latency-ms=" << peer.latencyMs << '\n';
        printCandidates(peer.endpoint.candidates, "[probe]   candidate=");
    }
}

} // namespace

int main(int argc, char** argv) {
    LaunchOptions options;
    if (!parseArguments(argc, argv, options)) {
        printUsage(argv[0]);
        return 2;
    }

    const bool directMode = options.directHost || options.directClient;
    NatConfig config;
    if (!directMode) {
        config.listenPort = options.listenPort;
        if (!parseEndpoint(options.stun, 3478, config.stunHost, config.stunPort) ||
            !parseEndpoint(options.rendezvous, 3479,
                           config.rendezvousHost, config.rendezvousPort)) {
            std::cerr << "[probe] invalid STUN or rendezvous endpoint\n";
            return 2;
        }
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    NetworkManager& manager = NetworkManager::get();
    manager.onRoleChanged = [](NetworkRole oldRole, NetworkRole newRole) {
        std::cout << "[probe] role " << NetworkManager::roleToString(oldRole)
                  << " -> " << NetworkManager::roleToString(newRole) << '\n';
    };
    manager.onChatMessage = [](PeerId sender, const std::string& message) {
        std::cout << "[probe] chat peer=" << sender << " text=" << message << '\n';
    };

    const bool started = options.directHost
        ? manager.startHost(options.directPort)
        : options.directClient
            ? manager.connect(options.directAddress, options.directPort, options.listenPort)
            : options.host
                ? manager.createRoom(config)
                : manager.joinRoom(options.roomCode, config);
    if (!started) {
        const ConnectionError error = manager.getConnectionError();
        std::cerr << "[probe] startup failed: "
                  << NetworkManager::connectionErrorToString(error) << '\n';
        return 10 + static_cast<int>(error);
    }

    const auto startedAt = std::chrono::steady_clock::now();
    auto previousTick = startedAt;
    auto nextMessage = startedAt + std::chrono::seconds(options.messageIntervalSeconds);
    ConnectionState previousState = ConnectionState::Offline;
    std::string previousRoster;
    bool printedRoom = false;
    bool printedCandidates = false;
    bool printedReady = false;
    uint32_t messageSequence = 0;
    int exitCode = 0;

    while (g_stopRequested == 0) {
        const auto now = std::chrono::steady_clock::now();
        const float elapsed =
            std::chrono::duration<float>(now - previousTick).count();
        previousTick = now;
        manager.update(elapsed);

        const ConnectionState state = manager.getConnectionState();
        if (state != previousState) {
            std::cout << "[probe] state " << stateName(previousState)
                      << " -> " << stateName(state) << '\n';
            previousState = state;
        }
        if (state == ConnectionState::Failed) {
            const ConnectionError error = manager.getConnectionError();
            std::cerr << "[probe] failed: "
                      << NetworkManager::connectionErrorToString(error) << '\n';
            exitCode = 10 + static_cast<int>(error);
            break;
        }
        if (state == ConnectionState::Connected && !printedCandidates) {
            printedCandidates = true;
            std::cout << "[probe] connected peer=" << manager.getLocalPeerId()
                      << " listen-port=" << manager.getListenPort() << '\n';
            printCandidates(manager.getLocalCandidates(), "[probe] local-candidate=");
        }
        if (options.host && state == ConnectionState::Connected && !printedRoom) {
            printedRoom = true;
            std::cout << "[probe] room-code=" << manager.getRoomCode() << '\n';
        }

        const std::string currentRoster = rosterFingerprint(manager.getRoster());
        if (currentRoster != previousRoster) {
            previousRoster = currentRoster;
            printRoster(manager);
        }
        if (!printedReady && options.expectedPeers > 0 &&
            manager.getRoster().size() >= options.expectedPeers) {
            printedReady = true;
            std::cout << "[probe] READY expected-peers=" << options.expectedPeers << '\n';
        }

        if (options.chatEnabled && state == ConnectionState::Connected &&
            manager.hasPeers() && now >= nextMessage) {
            manager.sendChatMessage(
                "network-probe-" + std::to_string(manager.getLocalPeerId()) +
                "-" + std::to_string(++messageSequence));
            nextMessage = now + std::chrono::seconds(options.messageIntervalSeconds);
        }
        if (options.durationSeconds > 0 &&
            now - startedAt >= std::chrono::seconds(options.durationSeconds)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    manager.shutdown();
    manager.onRoleChanged = nullptr;
    manager.onChatMessage = nullptr;
    return exitCode;
}
