#include "include/scene.glsl"
#include "include/objects.glsl"

void VertexMain(MiniVertexInput inValue, out MiniVaryings outValue)
{
    mat4 model = Objects.transforms[InstanceIndices.objectIndices[gl_InstanceIndex]];
    gl_Position = Scene.lightSpaceMatrix * model * vec4(inValue.position, 1.0);
}