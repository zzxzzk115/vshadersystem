#pragma once

#include "vshadersystem/keywords.hpp"
#include "vshadersystem/result.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vshadersystem
{
    // ------------------------------------------------------------
    // engine_keywords.vkw
    //
    // A tiny, line-oriented text format for defining and setting
    // engine-wide (typically global) keywords.
    //
    // Lines:
    //   - Comments start with '#'
    //   - Declaration (same semantics as shader pragma keyword):
    //       keyword <permute|runtime|special> [<global|material|pass|local>] <NAME>[=<DEFAULT_OR_ENUMS>]
    //     Examples:
    //       keyword permute global USE_SHADOW
    //       keyword runtime global DEBUG_VIEW=NONE|NORMAL|ALBEDO
    //
    //   - Setting (optional; values are stored as raw strings for now):
    //       set <NAME>=<VALUE>
    //     Examples:
    //       set USE_SHADOW=1
    //       set DEBUG_VIEW=NORMAL
    //
    // Notes:
    //   - vshadersystem currently uses declarations for tooling and
    //     future variant resolution. Settings are provided for parity
    //     with Unity-like global keyword configuration.
    // ------------------------------------------------------------

    struct EngineKeywordsFile
    {
        std::vector<KeywordDecl>                     decls;
        std::unordered_map<std::string, std::string> values; // NAME -> raw VALUE
    };

    Result<EngineKeywordsFile> parse_engine_keywords_vkw(std::string_view text);
    Result<EngineKeywordsFile> load_engine_keywords_vkw(const std::string& filePath);
} // namespace vshadersystem
