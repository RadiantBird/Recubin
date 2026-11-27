#include <recubin_process.h>

#include <cstdint>

namespace {
const RCBNProcessHostApi* g_host = nullptr;
std::uint64_t g_connection = 0;
volatile std::uint32_t g_heartbeatCount = 0;

void onHeartbeat(void*, double) {
    ++g_heartbeatCount;
    if (g_heartbeatCount >= 3 && g_connection != 0 && g_host &&
        g_host->disconnectHeartbeat) {
        g_host->disconnectHeartbeat(g_host->userdata, g_connection);
        g_connection = 0;
    }
}
}

RCBN_PROCESS_BIND_HOST {
    g_host = api;
}

RCBN_PROCESS_START {
    if (!g_host || !g_host->wait) return;
    g_heartbeatCount = 0;
    g_connection = 0;
    if (g_host->connectHeartbeat) {
        g_host->connectHeartbeat(g_host->userdata, &onHeartbeat, nullptr, &g_connection);
    }
}

RCBN_PROCESS_STOP {
    if (g_host && g_connection != 0 && g_host->disconnectHeartbeat) {
        g_host->disconnectHeartbeat(g_host->userdata, g_connection);
    }
    g_connection = 0;
    g_host = nullptr;
}
