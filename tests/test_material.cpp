#include <doctest/doctest.h>

#include "test_helpers.hpp"

using namespace vsht;

static const char* kMaterialShader = R"SLANG(
    import vsh;

    [VshMaterial]
    struct Material
    {
        [VshSemantic("baseColor")]                 float4 baseColorFactor;
        [VshSemantic("metallic")] [VshRange(0, 1)] float  metallicFactor;
        [VshSemantic("roughness")][VshRange(0, 1)] float  roughnessFactor;
        [VshTexture("Texture2D")]                  int    baseColorTex_index;
    };

    [[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;

    [shader("fragment")]
    float4 fragmentMain() : SV_Target { return uMat.baseColorFactor; }
)SLANG";

TEST_CASE("material metadata: semantics, ranges, texture fields")
{
    auto m = metadata(kMaterialShader);
    REQUIRE(m.isOk());
    const auto& meta = m.value();

    CHECK(meta.materialStructName == "Material");
    REQUIRE(meta.materialFields.size() == 4);

    CHECK(meta.materialFields[0].semantic == "baseColor");
    CHECK_FALSE(meta.materialFields[0].hasRange);
    CHECK(meta.materialFields[1].semantic == "metallic");
    CHECK(meta.materialFields[1].hasRange);
    CHECK(meta.materialFields[1].rangeLo == doctest::Approx(0.0));
    CHECK(meta.materialFields[1].rangeHi == doctest::Approx(1.0));
    CHECK(meta.materialFields[3].textureKind == "Texture2D");
}

TEST_CASE("material description: param layout + offsets, textures split out")
{
    auto r = reflect(kMaterialShader);
    REQUIRE(r.isOk());
    const auto& mat = r.value().material;

    CHECK(mat.materialBlockName == "Material");
    CHECK(mat.materialParamSize > 0);

    // The texture-index field is surfaced as a texture, not a scalar param.
    REQUIRE(mat.params.size() == 3);
    REQUIRE(mat.textures.size() == 1);

    CHECK(mat.params[0].name == "baseColorFactor");
    CHECK(mat.params[0].type == ParamType::eVec4);
    CHECK(mat.params[0].offset == 0);
    CHECK(mat.params[0].semantic == Semantic::eBaseColor);

    CHECK(mat.params[1].name == "metallicFactor");
    CHECK(mat.params[1].type == ParamType::eFloat);
    CHECK(mat.params[1].semantic == Semantic::eMetallic);
    CHECK(mat.params[1].hasRange);

    CHECK(mat.textures[0].name == "baseColorTex_index");
}

TEST_CASE("std140-style offsets: vec3 followed by scalar packs correctly")
{
    const char* src = R"SLANG(
        import vsh;
        [VshMaterial]
        struct Material { float3 a; float b; float4 c; };
        [[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return uMat.c + float4(uMat.a, uMat.b); }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    REQUIRE(r.value().reflection.blocks.size() == 1);
    const auto& b = r.value().reflection.blocks[0];
    REQUIRE(b.members.size() == 3);
    CHECK(b.members[0].name == "a");
    CHECK(b.members[0].offset == 0);
    CHECK(b.members[1].name == "b");
    CHECK(b.members[1].offset == 12); // float packs into the vec3's 4th slot
    CHECK(b.members[2].name == "c");
    CHECK(b.members[2].offset == 16); // vec4 aligns to 16
}
