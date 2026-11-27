#pragma once
// Tiny assertion harness, in the spirit of the donor's check.h: one counter, one failure line.

#include <cstdio>
#include <string>

extern int g_checks;
extern int g_failures;
extern std::wstring g_tempRoot;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                 \
    } while (0)

#define CHECK_MSG(cond, msg)                                                           \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::printf("FAIL %s:%d  %s  [%s]\n", __FILE__, __LINE__, #cond,           \
                        std::string(msg).c_str());                                     \
        }                                                                              \
    } while (0)
