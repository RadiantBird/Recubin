#include "Util/FrameProfiler.hpp"
#include "Util/Logger.hpp"
#include <cstring>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <algorithm>

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
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(now - s->begin).count();
    s->accumMs += elapsedMs;
    s->frameMs += elapsedMs;
    s->running = false;
}

void FrameProfiler::addCount(const char* name, long long n) {
    Counter* c = findCounter(name);
    c->accum += n;
    c->frameAccum += n;
}

void FrameProfiler::endFrame() {
    auto now = std::chrono::steady_clock::now();

    for (Section& section : m_sections) {
        section.history[section.historyWriteIndex] =
            static_cast<float>(section.frameMs);
        section.historyWriteIndex =
            (section.historyWriteIndex + 1) % HISTORY_CAPACITY;
        section.historyCount =
            std::min(section.historyCount + 1, HISTORY_CAPACITY);
        section.frameMs = 0.0;
    }
    for (Counter& counter : m_counters) {
        counter.history[counter.historyWriteIndex] = counter.frameAccum;
        counter.historyWriteIndex =
            (counter.historyWriteIndex + 1) % HISTORY_CAPACITY;
        counter.historyCount =
            std::min(counter.historyCount + 1, HISTORY_CAPACITY);
        counter.frameAccum = 0;
    }

    if (!m_hasWindowStart) {
        m_windowStart = now;
        m_hasWindowStart = true;
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

bool FrameProfiler::getSectionSnapshot(
    const char* name, SectionSnapshot& snapshot) const {
    snapshot = {};
    if (!name) return false;
    const auto section = std::find_if(
        m_sections.begin(), m_sections.end(),
        [&](const Section& value) {
            return std::strcmp(value.name, name) == 0;
        });
    if (section == m_sections.end()) return false;

    snapshot.count = section->historyCount;
    const std::size_t first =
        (section->historyWriteIndex + HISTORY_CAPACITY - section->historyCount) %
        HISTORY_CAPACITY;
    double total = 0.0;
    for (std::size_t index = 0; index < section->historyCount; ++index) {
        const float sample =
            section->history[(first + index) % HISTORY_CAPACITY];
        snapshot.samples[index] = sample;
        total += sample;
        snapshot.peakMs = std::max(snapshot.peakMs, sample);
    }
    if (snapshot.count != 0) {
        snapshot.latestMs = snapshot.samples[snapshot.count - 1];
        snapshot.averageMs =
            static_cast<float>(total / static_cast<double>(snapshot.count));
    }
    return true;
}

bool FrameProfiler::getCounterSnapshot(
    const char* name, CounterSnapshot& snapshot) const {
    snapshot = {};
    if (!name) return false;
    const auto counter = std::find_if(
        m_counters.begin(), m_counters.end(),
        [&](const Counter& value) {
            return std::strcmp(value.name, name) == 0;
        });
    if (counter == m_counters.end()) return false;

    snapshot.count = counter->historyCount;
    const std::size_t first =
        (counter->historyWriteIndex + HISTORY_CAPACITY - counter->historyCount) %
        HISTORY_CAPACITY;
    long double total = 0.0;
    for (std::size_t index = 0; index < counter->historyCount; ++index) {
        const long long sample =
            counter->history[(first + index) % HISTORY_CAPACITY];
        snapshot.samples[index] = sample;
        total += static_cast<long double>(sample);
        snapshot.peak = std::max(snapshot.peak, sample);
    }
    if (snapshot.count != 0) {
        snapshot.latest = snapshot.samples[snapshot.count - 1];
        snapshot.average = static_cast<double>(
            total / static_cast<long double>(snapshot.count));
    }
    return true;
}

FrameProfiler::Scope::Scope(const char* name) : m_name(name) {
    FrameProfiler::get().beginSection(m_name);
}

FrameProfiler::Scope::~Scope() {
    FrameProfiler::get().endSection(m_name);
}
