# vshadersystem

vshadersystem is a standalone shader compilation and material reflection
pipeline.

## Overview

**vshadersystem** compiles GLSL shaders into SPIR-V and extracts
reflection and material metadata into a unified binary format.

It is designed as a **standalone shader pipeline library** and can be
integrated into:

-   Game engines
-   Rendering frameworks
-   Offline asset pipelines
-   Tools and editors

## Features

-   GLSL → SPIR-V compilation (via glslang)
-   Reflection extraction (via spirv-cross)
-   Custom material semantic system (`#pragma`)
-   Production-grade shader binary format (`.vshbin`)
-   Deterministic hashing
-   Dependency tracking (`#include`)
-   Library-friendly API
-   Cross‑platform support

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

``` glsl
#version 460

#include "common.glsl"

#pragma vultra material
#pragma vultra param baseColor semantic(BaseColor) default(1,1,1,1)
#pragma vultra param metallic semantic(Metallic) default(0) range(0,1)
#pragma vultra texture baseColorTex semantic(BaseColor)
#pragma vultra blend off
#pragma vultra depthTest on
#pragma vultra depthWrite on
#pragma vultra cull back

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

## Binary Format

`.vshbin` contains:

Header

-   magic
-   version
-   flags
-   hashes

Chunks

-   SPRV → SPIR-V
-   REFL → reflection
-   MDES → material description

## CLI Usage

```
Usage:
  vshaderc -i <input.vshader> -o <output.vshbin> -S <stage> [options]

Stages:
  vert, frag, comp, task, mesh, rgen, rmiss, rchit, rahit, rint

Options:
  -I <dir>         Add include directory (repeatable)
  -D <NAME=VALUE>  Define macro (repeatable; VALUE optional)
  --no-cache       Disable cache
  --cache <dir>    Cache directory (default: .vshader_cache)

Examples:
  vshaderc -i shaders/pbr.frag.vshader -o out/pbr.frag.vshbin -S frag -I shaders/include -D USE_FOO=1
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

auto result = compile_shader(input, opt);

write_vshbin_file("shader.vshbin", result.value());
```

Load shader:

``` cpp
#include <vshadersystem/binary.hpp>

auto shader = read_vshbin_file("shader.vshbin").value();

// You can get SPIR-V binary code, shader reflection information, and material description from it.
```

## Build Instructions

Prerequisites:

-   Git
-   XMake
-   Visual Studio, GCC, or Clang

Clone:

    git clone https://github.com/zzxzzk115/vshadersystem.git

Build:

    cd vshadersystem
    xmake -vD

Run the example:

	xmake run example_build_shader

## License

This project is under the [MIT](./LICENSE) license.
