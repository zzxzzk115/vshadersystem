#include <doctest/doctest.h>

#include "test_helpers.hpp"

using namespace vsht;

// Every backend an engine might target. If Slang can generate each backend's shading
// language for a shader, that shader is portable across those backends.
static const std::vector<vshaderc::Backend> kAllBackends = {
    vshaderc::Backend::Vulkan, vshaderc::Backend::OpenGL, vshaderc::Backend::WebGPU,
    vshaderc::Backend::D3D12,  vshaderc::Backend::Metal,
};

static void check_portable(const char* src, const std::vector<vshaderc::Backend>& backends = kAllBackends)
{
    auto results = vshaderc::validate_backends(compiler(), "test", "test.slang", src, opts(), backends);
    REQUIRE(results.size() == backends.size());
    for (const auto& r : results)
    {
        INFO("backend: " << vshaderc::backend_name(r.backend) << " log: " << r.log);
        CHECK(r.ok);
        CHECK(r.outputSize > 0);
    }
}

TEST_CASE("triangle is portable across all backends")
{
    check_portable(R"SLANG(
        struct VSOut { float4 pos : SV_Position; float3 col : COLOR0; };
        [shader("vertex")]
        VSOut vertexMain(uint vid : SV_VertexID)
        {
            VSOut o;
            float2 p = float2((vid << 1) & 2, vid & 2);
            o.pos = float4(p * 2 - 1, 0, 1);
            o.col = float3(p, 0.5);
            return o;
        }
        [shader("fragment")]
        float4 fragmentMain(VSOut i) : SV_Target { return float4(i.col, 1); }
    )SLANG");
}

TEST_CASE("material + ubo + texture + sampler is portable across all backends")
{
    check_portable(R"SLANG(
        import vsh;
        [VshMaterial]
        struct Material { float4 baseColorFactor; float metallicFactor; };
        [[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
        [[vk::binding(1, 0)]] Texture2D<float4>        tex;
        [[vk::binding(2, 0)]] SamplerState             smp;
        struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
        [shader("vertex")]
        V vertexMain(uint vid : SV_VertexID) { V o; o.pos = (float4)0; o.uv = (float2)0; return o; }
        [shader("fragment")]
        float4 fragmentMain(V i) : SV_Target
        {
            return uMat.baseColorFactor * tex.Sample(smp, i.uv) * uMat.metallicFactor;
        }
    )SLANG");
}

TEST_CASE("compute + storage buffer is portable across all backends")
{
    check_portable(R"SLANG(
        [[vk::binding(0, 0)]] RWStructuredBuffer<float4> uOut;
        [[vk::binding(1, 0)]] StructuredBuffer<float4>   uIn;
        [shader("compute")]
        [numthreads(64, 1, 1)]
        void computeMain(uint3 tid : SV_DispatchThreadID)
        {
            uOut[tid.x] = uIn[tid.x] * 2.0;
        }
    )SLANG");
}

TEST_CASE("bindless texture array (NonUniformResourceIndex) on the backends that support it")
{
    // NonUniformResourceIndex is a Vulkan descriptor-indexing / D3D12 SM5.1+ feature;
    // WGSL and Metal MSL (via Slang) reject it in a fragment entry point. This documents
    // the portability boundary: bindless dynamic indexing targets Vulkan + D3D12.
    check_portable(R"SLANG(
        [[vk::binding(0, 0)]] Texture2D<float4> textures[8];
        [[vk::binding(1, 0)]] SamplerState      smp;
        struct V { float4 pos : SV_Position; nointerpolation uint id : TEXCOORD0; };
        [shader("vertex")]
        V vertexMain(uint vid : SV_VertexID) { V o; o.pos = (float4)0; o.id = vid; return o; }
        [shader("fragment")]
        float4 fragmentMain(V i) : SV_Target
        {
            return textures[NonUniformResourceIndex(i.id)].Sample(smp, (float2)0);
        }
    )SLANG",
                   {vshaderc::Backend::Vulkan, vshaderc::Backend::D3D12});
}

TEST_CASE("a backend failure is reported, not thrown")
{
    // Ray tracing has no WGSL target; validate_backends must report ok=false with a log
    // rather than crashing.
    const char* rt = R"SLANG(
        struct Payload { float3 c; };
        [[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
        [shader("raygeneration")]
        void rayGenMain()
        {
            RayDesc r; r.Origin=(float3)0; r.Direction=float3(0,0,1); r.TMin=0; r.TMax=1;
            Payload p; p.c=(float3)0;
            TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, r, p);
        }
    )SLANG";

    auto results = vshaderc::validate_backends(compiler(), "test", "test.slang", rt, opts(),
                                               {vshaderc::Backend::Vulkan, vshaderc::Backend::WebGPU});
    REQUIRE(results.size() == 2);
    CHECK(results[0].ok);        // Vulkan supports ray tracing
    CHECK_FALSE(results[1].ok);  // WebGPU has no ray tracing -> reported failure
    CHECK_FALSE(results[1].log.empty());
}
