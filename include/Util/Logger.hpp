#pragma once

#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#define RECUBIN_DEBUG
#define DEBUGLEVEL 0

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)

#ifdef RECUBIN_DEBUG
    #pragma message("Recubin: Debug logging enabled")
    #pragma message("DEBUGLEVEL = " STRINGIFY(DEBUGLEVEL))
#endif

namespace Util {
    constexpr std::string_view getFileName(std::string_view path) {
        const size_t pos = path.find_last_of("\\/");
        return (pos == std::string_view::npos)
            ? path
            : path.substr(pos + 1);
    }
}

// ConsolePanel への転送フック（Editor 側で設定される）
inline std::function<void(const std::string&)> g_logHook;
inline std::function<void(const std::string&)> g_luauLogHook;

#ifdef RECUBIN_DEBUG

#define RCBN_ASSERT(cond, msg)                                          \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::cerr                                                   \
                << "[ASSERT] " << msg                                   \
                << " (" << __FILE__ << ":" << __LINE__ << ")\n";       \
            std::abort();                                               \
        }                                                               \
    } while (0)

#if DEBUGLEVEL <= 0

#define RCBN_TRACE(msg)                                                 \
    do {                                                                \
        std::ostringstream _ss;                                         \
        _ss                                                             \
            << "[" << ::Util::getFileName(__FILE__)                     \
            << ":" << __LINE__ << "] "                                  \
            << msg;                                                     \
        std::cout                                                       \
            << "\033[36m[RCBN_TRACE]"                                   \
            << _ss.str()                                                \
            << "\033[0m"                                                \
            << std::endl;                                               \
        if (g_logHook)                                                  \
            g_logHook("[TRACE]" + _ss.str());                           \
    } while (0)

#else

#define RCBN_TRACE(msg) ((void)0)

#endif


#if DEBUGLEVEL <= 1

#define RCBN_LOG(msg)                                                   \
    do {                                                                \
        std::ostringstream _ss;                                         \
        _ss                                                             \
            << "[" << ::Util::getFileName(__FILE__)                     \
            << ":" << __LINE__ << "] "                                  \
            << msg;                                                     \
        std::cout                                                       \
            << "[RCBN_DEBUG]"                                           \
            << _ss.str()                                                \
            << std::endl;                                               \
        if (g_logHook)                                                  \
            g_logHook("[LOG]" + _ss.str());                             \
    } while (0)

#else

#define RCBN_LOG(msg) ((void)0)

#endif


#if DEBUGLEVEL <= 2

#define RCBN_WARN(msg)                                                  \
    do {                                                                \
        std::ostringstream _ss;                                         \
        _ss                                                             \
            << "[" << ::Util::getFileName(__FILE__)                     \
            << ":" << __LINE__ << "] "                                  \
            << msg;                                                     \
        std::cout                                                       \
            << "[RCBN_WARN]"                                            \
            << _ss.str()                                                \
            << std::endl;                                               \
        if (g_logHook)                                                  \
            g_logHook("[WARN]" + _ss.str());                            \
    } while (0)

#else

#define RCBN_WARN(msg) ((void)0)

#endif


#if DEBUGLEVEL <= 3

#define RCBN_ERROR(msg)                                                 \
    do {                                                                \
        std::ostringstream _ss;                                         \
        _ss                                                             \
            << "[" << ::Util::getFileName(__FILE__)                     \
            << ":" << __LINE__ << "] "                                  \
            << msg;                                                     \
        std::cerr                                                       \
            << "\033[31m[RCBN_ERROR]"                                   \
            << _ss.str()                                                \
            << "\033[0m"                                                \
            << std::endl;                                               \
        if (g_logHook)                                                  \
            g_logHook("[ERROR]" + _ss.str());                           \
    } while (0)

#else

#define RCBN_ERROR(msg) ((void)0)

#endif

#else

#define RCBN_ASSERT(cond, msg) ((void)0)
#define RCBN_TRACE(msg)       ((void)0)
#define RCBN_LOG(msg)         ((void)0)
#define RCBN_WARN(msg)        ((void)0)
#define RCBN_ERROR(msg)       ((void)0)

#endif