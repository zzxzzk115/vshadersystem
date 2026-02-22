#version 460
#pragma keyword permute global USE_SHADOW=1

layout(location = 0) out vec4 outColor;

void main()
{
#ifdef USE_SHADOW
    outColor = vec4(1.0);
#else
    discard;
#endif
}
