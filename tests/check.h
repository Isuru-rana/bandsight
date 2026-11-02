// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal assertions for the tests below: no test framework dependency.
#pragma once
#include <cstdio>

inline int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                         #cond);                                                 \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)
