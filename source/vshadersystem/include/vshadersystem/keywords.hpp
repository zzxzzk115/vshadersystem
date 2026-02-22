#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vshadersystem
{
    enum class KeywordDispatch : uint8_t
    {
        ePermutation    = 0,
        eRuntime        = 1,
        eSpecialization = 2
    };

    enum class KeywordScope : uint8_t
    {
        eShaderLocal = 0,
        eGlobal      = 1,
        eMaterial    = 2,
        ePass        = 3
    };

    enum class KeywordValueKind : uint8_t
    {
        eBool = 0,
        eEnum = 1
        // Future: eIntRange, eUIntRange...
    };

    struct KeywordDecl
    {
        std::string      name;
        KeywordDispatch  dispatch = KeywordDispatch::eRuntime;
        KeywordScope     scope    = KeywordScope::eShaderLocal;
        KeywordValueKind kind     = KeywordValueKind::eBool;

        // Default value:
        // - Bool: 0/1
        // - Enum: index into enumValues
        uint32_t defaultValue = 0;

        // Enum values (only used when kind == eEnum)
        std::vector<std::string> enumValues;

        // Optional constraint expression for pruning (kept as raw string for now)
        // Example: "only_if(SURFACE==CUTOUT)"
        std::string constraint;
    };
} // namespace vshadersystem
