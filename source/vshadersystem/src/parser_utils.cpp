#include "vshadersystem/parser_utils.hpp"

namespace vshadersystem
{
    Result<uint32_t> parse_keyword_value(const KeywordDecl& d, std::string_view raw)
    {
        if (d.kind == KeywordValueKind::eBool)
        {
            uint32_t v = 0;
            if (!parse_bool_value(raw, v))
                return Result<uint32_t>::err(
                    {ErrorCode::eParseError, "Invalid bool value for keyword '" + d.name + "'"});
            return Result<uint32_t>::ok(v);
        }

        // Enum
        if (raw.empty())
            return Result<uint32_t>::ok(d.defaultValue);

        // Accept numeric index
        {
            bool     ok  = true;
            uint32_t idx = 0;
            for (char c : raw)
            {
                if (c < '0' || c > '9')
                {
                    ok = false;
                    break;
                }
                idx = idx * 10u + static_cast<uint32_t>(c - '0');
            }
            if (ok)
            {
                if (idx >= d.enumValues.size())
                    return Result<uint32_t>::err(
                        {ErrorCode::eParseError, "Enum index out of range for keyword '" + d.name + "'"});
                return Result<uint32_t>::ok(idx);
            }
        }

        // Accept enumerant name
        for (uint32_t i = 0; i < static_cast<uint32_t>(d.enumValues.size()); ++i)
        {
            if (d.enumValues[i] == raw)
                return Result<uint32_t>::ok(i);
        }

        return Result<uint32_t>::err(
            {ErrorCode::eParseError, "Unknown enum value '" + std::string(raw) + "' for keyword '" + d.name + "'"});
    }

    bool parse_bool_value(std::string_view s, uint32_t& out)
    {
        if (s.empty())
        {
            out = 1;
            return true;
        }
        if (s == "1" || s == "true" || s == "TRUE" || s == "True")
        {
            out = 1;
            return true;
        }
        if (s == "0" || s == "false" || s == "FALSE" || s == "False")
        {
            out = 0;
            return true;
        }
        return false;
    }
} // namespace vshadersystem