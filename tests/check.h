#pragma once

#include <cstdio>

// Minimal assertion helpers: tests are plain executables run by CTest, so
// they need no test framework on clean CI machines.
inline int g_failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

#define TEST_RESULT() (g_failures ? (std::fprintf(stderr, "%d check(s) failed\n", g_failures), 1) : 0)
