#pragma once

// vshaderc v1.0 command-line driver (Slang pipeline). Subcommands:
//   compile  -i <in.slang> -o <out.vshbin> [-S <stage>] [-I <dir>] [-D K=V] [--no-wgsl] [--id <id>]
//   build    --shader_root <dir> -o <out.vshlib> [--keywords-file <vkw>] [-I <dir>] [--no-wgsl]
//   pack-slang --root <dir> -o <out.vshslang> [--ext .slang]
// Returns a process exit code.

namespace vshaderc::cli
{
    int run(int argc, char** argv);
} // namespace vshaderc::cli
