// Minimal CHECK macro shared by the newer host tests.
#pragma once
#include <stdio.h>
static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (c) {                                                         \
            g_pass++;                                                    \
        } else {                                                         \
            g_fail++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                \
    } while (0)
#define CHECK_DONE()                                       \
    do {                                                   \
        printf("%d passed, %d failed\n", g_pass, g_fail);  \
        return g_fail ? 1 : 0;                             \
    } while (0)
