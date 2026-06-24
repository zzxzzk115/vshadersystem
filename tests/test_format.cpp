#include <doctest/doctest.h>

#include "test_helpers.hpp"

#include <vshadersystem/variant_key.hpp>
#include <vshadersystem/vsh_format.hpp>

namespace v1 = vshadersystem::v1;
using namespace vsht;

static const char* kShader = R"SLANG(
    import vsh;
    [VshMaterial]
    struct Material { float4 baseColorFactor; float roughnessFactor; };
    [VshKeyword("USE_SHADOW", "bool", "permute", "global")]
    void __vsh_meta() {}
    [[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
    [shader("fragment")]
    float4 fragmentMain() : SV_Target
    {
        float3 c = uMat.baseColorFactor.rgb * (1 - uMat.roughnessFactor);
    #if USE_SHADOW
        c *= 0.5;
    #endif
        return float4(c, uMat.baseColorFactor.a);
    }
)SLANG";

TEST_CASE("binary round-trip preserves spirv, wgsl, reflection, material")
{
    auto b = build(kShader, "test/fmt");
    REQUIRE(b.isOk());
    REQUIRE_FALSE(b.value().variants.empty());

    const auto& v   = b.value().variants.front();
    auto        sb  = vshaderc::to_shader_binary(v, b.value().shaderIdHash);
    auto        enc = v1::write_binary(sb);
    REQUIRE(enc.isOk());

    auto dec = v1::read_binary(enc.value());
    REQUIRE(dec.isOk());
    const auto& got = dec.value();

    CHECK(got.stage == sb.stage);
    CHECK(got.variantHash == sb.variantHash);
    CHECK(got.spirv == sb.spirv);
    CHECK(got.wgsl == sb.wgsl);
    REQUIRE(got.reflection.descriptors.size() == sb.reflection.descriptors.size());
    CHECK(got.reflection.descriptors[0].name == sb.reflection.descriptors[0].name);
    REQUIRE(got.materialDesc.params.size() == sb.materialDesc.params.size());
    CHECK(got.materialDesc.materialParamSize == sb.materialDesc.materialParamSize);
}

TEST_CASE("library round-trip + lookup by (variantHash, stage)")
{
    auto b = build(kShader, "test/fmt");
    REQUIRE(b.isOk());

    std::vector<v1::LibraryEntry> entries;
    for (const auto& v : b.value().variants)
        entries.push_back({v.variantHash, v.stage, v1::write_binary(vshaderc::to_shader_binary(v, b.value().shaderIdHash)).value()});

    auto enc = v1::write_library(entries, nullptr);
    REQUIRE(enc.isOk());
    auto lib = v1::read_library(enc.value());
    REQUIRE(lib.isOk());
    CHECK(lib.value().entries.size() == entries.size());

    // engine-side lookup for USE_SHADOW=1
    VariantKey key;
    key.setShaderId("test/fmt");
    key.setStage(ShaderStage::eFrag);
    key.set("USE_SHADOW", 1);
    const auto* blob = v1::find(lib.value(), key.build(), ShaderStage::eFrag);
    REQUIRE(blob);
    auto bin = v1::read_binary(*blob);
    REQUIRE(bin.isOk());
    CHECK(bin.value().stage == ShaderStage::eFrag);

    // a hash that doesn't exist returns null
    CHECK(v1::find(lib.value(), 0xdeadbeef, ShaderStage::eFrag) == nullptr);
}

TEST_CASE("library can embed engine keyword bytes")
{
    auto b = build(kShader, "test/fmt");
    REQUIRE(b.isOk());
    std::vector<v1::LibraryEntry> entries;
    for (const auto& v : b.value().variants)
        entries.push_back({v.variantHash, v.stage, v1::write_binary(vshaderc::to_shader_binary(v, b.value().shaderIdHash)).value()});

    std::vector<uint8_t> vkw = {'k', 'e', 'y', 'w', 'o', 'r', 'd'};
    auto                 enc = v1::write_library(entries, &vkw);
    REQUIRE(enc.isOk());
    auto lib = v1::read_library(enc.value());
    REQUIRE(lib.isOk());
    CHECK(lib.value().engineKeywords == vkw);
}

TEST_CASE("source pack round-trip")
{
    std::vector<v1::SourceFile> files = {
        {"common/pbr.slang", "public float foo() { return 1; }"},
        {"lib/color.slang", "public float3 srgb(float3 c) { return c; }"},
    };
    auto enc = v1::write_source_pack(files);
    REQUIRE(enc.isOk());
    auto dec = v1::read_source_pack(enc.value());
    REQUIRE(dec.isOk());
    REQUIRE(dec.value().size() == 2);
    // deterministic sort by path
    CHECK(dec.value()[0].path == "common/pbr.slang");
    CHECK(dec.value()[1].path == "lib/color.slang");
    CHECK(dec.value()[1].text.find("srgb") != std::string::npos);
}

TEST_CASE("corrupt / wrong-magic input is rejected, not crashed")
{
    std::vector<uint8_t> garbage = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    CHECK_FALSE(v1::read_binary(garbage).isOk());
    CHECK_FALSE(v1::read_library(garbage).isOk());
    CHECK_FALSE(v1::read_source_pack(garbage).isOk());
    CHECK_FALSE(v1::read_binary({}).isOk());
}
