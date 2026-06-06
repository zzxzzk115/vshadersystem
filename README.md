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
    <a href="https://github.com/zzxzzk115/vshadersystem/actions" alt="Build-Android">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vshadersystem/build_android.yaml?branch=master&label=Build-Android&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vshadersystem/actions" alt="Build-WASM">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vshadersystem/build_wasm.yaml?branch=master&label=Build-WASM&logo=github" /></a>
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
- WebGPU single shader binaries (`.vshwebbin`)
- Shader libraries with variants (`.vshlib`)
- WebGPU shader libraries with variants (`.vshweblib`)

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
- Optional SPIR-V -> WGSL conversion (via Tint)
- Deterministic hashing
- Permutation & runtime keyword system
- Engine-agnostic material injection
- Cross-platform support (Windows / Linux / macOS / Android / WASM)

> v0.7.1 currently targets GLSL-only shader authoring.
> `slang` support has been removed until the upstream toolchain is stable on Android.

## Shader DSL (v0.5+)

Shaders are written using structured sections.

### Example

```glsl
[vshader]
id       = "example/pbr"
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

| Key      | Description                                                        |
| -------- | ----------------------------------------------------------------- |
| id       | **Required.** Stable logical shader id, e.g. `id = "builtin/fxaa"`. Used as the shader's identity for variant lookup; must be unique across a library. It is no longer derived from the file name. |
| language | glsl                                                              |
| version  | GLSL version (e.g., 460)                                          |

> Migration note: the legacy filename-stem id derivation has been removed. Every
> shader must declare an explicit `id`. Duplicate ids within a library are a build
> error. (When compiling raw GLSL through the C++ API without a `[vshader]`
> section, set `BuildRequest.id` instead.)

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
- enum(name=0,other=1) (defaults may use either the label or the integer value)
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
  vshaderc compile --webgpu -i <input.vshader> -o <output.vshwebbin> -S <stage> [options]
  vshaderc build --shader_root <dir> -o <output.vshlib> [options]
  vshaderc build --webgpu --shader_root <dir> -o <output.vshweblib> [options]
  vshaderc packlib -o <output.vshlib> <in1.vshbin> <in2.vshbin> ...
  vshaderc packlib --webgpu -o <output.vshweblib> <in1.vshwebbin> <in2.vshwebbin> ...
  vshaderc pack-glsl --root <dir> -o <output.vshglsl> [--ext .glsl ...]
  vshaderc wgsl -i <input.vshbin|input.vshwebbin|input.spv> -o <output.wgsl>

build options:
  -I <dir>               add a filesystem include directory
  --mount <name>=<lib.vshglsl>
                         mount a packed GLSL library under <name> in the include
                         VFS, so `#include "<name>/<path>"` resolves to it from
                         any source location (repeatable)
```

## Includes & the VFS

Includes are resolved against an in-memory **VFS** by their **absolute VFS path
first** (then, as a fallback, relative to the including file's directory). This
makes shared/library includes resolve identically from any source location
(project shaders, generated shaders in subdirectories, etc.).

A **GLSL library** (`.vshglsl`) packages a directory of `.glsl` files and is
mounted at compile time. The mount name is prefixed onto each library file's
relative path:

```bash
# Package builtin GLSL whose contents live under .../include (vultra/, common/, ...)
vshaderc pack-glsl --root engine/shaders/include -o out/builtin.vshglsl

# Mount at root so `#include "vultra/mesh_material.glsl"` resolves
vshaderc build --shader_root project/shaders --mount =out/builtin.vshglsl -o out/project.vshlib

# Or mount under a namespace: pack the namespace contents and mount as that name
vshaderc pack-glsl --root engine/shaders/include/vultra -o out/vultra.vshglsl
vshaderc build --shader_root project/shaders --mount vultra=out/vultra.vshglsl -o out/project.vshlib
```

The C++ API exposes the same via `CompileOptions::vfsMounts` (a list of
`VfsMount{ mount, files }`) and `read_glsl_library_file()` / `write_glsl_library()`.

## WGSL Workflow

### Compile Single Shader for WebGPU

```bash
vshaderc compile --webgpu --material-mode ssbo \
  -i shaders/pbr.frag.vshader \
  -o out/pbr.frag.vshwebbin \
  -S frag
```

Notes:

- `--webgpu` forces `.vshwebbin` output.
- `--material-mode=bda` is rejected in WebGPU mode.
- WebGPU profile constrains compile target to Tint reader-compatible settings
  (`Vulkan 1.1` + `SPIR-V 1.3`) to avoid validation mismatch.
- Output binary embeds SPIR-V and WGSL text.

### Build Variant Library for WebGPU

```bash
vshaderc build --webgpu --material-mode ssbo \
  --shader_root shaders \
  --keywords-file shaders/engine_keywords.vkw \
  -o out/shaders.vshweblib
```

Notes:

- Variant expansion still works the same as native build.
- Each variant is validated through SPIR-V -> WGSL conversion.
- Output is forced to `.vshweblib`.

### Pack Prebuilt WebGPU Binaries

```bash
vshaderc packlib --webgpu \
  -o out/shaders.vshweblib \
  out/a.frag.vshwebbin out/b.vert.vshwebbin
```

Notes:

- `packlib --webgpu` only accepts `.vshwebbin`.
- Non-webgpu `packlib` rejects `.vshwebbin` to prevent mixing formats.

### Extract / Inspect WGSL

```bash
vshaderc wgsl -i out/pbr.frag.vshwebbin -o out/pbr.frag.wgsl
```

Also supports `.vshbin` and raw `.spv` input.

## Library Usage

### Build Single Shader

```cpp
BuildRequest req;
req.source.sourceText  = loadFile("shader.vshader");
req.source.virtualPath = "shader.vshader";
req.options.stage      = ShaderStage::eFrag;

auto r = build_single_shader(req);
```

If you compile for a WebGPU/Tint pipeline through the C++ API (without `vshaderc --webgpu`),
enable the WebGPU profile explicitly:

```cpp
req.options.webgpuProfile = true;
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

Tint is built-in by default (used for SPIR-V -> WGSL conversion).

Build for Android:

- Install Android NDK first.
- Android currently builds the runtime library only (`vshadersystem`).
- `vshaderc` and example targets are disabled on Android in v0.6.1.
- If XMake cannot auto-detect your Android toolchain, pass the NDK path explicitly.

Example:

    cd vshadersystem
    xmake f -p android --ndk=/path/to/android/sdk/ndk/<version>
    xmake

Build for WASM:

- WASM currently builds the runtime library only (`vshadersystem`).
- `vshaderc` and example targets are disabled on WASM.
- Install Emscripten SDK first.

Example:

    cd vshadersystem
    xmake f -p wasm -a wasm32
    xmake

Run the example (for desktop):

    xmake run example_build_shader
    xmake run example_keywords
    xmake run example_runtime_load_library
    xmake run example_webgpu

## Prebuilt Releases

To avoid long multi-platform compile times (especially with Tint), this repository provides
an automated prebuilt packaging workflow.

- Workflow: `.github/workflows/release_prebuilt.yaml`
- Trigger:
  - Publish a GitHub Release (recommended), or
  - Run workflow manually with a tag (e.g. `v0.7.1`)
- Output assets (attached to the release), one prebuilt bundle per platform/arch:
  - `vshadersystem-prebuilt-<tag>-linux-x86.zip`
  - `vshadersystem-prebuilt-<tag>-linux-x86_64.zip`
  - `vshadersystem-prebuilt-<tag>-linux-arm64.zip`
  - `vshadersystem-prebuilt-<tag>-windows-x64.zip`
  - `vshadersystem-prebuilt-<tag>-macosx-arm64.zip`
  - `vshadersystem-prebuilt-<tag>-android-arm64-v8a.zip`
  - `vshadersystem-prebuilt-<tag>-android-armeabi-v7a.zip`
  - `vshadersystem-prebuilt-<tag>-android-x86_64.zip`
  - `vshadersystem-prebuilt-<tag>-wasm-wasm32.zip`

Each bundle is produced by `xmake install` and may contain:

- `include/`
- `lib/`
- `bin/` (if available on that platform; e.g. `vshaderc` on desktop)

Android/WASM bundles are runtime-only and typically do not include `bin/`.

This makes downstream xmake-repo packages simple: select asset by `(plat, arch)`,
download, unpack, then export `includedirs/linkdirs/links`.

## License

This project is under the [MIT](./LICENSE) license.
