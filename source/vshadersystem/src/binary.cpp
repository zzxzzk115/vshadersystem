#include "vshadersystem/binary.hpp"
#include "vshadersystem/hash.hpp"
#include "vshadersystem/types.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#include <process.h>
#define VSS_GETPID _getpid
#else
#include <unistd.h>
#define VSS_GETPID getpid
#endif

namespace vshadersystem
{
    static constexpr uint8_t  kMagic[8] = {'V', 'S', 'H', 'B', 'I', 'N', 0, 0};
    static constexpr uint32_t kVersion  = 2;

    static inline void write_u32(std::vector<uint8_t>& out, uint32_t v)
    {
        uint8_t b[4];
        std::memcpy(b, &v, 4);
        out.insert(out.end(), b, b + 4);
    }

    static inline void write_u64(std::vector<uint8_t>& out, uint64_t v)
    {
        uint8_t b[8];
        std::memcpy(b, &v, 8);
        out.insert(out.end(), b, b + 8);
    }

    static inline void write_u8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

    static inline bool read_u32(const uint8_t*& p, const uint8_t* e, uint32_t& v)
    {
        if (p + 4 > e)
            return false;
        std::memcpy(&v, p, 4);
        p += 4;
        return true;
    }

    static inline bool read_u64(const uint8_t*& p, const uint8_t* e, uint64_t& v)
    {
        if (p + 8 > e)
            return false;
        std::memcpy(&v, p, 8);
        p += 8;
        return true;
    }

    static inline bool read_u8(const uint8_t*& p, const uint8_t* e, uint8_t& v)
    {
        if (p + 1 > e)
            return false;
        v = *p++;
        return true;
    }

    static inline void write_bytes(std::vector<uint8_t>& out, const void* data, size_t n)
    {
        const uint8_t* b = static_cast<const uint8_t*>(data);
        out.insert(out.end(), b, b + n);
    }

    static inline void write_string(std::vector<uint8_t>& out, const std::string& s)
    {
        write_u32(out, static_cast<uint32_t>(s.size()));
        write_bytes(out, s.data(), s.size());
    }

    static inline bool read_string(const uint8_t*& p, const uint8_t* e, std::string& out)
    {
        uint32_t n = 0;
        if (!read_u32(p, e, n))
            return false;
        if (p + n > e)
            return false;
        out.assign(reinterpret_cast<const char*>(p), reinterpret_cast<const char*>(p + n));
        p += n;
        return true;
    }

    static uint32_t tag_u32(const char t[4])
    {
        uint32_t v;
        std::memcpy(&v, t, 4);
        return v;
    }

    // ------------------------------------------------------------
    // REFL chunk
    // ------------------------------------------------------------
    static std::vector<uint8_t> serialize_reflection(const ShaderReflection& r)
    {
        std::vector<uint8_t> out;

        write_u32(out, static_cast<uint32_t>(r.descriptors.size()));
        for (const auto& d : r.descriptors)
        {
            write_string(out, d.name);
            write_u32(out, d.set);
            write_u32(out, d.binding);
            write_u32(out, d.count);
            write_u8(out, static_cast<uint8_t>(d.kind));
            write_u32(out, d.stageFlags);
            write_u8(out, static_cast<uint8_t>(d.runtimeSized ? 1 : 0));
        }

        write_u32(out, static_cast<uint32_t>(r.blocks.size()));
        for (const auto& b : r.blocks)
        {
            write_string(out, b.name);
            write_u32(out, b.set);
            write_u32(out, b.binding);
            write_u32(out, b.size);
            write_u8(out, static_cast<uint8_t>(b.isPushConstant ? 1 : 0));
            write_u32(out, b.stageFlags);

            write_u32(out, static_cast<uint32_t>(b.members.size()));
            for (const auto& m : b.members)
            {
                write_string(out, m.name);
                write_u32(out, m.offset);
                write_u32(out, m.size);
            }
        }

        return out;
    }

    static Result<ShaderReflection> deserialize_reflection(const uint8_t* p0, size_t n)
    {
        const uint8_t* p = p0;
        const uint8_t* e = p0 + n;

        ShaderReflection r;

        uint32_t descCount = 0;
        if (!read_u32(p, e, descCount))
            return Result<ShaderReflection>::err(
                {ErrorCode::eDeserializeError, "REFL: failed to read descriptor count."});

        r.descriptors.reserve(descCount);
        for (uint32_t i = 0; i < descCount; ++i)
        {
            DescriptorBinding d;
            if (!read_string(p, e, d.name))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read descriptor name."});
            if (!read_u32(p, e, d.set))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read descriptor set."});
            if (!read_u32(p, e, d.binding))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read descriptor binding."});
            if (!read_u32(p, e, d.count))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read descriptor count."});
            uint8_t kind = 0;
            if (!read_u8(p, e, kind))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read descriptor kind."});
            d.kind = static_cast<DescriptorKind>(kind);

            uint32_t stageFlags = 0;
            if (!read_u32(p, e, stageFlags))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read descriptor stage flags."});
            d.stageFlags = stageFlags;

            uint8_t runtimeSized = 0;
            if (!read_u8(p, e, runtimeSized))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read descriptor runtime sized flag."});
            d.runtimeSized = (runtimeSized != 0);

            r.descriptors.push_back(std::move(d));
        }

        uint32_t blockCount = 0;
        if (!read_u32(p, e, blockCount))
            return Result<ShaderReflection>::err({ErrorCode::eDeserializeError, "REFL: failed to read block count."});

        r.blocks.reserve(blockCount);
        for (uint32_t i = 0; i < blockCount; ++i)
        {
            BlockLayout b;
            if (!read_string(p, e, b.name))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read block name."});
            if (!read_u32(p, e, b.set))
                return Result<ShaderReflection>::err({ErrorCode::eDeserializeError, "REFL: failed to read block set."});
            if (!read_u32(p, e, b.binding))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read block binding."});
            if (!read_u32(p, e, b.size))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read block size."});
            uint8_t isPush = 0;
            if (!read_u8(p, e, isPush))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read block push flag."});
            b.isPushConstant = (isPush != 0);

            uint32_t stageFlags = 0;
            if (!read_u32(p, e, stageFlags))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read block stage flags."});
            b.stageFlags = stageFlags;

            uint32_t memberCount = 0;
            if (!read_u32(p, e, memberCount))
                return Result<ShaderReflection>::err(
                    {ErrorCode::eDeserializeError, "REFL: failed to read member count."});
            b.members.reserve(memberCount);
            for (uint32_t mi = 0; mi < memberCount; ++mi)
            {
                BlockMember m;
                if (!read_string(p, e, m.name))
                    return Result<ShaderReflection>::err(
                        {ErrorCode::eDeserializeError, "REFL: failed to read member name."});
                if (!read_u32(p, e, m.offset))
                    return Result<ShaderReflection>::err(
                        {ErrorCode::eDeserializeError, "REFL: failed to read member offset."});
                if (!read_u32(p, e, m.size))
                    return Result<ShaderReflection>::err(
                        {ErrorCode::eDeserializeError, "REFL: failed to read member size."});
                b.members.push_back(std::move(m));
            }

            r.blocks.push_back(std::move(b));
        }

        if (p != e)
        {
            // We tolerate extra bytes for forward compatibility in chunk payloads,
            // but in v1 it is safer to report an error.
            return Result<ShaderReflection>::err({ErrorCode::eDeserializeError, "REFL: trailing bytes detected."});
        }

        return Result<ShaderReflection>::ok(std::move(r));
    }

    // ------------------------------------------------------------
    // MDES chunk
    // ------------------------------------------------------------
    static std::vector<uint8_t> serialize_mdesc(const MaterialDescription& m)
    {
        std::vector<uint8_t> out;

        write_string(out, m.materialBlockName);
        write_u32(out, m.materialParamSize);

        // render state
        write_u8(out, m.renderState.depthTest ? 1 : 0);
        write_u8(out, m.renderState.depthWrite ? 1 : 0);
        write_u8(out, static_cast<uint8_t>(m.renderState.depthFunc));

        write_u8(out, static_cast<uint8_t>(m.renderState.cull));

        write_u8(out, m.renderState.blendEnable ? 1 : 0);

        write_u8(out, static_cast<uint8_t>(m.renderState.srcColor));
        write_u8(out, static_cast<uint8_t>(m.renderState.dstColor));
        write_u8(out, static_cast<uint8_t>(m.renderState.colorOp));

        write_u8(out, static_cast<uint8_t>(m.renderState.srcAlpha));
        write_u8(out, static_cast<uint8_t>(m.renderState.dstAlpha));
        write_u8(out, static_cast<uint8_t>(m.renderState.alphaOp));

        write_u8(out, m.renderState.colorMask);

        write_u8(out, m.renderState.alphaToCoverage ? 1 : 0);

        write_bytes(out, &m.renderState.depthBiasFactor, sizeof(float));
        write_bytes(out, &m.renderState.depthBiasUnits, sizeof(float));

        write_u32(out, static_cast<uint32_t>(m.params.size()));
        for (const auto& p : m.params)
        {
            write_string(out, p.name);
            write_u8(out, static_cast<uint8_t>(p.type));
            write_u32(out, p.offset);
            write_u32(out, p.size);
            write_u32(out, static_cast<uint32_t>(p.semantic));

            write_u8(out, static_cast<uint8_t>(p.hasDefault ? 1 : 0));
            if (p.hasDefault)
            {
                write_u8(out, static_cast<uint8_t>(p.defaultValue.type));
                write_bytes(out, p.defaultValue.valueBuffer, sizeof(p.defaultValue.valueBuffer));
            }

            write_u8(out, static_cast<uint8_t>(p.hasRange ? 1 : 0));
            if (p.hasRange)
            {
                write_bytes(out, &p.range.min, sizeof(double));
                write_bytes(out, &p.range.max, sizeof(double));
            }
        }

        write_u32(out, static_cast<uint32_t>(m.textures.size()));
        for (const auto& t : m.textures)
        {
            write_string(out, t.name);
            write_u8(out, static_cast<uint8_t>(t.type));
            write_u32(out, t.set);
            write_u32(out, t.binding);
            write_u32(out, t.count);
            write_u32(out, static_cast<uint32_t>(t.semantic));
        }

        return out;
    }

    static Result<MaterialDescription> deserialize_mdesc(const uint8_t* p0, size_t n)
    {
        const uint8_t* p = p0;
        const uint8_t* e = p0 + n;

        MaterialDescription m;
        if (!read_string(p, e, m.materialBlockName))
            return Result<MaterialDescription>::err(
                {ErrorCode::eDeserializeError, "MDES: failed to read material block name."});
        if (!read_u32(p, e, m.materialParamSize))
            return Result<MaterialDescription>::err(
                {ErrorCode::eDeserializeError, "MDES: failed to read material param size."});

        uint8_t depthTest  = 1;
        uint8_t depthWrite = 1;
        uint8_t depthFunc  = 0;
        uint8_t cull       = 0;

        uint8_t blendEnable = 0;
        uint8_t srcColor    = 0;
        uint8_t dstColor    = 0;
        uint8_t colorOp     = 0;

        uint8_t srcAlpha = 0;
        uint8_t dstAlpha = 0;
        uint8_t alphaOp  = 0;

        uint8_t colorMask = 0;

        uint8_t alphaToCoverage = 0;

        float depthBiasFactor = 0.0f;
        float depthBiasUnits  = 0.0f;

        if (!read_u8(p, e, depthTest))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read depthTest."});

        if (!read_u8(p, e, depthWrite))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read depthWrite."});

        if (!read_u8(p, e, depthFunc))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read depthFunc."});

        if (!read_u8(p, e, cull))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read cull."});

        if (!read_u8(p, e, blendEnable))
            return Result<MaterialDescription>::err(
                {ErrorCode::eDeserializeError, "MDES: failed to read blendEnable."});

        if (!read_u8(p, e, srcColor))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read srcColor."});

        if (!read_u8(p, e, dstColor))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read dstColor."});

        if (!read_u8(p, e, colorOp))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read colorOp."});

        if (!read_u8(p, e, srcAlpha))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read srcAlpha."});

        if (!read_u8(p, e, dstAlpha))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read dstAlpha."});

        if (!read_u8(p, e, alphaOp))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read alphaOp."});

        if (!read_u8(p, e, colorMask))
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read colorMask."});

        if (!read_u8(p, e, alphaToCoverage))
            return Result<MaterialDescription>::err(
                {ErrorCode::eDeserializeError, "MDES: failed to read alphaToCoverage."});

        if (p + sizeof(float) * 2 > e)
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: failed to read depthBias."});

        std::memcpy(&depthBiasFactor, p, sizeof(float));
        p += sizeof(float);

        std::memcpy(&depthBiasUnits, p, sizeof(float));
        p += sizeof(float);

        m.renderState.depthTest  = (depthTest != 0);
        m.renderState.depthWrite = (depthWrite != 0);
        m.renderState.depthFunc  = static_cast<CompareOp>(depthFunc);

        m.renderState.cull = static_cast<CullMode>(cull);

        m.renderState.blendEnable = (blendEnable != 0);

        m.renderState.srcColor = static_cast<BlendFactor>(srcColor);
        m.renderState.dstColor = static_cast<BlendFactor>(dstColor);
        m.renderState.colorOp  = static_cast<BlendOp>(colorOp);

        m.renderState.srcAlpha = static_cast<BlendFactor>(srcAlpha);
        m.renderState.dstAlpha = static_cast<BlendFactor>(dstAlpha);
        m.renderState.alphaOp  = static_cast<BlendOp>(alphaOp);

        m.renderState.colorMask = static_cast<ColorMaskFlags>(colorMask);

        m.renderState.alphaToCoverage = (alphaToCoverage != 0);

        m.renderState.depthBiasFactor = depthBiasFactor;
        m.renderState.depthBiasUnits  = depthBiasUnits;

        uint32_t paramCount = 0;
        if (!read_u32(p, e, paramCount))
            return Result<MaterialDescription>::err(
                {ErrorCode::eDeserializeError, "MDES: failed to read param count."});

        m.params.reserve(paramCount);
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            MaterialParamDesc pd;
            if (!read_string(p, e, pd.name))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read param name."});
            uint8_t t = 0;
            if (!read_u8(p, e, t))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read param type."});
            pd.type = static_cast<ParamType>(t);
            if (!read_u32(p, e, pd.offset))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read param offset."});
            if (!read_u32(p, e, pd.size))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read param size."});
            uint32_t sem = 0;
            if (!read_u32(p, e, sem))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read param semantic."});
            pd.semantic = static_cast<Semantic>(sem);

            uint8_t hasDef = 0;
            if (!read_u8(p, e, hasDef))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read hasDefault."});
            pd.hasDefault = (hasDef != 0);
            if (pd.hasDefault)
            {
                uint8_t dt = 0;
                if (!read_u8(p, e, dt))
                    return Result<MaterialDescription>::err(
                        {ErrorCode::eDeserializeError, "MDES: failed to read default type."});
                pd.defaultValue.type = static_cast<ParamType>(dt);
                if (p + sizeof(pd.defaultValue.valueBuffer) > e)
                    return Result<MaterialDescription>::err(
                        {ErrorCode::eDeserializeError, "MDES: failed to read default values."});
                std::memcpy(pd.defaultValue.valueBuffer, p, sizeof(pd.defaultValue.valueBuffer));
                p += sizeof(pd.defaultValue.valueBuffer);
            }

            uint8_t hasRange = 0;
            if (!read_u8(p, e, hasRange))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read hasRange."});
            pd.hasRange = (hasRange != 0);
            if (pd.hasRange)
            {
                if (p + sizeof(double) * 2 > e)
                    return Result<MaterialDescription>::err(
                        {ErrorCode::eDeserializeError, "MDES: failed to read range."});
                std::memcpy(&pd.range.min, p, sizeof(double));
                p += sizeof(double);
                std::memcpy(&pd.range.max, p, sizeof(double));
                p += sizeof(double);
            }

            m.params.push_back(std::move(pd));
        }

        uint32_t texCount = 0;
        if (!read_u32(p, e, texCount))
            return Result<MaterialDescription>::err(
                {ErrorCode::eDeserializeError, "MDES: failed to read texture count."});

        m.textures.reserve(texCount);
        for (uint32_t i = 0; i < texCount; ++i)
        {
            MaterialTextureDesc td;
            if (!read_string(p, e, td.name))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read texture name."});
            uint8_t tt = 0;
            if (!read_u8(p, e, tt))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read texture type."});
            td.type = static_cast<TextureType>(tt);
            if (!read_u32(p, e, td.set))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read texture set."});
            if (!read_u32(p, e, td.binding))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read texture binding."});
            if (!read_u32(p, e, td.count))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read texture count."});
            uint32_t sem = 0;
            if (!read_u32(p, e, sem))
                return Result<MaterialDescription>::err(
                    {ErrorCode::eDeserializeError, "MDES: failed to read texture semantic."});
            td.semantic = static_cast<Semantic>(sem);

            m.textures.push_back(std::move(td));
        }

        if (p != e)
            return Result<MaterialDescription>::err({ErrorCode::eDeserializeError, "MDES: trailing bytes detected."});

        return Result<MaterialDescription>::ok(std::move(m));
    }

    // ------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------
    Result<std::vector<uint8_t>> write_vshbin(const ShaderBinary& bin)
    {
        if (bin.spirv.empty())
            return Result<std::vector<uint8_t>>::err(
                {ErrorCode::eSerializeError, "Cannot write .vshbin with empty SPIR-V."});

        std::vector<uint8_t> out;
        out.reserve(256 + bin.spirv.size() * 4);

        // Header
        write_bytes(out, kMagic, sizeof(kMagic));
        write_u32(out, kVersion);

        // Store stage in flags (low 8 bits). This keeps the header 32 bytes as intended.
        uint32_t flags = 0; // flags reserved
        flags          = (flags & ~0xFFu) | static_cast<uint32_t>(static_cast<uint8_t>(bin.stage));
        write_u32(out, flags);

        write_u64(out, bin.contentHash);
        write_u64(out, bin.spirvHash);

        // pad to 32 bytes header
        while (out.size() < 32)
            out.push_back(0);

        // Chunks
        auto write_chunk = [&](const char tag[4], const std::vector<uint8_t>& payload) {
            write_u32(out, tag_u32(tag));
            write_u32(out, static_cast<uint32_t>(payload.size()));
            write_bytes(out, payload.data(), payload.size());
        };

        // SIDH (optional, v2+): stable logical shader id hash for runtime lookup
        if (bin.shaderIdHash != 0)
        {
            std::vector<uint8_t> sidh;
            sidh.reserve(8);
            write_u64(sidh, bin.shaderIdHash);
            write_chunk("SIDH", sidh);
        }

        // VKEY (optional)
        if (bin.variantHash != 0)
        {
            std::vector<uint8_t> vkey;
            vkey.reserve(8);
            write_u64(vkey, bin.variantHash);
            write_chunk("VKEY", vkey);
        }

        // SPRV
        std::vector<uint8_t> sprvPayload;
        sprvPayload.resize(bin.spirv.size() * sizeof(uint32_t));
        std::memcpy(sprvPayload.data(), bin.spirv.data(), sprvPayload.size());
        write_chunk("SPRV", sprvPayload);

        // REFL
        write_chunk("REFL", serialize_reflection(bin.reflection));

        // MDES
        write_chunk("MDES", serialize_mdesc(bin.materialDesc));

        return Result<std::vector<uint8_t>>::ok(std::move(out));
    }

    Result<ShaderBinary> read_vshbin(const std::vector<uint8_t>& bytes)
    {
        if (bytes.size() < 32)
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "File too small to be a valid .vshbin."});

        const uint8_t* p = bytes.data();
        const uint8_t* e = bytes.data() + bytes.size();

        if (std::memcmp(p, kMagic, sizeof(kMagic)) != 0)
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Invalid magic header (not a .vshbin)."});

        p += sizeof(kMagic);

        uint32_t version     = 0;
        uint32_t flags       = 0;
        uint64_t contentHash = 0;
        uint64_t spirvHash   = 0;

        if (!read_u32(p, e, version))
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Failed to read version."});

        if (version < 1 || version > kVersion)
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Unsupported .vshbin version."});

        if (!read_u32(p, e, flags))
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Failed to read flags."});

        if (!read_u64(p, e, contentHash))
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Failed to read contentHash."});

        if (!read_u64(p, e, spirvHash))
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Failed to read spirvHash."});

        // Skip padding to fixed 32-byte header
        constexpr size_t headerSize = 32;
        constexpr size_t headerRead = sizeof(kMagic) + 4 + 4 + 8 + 8;

        static_assert(headerRead == 32, "Header layout mismatch");

        ShaderBinary out;

        out.contentHash = contentHash;
        out.spirvHash   = spirvHash;

        out.stage = static_cast<ShaderStage>(flags & 0xFF);

        bool hasVKEY = false;
        bool hasSPRV = false;
        bool hasREFL = false;
        bool hasMDES = false;

        while (p < e)
        {
            uint32_t tag;
            uint32_t size;

            if (!read_u32(p, e, tag))
                return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Failed to read chunk tag."});

            if (!read_u32(p, e, size))
                return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Failed to read chunk size."});

            if (p + size > e)
                return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Chunk size exceeds file bounds."});

            const uint8_t* payload = p;

            p += size;

            if (tag == tag_u32("SIDH"))
            {
                if (size != 8)
                    return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "SIDH chunk size invalid."});

                uint64_t       sh = 0;
                const uint8_t* p2 = payload;
                const uint8_t* e2 = payload + size;
                if (!read_u64(p2, e2, sh) || p2 != e2)
                    return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Failed to read SIDH chunk."});

                out.shaderIdHash = sh;
            }
            else if (tag == tag_u32("VKEY"))
            {
                if (size != 8)
                    return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "VKEY chunk size invalid."});

                uint64_t       vh = 0;
                const uint8_t* p2 = payload;
                const uint8_t* e2 = payload + size;
                if (!read_u64(p2, e2, vh) || p2 != e2)
                    return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Failed to read VKEY chunk."});

                out.variantHash = vh;
                hasVKEY         = true;
            }
            else if (tag == tag_u32("SPRV"))
            {
                if (size % 4 != 0)
                    return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "SPRV chunk size not aligned."});

                out.spirv.resize(size / 4);

                std::memcpy(out.spirv.data(), payload, size);

                hasSPRV = true;
            }
            else if (tag == tag_u32("REFL"))
            {
                auto rr = deserialize_reflection(payload, size);

                if (!rr.isOk())
                    return Result<ShaderBinary>::err(rr.error());

                out.reflection = std::move(rr.value());

                hasREFL = true;
            }
            else if (tag == tag_u32("MDES"))
            {
                auto mm = deserialize_mdesc(payload, size);

                if (!mm.isOk())
                    return Result<ShaderBinary>::err(mm.error());

                out.materialDesc = std::move(mm.value());

                hasMDES = true;
            }
            else
            {
                // Skip unknown chunks (forward compatibility)
            }
        }

        if (!hasSPRV)
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Missing SPRV chunk."});

        if (!hasREFL)
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Missing REFL chunk."});

        if (!hasMDES)
            return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "Missing MDES chunk."});

        (void)hasVKEY; // optional

        if (out.spirvHash != 0)
        {
            uint64_t computed = xxhash64_words(out.spirv);

            if (computed != out.spirvHash)
                return Result<ShaderBinary>::err({ErrorCode::eDeserializeError, "SPIR-V hash mismatch."});
        }

        return Result<ShaderBinary>::ok(std::move(out));
    }

    Result<void> write_vshbin_file(const std::string& path, const ShaderBinary& bin)
    {
        auto bytes = write_vshbin(bin);
        if (!bytes.isOk())
            return Result<void>::err(bytes.error());

        // Make sure the parent directory exists
        auto parentPath = std::filesystem::path(path).parent_path();
        if (!parentPath.empty())
            std::filesystem::create_directories(parentPath);

        // Production-grade atomic write:
        // write to a temp file then rename.
        const std::string tmpPath = path + ".tmp." + std::to_string(static_cast<uint64_t>(VSS_GETPID()));

        {
            std::ofstream f(tmpPath, std::ios::binary);
            if (!f)
                return Result<void>::err({ErrorCode::eIO, "Failed to open file for writing: " + tmpPath});

            f.write(reinterpret_cast<const char*>(bytes.value().data()),
                    static_cast<std::streamsize>(bytes.value().size()));
            if (!f)
                return Result<void>::err({ErrorCode::eIO, "Failed to write file: " + tmpPath});
        }

        std::error_code ec;
        std::filesystem::rename(tmpPath, path, ec);
        if (ec)
        {
            // Try replace existing (Windows compatibility)
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tmpPath, path, ec);
            if (ec)
            {
                std::filesystem::remove(tmpPath, ec);
                return Result<void>::err({ErrorCode::eIO, "Failed to rename temp file to: " + path});
            }
        }

        return Result<void>::ok();
    }

    Result<ShaderBinary> read_vshbin_file(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return Result<ShaderBinary>::err({ErrorCode::eIO, "Failed to open file: " + path});

        f.seekg(0, std::ios::end);
        const auto size = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        std::vector<uint8_t> bytes(size);
        f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (!f)
            return Result<ShaderBinary>::err({ErrorCode::eIO, "Failed to read file: " + path});

        f.close();

        return read_vshbin(bytes);
    }
} // namespace vshadersystem