// Minimal unit test registry for Polyhymnia --selftest.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace Test {

using Fn = bool (*)();

void add(const char* name, Fn fn);
int runAll();

#define EXPECT_EQ(a, b)                                                                                                                    \
    do {                                                                                                                                   \
        const auto _ea = (a);                                                                                                              \
        const auto _eb = (b);                                                                                                              \
        if (!(_ea == _eb)) {                                                                                                               \
            std::fprintf(stderr, "  EXPECT_EQ failed at %s:%d (%s)\n", __FILE__, __LINE__, #a " == " #b);                                   \
            return false;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

// MSVC-safe equality when the expected value contains commas (e.g. brace-init lists).
#define EXPECT_EQ_T(a, b) EXPECT_EQ(a, b)

#define EXPECT_TRUE(x)                                                                                                                     \
    do {                                                                                                                                   \
        if (!(x)) {                                                                                                                        \
            std::fprintf(stderr, "  EXPECT_TRUE failed at %s:%d (%s)\n", __FILE__, __LINE__, #x);                                         \
            return false;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

#define POLY_TEST(name)                                                                                                                    \
    static bool test_##name();                                                                                                             \
    static struct Reg_##name {                                                                                                             \
        Reg_##name() { Test::add(#name, test_##name); }                                                                                    \
    } reg_##name;                                                                                                                          \
    static bool test_##name()

} // namespace Test