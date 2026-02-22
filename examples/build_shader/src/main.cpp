#include <vshadersystem/binary.hpp>
#include <vshadersystem/system.hpp>

#include <cstring>
#include <fstream>
#include <iostream>

using namespace vshadersystem;

static std::string readFile(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};

    f.seekg(0, std::ios::end);
    size_t s = f.tellg();
    f.seekg(0, std::ios::beg);
    std::string out(s, '\0');
    f.read(out.data(), s);

    f.close();

    return out;
}

int main()
{
    auto sourceText = readFile("shaders/pbr.frag.vshader");
    if (sourceText.empty())
    {
        std::cout << "Failed to read shader source.\n";
        return 1;
    }

    BuildRequest req;
    req.source.virtualPath  = "pbr.frag.vshader";
    req.source.sourceText   = sourceText;
    req.options.stage       = ShaderStage::eFrag;
    req.options.includeDirs = {"shaders/include"};
    // Defines
    req.options.defines.push_back({"USE_SHADOW", "1"});
    req.options.defines.push_back({"LIGHT_COUNT", "4"});

    auto r = build_shader(req);

    if (!r.isOk())
    {
        std::cout << "FAIL: " << r.error().message << "\n";
        return 1;
    }

    auto&          bin       = r.value().binary;
    const uint32_t wordCount = static_cast<uint32_t>(bin.spirv.size());

    std::cout << "OK\n";
    std::cout << "SPIRV words: " << wordCount << "\n";
    std::cout << "Material desc params: " << bin.materialDesc.params.size() << "\n";
    std::cout << "Material desc textures: " << bin.materialDesc.textures.size() << "\n";

    // For demonstration, write the binary to disk and read it back.
    const std::string path = "pbr.frag.vshbin";
    auto              w    = write_vshbin_file(path, bin);
    if (!w.isOk())
    {
        std::cout << "Write failed: " << w.error().message << "\n";
        return 1;
    }

    auto r2 = read_vshbin_file(path);
    if (!r2.isOk())
    {
        std::cout << "Read failed: " << r2.error().message << "\n";
        return 1;
    }

    const uint32_t readBackWordCount = static_cast<uint32_t>(r2.value().spirv.size());
    std::cout << "Read back SPIRV words: " << readBackWordCount << "\n";

    if (readBackWordCount != wordCount)
    {
        std::cout << "Word count mismatch! Original: " << wordCount << ", Read back: " << readBackWordCount << "\n";
        return 1;
    }

    // Print more shader info to verify reflection and material description are intact.
    std::cout << "Read back material desc params: " << r2.value().materialDesc.params.size() << "\n";
    std::cout << "Read back material desc textures: " << r2.value().materialDesc.textures.size() << "\n";

    for (const auto& p : r2.value().materialDesc.params)
    {
        std::cout << "Param: " << p.name << ", type=" << static_cast<uint32_t>(p.type)
                  << ", semantic=" << static_cast<uint32_t>(p.semantic) << "\n";

        if (!p.hasDefault)
        {
            std::cout << "  No default value\n";
            continue;
        }

        switch (p.type)
        {
            case ParamType::eFloat:
            case ParamType::eInt:
            case ParamType::eUInt:
            case ParamType::eBool: {
                float v = 0.0f;
                std::memcpy(&v, p.defaultValue.valueBuffer, sizeof(float));
                std::cout << "  Type: "
                          << (p.type == ParamType::eFloat ?
                                  "float" :
                                  (p.type == ParamType::eInt ? "int" : (p.type == ParamType::eUInt ? "uint" : "bool")))
                          << ", default=" << v << "\n";
                break;
            }
            case ParamType::eVec2: {
                float v2[2] = {};
                std::memcpy(v2, p.defaultValue.valueBuffer, sizeof(float) * 2);
                std::cout << "  Type: vec2, default=(" << v2[0] << ", " << v2[1] << ")\n";
                break;
            }
            case ParamType::eVec3: {
                float v3[3] = {};
                std::memcpy(v3, p.defaultValue.valueBuffer, sizeof(float) * 3);
                std::cout << "  Type: vec3, default=(" << v3[0] << ", " << v3[1] << ", " << v3[2] << ")\n";
                break;
            }
            case ParamType::eVec4: {
                float v4[4] = {};
                std::memcpy(v4, p.defaultValue.valueBuffer, sizeof(float) * 4);
                std::cout << "  Type: vec4, default=(" << v4[0] << ", " << v4[1] << ", " << v4[2] << ", " << v4[3]
                          << ")\n";
                break;
            }

            case ParamType::eMat3: {
                float m3[9] = {};
                std::memcpy(m3, p.defaultValue.valueBuffer, sizeof(float) * 9);
                std::cout << "  Type: mat3, default=[";
                ;
                for (int i = 0; i < 9; ++i)
                {
                    std::cout << m3[i] << (i < 8 ? ", " : "]\n");
                }
                break;
            }
            case ParamType::eMat4: {
                float m4[16] = {};
                std::memcpy(m4, p.defaultValue.valueBuffer, sizeof(float) * 16);
                std::cout << "  Type: mat4, default=[";
                ;
                for (int i = 0; i < 16; ++i)
                {
                    std::cout << m4[i] << (i < 15 ? ", " : "]\n");
                }
                break;
            }
        }
    }

    for (const auto& t : r2.value().materialDesc.textures)
    {
        std::cout << "Texture: " << t.name << ", type=" << static_cast<uint32_t>(t.type)
                  << ", semantic=" << static_cast<uint32_t>(t.semantic) << "\n";
    }

    for (const auto& d : r2.value().reflection.descriptors)
    {
        std::cout << "Descriptor: " << d.name << ", set=" << d.set << ", binding=" << d.binding << ", count=" << d.count
                  << ", runtimeSized=" << d.runtimeSized << ", kind=" << static_cast<uint32_t>(d.kind) << "\n";
    }

    for (const auto& b : r2.value().reflection.blocks)
    {
        std::cout << "Block: " << b.name << ", set=" << b.set << ", binding=" << b.binding << ", size=" << b.size
                  << ", isPushConstant=" << b.isPushConstant << "\n";
        for (const auto& m : b.members)
        {
            std::cout << "  Member: " << m.name << ", offset=" << m.offset << ", size=" << m.size << "\n";
        }
    }

    // Render state
    std::cout << "Render state:\n";

    const auto& rs = r2.value().materialDesc.renderState;
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

    return 0;
}
