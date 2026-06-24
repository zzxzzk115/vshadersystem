#include "vshadersystem/vsh_format.hpp"

#include "vshadersystem/hash.hpp"

#include <algorithm>
#include <cstring>
#include <functional>

namespace vshadersystem::v1
{
    namespace
    {
        constexpr char     kBinMagic[8] = {'V', 'S', 'H', 'B', '1', 0, 0, 0};
        constexpr char     kLibMagic[8] = {'V', 'S', 'H', 'L', '1', 0, 0, 0};
        constexpr char     kSrcMagic[8] = {'V', 'S', 'H', 'S', '1', 0, 0, 0};
        constexpr uint32_t kVersion     = 1;

        // ---- little-endian byte writer ----
        struct Writer
        {
            std::vector<uint8_t> buf;

            void u8v(uint8_t v) { buf.push_back(v); }
            void u16v(uint16_t v)
            {
                buf.push_back(uint8_t(v));
                buf.push_back(uint8_t(v >> 8));
            }
            void u32v(uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                    buf.push_back(uint8_t(v >> (8 * i)));
            }
            void u64v(uint64_t v)
            {
                for (int i = 0; i < 8; ++i)
                    buf.push_back(uint8_t(v >> (8 * i)));
            }
            void f32v(float v)
            {
                uint32_t u;
                std::memcpy(&u, &v, 4);
                u32v(u);
            }
            void f64v(double v)
            {
                uint64_t u;
                std::memcpy(&u, &v, 8);
                u64v(u);
            }
            void bytes(const void* p, size_t n)
            {
                const auto* b = static_cast<const uint8_t*>(p);
                buf.insert(buf.end(), b, b + n);
            }
            void str(const std::string& s)
            {
                u32v(static_cast<uint32_t>(s.size()));
                bytes(s.data(), s.size());
            }
        };

        // ---- little-endian byte reader with bounds checks ----
        struct Reader
        {
            const uint8_t* p   = nullptr;
            size_t         n   = 0;
            size_t         pos = 0;
            bool           bad = false;

            bool need(size_t k)
            {
                if (bad || pos + k > n)
                {
                    bad = true;
                    return false;
                }
                return true;
            }
            uint8_t u8v() { return need(1) ? p[pos++] : 0; }
            uint16_t u16v()
            {
                if (!need(2))
                    return 0;
                uint16_t v = uint16_t(p[pos]) | (uint16_t(p[pos + 1]) << 8);
                pos += 2;
                return v;
            }
            uint32_t u32v()
            {
                if (!need(4))
                    return 0;
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(p[pos + i]) << (8 * i);
                pos += 4;
                return v;
            }
            uint64_t u64v()
            {
                if (!need(8))
                    return 0;
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= uint64_t(p[pos + i]) << (8 * i);
                pos += 8;
                return v;
            }
            float f32v()
            {
                uint32_t u = u32v();
                float    v;
                std::memcpy(&v, &u, 4);
                return v;
            }
            double f64v()
            {
                uint64_t u = u64v();
                double   v;
                std::memcpy(&v, &u, 8);
                return v;
            }
            std::string str()
            {
                uint32_t len = u32v();
                if (!need(len))
                    return {};
                std::string s(reinterpret_cast<const char*>(p + pos), len);
                pos += len;
                return s;
            }
        };

        // ---- reflection / material serialization ----
        void write_reflection(Writer& w, const ShaderReflection& r)
        {
            w.u32v(static_cast<uint32_t>(r.descriptors.size()));
            for (const auto& d : r.descriptors)
            {
                w.str(d.name);
                w.u32v(d.set);
                w.u32v(d.binding);
                w.u32v(d.count);
                w.u8v(static_cast<uint8_t>(d.kind));
                w.u8v(static_cast<uint8_t>(d.access));
                w.u32v(d.stageFlags);
                w.u8v(d.runtimeSized ? 1 : 0);
                w.u8v(static_cast<uint8_t>(d.textureType));
            }
            w.u32v(static_cast<uint32_t>(r.blocks.size()));
            for (const auto& b : r.blocks)
            {
                w.str(b.name);
                w.u32v(b.set);
                w.u32v(b.binding);
                w.u32v(b.size);
                w.u8v(b.isPushConstant ? 1 : 0);
                w.u8v(static_cast<uint8_t>(b.access));
                w.u32v(b.stageFlags);
                w.u32v(static_cast<uint32_t>(b.members.size()));
                for (const auto& m : b.members)
                {
                    w.str(m.name);
                    w.u32v(m.offset);
                    w.u32v(m.size);
                    w.u8v(static_cast<uint8_t>(m.type));
                }
            }
            w.u8v(r.hasLocalSize ? 1 : 0);
            w.u32v(r.localSizeX);
            w.u32v(r.localSizeY);
            w.u32v(r.localSizeZ);
        }

        void read_reflection(Reader& r, ShaderReflection& out)
        {
            uint32_t dc = r.u32v();
            for (uint32_t i = 0; i < dc && !r.bad; ++i)
            {
                DescriptorBinding d;
                d.name         = r.str();
                d.set          = r.u32v();
                d.binding      = r.u32v();
                d.count        = r.u32v();
                d.kind         = static_cast<DescriptorKind>(r.u8v());
                d.access       = static_cast<ResourceAccess>(r.u8v());
                d.stageFlags   = r.u32v();
                d.runtimeSized = r.u8v() != 0;
                d.textureType  = static_cast<TextureType>(r.u8v());
                out.descriptors.push_back(std::move(d));
            }
            uint32_t bc = r.u32v();
            for (uint32_t i = 0; i < bc && !r.bad; ++i)
            {
                BlockLayout b;
                b.name           = r.str();
                b.set            = r.u32v();
                b.binding        = r.u32v();
                b.size           = r.u32v();
                b.isPushConstant = r.u8v() != 0;
                b.access         = static_cast<ResourceAccess>(r.u8v());
                b.stageFlags     = r.u32v();
                uint32_t mc      = r.u32v();
                for (uint32_t j = 0; j < mc && !r.bad; ++j)
                {
                    BlockMember m;
                    m.name   = r.str();
                    m.offset = r.u32v();
                    m.size   = r.u32v();
                    m.type   = static_cast<ParamType>(r.u8v());
                    b.members.push_back(std::move(m));
                }
                out.blocks.push_back(std::move(b));
            }
            out.hasLocalSize = r.u8v() != 0;
            out.localSizeX   = r.u32v();
            out.localSizeY   = r.u32v();
            out.localSizeZ   = r.u32v();
        }

        void write_material(Writer& w, const MaterialDescription& m)
        {
            w.str(m.materialBlockName);
            w.u32v(m.materialParamSize);
            const RenderState& rs = m.renderState;
            w.u8v(rs.depthTest ? 1 : 0);
            w.u8v(rs.depthWrite ? 1 : 0);
            w.u8v(static_cast<uint8_t>(rs.depthFunc));
            w.u8v(static_cast<uint8_t>(rs.cull));
            w.u8v(rs.blendEnable ? 1 : 0);
            w.u8v(static_cast<uint8_t>(rs.srcColor));
            w.u8v(static_cast<uint8_t>(rs.dstColor));
            w.u8v(static_cast<uint8_t>(rs.colorOp));
            w.u8v(static_cast<uint8_t>(rs.srcAlpha));
            w.u8v(static_cast<uint8_t>(rs.dstAlpha));
            w.u8v(static_cast<uint8_t>(rs.alphaOp));
            w.u8v(rs.colorMask);
            w.u8v(rs.alphaToCoverage ? 1 : 0);
            w.f32v(rs.depthBiasFactor);
            w.f32v(rs.depthBiasUnits);

            w.u32v(static_cast<uint32_t>(m.params.size()));
            for (const auto& p : m.params)
            {
                w.str(p.name);
                w.u8v(static_cast<uint8_t>(p.type));
                w.u32v(p.offset);
                w.u32v(p.size);
                w.u16v(static_cast<uint16_t>(p.semantic));
                w.u8v(p.hasDefault ? 1 : 0);
                if (p.hasDefault)
                {
                    w.u8v(static_cast<uint8_t>(p.defaultValue.type));
                    w.bytes(p.defaultValue.valueBuffer, sizeof(p.defaultValue.valueBuffer));
                }
                w.u8v(p.hasRange ? 1 : 0);
                w.f64v(p.range.min);
                w.f64v(p.range.max);
                w.u32v(static_cast<uint32_t>(p.enumOptions.size()));
                for (const auto& e : p.enumOptions)
                {
                    w.str(e.label);
                    w.u32v(static_cast<uint32_t>(e.value));
                }
            }
            w.u32v(static_cast<uint32_t>(m.textures.size()));
            for (const auto& t : m.textures)
            {
                w.str(t.name);
                w.u8v(static_cast<uint8_t>(t.type));
                w.u32v(t.set);
                w.u32v(t.binding);
                w.u32v(t.count);
                w.u16v(static_cast<uint16_t>(t.semantic));
            }
        }

        void read_material(Reader& r, MaterialDescription& m)
        {
            m.materialBlockName = r.str();
            m.materialParamSize = r.u32v();
            RenderState& rs     = m.renderState;
            rs.depthTest        = r.u8v() != 0;
            rs.depthWrite       = r.u8v() != 0;
            rs.depthFunc        = static_cast<CompareOp>(r.u8v());
            rs.cull             = static_cast<CullMode>(r.u8v());
            rs.blendEnable      = r.u8v() != 0;
            rs.srcColor         = static_cast<BlendFactor>(r.u8v());
            rs.dstColor         = static_cast<BlendFactor>(r.u8v());
            rs.colorOp          = static_cast<BlendOp>(r.u8v());
            rs.srcAlpha         = static_cast<BlendFactor>(r.u8v());
            rs.dstAlpha         = static_cast<BlendFactor>(r.u8v());
            rs.alphaOp          = static_cast<BlendOp>(r.u8v());
            rs.colorMask        = r.u8v();
            rs.alphaToCoverage  = r.u8v() != 0;
            rs.depthBiasFactor  = r.f32v();
            rs.depthBiasUnits   = r.f32v();

            uint32_t pc = r.u32v();
            for (uint32_t i = 0; i < pc && !r.bad; ++i)
            {
                MaterialParamDesc p;
                p.name     = r.str();
                p.type     = static_cast<ParamType>(r.u8v());
                p.offset   = r.u32v();
                p.size     = r.u32v();
                p.semantic = static_cast<Semantic>(r.u16v());
                p.hasDefault = r.u8v() != 0;
                if (p.hasDefault)
                {
                    p.defaultValue.type = static_cast<ParamType>(r.u8v());
                    if (r.need(sizeof(p.defaultValue.valueBuffer)))
                    {
                        std::memcpy(p.defaultValue.valueBuffer, r.p + r.pos, sizeof(p.defaultValue.valueBuffer));
                        r.pos += sizeof(p.defaultValue.valueBuffer);
                    }
                }
                p.hasRange  = r.u8v() != 0;
                p.range.min = r.f64v();
                p.range.max = r.f64v();
                uint32_t ec = r.u32v();
                for (uint32_t j = 0; j < ec && !r.bad; ++j)
                {
                    MaterialParamDesc::EnumOption e;
                    e.label = r.str();
                    e.value = static_cast<int32_t>(r.u32v());
                    p.enumOptions.push_back(std::move(e));
                }
                m.params.push_back(std::move(p));
            }
            uint32_t tc = r.u32v();
            for (uint32_t i = 0; i < tc && !r.bad; ++i)
            {
                MaterialTextureDesc t;
                t.name     = r.str();
                t.type     = static_cast<TextureType>(r.u8v());
                t.set      = r.u32v();
                t.binding  = r.u32v();
                t.count    = r.u32v();
                t.semantic = static_cast<Semantic>(r.u16v());
                m.textures.push_back(std::move(t));
            }
        }
    } // namespace

    Result<std::vector<uint8_t>> write_binary(const ShaderBinary& bin)
    {
        Writer w;
        w.bytes(kBinMagic, 8);
        w.u32v(kVersion);
        w.u32v(static_cast<uint32_t>(bin.stage) & 0xFFu);
        const uint64_t spirvHash = bin.spirvHash ? bin.spirvHash : xxhash64_words(bin.spirv);
        w.u64v(bin.contentHash);
        w.u64v(bin.shaderIdHash);
        w.u64v(bin.variantHash);
        w.u64v(spirvHash);

        // chunks
        auto chunk = [&](const char tag[4], const std::function<void(Writer&)>& body) {
            Writer c;
            body(c);
            w.bytes(tag, 4);
            w.u32v(static_cast<uint32_t>(c.buf.size()));
            w.bytes(c.buf.data(), c.buf.size());
        };
        if (!bin.spirv.empty())
            chunk("SPRV", [&](Writer& c) { c.bytes(bin.spirv.data(), bin.spirv.size() * sizeof(uint32_t)); });
        if (!bin.wgsl.empty())
            chunk("WGSL", [&](Writer& c) { c.bytes(bin.wgsl.data(), bin.wgsl.size()); });
        chunk("REFL", [&](Writer& c) { write_reflection(c, bin.reflection); });
        chunk("MDES", [&](Writer& c) { write_material(c, bin.materialDesc); });

        return Result<std::vector<uint8_t>>::ok(std::move(w.buf));
    }

    Result<ShaderBinary> read_binary(const std::vector<uint8_t>& bytes)
    {
        using R = Result<ShaderBinary>;
        Reader r{bytes.data(), bytes.size(), 0, false};
        char   magic[8];
        if (!r.need(8))
            return R::err({ErrorCode::eDeserializeError, "truncated header"});
        std::memcpy(magic, r.p + r.pos, 8);
        r.pos += 8;
        if (std::memcmp(magic, kBinMagic, 8) != 0)
            return R::err({ErrorCode::eDeserializeError, "bad magic (not a v1 .vshbin)"});
        uint32_t version = r.u32v();
        if (version != kVersion)
            return R::err({ErrorCode::eDeserializeError, "unsupported version " + std::to_string(version)});

        ShaderBinary bin;
        bin.stage        = static_cast<ShaderStage>(r.u32v() & 0xFFu);
        bin.contentHash  = r.u64v();
        bin.shaderIdHash = r.u64v();
        bin.variantHash  = r.u64v();
        bin.spirvHash    = r.u64v();

        while (!r.bad && r.pos + 8 <= r.n)
        {
            char tag[4];
            std::memcpy(tag, r.p + r.pos, 4);
            r.pos += 4;
            uint32_t size = r.u32v();
            if (!r.need(size))
                return R::err({ErrorCode::eDeserializeError, "truncated chunk"});
            const size_t chunkStart = r.pos;
            if (std::memcmp(tag, "SPRV", 4) == 0)
            {
                bin.spirv.assign(reinterpret_cast<const uint32_t*>(r.p + chunkStart),
                                 reinterpret_cast<const uint32_t*>(r.p + chunkStart) + size / sizeof(uint32_t));
            }
            else if (std::memcmp(tag, "WGSL", 4) == 0)
            {
                bin.wgsl.assign(reinterpret_cast<const char*>(r.p + chunkStart), size);
            }
            else if (std::memcmp(tag, "REFL", 4) == 0)
            {
                Reader cr{r.p + chunkStart, size, 0, false};
                read_reflection(cr, bin.reflection);
            }
            else if (std::memcmp(tag, "MDES", 4) == 0)
            {
                Reader cr{r.p + chunkStart, size, 0, false};
                read_material(cr, bin.materialDesc);
            }
            r.pos = chunkStart + size; // skip unknown chunks too
        }
        if (r.bad)
            return R::err({ErrorCode::eDeserializeError, "corrupt binary"});
        return R::ok(std::move(bin));
    }

    Result<std::vector<uint8_t>> write_library(const std::vector<LibraryEntry>& entriesIn,
                                               const std::vector<uint8_t>*      engineKeywords)
    {
        std::vector<LibraryEntry> entries = entriesIn;
        std::sort(entries.begin(), entries.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
            if (a.variantHash != b.variantHash)
                return a.variantHash < b.variantHash;
            return static_cast<uint8_t>(a.stage) < static_cast<uint8_t>(b.stage);
        });

        // Header layout: magic[8], u32 version, u32 entryCount, u64 tocOffset, u64 tocSize,
        //                u64 vkwOffset, u64 vkwSize. Then blobs, then TOC, then vkw.
        constexpr uint64_t kHeaderSize = 8 + 4 + 4 + 8 + 8 + 8 + 8;

        Writer blobs;
        struct Loc
        {
            uint64_t offset, size;
        };
        std::vector<Loc> locs;
        locs.reserve(entries.size());
        for (const auto& e : entries)
        {
            Loc l{kHeaderSize + blobs.buf.size(), e.blob.size()};
            blobs.bytes(e.blob.data(), e.blob.size());
            locs.push_back(l);
        }

        Writer toc;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            toc.u64v(entries[i].variantHash);
            toc.u8v(static_cast<uint8_t>(entries[i].stage));
            for (int p = 0; p < 7; ++p)
                toc.u8v(0);
            toc.u64v(locs[i].offset);
            toc.u64v(locs[i].size);
        }

        const uint64_t tocOffset = kHeaderSize + blobs.buf.size();
        const uint64_t tocSize   = toc.buf.size();
        const uint64_t vkwOffset = tocOffset + tocSize;
        const uint64_t vkwSize   = engineKeywords ? engineKeywords->size() : 0;

        Writer w;
        w.bytes(kLibMagic, 8);
        w.u32v(kVersion);
        w.u32v(static_cast<uint32_t>(entries.size()));
        w.u64v(tocOffset);
        w.u64v(tocSize);
        w.u64v(vkwOffset);
        w.u64v(vkwSize);
        w.bytes(blobs.buf.data(), blobs.buf.size());
        w.bytes(toc.buf.data(), toc.buf.size());
        if (vkwSize)
            w.bytes(engineKeywords->data(), engineKeywords->size());

        return Result<std::vector<uint8_t>>::ok(std::move(w.buf));
    }

    Result<Library> read_library(const std::vector<uint8_t>& bytes)
    {
        using R = Result<Library>;
        Reader r{bytes.data(), bytes.size(), 0, false};
        if (!r.need(8))
            return R::err({ErrorCode::eDeserializeError, "truncated header"});
        if (std::memcmp(r.p + r.pos, kLibMagic, 8) != 0)
            return R::err({ErrorCode::eDeserializeError, "bad magic (not a v1 .vshlib)"});
        r.pos += 8;
        uint32_t version = r.u32v();
        if (version != kVersion)
            return R::err({ErrorCode::eDeserializeError, "unsupported version " + std::to_string(version)});

        uint32_t entryCount = r.u32v();
        uint64_t tocOffset  = r.u64v();
        uint64_t tocSize    = r.u64v();
        uint64_t vkwOffset  = r.u64v();
        uint64_t vkwSize    = r.u64v();
        (void)tocSize;
        if (tocOffset > r.n)
            return R::err({ErrorCode::eDeserializeError, "bad TOC offset"});

        Library lib;
        Reader  tr{r.p, r.n, tocOffset, false};
        for (uint32_t i = 0; i < entryCount; ++i)
        {
            LibraryEntry e;
            e.variantHash = tr.u64v();
            e.stage       = static_cast<ShaderStage>(tr.u8v());
            for (int p = 0; p < 7; ++p)
                tr.u8v();
            uint64_t off = tr.u64v();
            uint64_t sz  = tr.u64v();
            if (tr.bad || off + sz > r.n)
                return R::err({ErrorCode::eDeserializeError, "bad TOC entry"});
            e.blob.assign(r.p + off, r.p + off + sz);
            lib.entries.push_back(std::move(e));
        }
        if (vkwSize && vkwOffset + vkwSize <= r.n)
            lib.engineKeywords.assign(r.p + vkwOffset, r.p + vkwOffset + vkwSize);
        return R::ok(std::move(lib));
    }

    const std::vector<uint8_t>* find(const Library& lib, uint64_t variantHash, ShaderStage stage)
    {
        for (const auto& e : lib.entries)
            if (e.variantHash == variantHash && e.stage == stage)
                return &e.blob;
        return nullptr;
    }

    Result<std::vector<uint8_t>> write_source_pack(const std::vector<SourceFile>& filesIn)
    {
        std::vector<SourceFile> files = filesIn;
        std::sort(files.begin(), files.end(),
                  [](const SourceFile& a, const SourceFile& b) { return a.path < b.path; });
        Writer w;
        w.bytes(kSrcMagic, 8);
        w.u32v(kVersion);
        w.u32v(static_cast<uint32_t>(files.size()));
        for (const auto& f : files)
        {
            w.str(f.path);
            w.str(f.text);
        }
        return Result<std::vector<uint8_t>>::ok(std::move(w.buf));
    }

    Result<std::vector<SourceFile>> read_source_pack(const std::vector<uint8_t>& bytes)
    {
        using R = Result<std::vector<SourceFile>>;
        Reader r{bytes.data(), bytes.size(), 0, false};
        if (!r.need(8) || std::memcmp(r.p + r.pos, kSrcMagic, 8) != 0)
            return R::err({ErrorCode::eDeserializeError, "bad magic (not a v1 .vshslang)"});
        r.pos += 8;
        uint32_t version = r.u32v();
        if (version != kVersion)
            return R::err({ErrorCode::eDeserializeError, "unsupported version " + std::to_string(version)});
        uint32_t                count = r.u32v();
        std::vector<SourceFile> files;
        for (uint32_t i = 0; i < count && !r.bad; ++i)
        {
            SourceFile f;
            f.path = r.str();
            f.text = r.str();
            files.push_back(std::move(f));
        }
        if (r.bad)
            return R::err({ErrorCode::eDeserializeError, "corrupt source pack"});
        return R::ok(std::move(files));
    }
} // namespace vshadersystem::v1
