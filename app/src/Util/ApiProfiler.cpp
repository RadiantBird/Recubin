#include <Util/ApiProfiler.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace {

struct Accum {
    std::uint64_t calls = 0;
    std::int64_t totalNs = 0;
    std::int64_t selfNs = 0;
    std::int64_t maxNs = 0;
};

std::mutex g_mutex;
std::unordered_map<std::string, Accum> g_entries;
thread_local ApiProfiler::Scope* t_currentScope = nullptr;

std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
    }
    return out;
}

} // namespace

void ApiProfiler::reset() {
    std::lock_guard lock(g_mutex);
    g_entries.clear();
}

std::vector<ApiProfiler::Entry> ApiProfiler::snapshot() {
    std::vector<Entry> result;
    {
        std::lock_guard lock(g_mutex);
        result.reserve(g_entries.size());
        for (const auto& [name, accum] : g_entries) {
            Entry entry;
            entry.name = name;
            entry.calls = accum.calls;
            entry.totalMs = static_cast<double>(accum.totalNs) / 1.0e6;
            entry.selfMs = static_cast<double>(accum.selfNs) / 1.0e6;
            entry.maxMs = static_cast<double>(accum.maxNs) / 1.0e6;
            result.push_back(std::move(entry));
        }
    }
    std::sort(result.begin(), result.end(),
              [](const Entry& a, const Entry& b) { return a.selfMs > b.selfMs; });
    return result;
}

std::string ApiProfiler::toJson(std::size_t topN) {
    const auto entries = snapshot();
    std::string json = std::string("{\"enabled\":") + (s_enabled ? "true" : "false") + ",\"entries\":[";
    char buffer[256];
    for (std::size_t i = 0; i < entries.size() && i < topN; ++i) {
        const Entry& e = entries[i];
        const double avgUs = e.calls ? e.totalMs * 1000.0 / static_cast<double>(e.calls) : 0.0;
        std::snprintf(buffer, sizeof(buffer),
                      "\"calls\":%llu,\"totalMs\":%.3f,\"selfMs\":%.3f,\"avgUs\":%.3f,\"maxMs\":%.3f}",
                      static_cast<unsigned long long>(e.calls), e.totalMs, e.selfMs, avgUs, e.maxMs);
        json += (i ? "," : "");
        json += "{\"name\":\"" + jsonEscape(e.name) + "\"," + buffer;
    }
    json += "]}";
    return json;
}

ApiProfiler::Scope::Scope(const char* name) {
    if (!s_enabled) return;
    begin(name);
}

ApiProfiler::Scope::Scope(const char* name, std::string_view detail) {
    if (!s_enabled) return;
    std::string full(name);
    full += ':';
    full += detail;
    begin(std::move(full));
}

void ApiProfiler::Scope::begin(std::string name) {
    m_active = true;
    m_name = std::move(name);
    m_parent = t_currentScope;
    t_currentScope = this;
    m_startNs = nowNs();  // 名前を作るコストは計測に含めない
}

ApiProfiler::Scope::~Scope() {
    if (!m_active) return;
    const std::int64_t totalNs = nowNs() - m_startNs;
    t_currentScope = m_parent;
    if (m_parent) m_parent->m_childNs += totalNs;
    std::lock_guard lock(g_mutex);
    Accum& accum = g_entries[m_name];
    ++accum.calls;
    accum.totalNs += totalNs;
    accum.selfNs += totalNs - m_childNs;
    accum.maxNs = std::max(accum.maxNs, totalNs);
}
