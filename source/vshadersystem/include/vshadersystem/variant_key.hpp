#pragma once

#include "vshadersystem/hash.hpp"
#include "vshadersystem/shader_id.hpp"
#include "vshadersystem/types.hpp"

#include <cstdint>
#include <vector>

namespace vshadersystem
{
    struct VariantKeyEntry
    {
        uint64_t nameHash = 0;
        uint32_t value    = 0;
        uint32_t reserved = 0;
    };

    // ------------------------------------------------------------
    // VariantKey
    //
    // Runtime helper to compute variantHash exactly like the build step.
    //
    // variantHash = hash(shaderIdHash, stage, permutation keyword values)
    // ------------------------------------------------------------
    class VariantKey
    {
    public:
        VariantKey() = default;

        void setShaderId(std::string_view shaderId) { m_ShaderIdHash = shader_id_hash(shaderId); }
        void setShaderIdHash(uint64_t shaderIdHash) { m_ShaderIdHash = shaderIdHash; }

        void setStage(ShaderStage stage) { m_Stage = stage; }

        // Set keyword by name (will be hashed with xxhash64)
        void set(std::string_view keywordName, uint32_t value)
        {
            VariantKeyEntry e;
            e.nameHash = xxhash64(keywordName);
            e.value    = value;
            m_Entries.push_back(e);
        }

        // Set keyword by pre-hashed name
        void set(uint64_t keywordNameHash, uint32_t value)
        {
            VariantKeyEntry e;
            e.nameHash = keywordNameHash;
            e.value    = value;
            m_Entries.push_back(e);
        }

        void clear() { m_Entries.clear(); }

        uint64_t build() const;

    private:
        uint64_t                     m_ShaderIdHash = 0;
        ShaderStage                  m_Stage        = ShaderStage::eUnknown;
        std::vector<VariantKeyEntry> m_Entries;
    };
} // namespace vshadersystem
