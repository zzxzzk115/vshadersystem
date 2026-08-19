# Changelog

## v1.2.1

- **Fix: reflection now describes the variant it is stored on.** `reflect_shader` built its own
  Slang session and never passed the per-variant keyword defines into it, so every variant of a
  shader was reflected with all keywords undefined and carried the **base variant's** descriptor
  table while its bytecode was correctly specialized. A binding a keyword ADDS was therefore
  absent from the reflection of the very variant that uses it, and one a keyword REMOVES was
  still listed. Nothing downstream can diagnose this: a consumer deriving a descriptor-set layout
  from the reflection hands the pipeline a short or misaligned set, which on Vulkan is a device
  hang on first use with no validation message. Found by a renderer whose compute shader binds 12
  descriptors under a keyword and was reported the base variant's 6, with the destination
  registers still at the base positions.
- **Fix: the reflection session now honours `matrixLayout`.** It defaulted to Slang's row-major
  while the compile session used the configured (column-major by default) layout, so the offsets
  and sizes reported for matrix block members described a layout that was not the one compiled.
- Tests: `test_variant_reflection.cpp` covers both shapes - a keyword that ADDS a binding, and the
  dangerous one where a keyword inserts sources so the destinations SHIFT (a stale table then
  reports the right names at the wrong registers, which is worse than a missing entry because
  every name still resolves).
- No format change and no runtime-loader change: only the cook is affected. Consumers need a
  re-cooked `.vshlib`, not a new runtime.

## v1.1.0

- **Direct3D 12 bytecode + release stripping.** `ShaderBinary` now also carries **DXBC**
  (SM5.1, via fxc) and **DXIL** (SM6.0, via dxc). `vshaderc compile`/`build` gain `--dxbc`
  and `--dxil` (and `SlangCompileOptions::emitDxbc`/`emitDxil`); DXBC/DXIL need the Windows
  shader compilers, and a host without them leaves the blob empty (best-effort, no failure).
- **`vshaderc strip`**: rewrite a `.vshlib` keeping only the bytecode for a set of targets
  (`--api vulkan,webgpu,d3d12,...` or `--keep spirv,wgsl,dxbc,dxil`) — cook all platforms in
  development, strip to the shipping targets at release.
- On-disk format: DXBC/DXIL are optional chunks; older readers skip unknown chunks, so this
  is backward- and forward-compatible (no format version change).
- Tests: `test_dx_targets.cpp` (chunk round-trip + emission).

## v1.0.1

- **Configurable matrix layout, defaulting to column-major.** Add `--matrix-layout column|row`
  to the `vshaderc compile` / `build` CLI and `SlangCompileOptions::matrixLayout` to the compile
  API. The default is **column-major** to match glm / GLSL / SPIR-V / Vulkan, so a host matrix
  used with `mul(m, v)` is not read transposed (Slang's own default is row-major, which
  otherwise collapses every transformed vertex). Per-field `row_major` / `column_major`
  qualifiers in the shader still override the global choice.
- Tests: add `test_matrix_layout.cpp` covering the option and the column-major default.

## v1.0.0

- Major rewrite: shaders are authored in [Slang](https://shader-slang.org); engine-level
  features (materials, keywords/variants, render state) are Slang user-defined attributes read
  back via reflection. On-disk formats (`.vshbin` / `.vshlib` / `.vshslang`) are a clean break
  and are not backward compatible with the pre-1.0 GLSL DSL.
