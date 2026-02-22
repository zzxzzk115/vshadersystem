#include <vshadersystem/binary.hpp>
#include <vshadersystem/engine_keywords.hpp>
#include <vshadersystem/library.hpp>
#include <vshadersystem/shader_id.hpp>
#include <vshadersystem/variant_key.hpp>

#include <iostream>
#include <string>

using namespace vshadersystem;

static void usage() { std::cout << "Usage: example_runtime_load_library <shaders.vshlib>\n"; }

int main()
{
    const std::string libPath = "shaders/shaders.vshlib";

    auto lr = read_vshlib_file(libPath);
    if (!lr.isOk())
    {
        std::cerr << "Failed to load vshlib: " << lr.error().message << "\n";
        return 2;
    }

    const auto& lib = lr.value();

    // Print library contents
    std::cout << "Loaded shader library: " << libPath << "\n";
    std::cout << "  Entries: " << lib.entries.size() << "\n";
    for (const auto& e : lib.entries)
    {
        std::cout << "    keyHash=" << e.keyHash << ", stage=" << static_cast<uint32_t>(e.stage)
                  << ", offset=" << e.offset << ", size=" << e.size << "\n";
    }

    // Example: shader id derived from path at cook time:
    // shaders/pbr.frag.vshader -> "pbr.frag"
    const std::string shaderId = "pbr.frag";

    VariantKey key;
    key.setShaderId(shaderId);
    key.setStage(ShaderStage::eFrag);

    // Example permutation keyword set
    key.set("USE_SHADOW", 1);
    key.set("PASS", 0);

    const uint64_t variantHash = key.build();

    auto blobR = extract_vshlib_blob(lib, variantHash, ShaderStage::eFrag);
    if (!blobR.isOk())
    {
        std::cerr << "Variant not found. shaderId=" << shaderId << " hash=" << variantHash << "\n";
        return 4;
    }

    auto br = read_vshbin(blobR.value());
    if (!br.isOk())
    {
        std::cerr << "Failed to parse embedded vshbin: " << br.error().message << "\n";
        return 5;
    }

    const auto& bin = br.value();

    std::cout << "OK:\n";
    std::cout << "  shaderIdHash: " << bin.shaderIdHash << "\n";
    std::cout << "  variantHash:  " << bin.variantHash << "\n";
    std::cout << "  spirv words:  " << bin.spirv.size() << "\n";
    return 0;
}
