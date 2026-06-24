#include <doctest/doctest.h>

#include "test_helpers.hpp"

#include <vshadersystem/engine_keywords.hpp>
#include <vshadersystem/variant_key.hpp>

#include <set>

using namespace vsht;

static const char* kKeywordShader = R"SLANG(
    import vsh;
    [VshKeyword("USE_SHADOW", "bool", "permute", "global")]
    [VshKeyword("QUALITY", "enum:low,medium,high", "permute", "local")]
    void __vsh_meta() {}

    [shader("fragment")]
    float4 fragmentMain() : SV_Target
    {
        float3 c = (float3)1;
    #if USE_SHADOW
        c *= 0.5;
    #endif
    #if QUALITY == 2
        c = pow(c, (float3)2.0);
    #endif
        return float4(c, 1);
    }
)SLANG";

TEST_CASE("keyword decls parsed: bool + enum, dispatch/scope")
{
    auto m = metadata(kKeywordShader);
    REQUIRE(m.isOk());
    REQUIRE(m.value().keywords.size() == 2);

    const auto& k0 = m.value().keywords[0];
    CHECK(k0.name == "USE_SHADOW");
    CHECK(k0.kind == KeywordValueKind::eBool);
    CHECK(k0.dispatch == KeywordDispatch::ePermutation);
    CHECK(k0.scope == KeywordScope::eGlobal);

    const auto& k1 = m.value().keywords[1];
    CHECK(k1.name == "QUALITY");
    CHECK(k1.kind == KeywordValueKind::eEnum);
    REQUIRE(k1.enumValues.size() == 3);
    CHECK(k1.enumValues[2] == "high");
}

TEST_CASE("variant expansion: cartesian product, distinct variant hashes")
{
    auto b = build(kKeywordShader, "test/kw");
    REQUIRE(b.isOk());
    const auto& res = b.value();

    CHECK(res.combinations == 6); // bool(2) x enum(3)
    CHECK(res.variants.size() == 6); // one fragment entry point per combo

    std::set<uint64_t> hashes;
    for (const auto& v : res.variants)
        hashes.insert(v.variantHash);
    CHECK(hashes.size() == 6); // all distinct
}

TEST_CASE("runtime variantHash matches the build-time hash (engine lookup parity)")
{
    auto b = build(kKeywordShader, "test/kw");
    REQUIRE(b.isOk());

    // Engine recomputes the key for (USE_SHADOW=1, QUALITY=2).
    VariantKey key;
    key.setShaderId("test/kw");
    key.setStage(ShaderStage::eFrag);
    key.set("USE_SHADOW", 1);
    key.set("QUALITY", 2);
    const uint64_t want = key.build();

    bool found = false;
    for (const auto& v : b.value().variants)
        if (v.variantHash == want)
            found = true;
    CHECK(found);
}

TEST_CASE("#if keyword actually changes the compiled code")
{
    auto off = opts();
    off.defines = {{"USE_SHADOW", "0"}, {"QUALITY", "0"}};
    auto on = opts();
    on.defines = {{"USE_SHADOW", "1"}, {"QUALITY", "2"}};

    auto a = compiler().compileModule("test", "test.slang", kKeywordShader, off);
    auto c = compiler().compileModule("test", "test.slang", kKeywordShader, on);
    REQUIRE(a.isOk());
    REQUIRE(c.isOk());
    const auto* fa = entry(a.value(), ShaderStage::eFrag);
    const auto* fc = entry(c.value(), ShaderStage::eFrag);
    REQUIRE(fa);
    REQUIRE(fc);
    CHECK(fa->spirv != fc->spirv); // shadow + quality branches change the SPIR-V
}

TEST_CASE("runtime keyword is not expanded into variants")
{
    const char* src = R"SLANG(
        import vsh;
        [VshKeyword("DEBUG_VIEW", "bool", "runtime", "global")]
        void __vsh_meta() {}
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return (float4)1; }
    )SLANG";

    auto b = build(src, "test/rt");
    REQUIRE(b.isOk());
    CHECK(b.value().combinations == 1); // runtime keyword does not multiply variants
    CHECK(b.value().variants.size() == 1);
}

TEST_CASE("engine .vkw keywords are expanded alongside shader keywords")
{
    auto kw = parse_engine_keywords_vkw("keyword permute global FOG\n");
    REQUIRE(kw.isOk());

    const char* src = R"SLANG(
        import vsh;
        [VshKeyword("USE_SHADOW", "bool", "permute", "global")]
        void __vsh_meta() {}
        [shader("fragment")]
        float4 fragmentMain() : SV_Target
        {
            float3 c = (float3)1;
        #if FOG
            c *= 0.9;
        #endif
        #if USE_SHADOW
            c *= 0.5;
        #endif
            return float4(c, 1);
        }
    )SLANG";

    auto b = build(src, "test/eng", &kw.value());
    REQUIRE(b.isOk());
    CHECK(b.value().combinations == 4); // FOG(2) x USE_SHADOW(2)
}
