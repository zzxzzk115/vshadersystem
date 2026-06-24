# vshadersystem

<h4 align="center">
  vshadersystem is a standalone shader compilation, variant generation, and material reflection pipeline built on Slang.
</h4>

<p align="center">
    <a href="https://github.com/zzxzzk115/vshadersystem/releases/latest" alt="Latest Release">
        <img src="https://img.shields.io/github/release/zzxzzk115/vshadersystem?include_prereleases=&sort=semver&color=blue" /></a>
    <a href="https://github.com/zzxzzk115/vshadersystem/actions" alt="Build-Windows">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vshadersystem/build_windows.yaml?branch=master&label=Build-Windows&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vshadersystem/issues" alt="GitHub Issues">
        <img src="https://img.shields.io/github/issues/zzxzzk115/vshadersystem"></a>
    <a href="https://github.com/zzxzzk115/vshadersystem/blob/master/LICENSE" alt="GitHub">
        <img src="https://img.shields.io/github/license/zzxzzk115/vshadersystem"></a>
</p>

> **v1.0 is a major rewrite.** The custom INI-style GLSL DSL is gone. Shaders are now
> authored in [Slang](https://shader-slang.org), and the engine-level features
> (materials, keywords/variants, render state) are described with **Slang user-defined
> attributes** read back through the Slang reflection API. On-disk formats are a clean
> break and are **not** backward compatible with pre-1.0 `.vshbin`/`.vshlib`/`.vshader`.

## Overview

**vshadersystem** is the *engine-level advanced* layer on top of Slang: it adds materials,
compile-time keyword variants, render state, and rich reflection that a thin Slang
front-end (like a render-hardware-interface's own shader tool) deliberately leaves out.

The pipeline is split in two:

- **`vshaderc` (offline compiler, desktop only)** drives the Slang compiler in-process:
  Slang → SPIR-V **and** WGSL (emitted directly by Slang, no SPIR-V→WGSL transpiler),
  plus reflection, material extraction, and variant expansion.
- **`vshadersystem` (runtime library, all platforms)** only *loads* the compiled
  binaries. It has **zero Slang dependency**, so Android / WASM build cleanly and just
  consume prebuilt libraries.

Outputs:

- Single shader binaries (`.vshbin`) — carry SPIR-V **and** WGSL in one file, so there is
  no separate WebGPU format; WebGPU consumers read the WGSL chunk.
- Shader libraries with variants (`.vshlib`).
- Slang source packs (`.vshslang`) — bundled `.slang` modules for compile-time `import` reuse.

## Authoring shaders

Shaders are plain Slang. `import vsh;` brings in the attribute library used to describe
engine metadata. `[shader("stage")]` marks entry points (multiple stages per file).

```slang
import vsh;

[VshMaterial]
struct Material
{
    [VshSemantic("baseColor")]                 float4 baseColorFactor;
    [VshSemantic("metallic")] [VshRange(0, 1)] float  metallicFactor;
    [VshSemantic("roughness")][VshRange(0, 1)] float  roughnessFactor;
    [VshTexture("Texture2D")]                  int    baseColorTex_index;
};

// Keyword + render-state declarations sit on one marker function and are read back via
// reflection. In code, gate on keywords with `#if KEYWORD` / `#if KEYWORD == N`.
[VshKeyword("USE_SHADOW", "bool", "permute", "global")]
[VshKeyword("QUALITY", "enum:low,medium,high", "permute", "local")]
[VshRenderState("cull", "back")]
[VshRenderState("depth_test", "on")]
void __vsh_meta() {}

[[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    float3 c = uMat.baseColorFactor.rgb;
#if USE_SHADOW
    c *= 0.8;
#endif
    return float4(c, uMat.baseColorFactor.a);
}
```

### vsh attributes

| Attribute | Target | Meaning |
| --- | --- | --- |
| `[VshMaterial]` | struct | Marks the material parameter struct. |
| `[VshSemantic("name")]` | field | Logical semantic (`baseColor`, `metallic`, `roughness`, `normal`, `emissive`, `occlusion`, `opacity`, `alphaClip`, or custom). |
| `[VshRange(lo, hi)]` | field | Editor/clamp range for a scalar parameter. |
| `[VshTexture("Texture2D")]` | field | Surfaces the field as a material texture (the int field is a bindless index). |
| `[VshKeyword(name, type, dispatch, scope)]` | function | A keyword. `type`: `bool` or `enum:a,b,c`. `dispatch`: `permute` (compile-time variant) / `runtime` / `special`. `scope`: `local` / `global` / `material` / `pass`. |
| `[VshRenderState(key, value)]` | function | Pipeline state, e.g. `("cull","back")`, `("depth_test","on")`, `("blend","off")`, `("depth_func","lequal")`. |

### Bindings & resources

Resources use Slang/HLSL types with explicit Vulkan bindings:
`[[vk::binding(binding, set)]] ConstantBuffer<T> / Texture2D / SamplerState /
RWStructuredBuffer<T> / RaytracingAccelerationStructure`. Reflection reads these back
into descriptor/block layouts.

### Module reuse (libraries)

Any `.slang` file can be a reusable module (`module foo;` + `public` decls) and imported
with `import foo;`. Package a directory of modules into a `.vshslang` source pack so they
resolve at compile time from any source location.

## CLI

```
vshaderc compile -i <in.slang> -o <out.vshbin> -S <stage> [-I <dir>] [-D K=V] [--no-wgsl] [--id <id>]
vshaderc build --shader_root <dir> -o <out.vshlib> [--keywords-file <vkw>] [-I <dir>] [--no-wgsl]
vshaderc pack-slang --root <dir> -o <out.vshslang> [--ext .slang]
```

- `compile` emits one binary (SPIR-V + WGSL) for the entry point of `-S <stage>`.
- `build` recursively compiles `.slang` under `--shader_root`, expands permutation
  keywords (shader `[VshKeyword]` + engine `.vkw`), and writes a variant library. The
  stable shader id is the path relative to the root (without extension).
- `pack-slang` bundles `.slang` sources for `import` reuse.

Stages: `vert frag geom comp task mesh rgen rmiss rchit rahit rint`.

## Runtime loading (engine side)

The runtime library is Slang-free. Load a library, compute the variant key, and read the
binary:

```cpp
#include <vshadersystem/vsh_format.hpp>
#include <vshadersystem/variant_key.hpp>
using namespace vshadersystem;
namespace v1 = vshadersystem::v1;

auto lib = v1::read_library(bytes).value();

VariantKey key;
key.setShaderId("pipelines/pbr");
key.setStage(ShaderStage::eFrag);
key.set("USE_SHADOW", 1);
key.set("QUALITY", 2);

if (const auto* blob = v1::find(lib, key.build(), ShaderStage::eFrag))
{
    auto bin = v1::read_binary(*blob).value();
    // bin.spirv / bin.wgsl / bin.reflection / bin.materialDesc
}
```

See [examples/runtime_load_library](examples/runtime_load_library) for the full
build-then-load flow.

### Engine API (offline)

To compile in-process instead of shelling out to `vshaderc`, link `vshaderc-lib`:

```cpp
#include <vshaderc/slang_build.hpp>
vshaderc::SlangCompiler compiler;
vshaderc::ShaderBuildOptions opt;
opt.shaderId = "pipelines/pbr";
opt.compile.emitWgsl = true;
auto r = vshaderc::build_shader(compiler, "pbr", "pbr.slang", source, opt); // -> variants
```

## Building

Prerequisites: Git, [XMake](https://xmake.io), and a C++23 compiler (MSVC / GCC / Clang).

```bash
git clone https://github.com/zzxzzk115/vshadersystem.git
cd vshadersystem
xmake -vD
```

The desktop build pulls a pinned **prebuilt Slang** (2026.11) via a local xmake package
and builds `vshadersystem` (runtime), `vshaderc-lib` + `vshaderc` (compiler), and the
examples.

### Android / WASM (runtime only)

These platforms build **only** the runtime loader (`vshadersystem`) — no Slang, no
compiler. They consume prebuilt `.vshbin` / `.vshlib` produced on a desktop host.

```bash
xmake f -p android --ndk=/path/to/ndk     # or: xmake f -p wasm -a wasm32
xmake
```

## License

This project is under the [MIT](./LICENSE) license.
