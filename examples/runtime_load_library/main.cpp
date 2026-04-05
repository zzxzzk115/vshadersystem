#include <vshadersystem/binary.hpp>
#include <vshadersystem/engine_keywords.hpp>
#include <vshadersystem/library.hpp>
#include <vshadersystem/system.hpp>

#include <filesystem>
#include <iostream>
#include <string>

using namespace vshadersystem;

static void usage() { std::cout << "Usage: example_runtime_load_library <shaders.vshlib>\n"; }

static const char* descriptor_kind_name(DescriptorKind kind)
{
    switch (kind)
    {
        case DescriptorKind::eUniformBuffer:
            return "uniform-buffer";
        case DescriptorKind::eStorageBuffer:
            return "storage-buffer";
        case DescriptorKind::eSampledImage:
            return "sampled-image";
        case DescriptorKind::eStorageImage:
            return "storage-image";
        case DescriptorKind::eSampler:
            return "sampler";
        case DescriptorKind::eCombinedImageSampler:
            return "combined-image-sampler";
        case DescriptorKind::eAccelerationStructure:
            return "accel-struct";
        default:
            return "unknown";
    }
}

static const char* resource_access_name(ResourceAccess access)
{
    switch (access)
    {
        case ResourceAccess::eReadOnly:
            return "read-only";
        case ResourceAccess::eWriteOnly:
            return "write-only";
        case ResourceAccess::eReadWrite:
            return "read-write";
        default:
            return "unknown";
    }
}

static void print_reflection(const ShaderBinary& bin)
{
    std::cout << "  descriptors: " << bin.reflection.descriptors.size() << "\n";
    for (const auto& d : bin.reflection.descriptors)
    {
        std::cout << "    - " << d.name << " set=" << d.set << " binding=" << d.binding
                  << " kind=" << descriptor_kind_name(d.kind) << " access=" << resource_access_name(d.access)
                  << "\n";
    }

    std::cout << "  blocks: " << bin.reflection.blocks.size() << "\n";
    for (const auto& b : bin.reflection.blocks)
    {
        std::cout << "    - " << b.name << " set=" << b.set << " binding=" << b.binding << " size=" << b.size
                  << " access=" << resource_access_name(b.access) << "\n";
    }
}

int main()
{
    const std::string shaderSrc = R"([vshader]
language = glsl
version = 460

[compute]
#extension GL_EXT_scalar_block_layout : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) readonly buffer ReadOnlyData
{
    vec4 values[];
} g_ReadOnlyData;

layout(set = 0, binding = 1, std430) writeonly buffer WriteOnlyData
{
    vec4 values[];
} g_WriteOnlyData;

layout(set = 0, binding = 2, std430) buffer ReadWriteData
{
    vec4 values[];
} g_ReadWriteData;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    vec4 v = g_ReadOnlyData.values[idx];
    g_ReadWriteData.values[idx] = v;
    g_WriteOnlyData.values[idx] = v * vec4(0.5, 1.0, 1.5, 1.0);
}
)";

    const std::string libPath = "shaders/generated_runtime_test.vshlib";

    BuildRequest req;
    req.source.virtualPath = "runtime_ssbo_access.vshader";
    req.source.sourceText  = shaderSrc;
    req.options.language   = ShaderLanguage::eGLSL;

    auto built = build_multiple_shaders(req);
    if (!built.isOk())
    {
        std::cerr << "Failed to build runtime test shader library input: " << built.error().message << "\n";
        return 1;
    }

    std::vector<ShaderLibraryEntry> entries;
    entries.reserve(built.value().size());
    for (const auto& [stage, result] : built.value())
    {
        auto bytes = write_vshbin(result.binary);
        if (!bytes.isOk())
        {
            std::cerr << "Failed to serialize stage to vshbin: " << bytes.error().message << "\n";
            return 1;
        }

        entries.push_back(ShaderLibraryEntry {
            .keyHash = result.binary.variantHash,
            .stage   = stage,
            .blob    = std::move(bytes.value()),
        });
    }

    std::filesystem::create_directories(std::filesystem::path(libPath).parent_path());
    auto wr = write_vslib(libPath, entries, nullptr);
    if (!wr.isOk())
    {
        std::cerr << "Failed to write vshlib: " << wr.error().message << "\n";
        return 1;
    }

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

    for (const auto& e : lib.entries)
    {
        auto blobR = extract_vshlib_blob(lib, e.keyHash, e.stage);
        if (!blobR.isOk())
        {
            std::cerr << "Failed to extract embedded vshbin: " << blobR.error().message << "\n";
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
        std::cout << "  stage:       " << static_cast<uint32_t>(bin.stage) << "\n";
        std::cout << "  shaderIdHash:" << bin.shaderIdHash << "\n";
        std::cout << "  variantHash: " << bin.variantHash << "\n";
        std::cout << "  spirv words: " << bin.spirv.size() << "\n";
        print_reflection(bin);
    }

    return 0;
}
