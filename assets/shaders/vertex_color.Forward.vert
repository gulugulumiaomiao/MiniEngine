#include "include/scene.glsl"
#include "include/objects.glsl"

void VertexMain(MiniVertexInput inValue, out MiniVaryings outValue)
{
    gl_Position = Scene.viewProjection *
                  Objects.transforms[InstanceIndices.objectIndices[gl_InstanceIndex]] *
                  vec4(inValue.position, 0.0, 1.0);
    outValue.color = inValue.color;
}
