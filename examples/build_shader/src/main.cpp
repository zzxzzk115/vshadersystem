#include <vshadersystem/binary.hpp>
#include <vshadersystem/system.hpp>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace vshadersystem;

static std::string readFile(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};

    f.seekg(0, std::ios::end);
    const std::streamoff endPos = f.tellg();
    if (endPos <= 0)
        return {};

    const size_t size = static_cast<size_t>(endPos);
    f.seekg(0, std::ios::beg);

    std::string out(size, '\0');
    f.read(out.data(), static_cast<std::streamsize>(size));

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
        default:
            return "UNKNOWN";
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
    std::cout << "SPIRV words: " << static_cast<uint32_t>(bin.spirv.size()) << "\n";
    std::cout << "Material desc params: " << bin.materialDesc.params.size() << "\n";
    std::cout << "Material desc textures: " << bin.materialDesc.textures.size() << "\n";

    std::cout << "\n--- Material Params ---\n";
    for (const auto& p : bin.materialDesc.params)
    {
        std::cout << "Param: " << p.name << ", type=" << static_cast<uint32_t>(p.type)
                  << ", semantic=" << static_cast<uint32_t>(p.semantic) << "\n";
        print_param_default(p);
    }

    std::cout << "\n--- Material Textures ---\n";
    for (const auto& t : bin.materialDesc.textures)
    {
        std::cout << "Texture: " << t.name << ", type=" << static_cast<uint32_t>(t.type)
                  << ", semantic=" << static_cast<uint32_t>(t.semantic) << "\n";
    }

    std::cout << "\n--- Descriptors ---\n";
    for (const auto& d : bin.reflection.descriptors)
    {
        std::cout << "Descriptor: " << d.name << ", set=" << d.set << ", binding=" << d.binding << ", count=" << d.count
                  << ", runtimeSized=" << d.runtimeSized << ", kind=" << static_cast<uint32_t>(d.kind) << "\n";
    }

    std::cout << "\n--- Blocks ---\n";
    for (const auto& b : bin.reflection.blocks)
    {
        std::cout << "Block: " << b.name << ", set=" << b.set << ", binding=" << b.binding << ", size=" << b.size
                  << ", isPushConstant=" << b.isPushConstant << "\n";
        for (const auto& m : b.members)
        {
            std::cout << "  Member: " << m.name << ", offset=" << m.offset << ", size=" << m.size << "\n";
        }
    }

    std::cout << "\n--- Render State ---\n";
    const auto& rs = bin.materialDesc.renderState;
    std::cout << "  Depth test: " << (rs.depthTest ? "enabled" : "disabled")
              << ", Depth write: " << (rs.depthWrite ? "enabled" : "disabled")
              << ", Depth func: " << static_cast<uint32_t>(rs.depthFunc) << "\n";
    std::cout << "  Cull mode: " << static_cast<uint32_t>(rs.cull) << "\n";
    std::cout << "  Blend: " << (rs.blendEnable ? "enabled" : "disabled")
              << ", Src color: " << static_cast<uint32_t>(rs.srcColor)
              << ", Dst color: " << static_cast<uint32_t>(rs.dstColor)
              << ", Color op: " << static_cast<uint32_t>(rs.colorOp)
              << ", Src alpha: " << static_cast<uint32_t>(rs.srcAlpha)
              << ", Dst alpha: " << static_cast<uint32_t>(rs.dstAlpha)
              << ", Alpha op: " << static_cast<uint32_t>(rs.alphaOp) << "\n";
    std::cout << "  Color mask: " << static_cast<uint32_t>(rs.colorMask) << "\n";
    std::cout << "  Alpha to coverage: " << (rs.alphaToCoverage ? "enabled" : "disabled") << "\n";
    std::cout << "  Depth bias factor: " << rs.depthBiasFactor << ", Depth bias units: " << rs.depthBiasUnits << "\n";
}

static bool build_write_read_verify_print(const std::string& sourceText,
                                          ShaderStage        stage,
                                          const std::string& sourceVirtualPath,
                                          const std::string& outVshbinPath)
{
    std::cout << "\n==============================\n";
    std::cout << "Stage: " << stage_name(stage) << "\n";
    std::cout << "Output: " << outVshbinPath << "\n";
    std::cout << "==============================\n";

    // ---- Build ----
    BuildRequest req;
    req.source.virtualPath  = sourceVirtualPath;
    req.source.sourceText   = sourceText;
    req.options.stage       = stage;
    req.options.includeDirs = {"shaders/include"};

    // Defines (example)
    req.options.defines.push_back({"USE_SHADOW", "1"});
    req.options.defines.push_back({"LIGHT_COUNT", "4"});

    auto buildR = build_shader(req);
    if (!buildR.isOk())
    {
        std::cout << "FAIL (build): " << buildR.error().message << "\n";
        return false;
    }

    const ShaderBinary& builtBin = buildR.value().binary;
    const uint32_t      builtWc  = static_cast<uint32_t>(builtBin.spirv.size());

    std::cout << "Build OK\n";
    std::cout << "Built SPIRV words: " << builtWc << "\n";

    // ---- Write ----
    auto w = write_vshbin_file(outVshbinPath, builtBin);
    if (!w.isOk())
    {
        std::cout << "FAIL (write): " << w.error().message << "\n";
        return false;
    }
    std::cout << "Write OK\n";

    // ---- Read ----
    auto r = read_vshbin_file(outVshbinPath);
    if (!r.isOk())
    {
        std::cout << "FAIL (read): " << r.error().message << "\n";
        return false;
    }
    std::cout << "Read OK\n";

    const ShaderBinary& readBin = r.value();
    const uint32_t      readWc  = static_cast<uint32_t>(readBin.spirv.size());

    // ---- Verify ----
    std::cout << "Read back SPIRV words: " << readWc << "\n";
    if (readWc != builtWc)
    {
        std::cout << "FAIL (verify): word count mismatch! Original=" << builtWc << " ReadBack=" << readWc << "\n";
        return false;
    }

    // - reflection.descriptors count
    // - materialDesc params/textures counts

    std::cout << "Verify OK\n";

    // ---- Print full info ----
    print_shader_binary(readBin);

    std::cout << "SUCCESS\n";
    return true;
}

int main()
{
    const auto sourceText = readFile("shaders/minimal.vshader");
    if (sourceText.empty())
    {
        std::cout << "Failed to read shader source.\n";
        return 1;
    }

    // Vertex
    if (!build_write_read_verify_print(sourceText, ShaderStage::eVert, "minimal.vshader", "minimal.vert.vshbin"))
        return 1;

    // Fragment
    if (!build_write_read_verify_print(sourceText, ShaderStage::eFrag, "minimal.vshader", "minimal.frag.vshbin"))
        return 1;

    return 0;
}