#pragma once

#include <xxhash.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace vshadersystem
{
    inline uint64_t xxhash64(const void* data, size_t len, uint64_t seed = 0) { return XXH64(data, len, seed); }
    inline uint64_t xxhash64(std::string_view s, uint64_t seed = 0) { return XXH64(s.data(), s.size(), seed); }

    inline uint64_t xxhash64_words(const std::vector<uint32_t>& words, uint64_t seed = 0)
    {
        return XXH64(words.data(), words.size() * sizeof(uint32_t), seed);
    }
} // namespace vshadersystem
