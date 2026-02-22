#pragma once

#include "vshadersystem/keywords.hpp"
#include "vshadersystem/result.hpp"

namespace vshadersystem
{
    bool parse_bool_value(std::string_view s, uint32_t& out);

    Result<uint32_t> parse_keyword_value(const KeywordDecl& d, std::string_view raw);
} // namespace vshadersystem