#include "include/scene.glsl"

layout(std430, set = 0, binding = 1) readonly buffer ObjectBuffer
{
    mat4 transforms[];
} Objects;

void VertexMain(MiniVertexInput inValue, out MiniVaryings outValue)
{
    gl_Position = Scene.viewProjection * Objects.transforms[gl_InstanceIndex] *
                  vec4(inValue.position, 0.0, 1.0);
    outValue.color = inValue.color;
}
