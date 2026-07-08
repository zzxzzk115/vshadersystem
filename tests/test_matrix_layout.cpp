#include <doctest/doctest.h>

#include "test_helpers.hpp"

#include <cstdint>
#include <vector>

using namespace vsht;

// A vertex shader with a float4x4 in a push constant, used via mul() so the matrix survives
// and its memory layout affects the emitted SPIR-V. (Slang emulates matrices as a vec4-array
// wrapper struct rather than a native OpTypeMatrix, so the layout shows up as different code
// generation, not as an OpMemberDecorate ColMajor/RowMajor - hence we assert on the bytes.)
namespace
{
    const char* kMatrixShader = R"SLANG(
        struct PushC { float4x4 m; };
        [[vk::push_constant]] PushC pc;
        struct VSOut { float4 pos : SV_Position; };
        [shader("vertex")]
        VSOut vertexMain(float3 p : POSITION)
        {
            VSOut o; o.pos = mul(pc.m, float4(p, 1.0)); return o;
        }
    )SLANG";

    std::vector<uint32_t> vertex_spirv(const vshaderc::SlangCompileOptions& o)
    {
        auto r = compile(kMatrixShader, {}, o);
        REQUIRE(r.isOk());
        const auto* vs = entry(r.value(), ShaderStage::eVert);
        REQUIRE(vs);
        REQUIRE_FALSE(vs->spirv.empty());
        return vs->spirv;
    }
} // namespace

TEST_CASE("matrix layout: the option reaches Slang and changes SPIR-V codegen")
{
    vshaderc::SlangCompileOptions colOpts = opts(/*wgsl=*/false);
    colOpts.matrixLayout                  = vshaderc::MatrixLayout::Column;
    vshaderc::SlangCompileOptions rowOpts = opts(/*wgsl=*/false);
    rowOpts.matrixLayout                  = vshaderc::MatrixLayout::Row;

    const auto col = vertex_spirv(colOpts);
    const auto row = vertex_spirv(rowOpts);

    // Column-major and row-major read the same float4x4 transposed, so the SPIR-V differs.
    CHECK(col != row);
}

TEST_CASE("matrix layout: vshaderc defaults to column-major (matches glm / GLSL / Vulkan)")
{
    // Slang's OWN default is row-major; vshaderc overrides it to column-major so a column-major
    // (glm) matrix used with mul(m, v) is not read transposed (which would collapse all vertices).
    const vshaderc::SlangCompileOptions defaultOpts = opts(/*wgsl=*/false);
    CHECK(defaultOpts.matrixLayout == vshaderc::MatrixLayout::Column);

    vshaderc::SlangCompileOptions colOpts = defaultOpts;
    colOpts.matrixLayout                  = vshaderc::MatrixLayout::Column;
    vshaderc::SlangCompileOptions rowOpts = defaultOpts;
    rowOpts.matrixLayout                  = vshaderc::MatrixLayout::Row;

    const auto def = vertex_spirv(defaultOpts); // layout left unset -> struct default
    const auto col = vertex_spirv(colOpts);     // explicit column
    const auto row = vertex_spirv(rowOpts);     // explicit row

    CHECK(def == col); // the default produces column-major output
    CHECK(def != row);
}
