#include "vshadersystem/reflect.hpp"

#include <spirv_cross/spirv_cross.hpp>

namespace vshadersystem
{
    // ------------------------------------------------------------
    // stage mapping
    // ------------------------------------------------------------
    static ShaderStageFlagBits map_stage(spv::ExecutionModel m)
    {
        switch (m)
        {
            case spv::ExecutionModelVertex:
                return eStageVert;
            case spv::ExecutionModelFragment:
                return eStageFrag;
            case spv::ExecutionModelGLCompute:
                return eStageComp;

            case spv::ExecutionModelTaskEXT:
                return eStageTask;
            case spv::ExecutionModelMeshEXT:
                return eStageMesh;

            case spv::ExecutionModelRayGenerationKHR:
                return eStageRgen;
            case spv::ExecutionModelMissKHR:
                return eStageRmiss;
            case spv::ExecutionModelClosestHitKHR:
                return eStageRchit;
            case spv::ExecutionModelAnyHitKHR:
                return eStageRahit;
            case spv::ExecutionModelIntersectionKHR:
                return eStageRint;

            default:
                return eStageVert;
        }
    }

    // ------------------------------------------------------------
    // type mapping
    // ------------------------------------------------------------
    static ParamType map_spirv_type(const spirv_cross::SPIRType& t)
    {
        using namespace spirv_cross;

        switch (t.basetype)
        {
            case SPIRType::Float:

                if (t.columns == 1)
                {
                    switch (t.vecsize)
                    {
                        case 1:
                            return ParamType::eFloat;
                        case 2:
                            return ParamType::eVec2;
                        case 3:
                            return ParamType::eVec3;
                        case 4:
                            return ParamType::eVec4;
                    }
                }

                if (t.columns == 3)
                    return ParamType::eMat3;

                if (t.columns == 4)
                    return ParamType::eMat4;

                break;

            case SPIRType::Int:
                return ParamType::eInt;

            case SPIRType::UInt:
                return ParamType::eUInt;

            case SPIRType::Boolean:
                return ParamType::eBool;

            default:
                break;
        }

        return ParamType::eFloat;
    }

    // ------------------------------------------------------------
    // reflection
    // ------------------------------------------------------------
    Result<ShaderReflection> reflect_spirv(const std::vector<uint32_t>& spirv, const ReflectionOptions& opt)
    {
        try
        {
            spirv_cross::Compiler comp(spirv);

            ShaderReflection out;

            auto stageBit = map_stage(comp.get_execution_model());

            // compute local size
            if (comp.get_execution_model() == spv::ExecutionModelGLCompute)
            {
                out.hasLocalSize = true;
                out.localSizeX   = comp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 0);
                out.localSizeY   = comp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 1);
                out.localSizeZ   = comp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 2);
            }

            auto resources = comp.get_shader_resources();

            // ----------------------------------------------------
            // descriptors
            // ----------------------------------------------------
            auto add_binding = [&](const spirv_cross::Resource& r, DescriptorKind kind) {
                DescriptorBinding b;
                b.name    = r.name.empty() ? comp.get_name(r.id) : r.name;
                b.set     = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
                b.binding = comp.get_decoration(r.id, spv::DecorationBinding);
                b.kind    = kind;
                b.stageFlags |= stageBit;

                const auto& type = comp.get_type(r.type_id);
                if (!type.array.empty())
                {
                    if (type.array[0] == 0)
                    {
                        b.runtimeSized = true;
                        b.count        = 0;
                    }
                    else
                    {
                        b.count = type.array[0];
                    }
                }

                out.descriptors.push_back(b);
            };

            for (auto& r : resources.uniform_buffers)
                add_binding(r, DescriptorKind::eUniformBuffer);

            for (auto& r : resources.storage_buffers)
                add_binding(r, DescriptorKind::eStorageBuffer);

            for (auto& r : resources.sampled_images)
                add_binding(r, DescriptorKind::eCombinedImageSampler);

            for (auto& r : resources.separate_images)
                add_binding(r, DescriptorKind::eSampledImage);

            for (auto& r : resources.separate_samplers)
                add_binding(r, DescriptorKind::eSampler);

            for (auto& r : resources.storage_images)
                add_binding(r, DescriptorKind::eStorageImage);

            for (auto& r : resources.acceleration_structures)
                add_binding(r, DescriptorKind::eAccelerationStructure);

            // ----------------------------------------------------
            // blocks
            // ----------------------------------------------------
            auto add_block = [&](const spirv_cross::Resource& r, bool isPush) {
                BlockLayout blk;
                blk.name           = r.name.empty() ? comp.get_name(r.id) : r.name;
                blk.isPushConstant = isPush;
                blk.stageFlags |= stageBit;

                if (!isPush)
                {
                    blk.set     = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
                    blk.binding = comp.get_decoration(r.id, spv::DecorationBinding);
                }

                const auto& type = comp.get_type(r.base_type_id);
                blk.size         = comp.get_declared_struct_size(type);
                if (opt.includeBlockMembers)
                {
                    for (uint32_t i = 0; i < type.member_types.size(); i++)
                    {
                        BlockMember m;
                        m.name   = comp.get_member_name(r.base_type_id, i);
                        m.offset = comp.type_struct_member_offset(type, i);
                        m.size   = comp.get_declared_struct_member_size(type, i);
                        m.type   = map_spirv_type(comp.get_type(type.member_types[i]));

                        blk.members.push_back(m);
                    }
                }

                out.blocks.push_back(blk);
            };

            for (auto& r : resources.uniform_buffers)
                add_block(r, false);

            for (auto& r : resources.storage_buffers)
                add_block(r, false);

            if (opt.includePushConstants)
            {
                for (auto& r : resources.push_constant_buffers)
                    add_block(r, true);
            }

            return Result<ShaderReflection>::ok(out);
        }
        catch (std::exception& e)
        {
            return Result<ShaderReflection>::err({ErrorCode::eReflectError, e.what()});
        }
    }
} // namespace vshadersystem