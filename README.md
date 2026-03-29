# vshadersystem

<h4 align="center">
  vshadersystem is a standalone shader compilation, variant generation, and material reflection pipeline.
</h4>

<p align="center">
    <a href="https://github.com/zzxzzk115/vshadersystem/releases/latest" alt="Latest Release">
        <img src="https://img.shields.io/github/release/zzxzzk115/vshadersystem?include_prereleases=&sort=semver&color=blue" /></a>
    <a href="https://github.com/zzxzzk115/vshadersystem/actions" alt="Build-Windows">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vshadersystem/build_windows.yaml?branch=master&label=Build-Windows&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vshadersystem/actions" alt="Build-Linux">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vshadersystem/build_linux.yaml?branch=master&label=Build-Linux&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vshadersystem/actions" alt="Build-macOS">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vshadersystem/build_macos.yaml?branch=master&label=Build-macOS&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vshadersystem/issues" alt="GitHub Issues">
        <img src="https://img.shields.io/github/issues/zzxzzk115/vshadersystem"></a>
    <a href="https://www.codefactor.io/repository/github/zzxzzk115/vshadersystem"><img src="https://www.codefactor.io/repository/github/zzxzzk115/vshadersystem/badge" alt="CodeFactor" /></a>
    <a href="https://github.com/zzxzzk115/vshadersystem/blob/master/LICENSE" alt="GitHub">
        <img src="https://img.shields.io/github/license/zzxzzk115/vshadersystem"></a>
</p>

> ⚠ Since v0.5, the shader DSL no longer uses `#pragma`.
> All metadata must be declared using structured INI-style sections.

## Overview

**vshadersystem** compiles structured multi-stage GLSL shaders into SPIR-V and extracts
reflection and material metadata into a unified binary format.

Supported outputs:

- Single shader binaries (`.vshbin`)
- Shader libraries with variants (`.vshlib`)

Designed for integration into:

- Game engines
- Rendering frameworks
- Offline asset pipelines
- Editor tooling

## Features

- Structured shader DSL (INI-style sections)
- Multi-stage single-file shaders
- GLSL → SPIR-V compilation (via glslang)
- Reflection extraction (via spirv-cross)
- Deterministic hashing
- Permutation & runtime keyword system
- Engine-agnostic material injection
- Cross-platform support (Windows / Linux / macOS / Android)

> v0.6.0 currently targets GLSL-only shader authoring.
> `slang` support has been removed until the upstream toolchain is stable on Android.

## Shader DSL (v0.5+)

Shaders are written using structured sections.

### Example

```glsl
[vshader]
language = glsl
version  = 460

[keywords]
USE_SHADOW : bool permute
QUALITY    : enum(low,medium,high) permute

[properties]
baseColorFactor : vec4 = (1,1,1,1)
metallicFactor  : float = 0.0
roughnessFactor : float = 0.5
baseColorTex    : Texture2D

[renderstate]
cull        = back
depth_test  = on
depth_write = on
blend       = off

[vert]
layout(location=0) in vec3 inPos;
void main() { }

[frag]
layout(location=0) out vec4 outColor;
void main() { }
```

## Sections

### [vshader]

Required.

| Key      | Description              |
| -------- | ------------------------ |
| language | glsl                     |
| version  | GLSL version (e.g., 460) |

### [keywords]

Defines permutation or runtime keywords.

```
NAME : bool permute
NAME : enum(a,b,c) permute
NAME : bool runtime
NAME : int special
```

Types:

- permute → compile-time variants
- runtime → no variant expansion
- special → specialization constant

### [properties]

Defines material parameters.

Supported types:

- float
- vec2 / vec3 / vec4
- int
- Texture2D
- Texture2DArray

Generates:

- Material struct
- Reflection metadata
- Default values

### [renderstate]

Describes pipeline state configuration.

```
cull = back
depth_test = on
depth_write = on
blend = off
```

### Stage Sections

Each shader stage is declared using a section:

```
[vert] (or [vertex])
[frag] (or [fragment])
[comp] (or [compute])
[mesh]
[task]
[rgen] (or [raygen])
[rmiss] (or [miss],[raymiss])
[rchit] (or [closesthit],[raychit])
[rahit] (or [anyhit],[rayahit])
[rint] (or [intersect],[rayint])
```

`build_multiple_shaders()` automatically compiles all present stages.

## Material Injection

vshadersystem does not assume a runtime binding model.

Engines may inject descriptor bindings and material access logic:

```cpp
BuildRequest req;

req.options.materialInjection = {
    .preamble = "...",
    .materialAddressExpr = "...",
    .bindlessTextureArrayName = "...",
    .macroPrefix = "VSH_"
};
```

This allows BDA, SSBO, UBO, push constant, or custom GPU-driven architectures.

## CLI Usage

```
Usage:
  vshaderc compile -i <input.vshader> -o <output.vshbin> -S <stage> [options]
  vshaderc build --shader_root <dir> -o <output.vshlib> [options]
  vshaderc packlib -o <output.vshlib> <in1.vshbin> <in2.vshbin> ...
```

## Library Usage

### Build Single Shader

```cpp
BuildRequest req;
req.source.sourceText  = loadFile("shader.vshader");
req.source.virtualPath = "shader.vshader";
req.options.stage      = ShaderStage::eFrag;

auto r = build_single_shader(req);
```

### Build Multi-Stage Shader

```cpp
BuildRequest req;
req.source.sourceText  = loadFile("shader.vshader");
req.source.virtualPath = "shader.vshader";

auto r = build_multiple_shaders(req);
```

## Build Instructions

Prerequisites:

- Git
- XMake
- Visual Studio, GCC, or Clang

Clone:

    git clone https://github.com/zzxzzk115/vshadersystem.git

Build:

    cd vshadersystem
    xmake -vD

Build for Android:

- Install Android NDK first.
- Android currently builds the runtime library only (`vshadersystem`).
- `vshaderc` and example targets are disabled on Android in v0.6.1.
- If XMake cannot auto-detect your Android toolchain, pass the NDK path explicitly.

Example:

    cd vshadersystem
    xmake f -p android --ndk=/path/to/android/sdk/ndk/<version>
    xmake

If your environment also requires an explicit SDK path, use:

    xmake f -p android --sdk=/path/to/android/sdk --ndk=/path/to/android/sdk/ndk/<version>
    xmake

Run the example:

    xmake run example_build_shader
    xmake run example_keywords
    xmake run example_runtime_load_library

## License

This project is under the [MIT](./LICENSE) license.
