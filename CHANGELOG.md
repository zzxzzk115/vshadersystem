# Changelog

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
