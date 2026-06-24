#include <doctest/doctest.h>

#include "test_helpers.hpp"

using namespace vsht;

TEST_CASE("resource kinds: ubo, storage buffers (ro/rw), texture, sampler, storage image")
{
    const char* src = R"SLANG(
        struct Cb { float4 tint; };
        [[vk::binding(0, 0)]] ConstantBuffer<Cb>            uCb;
        [[vk::binding(1, 0)]] StructuredBuffer<float4>      uIn;   // read-only storage
        [[vk::binding(2, 0)]] RWStructuredBuffer<float4>    uOut;  // read-write storage
        [[vk::binding(3, 0)]] Texture2D<float4>             uTex;
        [[vk::binding(4, 0)]] SamplerState                  uSmp;
        [[vk::binding(5, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> uImg;

        [shader("compute")]
        [numthreads(1,1,1)]
        void computeMain(uint3 tid : SV_DispatchThreadID)
        {
            uOut[tid.x] = uCb.tint + uIn[tid.x] + uTex.SampleLevel(uSmp, (float2)0, 0);
            uImg[tid.xy] = (float4)1;
        }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    const auto& refl = r.value().reflection;

    CHECK(count_kind(refl, DescriptorKind::eUniformBuffer) == 1);
    CHECK(count_kind(refl, DescriptorKind::eStorageBuffer) == 2);
    CHECK(count_kind(refl, DescriptorKind::eSampledImage) == 1);
    CHECK(count_kind(refl, DescriptorKind::eSampler) == 1);
    CHECK(count_kind(refl, DescriptorKind::eStorageImage) == 1);

    const auto* in  = descriptor(refl, "uIn");
    const auto* out = descriptor(refl, "uOut");
    REQUIRE(in);
    REQUIRE(out);
    CHECK(in->set == 0);
    CHECK(in->binding == 1);
    CHECK(in->access == ResourceAccess::eReadOnly);
    CHECK(out->access == ResourceAccess::eReadWrite);
}

TEST_CASE("texture view dimensions: 2D / Cube / 2DArray / 3D")
{
    const char* src = R"SLANG(
        [[vk::binding(0, 0)]] Texture2D<float4>      t2d;
        [[vk::binding(1, 0)]] TextureCube<float4>    tcube;
        [[vk::binding(2, 0)]] Texture2DArray<float4> tarr;
        [[vk::binding(3, 0)]] Texture3D<float4>      t3d;
        [[vk::binding(4, 0)]] SamplerState           s;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target
        {
            return t2d.SampleLevel(s,(float2)0,0) + tcube.SampleLevel(s,(float3)0,0)
                 + tarr.SampleLevel(s,(float3)0,0) + t3d.SampleLevel(s,(float3)0,0);
        }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    const auto& refl = r.value().reflection;
    CHECK(descriptor(refl, "t2d")->textureType == TextureType::eTex2D);
    CHECK(descriptor(refl, "tcube")->textureType == TextureType::eTexCube);
    CHECK(descriptor(refl, "tarr")->textureType == TextureType::eTex2DArray);
    CHECK(descriptor(refl, "t3d")->textureType == TextureType::eTex3D);
}

TEST_CASE("bindless: fixed-size texture array reports its count")
{
    const char* src = R"SLANG(
        [[vk::binding(0, 0)]] Texture2D<float4> textures[16];
        [[vk::binding(1, 0)]] SamplerState      s;
        [shader("fragment")]
        float4 fragmentMain(uint id : SV_PrimitiveID) : SV_Target
        {
            return textures[NonUniformResourceIndex(id)].SampleLevel(s, (float2)0, 0);
        }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    const auto* arr = descriptor(r.value().reflection, "textures");
    REQUIRE(arr);
    CHECK(arr->kind == DescriptorKind::eSampledImage);
    CHECK(arr->count == 16);
}

TEST_CASE("push constant block is flagged")
{
    const char* src = R"SLANG(
        struct Push { float4 data; };
        [[vk::push_constant]] ConstantBuffer<Push> pc;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return pc.data; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    REQUIRE(r.value().reflection.blocks.size() == 1);
    CHECK(r.value().reflection.blocks[0].isPushConstant);
}

TEST_CASE("acceleration structure descriptor")
{
    const char* src = R"SLANG(
        [[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
        [[vk::binding(1, 0)]] RWStructuredBuffer<float4>      uOut;
        [shader("compute")]
        [numthreads(1,1,1)]
        void computeMain()
        {
            RayQuery<RAY_FLAG_NONE> q;
            RayDesc ray; ray.Origin=(float3)0; ray.Direction=float3(0,0,1); ray.TMin=0; ray.TMax=1;
            q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
            uOut[0] = (float4)1;
        }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    CHECK(count_kind(r.value().reflection, DescriptorKind::eAccelerationStructure) == 1);
}

TEST_CASE("multi-set bindings keep their set index")
{
    const char* src = R"SLANG(
        struct A { float4 v; };
        struct B { float4 v; };
        [[vk::binding(0, 0)]] ConstantBuffer<A> a;
        [[vk::binding(0, 1)]] ConstantBuffer<B> b;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return a.v + b.v; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    CHECK(descriptor(r.value().reflection, "a")->set == 0);
    CHECK(descriptor(r.value().reflection, "b")->set == 1);
}
