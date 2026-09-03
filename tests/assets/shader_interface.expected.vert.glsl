#version 450
#extension GL_GOOGLE_cpp_style_line_directive : enable
#define MINI_VERTEX_STAGE 1

// Generated from ShaderLab properties. Do not edit.

struct MiniVertexInput
{
    vec3 position;
    vec2 uv;
};

struct MiniVaryings
{
    vec2 uv;
    float objectId;
};

layout(location = 0) in vec3 _MiniIn_position;
layout(location = 1) in vec2 _MiniIn_uv;

layout(location = 0) smooth out vec2 _MiniOut_uv;
layout(location = 1) flat out float _MiniOut_objectId;

// User shader source.
#line 1 "generated_interface.vert"
void VertexMain(MiniVertexInput inValue, out MiniVaryings outValue)
{
    gl_Position = vec4(inValue.position, 1.0);
    outValue.uv = inValue.uv;
    outValue.objectId = 7.0;
}
#line 1 "MiniShaderCompiler/Forward/vertex-wrapper"

// Generated vertex entry wrapper.
void main()
{
    MiniVertexInput inputValue;
    inputValue.position = _MiniIn_position;
    inputValue.uv = _MiniIn_uv;
    MiniVaryings outputValue;
    VertexMain(inputValue, outputValue);
    _MiniOut_uv = outputValue.uv;
    _MiniOut_objectId = outputValue.objectId;
}
