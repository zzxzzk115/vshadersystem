// material_editor: compile a Slang shader, then print everything a material editor and a
// graphics engine would consume from it -- pipeline reflection, the material inspector
// schema (with editor hints), and the keyword toggles.

#include <vshaderc/slang_build.hpp>
#include <vshaderc/slang_compiler.hpp>

#include <vshadersystem/vsh_format.hpp>

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace vshadersystem;

static std::string read_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static const char* type_name(ParamType t)
{
    switch (t)
    {
        case ParamType::eFloat: return "float";
        case ParamType::eVec2: return "vec2";
        case ParamType::eVec3: return "vec3";
        case ParamType::eVec4: return "vec4";
        case ParamType::eInt: return "int";
        case ParamType::eUInt: return "uint";
        case ParamType::eBool: return "bool";
        case ParamType::eMat3: return "mat3";
        case ParamType::eMat4: return "mat4";
    }
    return "?";
}

static const char* kind_name(DescriptorKind k)
{
    switch (k)
    {
        case DescriptorKind::eUniformBuffer: return "ubo";
        case DescriptorKind::eStorageBuffer: return "ssbo";
        case DescriptorKind::eSampledImage: return "texture";
        case DescriptorKind::eStorageImage: return "storage-image";
        case DescriptorKind::eSampler: return "sampler";
        case DescriptorKind::eAccelerationStructure: return "accel-struct";
        default: return "?";
    }
}

static void print_default(const MaterialParamDesc& p)
{
    if (!p.hasDefault)
        return;
    std::cout << " default=(";
    const int comps = p.type == ParamType::eVec4   ? 4
                      : p.type == ParamType::eVec3 ? 3
                      : p.type == ParamType::eVec2 ? 2
                                                   : 1;
    for (int i = 0; i < comps; ++i)
    {
        float v;
        std::memcpy(&v, p.defaultValue.valueBuffer + i * sizeof(float), sizeof(float));
        std::cout << (i ? "," : "") << v;
    }
    std::cout << ")";
}

int main()
{
    namespace v1 = vshadersystem::v1;

    const std::string src = read_file("shaders/pbr.slang");
    if (src.empty())
    {
        std::cerr << "could not read shaders/pbr.slang\n";
        return 1;
    }

    vshaderc::SlangCompiler compiler;
    vshaderc::ShaderBuildOptions bo;
    bo.shaderId         = "examples/pbr";
    bo.compile.emitWgsl = true;

    auto br = vshaderc::build_shader(compiler, "pbr", "pbr.slang", src, bo);
    if (!br.isOk())
    {
        std::cerr << "build failed: " << br.error().message << "\n";
        return 1;
    }
    const auto& res = br.value();
    std::cout << "Built '" << res.shaderId << "': " << res.combinations << " keyword combinations, "
              << res.variants.size() << " stage-variants\n\n";

    // Decode one vertex + one fragment binary (reflection is shared across variants).
    auto pick = [&](ShaderStage s) -> ShaderBinary {
        for (const auto& v : res.variants)
            if (v.stage == s)
                return vshaderc::to_shader_binary(v, res.shaderIdHash, res.keywords);
        return {};
    };
    const ShaderBinary vs = pick(ShaderStage::eVert);
    const ShaderBinary fs = pick(ShaderStage::eFrag);

    // --- pipeline reflection (engine) ---
    std::cout << "== PIPELINE ==\n";
    std::cout << "vertex entry:   " << vs.entryPointName << "\n";
    std::cout << "fragment entry: " << fs.entryPointName << "\n";
    std::cout << "vertex inputs:\n";
    for (const auto& vi : vs.reflection.vertexInputs)
        std::cout << "  location " << vi.location << "  " << vi.semantic << "  " << type_name(vi.type) << "\n";
    std::cout << "color outputs:  " << fs.reflection.colorOutputs.size() << "\n";
    std::cout << "descriptors:\n";
    for (const auto& d : fs.reflection.descriptors)
        std::cout << "  set " << d.set << " binding " << d.binding << "  " << d.name << "  ("
                  << kind_name(d.kind) << (d.count != 1 ? ", count=" + std::to_string(d.count) : "") << ")\n";

    // --- material inspector (editor) ---
    std::cout << "\n== MATERIAL INSPECTOR ==\n";
    std::cout << fs.materialDesc.materialBlockName << "  (" << fs.materialDesc.materialParamSize << " bytes)\n";
    for (const auto& p : fs.materialDesc.params)
    {
        std::cout << "  " << (p.displayName.empty() ? p.name : p.displayName) << " : " << type_name(p.type);
        if (p.isColor)
            std::cout << " [color]";
        if (p.hasRange)
            std::cout << " range=[" << p.range.min << "," << p.range.max << "]";
        print_default(p);
        std::cout << "\n";
    }
    for (const auto& t : fs.materialDesc.textures)
        std::cout << "  " << t.name << " : texture2D\n";
    const auto& rs = fs.materialDesc.renderState;
    std::cout << "  [render state] cull=" << static_cast<int>(rs.cull) << " depthTest=" << rs.depthTest
              << " blend=" << rs.blendEnable << "\n";

    // --- keyword toggles (editor) ---
    std::cout << "\n== KEYWORDS ==\n";
    for (const auto& k : fs.keywords)
    {
        std::cout << "  " << k.name << " : "
                  << (k.kind == KeywordValueKind::eBool ? "bool" : "enum");
        if (k.kind == KeywordValueKind::eEnum)
        {
            std::cout << "(";
            for (size_t i = 0; i < k.enumValues.size(); ++i)
                std::cout << (i ? "," : "") << k.enumValues[i];
            std::cout << ")";
        }
        std::cout << "\n";
    }

    return 0;
}
