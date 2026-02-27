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

## Overview

**vshadersystem** compiles GLSL shaders into SPIR-V and extracts
reflection and material metadata into a unified binary format.

It supports both:

- Single shader binaries (`.vshbin`)
- Shader libraries with variants (`.vshlib`)

It is designed as a standalone shader pipeline library and can be
integrated into:

- Game engines
- Rendering frameworks
- Offline asset pipelines
- Tools and editors

## Features

- GLSL → SPIR-V compilation (via glslang)
- Reflection extraction (via spirv-cross)
- Custom material semantic system (`#pragma`)
- Production-grade shader binary format (`.vshbin`)
- Deterministic hashing
- Dependency tracking (`#include`)
- Library-friendly API
- Cross‑platform support

## Pipeline

    GLSL Shader
         │
         ▼
    vshadersystem
         │
         ▼
    .vshbin
         │
         ├── SPIR-V
         ├── Reflection
         ├── Material Description
         └── Hash

## Shader Example

```glsl
#version 460

#include "common.glsl"

// Keywords
#pragma keyword permute global USE_SHADOW=1
#pragma keyword permute pass PASS=GBUFFER|FORWARD
#pragma keyword runtime material USE_CLEARCOAT=0
#pragma keyword runtime global DEBUG_VIEW=NONE|NORMAL|ALBEDO
#pragma keyword special USE_BINDLESS=1

// Material marker (required)
#pragma vultra material

// Parameters
#pragma vultra param baseColor semantic(BaseColor) default(1,1,1,1)
#pragma vultra param metallic semantic(Metallic) default(0) range(0,1)
#pragma vultra texture baseColorTex semantic(BaseColor)

// Render states
#pragma vultra state Blend One Zero
#pragma vultra state ZTest On
#pragma vultra state ZWrite On
#pragma vultra state Cull Back

layout(set=0, binding=0) uniform Material
{
    vec4 baseColor;
    float metallic;
};

layout(set=0, binding=1) uniform sampler2D baseColorTex;

layout(location=0) out vec4 outColor;

void main()
{
    // ...
}
```

## Shader Keywords

Keywords control shader variant generation and runtime behaviour.

Keywords are declared using:

```
#pragma keyword <dispatch> <name>
```

Where `<dispatch>` can be:

### permute

Compile‑time permutation keyword.

Each value produces a separate shader variant and a unique `variantHash`.

Example:

```
#pragma keyword permute USE_SHADOW
```

Cook manifest:

```
variant = USE_SHADOW=0
variant = USE_SHADOW=1
```

### runtime

Runtime keyword.

Does not generate additional variants.

Example:

```
#pragma keyword runtime USE_FOG
```

### special

Specialization constant keyword.

Example:

```
#pragma keyword special LIGHT_COUNT
```

### global keywords (engine_keywords.vkw)

Example:

```
USE_SHADOW=1
LIGHT_COUNT=4
```

## Binary Format

### .vshbin

Header

- magic
- version
- flags
- hashes

Chunks

- SPRV → SPIR-V
- REFL → reflection
- MDES → material description

### .vshlib

Shader library containing:

- Multiple shader variants
- Fast runtime lookup table
- Embedded engine keywords (optional)

## CLI Usage

```
Usage:
  vshaderc compile -i <input.vshader> -o <output.vshbin> -S <stage> [options]
  vshaderc build --shader_root <dir> [--shader <path> ...] [-I <dir> ...] [--keywords-file <path.vkw>] -o <output.vshlib> [options]
  vshaderc packlib -o <output.vshlib> [--keywords-file <path.vkw>] <in1.vshbin> <in2.vshbin> ...

Stages:
  vert, frag, comp, task, mesh, rgen, rmiss, rchit, rahit, rint

Options (compile):
  -I <dir>               Add include directory (repeatable)
  -D <NAME=VALUE>        Define macro (repeatable; VALUE optional)
  --keywords-file <vkw>  Load engine_keywords.vkw and inject global permute values if shader declares them
  --no-cache             Disable cache
  --cache <dir>          Cache directory (default: .vshader_cache)
  --verbose              Verbose logging

Options (build):
  --shader_root <dir>    Root directory used for scanning shaders and computing stable shader ids
  --shader <path>        Build only a specific shader (repeatable). Path is relative to --shader_root unless absolute.
  -I <dir>               Add include directory (repeatable)
  --keywords-file <vkw>  Load engine keywords (.vkw) and embed it into the output vshlib
  --no-cache             Disable cache
  --cache <dir>          Cache directory (default: .vshader_cache)
  --skip-invalid          Skip variants failing only_if constraints
  --verbose               Verbose logging

Examples:
  vshaderc compile -i shaders/pbr.frag.vshader -o out/pbr.frag.vshbin -S frag -I shaders/include -D USE_FOO=1
  vshaderc build --shader_root examples/keywords/shaders --keywords-file examples/keywords/engine_keywords.vkw -o out/shaders.vshlib --verbose
  vshaderc packlib -o out/shaders.vshlib --keywords-file engine_keywords.vkw out/*.vshbin
```

## Library Usage

Compile shader:

``` cpp
#include <vshadersystem/compiler.hpp>

using namespace vshadersystem;

SourceInput input;

input.sourceText = loadFile("shader.frag.vshader");
input.virtualPath = "shader.frag.vshader";

CompileOptions opt;
opt.stage = ShaderStage::eFrag;

// Keyword values are passed via defines
opt.defines.push_back({"USE_SHADOW", "1"});

auto result = compile_shader(input, opt);

write_vshbin_file("shader.vshbin", result.value());
```

Build shader library (offline):

```bash
vshaderc build --shader_root <shader_root> --keywords-file <engine_keywords.vkw> -o <output.vshlib>
```

Load shader binary:

``` cpp
#include <vshadersystem/binary.hpp>

auto lr = read_vshbin_file("shader.vshbin");
if (!lr.isOk())
{
    // error..
}

const auto& shader = lr.value();
```

Load shader binary from a shader library:

```cpp
auto lr = read_vshlib_file(libPath);
if (!lr.isOk())
{
    // error..
}

const auto& lib = lr.value();

// Example: shader id derived from path at build time (relative to --shader_root):
// shaders/pbr.frag.vshader -> "pbr.frag"
const std::string shaderId = "pbr.frag";

VariantKey key;
key.setShaderId(shaderId);
key.setStage(ShaderStage::eFrag);

// Example permutation keyword set
key.set("USE_SHADOW", 1);

const uint64_t variantHash = key.build();

auto blobR = extract_vshlib_blob(lib, variantHash, ShaderStage::eFrag);
if (!blobR.isOk())
{
    // error..
}

auto br = read_vshbin(blobR.value());
if (!br.isOk())
{
    // error..
}

const auto& bin = br.value();
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

Run the example:

    xmake run example_build_shader
    xmake run example_keywords
    xmake run example_runtime_load_library

## License

This project is under the [MIT](./LICENSE) license.
