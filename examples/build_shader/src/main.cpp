#include <vshadersystem/binary.hpp>
#include <vshadersystem/system.hpp>

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

    return 0;
}
