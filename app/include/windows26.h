#ifndef WINDOWS26_H
#define WINDOWS26_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN

#define NODRAWTEXT
#define NOKANJI
#define NOSERVICE

#include <windows.h>

#undef GetInstanceName

#endif