#include <doctest/doctest.h>

#include "test_helpers.hpp"
#include <vshadersystem/vsh_format.hpp>

#include <cstring>
#include <string>

namespace
{
    bool HasDebugInfo(const std::vector<uint32_t>& words)
    {
        for (size_t i = 5; i < words.size();)
        {
            const auto count = words[i] >> 16;
            if (count == 0 || i + count > words.size())
                return false;
            if ((words[i] & 0xFFFFu) == 11 && count > 2)
            {
                const char*  name  = reinterpret_cast<const char*>(&words[i + 2]);
                const size_t bytes = (count - 2) * sizeof(uint32_t);
                const auto*  end   = static_cast<const char*>(std::memchr(name, 0, bytes));
                if (end && std::string(name, end).find("NonSemantic.Shader.DebugInfo") == 0)
                    return true;
            }
            i += count;
        }
        return false;
    }
} // namespace

TEST_CASE("optimized shader debug info survives every permutation and library serialization")
{
    const char* source = R"SLANG(
        import vsh;
        [VshKeyword("QUALITY", "bool", "permute", "global")]
        void __vsh_meta() {}
        RWStructuredBuffer<float> output;
        float evaluate(uint index) {
        #if QUALITY
            return float(index) * 2.0;
        #else
            return float(index);
        #endif
        }
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void computeMain(uint3 id : SV_DispatchThreadID) { output[id.x] = evaluate(id.x); }
    )SLANG";
    for (const bool debugInfo : {false, true})
    {
        vshaderc::ShaderBuildOptions options;
        options.shaderId          = "test/debug-info";
        options.compile.optimize  = true;
        options.compile.debugInfo = debugInfo;
        auto built = vshaderc::build_shader(vsht::compiler(), "debug_info", "debug_info.slang", source, options);
        if (!built.isOk())
            INFO(built.error().message);
        REQUIRE(built.isOk());
        REQUIRE(built.value().variants.size() == 2);
        std::vector<vshadersystem::v1::LibraryEntry> entries;
        for (const auto& variant : built.value().variants)
        {
            REQUIRE_FALSE(variant.spirv.empty());
            CHECK(HasDebugInfo(variant.spirv) == debugInfo);
            if (debugInfo)
            {
                const std::string bytes(reinterpret_cast<const char*>(variant.spirv.data()),
                                        variant.spirv.size() * sizeof(uint32_t));
                CHECK(bytes.find("debug_info.slang") != std::string::npos);
                CHECK(bytes.find("float evaluate") != std::string::npos);
            }
            auto binary  = vshaderc::to_shader_binary(variant, built.value().shaderIdHash, built.value().keywords);
            auto encoded = vshadersystem::v1::write_binary(binary);
            REQUIRE(encoded.isOk());
            entries.push_back({variant.variantHash, variant.stage, std::move(encoded.value())});
        }
        auto library = vshadersystem::v1::write_library(entries);
        REQUIRE(library.isOk());
        auto decoded = vshadersystem::v1::read_library(library.value());
        REQUIRE(decoded.isOk());
        for (const auto& entry : decoded.value().entries)
        {
            auto binary = vshadersystem::v1::read_binary(entry.blob);
            REQUIRE(binary.isOk());
            CHECK(HasDebugInfo(binary.value().spirv) == debugInfo);
        }
    }
}
