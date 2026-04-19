#pragma once

#include <cstdio>
#include <cstdlib>

namespace vksumi
{
    inline bool debug_enabled()
    {
        static const bool enabled = []() {
            const char* env = std::getenv("VKSUMI_DEBUG");
            return env && env[0] != '0';
        }();
        return enabled;
    }

    #define VKSUMI_LOG(...) do { \
        std::fprintf(stderr, "[vksumi] "); \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fputc('\n', stderr); \
    } while (0)

    #define VKSUMI_TRACE(...) do { \
        if (::vksumi::debug_enabled()) VKSUMI_LOG(__VA_ARGS__); \
    } while (0)
}
