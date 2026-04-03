#pragma once

#include <cstdio>

// Unified, lightweight logging macros.
// - printf/fprintf based for broad platform compatibility (including wasm).
// - default tag: "vss"
// - custom tag variants are provided for tools/examples.

#define VSS_LOG_TAG_INFO(tag, fmt, ...) \
    do \
    { \
        std::fprintf(stdout, "[%s][info] " fmt "\n", (tag)__VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define VSS_LOG_TAG_WARN(tag, fmt, ...) \
    do \
    { \
        std::fprintf(stdout, "[%s][warn] " fmt "\n", (tag)__VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define VSS_LOG_TAG_ERROR(tag, fmt, ...) \
    do \
    { \
        std::fprintf(stderr, "[%s][error] " fmt "\n", (tag)__VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define VSS_LOG_TAG_DEBUG(tag, fmt, ...) \
    do \
    { \
        std::fprintf(stdout, "[%s][debug] " fmt "\n", (tag)__VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define VSS_LOG_INFO(fmt, ...) VSS_LOG_TAG_INFO("vss", fmt __VA_OPT__(, ) __VA_ARGS__)
#define VSS_LOG_WARN(fmt, ...) VSS_LOG_TAG_WARN("vss", fmt __VA_OPT__(, ) __VA_ARGS__)
#define VSS_LOG_ERROR(fmt, ...) VSS_LOG_TAG_ERROR("vss", fmt __VA_OPT__(, ) __VA_ARGS__)
#define VSS_LOG_DEBUG(fmt, ...) VSS_LOG_TAG_DEBUG("vss", fmt __VA_OPT__(, ) __VA_ARGS__)
