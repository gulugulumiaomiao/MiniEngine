#version 450

// Standalone GLSL: the editor UI pipeline is built straight from the RHI instead of
// going through the ShaderLab asset pipeline, so this file carries its own layout
// declarations and is compiled to an embedded SPIR-V array at build time.
//
// Positions already arrive in clip space: ImGuiRenderer folds ImGui's display offset
// and size into the vertex copy it has to make anyway, which keeps this pipeline free
// of any uniform buffer (the RHI exposes no push constants).
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

void main()
{
    outUv = inUv;
    outColor = inColor;
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
