#ifndef COMMON_GLSL
#define COMMON_GLSL

vec3 applyGamma(vec3 c)
{
    return pow(c, vec3(1.0/2.2));
}
#endif // COMMON_GLSL