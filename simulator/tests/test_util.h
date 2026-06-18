#pragma once
#include <cstdio>
#include <cstring>

// Minimal test framework — no external deps, compiles on Arduino-class toolchains.

static int _pass = 0, _fail = 0;
static const char* _suite = "";

static inline void _suite_set(const char* name) {
    _suite = name;
    printf("\n%s\n", name);
}

#define SUITE(name)   _suite_set(name)

#define CHECK(expr) do { \
    if (expr) { \
        _pass++; \
    } else { \
        fprintf(stderr, "  FAIL  %s:%d  " #expr "\n", __FILE__, __LINE__); \
        _fail++; \
    } \
} while(0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) { _pass++; } \
    else { \
        fprintf(stderr, "  FAIL  %s:%d  " #a " == " #b \
                "  (got %lld vs %lld)\n", \
                __FILE__, __LINE__, (long long)_a, (long long)_b); \
        _fail++; \
    } \
} while(0)

#define RESULTS() do { \
    printf("\n%d/%d passed", _pass, _pass + _fail); \
    if (_fail == 0) printf("  \033[32m✓ all green\033[0m"); \
    else            printf("  \033[31m%d FAILED\033[0m", _fail); \
    printf("\n"); \
    return (_fail > 0) ? 1 : 0; \
} while(0)
