#include "vshaderc/slang_metadata.hpp"

#include "slang_internal.hpp"

#include <slang-com-ptr.h>
#include <slang.h>

#include <cstdlib>
#include <string_view>

namespace vshaderc
{
    using Slang::ComPtr;
    using vshadersystem::CompareOp;
    using vshadersystem::CullMode;
    using vshadersystem::ErrorCode;
    using vshadersystem::KeywordDecl;
    using vshadersystem::KeywordDispatch;
    using vshadersystem::KeywordScope;
    using vshadersystem::KeywordValueKind;

    namespace
    {
        std::string attr_string(slang::UserAttribute* a, unsigned index)
        {
            size_t      len = 0;
            const char* s   = a->getArgumentValueString(index, &len);
            if (!s)
                return {};
            // Slang returns the literal including surrounding quotes; strip them.
            std::string_view v(s, len);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                v = v.substr(1, v.size() - 2);
            return std::string(v);
        }

        bool name_contains(slang::UserAttribute* a, std::string_view needle)
        {
            const char* n = a->getName();
            return n && std::string_view(n).find(needle) != std::string_view::npos;
        }

        void parse_keyword(slang::UserAttribute* a, std::vector<KeywordDecl>& out)
        {
            if (a->getArgumentCount() < 1)
                return;
            KeywordDecl k;
            k.name             = attr_string(a, 0);
            const std::string type     = a->getArgumentCount() > 1 ? attr_string(a, 1) : "bool";
            const std::string dispatch = a->getArgumentCount() > 2 ? attr_string(a, 2) : "runtime";
            const std::string scope    = a->getArgumentCount() > 3 ? attr_string(a, 3) : "local";

            if (type.rfind("enum:", 0) == 0)
            {
                k.kind = KeywordValueKind::eEnum;
                std::string_view list = std::string_view(type).substr(5);
                size_t           pos  = 0;
                while (pos <= list.size())
                {
                    size_t comma = list.find(',', pos);
                    if (comma == std::string_view::npos)
                        comma = list.size();
                    if (comma > pos)
                        k.enumValues.emplace_back(list.substr(pos, comma - pos));
                    pos = comma + 1;
                }
            }
            else
            {
                k.kind = KeywordValueKind::eBool;
            }

            k.dispatch = dispatch == "permute"   ? KeywordDispatch::ePermutation
                         : dispatch == "special" ? KeywordDispatch::eSpecialization
                                                 : KeywordDispatch::eRuntime;
            k.scope = scope == "global"     ? KeywordScope::eGlobal
                      : scope == "material" ? KeywordScope::eMaterial
                      : scope == "pass"     ? KeywordScope::ePass
                                            : KeywordScope::eShaderLocal;
            out.push_back(std::move(k));
        }

        bool to_on(const std::string& v) { return v == "on" || v == "true" || v == "1"; }

        void apply_render_state(const std::string& key, const std::string& value, ShaderMetadata& meta)
        {
            meta.hasRenderState = true;
            meta.renderStateRaw.emplace_back(key, value);
            auto& rs = meta.renderState;
            if (key == "cull")
                rs.cull = value == "front" ? CullMode::eFront : (value == "none" || value == "off") ? CullMode::eNone
                                                                                                    : CullMode::eBack;
            else if (key == "depth_test")
                rs.depthTest = to_on(value);
            else if (key == "depth_write")
                rs.depthWrite = to_on(value);
            else if (key == "blend")
                rs.blendEnable = to_on(value);
            else if (key == "alpha_to_coverage")
                rs.alphaToCoverage = to_on(value);
            else if (key == "depth_func")
            {
                rs.depthFunc = value == "less"             ? CompareOp::eLess
                               : value == "lequal"         ? CompareOp::eLessOrEqual
                               : value == "less_or_equal"  ? CompareOp::eLessOrEqual
                               : value == "greater"        ? CompareOp::eGreater
                               : value == "gequal"         ? CompareOp::eGreaterOrEqual
                               : value == "equal"          ? CompareOp::eEqual
                               : value == "always"         ? CompareOp::eAlways
                               : value == "never"          ? CompareOp::eNever
                                                           : CompareOp::eLessOrEqual;
            }
        }
    } // namespace

    Result<ShaderMetadata> extract_shader_metadata(const SlangCompiler&       compiler,
                                                   const std::string&         moduleName,
                                                   const std::string&         modulePath,
                                                   const std::string&         moduleSource,
                                                   const SlangCompileOptions& opt)
    {
        using R = Result<ShaderMetadata>;

        auto* global = static_cast<slang::IGlobalSession*>(compiler.nativeGlobalSession());
        if (!global)
            return R::err({ErrorCode::eReflectError, "Slang global session unavailable"});

        // SPIR-V target only; metadata extraction needs reflection, not codegen.
        slang::TargetDesc target = {};
        target.format            = SLANG_SPIRV;
        target.profile           = global->findProfile(opt.spirvProfile.c_str());

        detail::MemoryFileSystem fs;
        const std::string        topPath = modulePath.empty() ? (moduleName + ".slang") : modulePath;
        detail::populate_filesystem(fs, opt, topPath, moduleSource);

        const char*         searchPaths[] = {""};
        slang::SessionDesc  sessionDesc   = {};
        sessionDesc.targets               = &target;
        sessionDesc.targetCount           = 1;
        sessionDesc.searchPaths           = searchPaths;
        sessionDesc.searchPathCount       = 1;
        sessionDesc.fileSystem            = &fs;

        ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global->createSession(sessionDesc, session.writeRef())))
            return R::err({ErrorCode::eReflectError, "createSession failed"});

        ComPtr<slang::IBlob> diag;
        slang::IModule*      module =
            session->loadModuleFromSourceString(moduleName.c_str(), topPath.c_str(), moduleSource.c_str(),
                                                diag.writeRef());
        if (!module)
        {
            std::string log = diag ? std::string(static_cast<const char*>(diag->getBufferPointer()),
                                                 diag->getBufferSize())
                                   : std::string();
            return R::err({ErrorCode::eReflectError, "loadModule failed:\n" + log});
        }

        ShaderMetadata meta;

        slang::DeclReflection* root = module->getModuleReflection();
        if (!root)
            return R::err({ErrorCode::eReflectError, "getModuleReflection returned null"});

        const unsigned childCount = root->getChildrenCount();
        for (unsigned i = 0; i < childCount; ++i)
        {
            slang::DeclReflection* child = root->getChild(i);
            if (!child)
                continue;
            const slang::DeclReflection::Kind kind = child->getKind();

            if (kind == slang::DeclReflection::Kind::Struct)
            {
                slang::TypeReflection* t = child->getType();
                if (!t)
                    continue;
                bool isMaterial = false;
                for (unsigned a = 0, n = t->getUserAttributeCount(); a < n; ++a)
                {
                    if (name_contains(t->getUserAttributeByIndex(a), "VshMaterial"))
                    {
                        isMaterial = true;
                        break;
                    }
                }
                if (!isMaterial)
                    continue;

                meta.materialStructName = t->getName() ? t->getName() : "Material";
                for (unsigned f = 0, fn = t->getFieldCount(); f < fn; ++f)
                {
                    slang::VariableReflection* field = t->getFieldByIndex(f);
                    if (!field)
                        continue;
                    MaterialFieldMeta fm;
                    fm.name = field->getName() ? field->getName() : "";
                    for (unsigned a = 0, an = field->getUserAttributeCount(); a < an; ++a)
                    {
                        slang::UserAttribute* attr = field->getUserAttributeByIndex(a);
                        if (name_contains(attr, "VshSemantic") && attr->getArgumentCount() >= 1)
                            fm.semantic = attr_string(attr, 0);
                        else if (name_contains(attr, "VshTexture") && attr->getArgumentCount() >= 1)
                            fm.textureKind = attr_string(attr, 0);
                        else if (name_contains(attr, "VshDisplayName") && attr->getArgumentCount() >= 1)
                            fm.displayName = attr_string(attr, 0);
                        else if (name_contains(attr, "VshDefault") && attr->getArgumentCount() >= 1)
                        {
                            fm.hasDefault   = true;
                            fm.defaultValue = attr_string(attr, 0);
                        }
                        else if (name_contains(attr, "VshColor"))
                            fm.isColor = true;
                        else if (name_contains(attr, "VshRange") && attr->getArgumentCount() >= 2)
                        {
                            float lo = 0.0f, hi = 0.0f;
                            attr->getArgumentValueFloat(0, &lo);
                            attr->getArgumentValueFloat(1, &hi);
                            fm.hasRange = true;
                            fm.rangeLo  = lo;
                            fm.rangeHi  = hi;
                        }
                    }
                    meta.materialFields.push_back(std::move(fm));
                }
            }
            else if (kind == slang::DeclReflection::Kind::Func)
            {
                slang::FunctionReflection* fn = child->asFunction();
                if (!fn)
                    continue;
                for (unsigned a = 0, an = fn->getUserAttributeCount(); a < an; ++a)
                {
                    slang::UserAttribute* attr = fn->getUserAttributeByIndex(a);
                    if (name_contains(attr, "VshKeyword"))
                        parse_keyword(attr, meta.keywords);
                    else if (name_contains(attr, "VshRenderState") && attr->getArgumentCount() >= 2)
                        apply_render_state(attr_string(attr, 0), attr_string(attr, 1), meta);
                }
            }
        }

        return R::ok(std::move(meta));
    }
} // namespace vshaderc
