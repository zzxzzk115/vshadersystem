-- Examples build variants with the Slang compile core, so they are desktop-only and
-- gated on the compiler option (Android/WASM build only the runtime loader).
-- Each example showcases a highlight of the v1.0 pipeline.
if has_config("vshadersystem_build_compiler") and not is_plat("android", "wasm") then
    includes("material_editor") -- authoring + full reflection + editor inspector
    includes("variant_library") -- keyword variants: build library, load, select variant
    includes("module_reuse")    -- .vshslang source packs + import reuse
    includes("cross_backend")   -- cross-backend portability validation
end
