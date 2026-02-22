#include "vshadersystem/variant_key.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace vshadersystem
{
    uint64_t VariantKey::build() const
    {
        // Deterministic order
        std::vector<VariantKeyEntry> kvs = m_Entries;
        std::sort(kvs.begin(), kvs.end(), [](const VariantKeyEntry& a, const VariantKeyEntry& b) {
            if (a.nameHash != b.nameHash)
                return a.nameHash < b.nameHash;
            return a.value < b.value;
        });

        std::vector<uint8_t> buf;
        buf.reserve(32 + kvs.size() * sizeof(VariantKeyEntry));

        auto append_u64 = [&](uint64_t v) {
            uint8_t b[8];
            std::memcpy(b, &v, 8);
            buf.insert(buf.end(), b, b + 8);
        };
        auto append_u32 = [&](uint32_t v) {
            uint8_t b[4];
            std::memcpy(b, &v, 4);
            buf.insert(buf.end(), b, b + 4);
        };

        append_u64(m_ShaderIdHash);
        append_u32(static_cast<uint32_t>(static_cast<uint8_t>(m_Stage)));
        append_u32(static_cast<uint32_t>(kvs.size()));
        for (const auto& kv : kvs)
        {
            append_u64(kv.nameHash);
            append_u32(kv.value);
            append_u32(0);
        }

        return xxhash64(buf.data(), buf.size());
    }
} // namespace vshadersystem
