#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <functional>

#define RECUBIN_DEBUG

// ConsolePanel への転送フック（Editor 側で設定される）
inline std::function<void(const std::string&)> g_logHook;

#ifdef RECUBIN_DEBUG
    #define RCBN_LOG(msg) \
        do { \
            std::ostringstream _ss; _ss << "[LOG] " << msg; \
            std::cout << "[RCBN_DEBUG] " << msg << std::endl; \
            if (g_logHook) g_logHook(_ss.str()); \
        } while(0)

    #define RCBN_WARN(msg) \
        do { \
            std::ostringstream _ss; _ss << "[WARN] " << msg; \
            std::cout << "[RCBN_WARN] " << msg << std::endl; \
            if (g_logHook) g_logHook(_ss.str()); \
        } while(0)

    #define RCBN_ERROR(msg) \
        do { \
            std::ostringstream _ss; _ss << "[ERROR] " << msg; \
            std::cerr << "[RCBN_ERROR] " << msg << std::endl; \
            if (g_logHook) g_logHook(_ss.str()); \
        } while(0)
#else
    #define RCBN_LOG(msg)   ((void)0)
    #define RCBN_WARN(msg)  ((void)0)
    #define RCBN_ERROR(msg) ((void)0)
#endif
