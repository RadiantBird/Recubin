#include "Util/FrameProfiler.hpp"
#include "Util/Logger.hpp"
#include <cstring>
#include <cstdio>
#include <sstream>
#include <iomanip>

FrameProfiler& FrameProfiler::get() {
    static FrameProfiler instance;
    return instance;
}

FrameProfiler::Section* FrameProfiler::findSection(const char* name) {
    for (auto& s : m_sections) {
        if (std::strcmp(s.name, name) == 0) return &s;
    }
    m_sections.push_back(Section{name});
    return &m_sections.back();
}

FrameProfiler::Counter* FrameProfiler::findCounter(const char* name) {
    for (auto& c : m_counters) {
        if (std::strcmp(c.name, name) == 0) return &c;
    }
    m_counters.push_back(Counter{name});
    return &m_counters.back();
}

void FrameProfiler::beginSection(const char* name) {
    Section* s = findSection(name);
    s->begin = std::chrono::steady_clock::now();
    s->running = true;
}

void FrameProfiler::endSection(const char* name) {
    Section* s = findSection(name);
    if (!s->running) return;
    auto now = std::chrono::steady_clock::now();
    s->accumMs += std::chrono::duration<double, std::milli>(now - s->begin).count();
    s->running = false;
}

void FrameProfiler::addCount(const char* name, long long n) {
    Counter* c = findCounter(name);
    c->accum += n;
}

void FrameProfiler::endFrame() {
    auto now = std::chrono::steady_clock::now();

    if (!m_hasWindowStart) {
        m_windowStart = now;
        m_hasWindowStart = true;
        return;
    }

    m_frames++;

    double elapsedMs = std::chrono::duration<double, std::milli>(now - m_windowStart).count();
    if (elapsedMs >= 1000.0) {
        double elapsedSec = elapsedMs / 1000.0;
        double fps = m_frames / elapsedSec;
        double frameMs = elapsedMs / m_frames;

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1);
        ss << "fps=" << fps << " frame=" << frameMs << "ms | ";
        for (size_t i = 0; i < m_sections.size(); ++i) {
            if (i != 0) ss << " ";
            ss << m_sections[i].name << "=" << (m_sections[i].accumMs / m_frames);
        }
        ss << " (ms) | ";
        for (size_t i = 0; i < m_counters.size(); ++i) {
            if (i != 0) ss << " ";
            long long avg = static_cast<long long>((m_counters[i].accum / m_frames));
            ss << m_counters[i].name << "=" << avg;
        }

        // RCBN_LOG("[PROF] " << ss.str());

        for (auto& s : m_sections) s.accumMs = 0.0;
        for (auto& c : m_counters) c.accum = 0;
        m_frames = 0;
        m_windowStart = now;
    }
}

FrameProfiler::Scope::Scope(const char* name) : m_name(name) {
    FrameProfiler::get().beginSection(m_name);
}

FrameProfiler::Scope::~Scope() {
    FrameProfiler::get().endSection(m_name);
}
