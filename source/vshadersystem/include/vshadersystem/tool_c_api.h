#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct VShaderSystemToolArgs
{
    uint32_t struct_size;
    int      argc;
    char**   argv;
} VShaderSystemToolArgs;

int vshadersystem_tool_run_vshaderc(const VShaderSystemToolArgs* args);

#ifdef __cplusplus
}
#endif
