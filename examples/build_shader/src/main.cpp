#include <vshadersystem/binary.hpp>
#include <vshadersystem/system.hpp>

#include <cstring> // memcpy
#include <fstream>
#include <iostream>
#include <string>

using namespace vshadersystem;

#define SHADER_NAME "minimal.vshader"
#define SHADER_DIR "shaders/"

static std::string readFile(const char* path)
{
    std::ifstream f(path, std::ios::binary);

    if (!f)
        return {};

    f.seekg(0, std::ios::end);

    size_t size = f.tellg();

    f.seekg(0, std::ios::beg);

    std::string out(size, '\0');

    f.read(out.data(), size);

    return out;
}

static const char* stage_name(ShaderStage s)
{
    switch (s)
    {
        case ShaderStage::eVert:
            return "VERT";
        case ShaderStage::eFrag:
            return "FRAG";
        case ShaderStage::eComp:
            return "COMP";
        case ShaderStage::eMesh:
            return "MESH";
        case ShaderStage::eTask:
            return "TASK";
        default:
            return "UNKNOWN";
    }
}

static std::string stage_ext(ShaderStage s)
{
    switch (s)
    {
        case ShaderStage::eVert:
            return "vert";
        case ShaderStage::eFrag:
            return "frag";
        case ShaderStage::eComp:
            return "comp";
        case ShaderStage::eMesh:
            return "mesh";
        case ShaderStage::eTask:
            return "task";
        default:
            return "unknown";
    }
}

static void print_param_default(const MaterialParamDesc& p)
{
    if (!p.hasDefault)
    {
        std::cout << "  No default value\n";
        return;
    }

    switch (p.type)
    {
        case ParamType::eFloat:
        case ParamType::eInt:
        case ParamType::eUInt:
        case ParamType::eBool: {
            float v = 0.0f;
            std::memcpy(&v, p.defaultValue.valueBuffer, sizeof(float));
            std::cout << "  Default(scalar)=" << v << "\n";
            break;
        }
        case ParamType::eVec2: {
            float v2[2] = {};
            std::memcpy(v2, p.defaultValue.valueBuffer, sizeof(float) * 2);
            std::cout << "  Default(vec2)=(" << v2[0] << ", " << v2[1] << ")\n";
            break;
        }
        case ParamType::eVec3: {
            float v3[3] = {};
            std::memcpy(v3, p.defaultValue.valueBuffer, sizeof(float) * 3);
            std::cout << "  Default(vec3)=(" << v3[0] << ", " << v3[1] << ", " << v3[2] << ")\n";
            break;
        }
        case ParamType::eVec4: {
            float v4[4] = {};
            std::memcpy(v4, p.defaultValue.valueBuffer, sizeof(float) * 4);
            std::cout << "  Default(vec4)=(" << v4[0] << ", " << v4[1] << ", " << v4[2] << ", " << v4[3] << ")\n";
            break;
        }
        case ParamType::eMat3: {
            float m3[9] = {};
            std::memcpy(m3, p.defaultValue.valueBuffer, sizeof(float) * 9);
            std::cout << "  Default(mat3)=[";
            for (int i = 0; i < 9; ++i)
            {
                std::cout << m3[i] << (i < 8 ? ", " : "]\n");
            }
            break;
        }
        case ParamType::eMat4: {
            float m4[16] = {};
            std::memcpy(m4, p.defaultValue.valueBuffer, sizeof(float) * 16);
            std::cout << "  Default(mat4)=[";
            for (int i = 0; i < 16; ++i)
            {
                std::cout << m4[i] << (i < 15 ? ", " : "]\n");
            }
            break;
        }
        default:
            std::cout << "  Default(unknown)\n";
            break;
    }
}

static void print_shader_binary(const ShaderBinary& bin)
{
    std::cout << "Stage: " << stage_name(bin.stage) << "\n";

    std::cout << "SPIRV words: " << bin.spirv.size() << "\n";

    if (bin.stage == ShaderStage::eFrag)
    {
        std::cout << "\nMaterial Params:\n";

        for (const auto& p : bin.materialDesc.params)
        {

            std::cout << "  " << p.name << " type=" << static_cast<int>(p.type)
                      << " semantic=" << static_cast<int>(p.semantic) << " offset=" << p.offset << " size=" << p.size;

            print_param_default(p);
        }

        std::cout << "\nTextures:\n";

        for (const auto& t : bin.materialDesc.textures)
        {
            std::cout << "  " << t.name << "\n";
        }
    }

    std::cout << "\nDescriptors:\n";

    for (const auto& d : bin.reflection.descriptors)
    {
        std::cout << "  " << d.name << " set=" << d.set << " binding=" << d.binding << "\n";
    }

    std::cout << "\nRenderState:\n";

    std::cout << "  DepthTest: " << bin.materialDesc.renderState.depthTest << "\n";
    std::cout << "  DepthWrite: " << bin.materialDesc.renderState.depthWrite << "\n";
    std::cout << "  DepthFunc: " << static_cast<int>(bin.materialDesc.renderState.depthFunc) << "\n";
    std::cout << "  Blend: " << bin.materialDesc.renderState.blendEnable << "\n";
    std::cout << "    SrcColor: " << static_cast<int>(bin.materialDesc.renderState.srcColor) << "\n";
    std::cout << "    DstColor: " << static_cast<int>(bin.materialDesc.renderState.dstColor) << "\n";
    std::cout << "    ColorOp: " << static_cast<int>(bin.materialDesc.renderState.colorOp) << "\n";
    std::cout << "    SrcAlpha: " << static_cast<int>(bin.materialDesc.renderState.srcAlpha) << "\n";
    std::cout << "    DstAlpha: " << static_cast<int>(bin.materialDesc.renderState.dstAlpha) << "\n";
    std::cout << "    AlphaOp: " << static_cast<int>(bin.materialDesc.renderState.alphaOp) << "\n";
    std::cout << "  Cull: " << static_cast<int>(bin.materialDesc.renderState.cull) << "\n";
    std::cout << "  ColorMask: " << static_cast<int>(bin.materialDesc.renderState.colorMask) << "\n";
    std::cout << "  AlphaToCoverage: " << bin.materialDesc.renderState.alphaToCoverage << "\n";
    std::cout << "  DepthBiasFactor: " << bin.materialDesc.renderState.depthBiasFactor << "\n";
    std::cout << "  DepthBiasUnits: " << bin.materialDesc.renderState.depthBiasUnits << "\n";

    std::cout << "\n";
}

static bool write_read_print(const ShaderBinary& bin, const std::string& path)
{
    write_vshbin_file(path, bin);

    auto r = read_vshbin_file(path);

    if (!r.isOk())
        return false;

    print_shader_binary(r.value());

    return true;
}

static bool test_single(const std::string& src, ShaderStage stage)
{
    std::cout << "\n=== SINGLE ===\n";

    BuildRequest req;

    req.source.virtualPath        = SHADER_NAME;
    req.source.sourceText         = src;
    req.options.includeDirs       = {"shaders/include"};
    req.options.stage             = stage;
    req.options.materialInjection = {
        .preamble                 = R"(
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set=0,binding=0,std430) readonly buffer DrawBuffer
{
    uint materialIndex;
} g_Draws;
layout(set=1,binding=0) uniform sampler2D uBindlessTextures[];
)",
        .materialIndexExpr        = "g_Draws.materialIndex",
        .bindlessTextureArrayName = "uBindlessTextures",
        .macroPrefix              = "VSH_",
    };

    auto r = build_single_shader(req);

    if (!r.isOk())
    {
        std::cerr << "Failed to build " << stage_name(stage) << " shader: " << r.error().message << "\n";
        return false;
    }

    return write_read_print(r.value().binary, "single." + stage_ext(stage) + ".vshbin");
}

static bool test_multiple(const std::string& src)
{
    std::cout << "\n=== MULTIPLE ===\n";

    BuildRequest req;

    req.source.virtualPath  = SHADER_NAME;
    req.source.sourceText   = src;
    req.options.includeDirs = {"shaders/include"};
    // We don't set stage here, to test build_multiple_shaders' ability to detect stages from markers.
    req.options.materialInjection = {
        .preamble                 = R"(
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set=0,binding=0,std430) readonly buffer DrawBuffer
{
    uint materialIndex;
} g_Draws;
layout(set=1,binding=0) uniform sampler2D uBindlessTextures[];
)",
        .materialIndexExpr        = "g_Draws.materialIndex",
        .bindlessTextureArrayName = "uBindlessTextures",
        .macroPrefix              = "VSH_",
    };

    auto r = build_multiple_shaders(req);

    if (!r.isOk())
    {
        std::cerr << "Failed to build multiple shaders: " << r.error().message << "\n";
        return false;
    }

    for (auto& [stage, result] : r.value())
    {
        if (!write_read_print(result.binary, "multi." + stage_ext(stage) + ".vshbin"))
        {
            return false;
        }
    }

    return true;
}

int main()
{
    auto src = readFile(SHADER_DIR SHADER_NAME);

    if (src.empty())
        return 1;

    if (!test_single(src, ShaderStage::eVert))
    {
        return 1;
    }

    if (!test_single(src, ShaderStage::eFrag))
    {
        return 1;
    }

    if (!test_multiple(src))
    {
        return 1;
    }

    return 0;
}