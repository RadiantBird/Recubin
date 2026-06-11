#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <functional>

#define RECUBIN_DEBUG

#include <string_view>

namespace Util {
    constexpr std::string_view getFileName(std::string_view path) {
        size_t pos = path.find_last_of("\\/");
        return (pos == std::string_view::npos) ? path : path.substr(pos + 1);
    }
}

// ConsolePanel への転送フック（Editor 側で設定される）
inline std::function<void(const std::string&)> g_logHook;
inline std::function<void(const std::string&)> g_luauLogHook;

#ifdef RECUBIN_DEBUG
    #define RCBN_LOG(msg) \
        do { \
            std::ostringstream _ss; _ss << "[" << ::Util::getFileName(__FILE__) << ":" << __LINE__ << "] " << msg; \
            std::cout << "[RCBN_DEBUG]" << _ss.str() << std::endl; \
            if (g_logHook) g_logHook("[LOG]" + _ss.str()); \
        } while(0)

    #define RCBN_WARN(msg) \
        do { \
            std::ostringstream _ss; _ss << "[" << ::Util::getFileName(__FILE__) << ":" << __LINE__ << "] " << msg; \
            std::cout << "[RCBN_WARN]" << _ss.str() << std::endl; \
            if (g_logHook) g_logHook("[WARN]" + _ss.str()); \
        } while(0)

    #define RCBN_ERROR(msg) \
        do { \
            std::ostringstream _ss; _ss << "[" << ::Util::getFileName(__FILE__) << ":" << __LINE__ << "] " << msg; \
            std::cerr << "\033[31m[RCBN_ERROR]" << _ss.str() << "\033[0m" << std::endl; \
            if (g_logHook) g_logHook("[ERROR]" + _ss.str()); \
        } while(0)

    #define RCBN_TRACE(msg) \
        do { \
            std::ostringstream _ss; _ss << "[" << ::Util::getFileName(__FILE__) << ":" << __LINE__ << "] " << msg; \
            std::cout << "\033[36m[RCBN_TRACE]" << _ss.str() << "\033[0m" << std::endl; \
            if (g_logHook) g_logHook("[TRACE]" + _ss.str()); \
        } while(0)
#else
    #define RCBN_LOG(msg)   ((void)0)
    #define RCBN_WARN(msg)  ((void)0)
    #define RCBN_ERROR(msg) ((void)0)
    #define RCBN_TRACE(msg) ((void)0)
#endif
