#include "include/scene.glsl"
#include "include/objects.glsl"

void VertexMain(MiniVertexInput inValue, out MiniVaryings outValue)
{
    mat4 model = Objects.transforms[InstanceIndices.objectIndices[gl_InstanceIndex]];
    vec4 worldPosition = model * vec4(inValue.position, 1.0);
    outValue.worldPosition = worldPosition.xyz;
    outValue.worldNormal = normalize(transpose(inverse(mat3(model))) *
                                     inValue.normal);
    outValue.uv = inValue.uv;
    gl_Position = Scene.viewProjection * worldPosition;
}
