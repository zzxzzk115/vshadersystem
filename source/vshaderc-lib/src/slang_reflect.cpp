#include "vshaderc/slang_reflect.hpp"

#include "slang_internal.hpp"

#include <slang-com-ptr.h>
#include <slang.h>

#include <cstring>

namespace vshaderc
{
    using Slang::ComPtr;
    using namespace vshadersystem;

    namespace
    {
        ParamType param_type_from_layout(slang::TypeLayoutReflection* tl)
        {
            if (!tl)
                return ParamType::eFloat;
            switch (tl->getKind())
            {
                case slang::TypeReflection::Kind::Scalar:
                    switch (tl->getScalarType())
                    {
                        case slang::TypeReflection::ScalarType::Bool: return ParamType::eBool;
                        case slang::TypeReflection::ScalarType::Int32: return ParamType::eInt;
                        case slang::TypeReflection::ScalarType::UInt32: return ParamType::eUInt;
                        default: return ParamType::eFloat;
                    }
                case slang::TypeReflection::Kind::Vector:
                    switch (tl->getType()->getElementCount())
                    {
                        case 2: return ParamType::eVec2;
                        case 3: return ParamType::eVec3;
                        default: return ParamType::eVec4;
                    }
                case slang::TypeReflection::Kind::Matrix:
                    return tl->getRowCount() <= 3 ? ParamType::eMat3 : ParamType::eMat4;
                default: return ParamType::eFloat;
            }
        }

        ResourceAccess access_from_slang(SlangResourceAccess a)
        {
            switch (a)
            {
                case SLANG_RESOURCE_ACCESS_READ: return ResourceAccess::eReadOnly;
                case SLANG_RESOURCE_ACCESS_WRITE: return ResourceAccess::eWriteOnly;
                case SLANG_RESOURCE_ACCESS_READ_WRITE:
                case SLANG_RESOURCE_ACCESS_RASTER_ORDERED: return ResourceAccess::eReadWrite;
                default: return ResourceAccess::eUnknown;
            }
        }

        TextureType texture_type_from_shape(SlangResourceShape shape)
        {
            const SlangResourceShape base = static_cast<SlangResourceShape>(shape & SLANG_RESOURCE_BASE_SHAPE_MASK);
            const bool               arr  = (shape & SLANG_TEXTURE_ARRAY_FLAG) != 0;
            switch (base)
            {
                case SLANG_TEXTURE_CUBE: return TextureType::eTexCube;
                case SLANG_TEXTURE_3D: return TextureType::eTex3D;
                case SLANG_TEXTURE_2D: return arr ? TextureType::eTex2DArray : TextureType::eTex2D;
                default: return TextureType::eUnknown;
            }
        }

        // Map a vsh field semantic string to the Semantic enum.
        Semantic semantic_from_string(const std::string& s)
        {
            if (s == "baseColor") return Semantic::eBaseColor;
            if (s == "metallic") return Semantic::eMetallic;
            if (s == "roughness") return Semantic::eRoughness;
            if (s == "normal") return Semantic::eNormal;
            if (s == "emissive") return Semantic::eEmissive;
            if (s == "occlusion") return Semantic::eOcclusion;
            if (s == "opacity") return Semantic::eOpacity;
            if (s == "alphaClip") return Semantic::eAlphaClip;
            return s.empty() ? Semantic::eUnknown : Semantic::eCustom;
        }

        uint32_t array_count(slang::TypeLayoutReflection* tl, bool& runtimeSized)
        {
            runtimeSized = false;
            if (!tl || tl->getKind() != slang::TypeReflection::Kind::Array)
                return 1;
            size_t n = tl->getType()->getElementCount();
            if (n == 0)
            {
                runtimeSized = true;
                return 0;
            }
            return static_cast<uint32_t>(n);
        }

        // Reflect one global parameter into descriptors/blocks.
        void reflect_param(slang::VariableLayoutReflection* var, ShaderReflection& out, ShaderStageFlags stageFlags)
        {
            if (!var)
                return;
            slang::TypeLayoutReflection* tl = var->getTypeLayout();
            if (!tl)
                return;

            const std::string name    = var->getName() ? var->getName() : "";
            const uint32_t    binding = var->getBindingIndex();
            const uint32_t    set     = var->getBindingSpace();
            const auto        kind    = tl->getKind();

            // Look through arrays for the element kind/shape.
            slang::TypeLayoutReflection* elemTl       = tl;
            bool                         runtimeSized = false;
            uint32_t                     count        = array_count(tl, runtimeSized);
            if (kind == slang::TypeReflection::Kind::Array)
                elemTl = tl->getElementTypeLayout();
            const auto elemKind = elemTl ? elemTl->getKind() : kind;

            auto pushDescriptor = [&](DescriptorKind dk, ResourceAccess access, TextureType tt) {
                DescriptorBinding d;
                d.name         = name;
                d.set          = set;
                d.binding      = binding;
                d.count        = count;
                d.kind         = dk;
                d.access       = access;
                d.stageFlags   = stageFlags;
                d.runtimeSized = runtimeSized;
                d.textureType  = tt;
                out.descriptors.push_back(std::move(d));
            };

            switch (elemKind)
            {
                case slang::TypeReflection::Kind::ConstantBuffer:
                case slang::TypeReflection::Kind::ParameterBlock:
                {
                    const bool isPush = var->getCategory() == slang::ParameterCategory::PushConstantBuffer;
                    pushDescriptor(DescriptorKind::eUniformBuffer, ResourceAccess::eReadOnly, TextureType::eUnknown);

                    BlockLayout block;
                    block.name          = name;
                    block.set           = set;
                    block.binding       = binding;
                    block.isPushConstant = isPush;
                    block.stageFlags    = stageFlags;
                    slang::TypeLayoutReflection* inner = elemTl->getElementTypeLayout();
                    if (inner)
                    {
                        block.size = static_cast<uint32_t>(inner->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));
                        for (unsigned f = 0, fc = inner->getFieldCount(); f < fc; ++f)
                        {
                            slang::VariableLayoutReflection* fv = inner->getFieldByIndex(f);
                            if (!fv)
                                continue;
                            BlockMember m;
                            m.name   = fv->getName() ? fv->getName() : "";
                            m.offset = static_cast<uint32_t>(fv->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM));
                            m.size   = static_cast<uint32_t>(
                                fv->getTypeLayout() ? fv->getTypeLayout()->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM)
                                                    : 0);
                            m.type = param_type_from_layout(fv->getTypeLayout());
                            block.members.push_back(std::move(m));
                        }
                    }
                    out.blocks.push_back(std::move(block));
                    break;
                }
                case slang::TypeReflection::Kind::SamplerState:
                    pushDescriptor(DescriptorKind::eSampler, ResourceAccess::eUnknown, TextureType::eUnknown);
                    break;
                case slang::TypeReflection::Kind::Resource:
                {
                    const SlangResourceShape shape = elemTl->getResourceShape();
                    const SlangResourceShape base =
                        static_cast<SlangResourceShape>(shape & SLANG_RESOURCE_BASE_SHAPE_MASK);
                    const ResourceAccess access = access_from_slang(elemTl->getResourceAccess());
                    if (base == SLANG_STRUCTURED_BUFFER || base == SLANG_BYTE_ADDRESS_BUFFER)
                    {
                        pushDescriptor(DescriptorKind::eStorageBuffer, access, TextureType::eUnknown);
                    }
                    else if (base == SLANG_ACCELERATION_STRUCTURE)
                    {
                        pushDescriptor(DescriptorKind::eAccelerationStructure, ResourceAccess::eReadOnly,
                                       TextureType::eUnknown);
                    }
                    else // texture
                    {
                        const bool storage = (elemTl->getResourceAccess() != SLANG_RESOURCE_ACCESS_READ);
                        pushDescriptor(storage ? DescriptorKind::eStorageImage : DescriptorKind::eSampledImage,
                                       access, texture_type_from_shape(shape));
                    }
                    break;
                }
                default:
                    // Loose uniforms (global-scope scalars) and unhandled kinds are ignored
                    // here; engines use explicit resources.
                    break;
            }
        }
    } // namespace

    Result<ProgramReflection> reflect_shader(const SlangCompiler&       compiler,
                                             const std::string&         moduleName,
                                             const std::string&         modulePath,
                                             const std::string&         moduleSource,
                                             const SlangCompileOptions& opt,
                                             const ShaderMetadata&      meta)
    {
        using R = Result<ProgramReflection>;
        auto* global = static_cast<slang::IGlobalSession*>(compiler.nativeGlobalSession());
        if (!global)
            return R::err({ErrorCode::eReflectError, "Slang global session unavailable"});

        slang::TargetDesc target = {};
        target.format            = SLANG_SPIRV;
        target.profile           = global->findProfile(opt.spirvProfile.c_str());

        std::vector<slang::CompilerOptionEntry> coptions;
        {
            slang::CompilerOptionEntry e = {};
            e.name                       = slang::CompilerOptionName::EmitSpirvDirectly;
            e.value.intValue0            = 1;
            coptions.push_back(e);
        }

        detail::MemoryFileSystem fs;
        const std::string        topPath = modulePath.empty() ? (moduleName + ".slang") : modulePath;
        detail::populate_filesystem(fs, opt, topPath, moduleSource);

        const char*        searchPaths[] = {""};
        slang::SessionDesc sessionDesc   = {};
        sessionDesc.targets              = &target;
        sessionDesc.targetCount          = 1;
        sessionDesc.searchPaths          = searchPaths;
        sessionDesc.searchPathCount      = 1;
        sessionDesc.fileSystem           = &fs;
        sessionDesc.compilerOptionEntries    = coptions.data();
        sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(coptions.size());

        ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global->createSession(sessionDesc, session.writeRef())))
            return R::err({ErrorCode::eReflectError, "createSession failed"});

        ComPtr<slang::IBlob> diag;
        slang::IModule*      module =
            session->loadModuleFromSourceString(moduleName.c_str(), topPath.c_str(), moduleSource.c_str(),
                                                diag.writeRef());
        if (!module)
        {
            std::string log =
                diag ? std::string(static_cast<const char*>(diag->getBufferPointer()), diag->getBufferSize()) : "";
            return R::err({ErrorCode::eReflectError, "loadModule failed:\n" + log});
        }

        std::vector<slang::IComponentType*>     comps{module};
        std::vector<ComPtr<slang::IEntryPoint>> eps;
        ShaderStageFlags                        stageFlags = 0;
        for (SlangInt32 i = 0, n = module->getDefinedEntryPointCount(); i < n; ++i)
        {
            ComPtr<slang::IEntryPoint> ep;
            if (SLANG_SUCCEEDED(module->getDefinedEntryPoint(i, ep.writeRef())) && ep)
            {
                comps.push_back(ep.get());
                eps.push_back(ep);
            }
        }

        ComPtr<slang::IComponentType> composed;
        diag = nullptr;
        session->createCompositeComponentType(comps.data(), static_cast<SlangInt>(comps.size()),
                                              composed.writeRef(), diag.writeRef());
        if (!composed)
            return R::err({ErrorCode::eReflectError, "compose failed"});
        ComPtr<slang::IComponentType> linked;
        diag = nullptr;
        composed->link(linked.writeRef(), diag.writeRef());
        if (!linked)
            return R::err({ErrorCode::eReflectError, "link failed"});

        slang::ProgramLayout* layout = linked->getLayout(0, nullptr);
        if (!layout)
            return R::err({ErrorCode::eReflectError, "getLayout returned null"});

        ProgramReflection out;
        ShaderReflection& refl = out.reflection;

        // Stage flags = union of entry-point stages; compute local size from the first
        // compute entry point.
        for (unsigned i = 0, n = layout->getEntryPointCount(); i < n; ++i)
        {
            slang::EntryPointReflection* er = layout->getEntryPointByIndex(i);
            if (!er)
                continue;
            switch (shader_stage_from_slang(er->getStage()))
            {
                case ShaderStage::eVert: stageFlags |= eStageVert; break;
                case ShaderStage::eFrag: stageFlags |= eStageFrag; break;
                case ShaderStage::eGeom: stageFlags |= eStageGeom; break;
                case ShaderStage::eComp: stageFlags |= eStageComp; break;
                case ShaderStage::eTask: stageFlags |= eStageTask; break;
                case ShaderStage::eMesh: stageFlags |= eStageMesh; break;
                default: break;
            }
            if (er->getStage() == SLANG_STAGE_COMPUTE)
            {
                SlangUInt sizes[3] = {1, 1, 1};
                er->getComputeThreadGroupSize(3, sizes);
                refl.hasLocalSize = true;
                refl.localSizeX   = static_cast<uint32_t>(sizes[0]);
                refl.localSizeY   = static_cast<uint32_t>(sizes[1]);
                refl.localSizeZ   = static_cast<uint32_t>(sizes[2]);
            }
        }

        for (unsigned i = 0, n = layout->getParameterCount(); i < n; ++i)
            reflect_param(layout->getParameterByIndex(i), refl, stageFlags);

        // --- material description: merge block layout with vsh metadata ---
        MaterialDescription& mat = out.material;
        mat.renderState          = meta.renderState;
        if (!meta.materialStructName.empty())
        {
            mat.materialBlockName = meta.materialStructName;

            // semantic/range lookup by field name
            auto findField = [&](const std::string& nm) -> const MaterialFieldMeta* {
                for (const auto& f : meta.materialFields)
                    if (f.name == nm)
                        return &f;
                return nullptr;
            };

            // Find a reflected block whose element type is the material struct (matched
            // by struct name on the block, or by the block's variable name).
            const BlockLayout* matBlock = nullptr;
            for (const auto& b : refl.blocks)
            {
                if (b.name == meta.materialStructName)
                {
                    matBlock = &b;
                    break;
                }
            }
            if (!matBlock && !refl.blocks.empty())
                matBlock = &refl.blocks.front();

            if (matBlock)
            {
                mat.materialParamSize = matBlock->size;
                for (const auto& m : matBlock->members)
                {
                    const MaterialFieldMeta* fm = findField(m.name);
                    // Texture index fields are surfaced as textures, not scalar params.
                    if (fm && !fm->textureKind.empty())
                        continue;
                    MaterialParamDesc p;
                    p.name     = m.name;
                    p.type     = m.type;
                    p.offset   = m.offset;
                    p.size     = m.size;
                    p.semantic = fm ? semantic_from_string(fm->semantic) : Semantic::eUnknown;
                    if (fm && fm->hasRange)
                    {
                        p.hasRange  = true;
                        p.range.min = fm->rangeLo;
                        p.range.max = fm->rangeHi;
                    }
                    mat.params.push_back(std::move(p));
                }
            }

            // Textures: material fields annotated with [VshTexture], matched to a
            // reflected image descriptor by name when present.
            for (const auto& f : meta.materialFields)
            {
                if (f.textureKind.empty())
                    continue;
                MaterialTextureDesc td;
                td.name     = f.name;
                td.semantic = semantic_from_string(f.semantic);
                td.type     = f.textureKind == "TextureCube"    ? TextureType::eTexCube
                              : f.textureKind == "Texture3D"     ? TextureType::eTex3D
                              : f.textureKind == "Texture2DArray" ? TextureType::eTex2DArray
                                                                  : TextureType::eTex2D;
                for (const auto& d : refl.descriptors)
                {
                    if ((d.kind == DescriptorKind::eSampledImage || d.kind == DescriptorKind::eStorageImage) &&
                        d.name == f.name)
                    {
                        td.set     = d.set;
                        td.binding = d.binding;
                        td.count   = d.count;
                        td.type    = d.textureType != TextureType::eUnknown ? d.textureType : td.type;
                        break;
                    }
                }
                mat.textures.push_back(std::move(td));
            }
        }

        return R::ok(std::move(out));
    }
} // namespace vshaderc
