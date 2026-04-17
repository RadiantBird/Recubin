#pragma once
#include <iostream>

#define RECUBIN_DEBUG

#ifdef RECUBIN_DEBUG
    #define RCBN_LOG(msg) std::cout << "[RCBN_DEBUG] " << msg << std::endl
    #define RCBN_WARN(msg) std::cout << "[RCBN_WARN] " << msg << std::endl
    #define RCBN_ERROR(msg) std::cerr << "[RCBN_ERROR] " << msg << std::endl
#else
    #define RCBN_LOG(msg) ((void)0)
    #define RCBN_WARN(msg) ((void)0)
    #define RCBN_ERROR(msg) ((void)0)
#endif
