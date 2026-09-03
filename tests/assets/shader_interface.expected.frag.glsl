#version 450
#extension GL_GOOGLE_cpp_style_line_directive : enable
#define MINI_FRAGMENT_STAGE 1

// Generated from ShaderLab properties. Do not edit.

struct MiniVaryings
{
    vec2 uv;
    float objectId;
};

struct MiniFragmentOutput
{
    vec4 color;
};

layout(location = 0) smooth in vec2 _MiniIn_uv;
layout(location = 1) flat in float _MiniIn_objectId;

layout(location = 0) out vec4 _MiniOut_color;

// User shader source.
#line 1 "generated_interface.frag"
void FragmentMain(MiniVaryings inValue, out MiniFragmentOutput outValue)
{
    outValue.color = vec4(inValue.uv, inValue.objectId, 1.0);
}
#line 1 "MiniShaderCompiler/Forward/fragment-wrapper"

// Generated fragment entry wrapper.
void main()
{
    MiniVaryings inputValue;
    inputValue.uv = _MiniIn_uv;
    inputValue.objectId = _MiniIn_objectId;
    MiniFragmentOutput outputValue;
    FragmentMain(inputValue, outputValue);
    _MiniOut_color = outputValue.color;
}
