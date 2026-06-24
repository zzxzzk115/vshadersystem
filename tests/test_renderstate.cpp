#include <doctest/doctest.h>

#include "test_helpers.hpp"

using namespace vsht;

TEST_CASE("render state attributes map into RenderState")
{
    const char* src = R"SLANG(
        import vsh;
        [VshRenderState("cull", "front")]
        [VshRenderState("depth_test", "off")]
        [VshRenderState("depth_write", "off")]
        [VshRenderState("blend", "on")]
        [VshRenderState("depth_func", "greater")]
        void __vsh_meta() {}
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return (float4)1; }
    )SLANG";

    auto m = metadata(src);
    REQUIRE(m.isOk());
    CHECK(m.value().hasRenderState);

    auto r = reflect(src);
    REQUIRE(r.isOk());
    const auto& rs = r.value().material.renderState;
    CHECK(rs.cull == CullMode::eFront);
    CHECK_FALSE(rs.depthTest);
    CHECK_FALSE(rs.depthWrite);
    CHECK(rs.blendEnable);
    CHECK(rs.depthFunc == CompareOp::eGreater);
}

TEST_CASE("render state defaults are sane when unspecified")
{
    const char* src = R"SLANG(
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return (float4)1; }
    )SLANG";

    auto r = reflect(src);
    REQUIRE(r.isOk());
    const auto& rs = r.value().material.renderState;
    CHECK(rs.cull == CullMode::eBack);   // struct defaults
    CHECK(rs.depthTest);
    CHECK_FALSE(rs.blendEnable);
}
