// Coverage for the engine-pipeline + editor-inspector metadata: vertex input / fragment
// output reflection, entry point names, serialized keyword declarations, and material
// default values + editor hints.

#include <doctest/doctest.h>

#include "test_helpers.hpp"

#include <vshadersystem/vsh_format.hpp>

namespace v1 = vshadersystem::v1;
using namespace vsht;

TEST_CASE("vertex input attributes are reflected (location, semantic, type)")
{
    const char* src = R"SLANG(
        struct VSIn  { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
        struct VSOut { float4 pos : SV_Position; };
        [shader("vertex")]
        VSOut vertexMain(VSIn i)
        {
            VSOut o; o.pos = float4(i.position + i.normal, 1) + float4(i.uv, 0, 0); return o;
        }
        [shader("fragment")]
        float4 fragmentMain(VSOut i) : SV_Target { return (float4)1; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    const auto& vin = r.value().reflection.vertexInputs;
    REQUIRE(vin.size() == 3);

    // SV_VertexID would be excluded; these are real vertex-buffer attributes.
    CHECK(vin[0].semantic == "POSITION");
    CHECK(vin[0].type == ParamType::eVec3);
    CHECK(vin[1].semantic == "NORMAL");
    CHECK(vin[2].semantic == "TEXCOORD"); // index 0 is implicit; TEXCOORD1 would be "TEXCOORD1"
    CHECK(vin[2].type == ParamType::eVec2);
    // locations are distinct
    CHECK(vin[0].location != vin[1].location);
}

TEST_CASE("system-value vertex inputs are not treated as vertex attributes")
{
    const char* src = R"SLANG(
        struct VSOut { float4 pos : SV_Position; };
        [shader("vertex")]
        VSOut vertexMain(uint vid : SV_VertexID) { VSOut o; o.pos = (float4)vid; return o; }
        [shader("fragment")]
        float4 fragmentMain(VSOut i) : SV_Target { return (float4)1; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    CHECK(r.value().reflection.vertexInputs.empty()); // SV_VertexID is not a vertex attribute
}

TEST_CASE("fragment color outputs are reflected, including MRT")
{
    const char* src = R"SLANG(
        struct VSOut { float4 pos : SV_Position; };
        struct GBuffer { float4 albedo : SV_Target0; float4 normal : SV_Target1; };
        [shader("vertex")]
        VSOut vertexMain(uint vid : SV_VertexID) { VSOut o; o.pos = (float4)0; return o; }
        [shader("fragment")]
        GBuffer fragmentMain(VSOut i) { GBuffer g; g.albedo = (float4)1; g.normal = (float4)0; return g; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    const auto& outs = r.value().reflection.colorOutputs;
    REQUIRE(outs.size() == 2);
    CHECK(outs[0].location != outs[1].location);
}

TEST_CASE("entry point names round-trip through the binary")
{
    const char* src = R"SLANG(
        struct V { float4 pos : SV_Position; };
        [shader("vertex")]   V vsMain(uint vid : SV_VertexID) { V o; o.pos = (float4)0; return o; }
        [shader("fragment")] float4 fsMain(V i) : SV_Target { return (float4)1; }
    )SLANG";

    auto b = build(src, "test/entry");
    REQUIRE(b.isOk());

    for (const auto& v : b.value().variants)
    {
        CHECK_FALSE(v.entryPointName.empty());
        auto sb  = vshaderc::to_shader_binary(v, b.value().shaderIdHash, b.value().keywords);
        auto dec = v1::read_binary(v1::write_binary(sb).value());
        REQUIRE(dec.isOk());
        CHECK(dec.value().entryPointName == v.entryPointName);
        if (v.stage == ShaderStage::eVert)
            CHECK(dec.value().entryPointName == "vsMain");
        if (v.stage == ShaderStage::eFrag)
            CHECK(dec.value().entryPointName == "fsMain");
    }
}

TEST_CASE("keyword declarations are serialized into the binary for the editor")
{
    const char* src = R"SLANG(
        import vsh;
        [VshKeyword("USE_SHADOW", "bool", "permute", "global")]
        [VshKeyword("QUALITY", "enum:low,medium,high", "permute", "local")]
        void __vsh_meta() {}
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return (float4)1; }
    )SLANG";

    auto b = build(src, "test/kwser");
    REQUIRE(b.isOk());
    REQUIRE_FALSE(b.value().variants.empty());

    auto sb  = vshaderc::to_shader_binary(b.value().variants.front(), b.value().shaderIdHash, b.value().keywords);
    auto dec = v1::read_binary(v1::write_binary(sb).value());
    REQUIRE(dec.isOk());

    const auto& kws = dec.value().keywords;
    REQUIRE(kws.size() == 2);
    CHECK(kws[0].name == "USE_SHADOW");
    CHECK(kws[0].kind == KeywordValueKind::eBool);
    CHECK(kws[1].name == "QUALITY");
    CHECK(kws[1].kind == KeywordValueKind::eEnum);
    REQUIRE(kws[1].enumValues.size() == 3);
    CHECK(kws[1].enumValues[1] == "medium");
}

TEST_CASE("material defaults + color + display-name hints reach MaterialDescription")
{
    const char* src = R"SLANG(
        import vsh;
        [VshMaterial]
        struct Material
        {
            [VshSemantic("baseColor")] [VshColor] [VshDefault("1,0.5,0.25,1")]
            float4 baseColorFactor;

            [VshDisplayName("Metallic")] [VshRange(0,1)] [VshDefault("0.3")]
            float metallicFactor;
        };
        [[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return uMat.baseColorFactor * uMat.metallicFactor; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    const auto& params = r.value().material.params;
    REQUIRE(params.size() == 2);

    // baseColorFactor: color picker + vec4 default
    const auto& c = params[0];
    CHECK(c.isColor);
    REQUIRE(c.hasDefault);
    float rgba[4];
    std::memcpy(rgba, c.defaultValue.valueBuffer, sizeof(rgba));
    CHECK(rgba[0] == doctest::Approx(1.0f));
    CHECK(rgba[1] == doctest::Approx(0.5f));
    CHECK(rgba[2] == doctest::Approx(0.25f));
    CHECK(rgba[3] == doctest::Approx(1.0f));

    // metallicFactor: display name + scalar default + range
    const auto& m = params[1];
    CHECK(m.displayName == "Metallic");
    CHECK(m.hasRange);
    REQUIRE(m.hasDefault);
    float metal;
    std::memcpy(&metal, m.defaultValue.valueBuffer, sizeof(metal));
    CHECK(metal == doctest::Approx(0.3f));
}

TEST_CASE("material default + hints survive binary serialization")
{
    const char* src = R"SLANG(
        import vsh;
        [VshMaterial]
        struct Material
        {
            [VshColor] [VshDefault("0.2,0.4,0.6,1")] float4 tint;
        };
        [[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return uMat.tint; }
    )SLANG";

    auto b = build(src, "test/matser");
    REQUIRE(b.isOk());
    auto sb  = vshaderc::to_shader_binary(b.value().variants.front(), b.value().shaderIdHash, b.value().keywords);
    auto dec = v1::read_binary(v1::write_binary(sb).value());
    REQUIRE(dec.isOk());
    REQUIRE(dec.value().materialDesc.params.size() == 1);
    const auto& p = dec.value().materialDesc.params[0];
    CHECK(p.isColor);
    REQUIRE(p.hasDefault);
    float rgba[4];
    std::memcpy(rgba, p.defaultValue.valueBuffer, sizeof(rgba));
    CHECK(rgba[2] == doctest::Approx(0.6f));
}
