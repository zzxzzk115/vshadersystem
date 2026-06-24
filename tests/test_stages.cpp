#include <doctest/doctest.h>

#include "test_helpers.hpp"

using namespace vsht;

TEST_CASE("compute shader: stage + numthreads local size")
{
    const char* src = R"SLANG(
        [[vk::binding(0, 0)]] RWStructuredBuffer<float4> uOut;
        [shader("compute")]
        [numthreads(8, 4, 2)]
        void computeMain(uint3 tid : SV_DispatchThreadID) { uOut[tid.x] = (float4)1; }
    )SLANG";

    auto c = compile(src);
    REQUIRE(c.isOk());
    const auto* cs = entry(c.value(), ShaderStage::eComp);
    REQUIRE(cs);
    CHECK_FALSE(cs->spirv.empty());

    auto r = reflect(src);
    REQUIRE(r.isOk());
    CHECK(r.value().reflection.hasLocalSize);
    CHECK(r.value().reflection.localSizeX == 8);
    CHECK(r.value().reflection.localSizeY == 4);
    CHECK(r.value().reflection.localSizeZ == 2);
}

TEST_CASE("ray tracing entry points map to the right stages")
{
    const char* src = R"SLANG(
        struct Payload { float3 color; };
        [[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
        [[vk::binding(1, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> img;

        [shader("raygeneration")]
        void rayGenMain()
        {
            RayDesc ray; ray.Origin = (float3)0; ray.Direction = float3(0,0,1); ray.TMin = 0; ray.TMax = 1;
            Payload p; p.color = (float3)0;
            TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p);
            img[uint2(0,0)] = float4(p.color, 1);
        }
        [shader("miss")]
        void missMain(inout Payload p) { p.color = (float3)0; }
        [shader("closesthit")]
        void chitMain(inout Payload p, BuiltInTriangleIntersectionAttributes a) { p.color = (float3)1; }
    )SLANG";

    auto c = compile(src, {}, opts(/*wgsl=*/false)); // RT has no WGSL target
    REQUIRE(c.isOk());
    CHECK(entry(c.value(), ShaderStage::eRgen));
    CHECK(entry(c.value(), ShaderStage::eRmiss));
    CHECK(entry(c.value(), ShaderStage::eRchit));
}

TEST_CASE("multi-stage single file: vertex+fragment+geometry coexist")
{
    const char* src = R"SLANG(
        struct V { float4 pos : SV_Position; };
        [shader("vertex")]
        V vertexMain(uint vid : SV_VertexID) { V o; o.pos = (float4)0; return o; }

        [shader("geometry")]
        [maxvertexcount(3)]
        void geomMain(triangle V input[3], inout TriangleStream<V> stream)
        {
            for (int i = 0; i < 3; ++i) stream.Append(input[i]);
        }

        [shader("fragment")]
        float4 fragmentMain(V i) : SV_Target { return (float4)1; }
    )SLANG";

    auto c = compile(src, {}, opts(/*wgsl=*/false)); // geometry has no WGSL target
    REQUIRE(c.isOk());
    CHECK(entry(c.value(), ShaderStage::eVert));
    CHECK(entry(c.value(), ShaderStage::eFrag));
    CHECK(entry(c.value(), ShaderStage::eGeom));
}
