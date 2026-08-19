#include <doctest/doctest.h>

#include "test_helpers.hpp"

#include <string>

using namespace vsht;

// Reflection has to describe the variant it is stored on. It did not: reflect_shader built its
// own Slang session and never passed the per-variant keyword defines into it, so every variant of
// a shader was reflected with all keywords undefined and carried the BASE variant's descriptor
// table while its bytecode was correctly specialized.
//
// Nothing downstream can diagnose that. A binding a keyword ADDS is absent from the reflection of
// the very variant that uses it, so a consumer deriving a descriptor-set layout hands the pipeline
// a short set - which on Vulkan is a device hang on first use with no validation message, not an
// error. It was found by a renderer whose shaders grow bindings under a keyword: a compute shader
// binding 12 descriptors reported the base variant's 6, with the destinations still at the base
// registers.
namespace
{
    // KEYWORD adds t_Extra and moves nothing else, so the two variants' descriptor tables differ
    // by exactly one entry - the smallest observable form of the bug.
    const char* kAddsBinding = R"SLANG(
        import vsh;
        [VshKeyword("EXTRA", "bool", "permute", "global")]
        void __vsh_meta() {}
        #ifndef EXTRA
        #define EXTRA 0
        #endif

        [[vk::binding(0, 0)]] Texture2D t_Base;
        [[vk::binding(1, 0)]] SamplerState s_Point;
        [[vk::binding(2, 0)]] [format("rgba32f")] RWTexture2D<float4> u_Dst;
        #if EXTRA
        [[vk::binding(3, 0)]] Texture2D t_Extra;
        #endif

        [shader("compute")]
        [numthreads(8, 8, 1)]
        void computeMain(uint3 tid: SV_DispatchThreadID)
        {
            float2 uv = (float2(tid.xy) + 0.5) / 64.0;
            float4 c = t_Base.SampleLevel(s_Point, uv, 0.0);
        #if EXTRA
            c += t_Extra.SampleLevel(s_Point, uv, 0.0);
        #endif
            u_Dst[tid.xy] = c;
        }
    )SLANG";

    const vshaderc::VariantBinary* variant_with(const vshaderc::ShaderBuildResult& r,
                                                const std::string&                 keyword,
                                                uint32_t                           value)
    {
        for (const auto& v : r.variants)
            for (const auto& kv : v.keywordValues)
                if (kv.first == keyword && kv.second == value)
                    return &v;
        return nullptr;
    }
} // namespace

TEST_CASE("variant reflection: a keyword-added binding appears in that variant only")
{
    auto built = build(kAddsBinding);
    REQUIRE_MESSAGE(built.isOk(), (built.isOk() ? std::string {} : built.error().message));

    const vshaderc::VariantBinary* off = variant_with(built.value(), "EXTRA", 0);
    const vshaderc::VariantBinary* on  = variant_with(built.value(), "EXTRA", 1);
    REQUIRE(off != nullptr);
    REQUIRE(on != nullptr);

    // The bytecode was always specialized; it is the reflection that was not.
    CHECK(off->spirv.size() < on->spirv.size());

    CHECK(descriptor(off->reflection, "t_Extra") == nullptr);
    const DescriptorBinding* extra = descriptor(on->reflection, "t_Extra");
    REQUIRE(extra != nullptr);
    CHECK(extra->set == 0);
    CHECK(extra->binding == 3);
    CHECK(extra->kind == DescriptorKind::eSampledImage);

    // The shared bindings still agree, so this is a difference of one entry rather than two
    // unrelated tables.
    CHECK(off->reflection.descriptors.size() + 1 == on->reflection.descriptors.size());
    for (const char* name : {"t_Base", "s_Point", "u_Dst"})
    {
        const DescriptorBinding* a = descriptor(off->reflection, name);
        const DescriptorBinding* b = descriptor(on->reflection, name);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(a->binding == b->binding);
        CHECK(a->kind == b->kind);
    }
}

TEST_CASE("variant reflection: a keyword that moves registers moves them in the reflection")
{
    // The renderer's actual shape, and the dangerous one: the keyword inserts sources, so the
    // destinations SHIFT. A stale base-variant table then reports the right names at the wrong
    // registers - a layout derived from it binds storage images where the shader samples
    // textures, which is worse than a missing entry because every name still resolves.
    const char* kMovesRegisters = R"SLANG(
        import vsh;
        [VshKeyword("WIDE", "bool", "permute", "global")]
        void __vsh_meta() {}
        #ifndef WIDE
        #define WIDE 0
        #endif

        [[vk::binding(0, 0)]] Texture2D t_SrcA;
        #if WIDE
        [[vk::binding(1, 0)]] Texture2D t_SrcB;
        [[vk::binding(2, 0)]] [format("rgba32f")] RWTexture2D<float4> u_DstA;
        #else
        [[vk::binding(1, 0)]] [format("rgba32f")] RWTexture2D<float4> u_DstA;
        #endif

        [shader("compute")]
        [numthreads(8, 8, 1)]
        void computeMain(uint3 tid: SV_DispatchThreadID)
        {
            float4 c = t_SrcA.Load(int3(tid.xy, 0));
        #if WIDE
            c += t_SrcB.Load(int3(tid.xy, 0));
        #endif
            u_DstA[tid.xy] = c;
        }
    )SLANG";

    auto built = build(kMovesRegisters);
    REQUIRE_MESSAGE(built.isOk(), (built.isOk() ? std::string {} : built.error().message));

    const vshaderc::VariantBinary* narrow = variant_with(built.value(), "WIDE", 0);
    const vshaderc::VariantBinary* wide   = variant_with(built.value(), "WIDE", 1);
    REQUIRE(narrow != nullptr);
    REQUIRE(wide != nullptr);

    const DescriptorBinding* dstNarrow = descriptor(narrow->reflection, "u_DstA");
    const DescriptorBinding* dstWide   = descriptor(wide->reflection, "u_DstA");
    REQUIRE(dstNarrow != nullptr);
    REQUIRE(dstWide != nullptr);
    CHECK(dstNarrow->binding == 1);
    CHECK(dstWide->binding == 2);
    CHECK(descriptor(narrow->reflection, "t_SrcB") == nullptr);
    CHECK(descriptor(wide->reflection, "t_SrcB") != nullptr);
}
