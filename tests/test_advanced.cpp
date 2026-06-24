#include <doctest/doctest.h>

#include "test_helpers.hpp"

#include <vshadersystem/variant_key.hpp>

using namespace vsht;

// Helper: find a block member by name.
static const BlockMember* member(const BlockLayout& b, const std::string& name)
{
    for (const auto& m : b.members)
        if (m.name == name)
            return &m;
    return nullptr;
}

TEST_CASE("scalar/vector/matrix param types map correctly")
{
    const char* src = R"SLANG(
        import vsh;
        [VshMaterial]
        struct Material
        {
            float  f;
            int    i;
            uint   u;
            float2 v2;
            float3 v3;
            float4 v4;
            float3x3 m3;
            float4x4 m4;
        };
        [[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target
        {
            return uMat.v4 + float4(uMat.v3, uMat.f) + float4(uMat.v2, (float)uMat.i, (float)uMat.u)
                 + mul(uMat.m4, (float4)1) + float4(mul(uMat.m3, (float3)1), 0);
        }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    REQUIRE(r.value().reflection.blocks.size() == 1);
    const auto& b = r.value().reflection.blocks[0];

    CHECK(member(b, "f")->type == ParamType::eFloat);
    CHECK(member(b, "i")->type == ParamType::eInt);
    CHECK(member(b, "u")->type == ParamType::eUInt);
    CHECK(member(b, "v2")->type == ParamType::eVec2);
    CHECK(member(b, "v3")->type == ParamType::eVec3);
    CHECK(member(b, "v4")->type == ParamType::eVec4);
    CHECK(member(b, "m3")->type == ParamType::eMat3);
    CHECK(member(b, "m4")->type == ParamType::eMat4);
}

TEST_CASE("array material member is reflected")
{
    const char* src = R"SLANG(
        import vsh;
        [VshMaterial]
        struct Material { float4 weights[4]; float scale; };
        [[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return uMat.weights[0] * uMat.scale; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    REQUIRE(r.value().reflection.blocks.size() == 1);
    const auto& b = r.value().reflection.blocks[0];
    const auto* w = member(b, "weights");
    REQUIRE(w);
    CHECK(w->size >= 64); // 4 * vec4 (16B each), std140 array stride
    CHECK(member(b, "scale") != nullptr);
}

TEST_CASE("multiple constant buffers reflect as separate blocks")
{
    const char* src = R"SLANG(
        struct Frame { float4x4 viewProj; };
        struct Object { float4x4 model; float4 tint; };
        [[vk::binding(0, 0)]] ConstantBuffer<Frame>  uFrame;
        [[vk::binding(1, 0)]] ConstantBuffer<Object> uObject;
        struct V { float4 pos : SV_Position; };
        [shader("vertex")]
        V vertexMain(float3 p : POSITION) { V o; o.pos = mul(uFrame.viewProj, mul(uObject.model, float4(p,1))); return o; }
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return uObject.tint; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    CHECK(r.value().reflection.blocks.size() == 2);
    CHECK(count_kind(r.value().reflection, DescriptorKind::eUniformBuffer) == 2);
}

TEST_CASE("compile selects the entry point for a requested stage (multi-entry file)")
{
    const char* src = R"SLANG(
        struct V { float4 pos : SV_Position; };
        [shader("vertex")]   V vertexMain(uint vid : SV_VertexID) { V o; o.pos = (float4)0; return o; }
        [shader("fragment")] float4 fragmentMain(V i) : SV_Target { return (float4)1; }
    )SLANG";

    auto c = compile(src);
    REQUIRE(c.isOk());
    // both entry points present and independently usable
    const auto* vs = entry(c.value(), ShaderStage::eVert);
    const auto* fs = entry(c.value(), ShaderStage::eFrag);
    REQUIRE(vs);
    REQUIRE(fs);
    CHECK(vs->spirv != fs->spirv);
}

TEST_CASE("enum keyword variant selection by value (low/medium/high)")
{
    const char* src = R"SLANG(
        import vsh;
        [VshKeyword("QUALITY", "enum:low,medium,high", "permute", "local")]
        void __vsh_meta() {}
        [shader("fragment")]
        float4 fragmentMain() : SV_Target
        {
            float q = 0;
        #if QUALITY == 0
            q = 0.25;
        #elif QUALITY == 1
            q = 0.5;
        #else
            q = 1.0;
        #endif
            return (float4)q;
        }
    )SLANG";

    auto b = build(src, "test/quality");
    REQUIRE(b.isOk());
    CHECK(b.value().combinations == 3);
    CHECK(b.value().variants.size() == 3);

    // each enum value yields a distinct, looked-up-able variant
    for (uint32_t q = 0; q < 3; ++q)
    {
        VariantKey key;
        key.setShaderId("test/quality");
        key.setStage(ShaderStage::eFrag);
        key.set("QUALITY", q);
        const uint64_t want = key.build();
        bool           found = false;
        for (const auto& v : b.value().variants)
            if (v.variantHash == want)
                found = true;
        CHECK(found);
    }
}

TEST_CASE("stage flags reflect which stages use a resource")
{
    const char* src = R"SLANG(
        struct Frame { float4x4 vp; };
        [[vk::binding(0, 0)]] ConstantBuffer<Frame> uFrame;
        struct V { float4 pos : SV_Position; };
        [shader("vertex")]
        V vertexMain(float3 p : POSITION) { V o; o.pos = mul(uFrame.vp, float4(p,1)); return o; }
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return (float4)1; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    const auto* d = descriptor(r.value().reflection, "uFrame");
    REQUIRE(d);
    // union of entry-point stages: at least vertex
    CHECK((d->stageFlags & eStageVert) != 0);
}
