#include "include/scene.glsl"

layout(std430, set = 0, binding = 1) readonly buffer ObjectBuffer
{
    mat4 transforms[];
} Objects;

void VertexMain(MiniVertexInput inValue, out MiniVaryings outValue)
{
    mat4 model = Objects.transforms[gl_InstanceIndex];
    vec4 worldPosition = model * vec4(inValue.position, 1.0);
    outValue.worldPosition = worldPosition.xyz;
    outValue.worldNormal = normalize(transpose(inverse(mat3(model))) *
                                     inValue.normal);
    outValue.uv = inValue.uv;
    gl_Position = Scene.viewProjection * worldPosition;
}
