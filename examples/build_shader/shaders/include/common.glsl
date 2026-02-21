#ifndef COMMON_GLSL
#define COMMON_GLSL

vec3 toLinear(vec3 c)
{
    return pow(c, vec3(2.2));
}

vec3 toGamma(vec3 c)
{
    return pow(c, vec3(1.0/2.2));
}
#endif // COMMON_GLSL