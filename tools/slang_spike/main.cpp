// slang_spike: M2-blocking spike for the vshadersystem v1.0 Slang refactor.
//
// Confirms that the Slang Reflection API can read back user-defined attributes from
// module-level declarations, and crucially whether REPEATED attributes of the same
// kind on a single declaration are preserved (the planned keyword/renderstate encoding
// attaches several [VshKeyword(...)] / [VshRenderState(...)] attributes to one marker
// function). Also proves the prebuilt Slang links and emits SPIR-V in this repo.
//
// Build: xmake f --vshadersystem_build_compiler=y && xmake build slang_spike
// Run:   xmake run slang_spike

#include <slang-com-ptr.h>
#include <slang.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using Slang::ComPtr;

// Attribute library + a representative shader, inlined so the spike needs no VFS.
static const char* kSource = R"SLANG(
// --- vsh attribute library (would live in vsh.slang) ---
[__AttributeUsage(_AttributeTargets.Struct)]
struct VshMaterialAttribute {}

[__AttributeUsage(_AttributeTargets.Var)]
struct VshSemanticAttribute { string name; }

[__AttributeUsage(_AttributeTargets.Var)]
struct VshRangeAttribute { float lo; float hi; }

[__AttributeUsage(_AttributeTargets.Function)]
struct VshKeywordAttribute { string name; string type; string dispatch; string scope; }

[__AttributeUsage(_AttributeTargets.Function)]
struct VshRenderStateAttribute { string key; string value; }

// --- authored shader using the attributes ---
[VshMaterial]
struct Material
{
    [VshSemantic("baseColor")]                 float4 baseColorFactor;
    [VshSemantic("metallic")] [VshRange(0, 1)] float  metallicFactor;
    int baseColorTex_index;
};

// keyword + renderstate declarations concentrated on one marker function, using
// REPEATED attributes of the same kind -- this is what the spike must validate.
[VshKeyword("USE_SHADOW", "bool", "permute", "global")]
[VshKeyword("QUALITY", "enum:low,medium,high", "permute", "local")]
[VshRenderState("cull", "back")]
[VshRenderState("depth_test", "on")]
void __vsh_meta() {}

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    Material m;
    m.baseColorFactor = float4(1, 1, 1, 1);
    m.metallicFactor = 0.0;
    m.baseColorTex_index = 0;
    return m.baseColorFactor;
}
)SLANG";

static void dump_diagnostics(slang::IBlob* diag)
{
    if (diag && diag->getBufferSize() > 0)
        std::printf("[slang] %.*s\n", (int)diag->getBufferSize(), (const char*)diag->getBufferPointer());
}

static void dump_user_attributes(const char* who, unsigned count,
                                 slang::UserAttribute* (*get)(void*, unsigned), void* owner)
{
    std::printf("  %s: userAttributeCount = %u\n", who, count);
    for (unsigned i = 0; i < count; ++i)
    {
        slang::UserAttribute* attr = get(owner, i);
        if (!attr)
            continue;
        const char* name = attr->getName();
        unsigned argc = attr->getArgumentCount();
        std::printf("    [%u] %s (argc=%u)", i, name ? name : "?", argc);
        for (unsigned a = 0; a < argc; ++a)
        {
            size_t len = 0;
            const char* s = attr->getArgumentValueString(a, &len);
            if (s)
                std::printf(" arg%u=\"%.*s\"", a, (int)len, s);
            else
            {
                int iv = 0;
                float fv = 0.0f;
                if (attr->getArgumentValueInt(a, &iv) == SLANG_OK)
                    std::printf(" arg%u=%d", a, iv);
                else if (attr->getArgumentValueFloat(a, &fv) == SLANG_OK)
                    std::printf(" arg%u=%g", a, fv);
                else
                    std::printf(" arg%u=?", a);
            }
        }
        std::printf("\n");
    }
}

static slang::UserAttribute* type_get_attr(void* owner, unsigned i)
{
    return ((slang::TypeReflection*)owner)->getUserAttributeByIndex(i);
}
static slang::UserAttribute* var_get_attr(void* owner, unsigned i)
{
    return ((slang::VariableReflection*)owner)->getUserAttributeByIndex(i);
}
static slang::UserAttribute* func_get_attr(void* owner, unsigned i)
{
    return ((slang::FunctionReflection*)owner)->getUserAttributeByIndex(i);
}

int main()
{
    ComPtr<slang::IGlobalSession> global;
    if (SLANG_FAILED(slang::createGlobalSession(global.writeRef())))
    {
        std::printf("FAILED: createGlobalSession\n");
        return 1;
    }

    slang::TargetDesc target = {};
    target.format = SLANG_SPIRV;
    target.profile = global->findProfile("spirv_1_5");

    slang::CompilerOptionEntry options[2] = {};
    options[0].name = slang::CompilerOptionName::EmitSpirvDirectly;
    options[0].value.intValue0 = 1;
    options[1].name = slang::CompilerOptionName::VulkanUseEntryPointName;
    options[1].value.intValue0 = 1;

    slang::SessionDesc sessionDesc = {};
    sessionDesc.targets = &target;
    sessionDesc.targetCount = 1;
    sessionDesc.compilerOptionEntries = options;
    sessionDesc.compilerOptionEntryCount = 2;

    ComPtr<slang::ISession> session;
    if (SLANG_FAILED(global->createSession(sessionDesc, session.writeRef())))
    {
        std::printf("FAILED: createSession\n");
        return 1;
    }

    ComPtr<slang::IBlob> diag;
    slang::IModule* module = session->loadModuleFromSourceString("spike", "spike.slang", kSource, diag.writeRef());
    dump_diagnostics(diag);
    if (!module)
    {
        std::printf("FAILED: loadModuleFromSourceString\n");
        return 1;
    }

    // --- Reflection of module-level declarations + their user attributes ---
    std::printf("=== module decl reflection ===\n");
    slang::DeclReflection* root = module->getModuleReflection();
    if (!root)
    {
        std::printf("FAILED: getModuleReflection returned null\n");
        return 1;
    }

    bool sawRepeatedKeyword = false;
    bool sawRepeatedRenderState = false;
    unsigned childCount = root->getChildrenCount();
    std::printf("module children = %u\n", childCount);
    for (unsigned i = 0; i < childCount; ++i)
    {
        slang::DeclReflection* child = root->getChild(i);
        if (!child)
            continue;
        slang::DeclReflection::Kind kind = child->getKind();

        if (kind == slang::DeclReflection::Kind::Struct)
        {
            slang::TypeReflection* t = child->getType();
            if (!t)
                continue;
            const char* tn = t->getName();
            unsigned tac = t->getUserAttributeCount();
            if (tac == 0 && (!tn || std::string_view(tn) != "Material"))
                continue; // skip the attribute-library structs themselves
            std::printf("struct %s\n", tn ? tn : "?");
            dump_user_attributes("type", tac, type_get_attr, t);
            unsigned fc = t->getFieldCount();
            for (unsigned f = 0; f < fc; ++f)
            {
                slang::VariableReflection* field = t->getFieldByIndex(f);
                if (!field)
                    continue;
                std::printf("  field %s\n", field->getName());
                dump_user_attributes("    field", field->getUserAttributeCount(), var_get_attr, field);
            }
        }
        else if (kind == slang::DeclReflection::Kind::Func)
        {
            slang::FunctionReflection* fn = child->asFunction();
            if (!fn)
                continue;
            unsigned fac = fn->getUserAttributeCount();
            if (fac == 0)
                continue;
            std::printf("func %s\n", fn->getName());
            dump_user_attributes("func", fac, func_get_attr, fn);
            unsigned kw = 0, rs = 0;
            for (unsigned a = 0; a < fac; ++a)
            {
                slang::UserAttribute* at = fn->getUserAttributeByIndex(a);
                const char* an = at ? at->getName() : nullptr;
                if (an && std::string_view(an).find("Keyword") != std::string_view::npos)
                    ++kw;
                if (an && std::string_view(an).find("RenderState") != std::string_view::npos)
                    ++rs;
            }
            if (kw >= 2)
                sawRepeatedKeyword = true;
            if (rs >= 2)
                sawRepeatedRenderState = true;
        }
    }

    // --- Prove SPIR-V codegen works with the attribute library present ---
    std::vector<slang::IComponentType*> comps;
    comps.push_back(module);
    SlangInt32 epCount = module->getDefinedEntryPointCount();
    for (SlangInt32 i = 0; i < epCount; ++i)
    {
        ComPtr<slang::IEntryPoint> ep;
        if (SLANG_SUCCEEDED(module->getDefinedEntryPoint(i, ep.writeRef())))
            comps.push_back(ep.get());
    }
    ComPtr<slang::IComponentType> composed;
    diag = nullptr;
    session->createCompositeComponentType(comps.data(), (SlangInt)comps.size(), composed.writeRef(), diag.writeRef());
    dump_diagnostics(diag);
    ComPtr<slang::IComponentType> linked;
    diag = nullptr;
    if (composed)
        composed->link(linked.writeRef(), diag.writeRef());
    dump_diagnostics(diag);
    ComPtr<slang::IBlob> spirv;
    diag = nullptr;
    if (linked)
        linked->getEntryPointCode(0, 0, spirv.writeRef(), diag.writeRef());
    dump_diagnostics(diag);
    std::printf("=== codegen ===\nentryPoints=%d  spirvBytes=%zu\n", (int)epCount,
                spirv ? (size_t)spirv->getBufferSize() : (size_t)0);

    std::printf("\n=== SPIKE VERDICT ===\n");
    std::printf("repeated [VshKeyword] preserved on one decl: %s\n", sawRepeatedKeyword ? "YES" : "NO");
    std::printf("repeated [VshRenderState] preserved on one decl: %s\n", sawRepeatedRenderState ? "YES" : "NO");
    std::printf("spirv emitted: %s\n", (spirv && spirv->getBufferSize() > 0) ? "YES" : "NO");

    bool ok = sawRepeatedKeyword && sawRepeatedRenderState && spirv && spirv->getBufferSize() > 0;
    std::printf("RESULT: %s\n", ok ? "PASS" : "NEEDS-FALLBACK");
    return ok ? 0 : 2;
}
