#include <doctest/doctest.h>

#include "test_helpers.hpp"

#include <vshadersystem/vsh_format.hpp>

using namespace vsht;

// The on-disk binary format carries DXBC/DXIL as optional chunks (older readers skip them).
// This round-trip is host-independent - it exercises the serialization directly.
TEST_CASE("vshbin round-trips DXBC and DXIL chunks")
{
    ShaderBinary b;
    b.stage          = ShaderStage::eVert;
    b.entryPointName = "vertexMain";
    b.spirv          = {0x07230203u, 1u, 2u, 3u};
    b.dxbc           = {0x44, 0x58, 0x42, 0x43, 1, 2, 3};    // "DXBC" + payload
    b.dxil           = {0x44, 0x58, 0x49, 0x4c, 9, 8, 7, 6}; // "DXIL" + payload

    auto w = vshadersystem::v1::write_binary(b);
    REQUIRE(w.isOk());
    auto r = vshadersystem::v1::read_binary(w.value());
    REQUIRE(r.isOk());
    CHECK(r.value().spirv == b.spirv);
    CHECK(r.value().dxbc == b.dxbc);
    CHECK(r.value().dxil == b.dxil);
}

// Empty DXBC/DXIL produce no chunks and read back empty (parity with a v1.0 binary).
TEST_CASE("vshbin without DXBC/DXIL reads back empty")
{
    ShaderBinary b;
    b.stage = ShaderStage::eFrag;
    b.spirv = {0x07230203u, 42u};
    auto w  = vshadersystem::v1::write_binary(b);
    REQUIRE(w.isOk());
    auto r = vshadersystem::v1::read_binary(w.value());
    REQUIRE(r.isOk());
    CHECK(r.value().dxbc.empty());
    CHECK(r.value().dxil.empty());
}

// Slang emits D3D12 bytecode when asked. DXBC needs fxc and DXIL needs dxc (Windows host); a host
// without them leaves the blob empty (best-effort), so only assert non-empty on Windows.
TEST_CASE("Slang emits DXBC/DXIL when requested")
{
    const char* src = R"SLANG(
        struct VSOut { float4 pos : SV_Position; };
        [shader("vertex")]   VSOut vertexMain(float3 p : POSITION) { VSOut o; o.pos = float4(p, 1); return o; }
        [shader("fragment")] float4 fragmentMain(VSOut i) : SV_Target { return float4(1, 0, 0, 1); }
    )SLANG";

    vshaderc::SlangCompileOptions o = opts(/*wgsl=*/false);
    o.emitDxbc                      = true;
    o.emitDxil                      = true;
    auto r                          = compile(src, {}, o);
    REQUIRE(r.isOk());
    const auto* vs = entry(r.value(), ShaderStage::eVert);
    REQUIRE(vs);
    CHECK_FALSE(vs->spirv.empty());
#if defined(_WIN32)
    CHECK_FALSE(vs->dxbc.empty());
    CHECK_FALSE(vs->dxil.empty());
#endif
}
