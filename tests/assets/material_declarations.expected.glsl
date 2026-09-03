// Generated from ShaderLab properties. Do not edit.
layout(std140, set = 1, binding = 0) uniform MaterialProperties
{
    layout(offset = 0) float FloatValue;
    layout(offset = 4) float RangeValue;
    layout(offset = 8) uint Enabled;
    layout(offset = 16) vec2 Uv;
    layout(offset = 32) vec3 Direction;
    layout(offset = 48) vec4 Params;
    layout(offset = 64) vec4 Tint;
} Material;

layout(set = 1, binding = 1) uniform sampler2D MainTexture;
