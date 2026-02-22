# vshadersystem

vshadersystem is a standalone shader compilation, variant generation,
and material reflection pipeline.

It compiles GLSL shaders into SPIR-V, generates shader variants using
keywords, and packages them into runtime‑ready shader libraries.

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
  vshaderc cook -m <manifest.vcook> -o <output.vshlib> [options]
  vshaderc cook-merge -o <merged.vcook> <a.vcook> <b.vcook> ... [options]
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

Options (cook):
  -m, --manifest <vcook> Input manifest
  -o <vshlib>             Output library
  -I <dir>               Extra include directory (repeatable, appended)
  --keywords-file <vkw>  Apply engine_keywords.vkw for global permute defaults + embed into vshlib
  --no-cache             Disable cache
  --cache <dir>          Cache directory (default: .vshader_cache)
  -j, --jobs <N>         (Ignored) Cook forced single-thread (determinism, avoid deadlocks)
  --skip-invalid          Skip variants failing only_if constraints
  --verbose               Verbose pruning + entrypoint probe

Options (cook-merge):
  -o <merged.vcook>       Output manifest
  --keywords-file <vkw>   Force keywords_file=... in output (otherwise only kept if all inputs match)
  --verbose               Print merge summary

Examples:
  vshaderc compile -i shaders/pbr.frag.vshader -o out/pbr.frag.vshbin -S frag -I shaders/include -D USE_FOO=1
  vshaderc cook -m examples/keywords/shader_cook.vcook -o out/shaders.vshlib --keywords-file examples/keywords/engine_keywords.vkw --verbose
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

Cook shader library:

``` cpp
#include <vshadersystem/cook.hpp>

using namespace vshadersystem;

CookOptions opt;

opt.manifestPath = "shader_cook.vcook";
opt.outputPath   = "shaders.vshlib";

auto result = cook_shader_library(opt);

if (!result.isOk())
{
    printf("Cook failed: %s\n", result.error().message.c_str());
}
```

Merge cook manifests:

``` cpp
#include <vshadersystem/cook.hpp>

using namespace vshadersystem;

CookMergeOptions opt;

opt.outputPath = "merged.vcook";

opt.inputs.push_back("a.vcook");
opt.inputs.push_back("b.vcook");

auto result = cook_merge_manifests(opt);
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

// Example: shader id derived from path at cook time:
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

## License

This project is under the [MIT](./LICENSE) license.
