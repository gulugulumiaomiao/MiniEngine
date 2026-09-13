#version 450

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// The RHI maps BindingType::SampledTexture to a combined image sampler, so the font
// atlas (and any future ImGui::Image texture) is declared as a single sampler2D.
layout(set = 0, binding = 0) uniform sampler2D uTexture;

#ifdef SRGB_TARGET
// ImGui authors its style colors directly in sRGB space, expecting them to land in the
// framebuffer as written. An sRGB attachment makes the hardware encode on write, which
// would brighten the whole theme, so the colors are decoded here first. Decoding per
// fragment rather than per vertex also keeps gradients ramping in the sRGB space ImGui
// intends. Alpha carries no encoding and is left alone.
vec3 SrgbToLinear(vec3 color)
{
    return mix(color / 12.92,
               pow((color + 0.055) / 1.055, vec3(2.4)),
               greaterThan(color, vec3(0.04045)));
}
#endif

void main()
{
    vec4 color = inColor;
#ifdef SRGB_TARGET
    color.rgb = SrgbToLinear(color.rgb);
#endif
    outColor = color * texture(uTexture, inUv);
}
