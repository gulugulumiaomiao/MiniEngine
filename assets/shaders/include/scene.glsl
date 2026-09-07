layout(std140, set = 0, binding = 0) uniform SceneBuffer
{
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 directionalLightDirection;
    vec4 directionalLightColorIntensity;
    vec4 pointLightPositionRange;
    vec4 pointLightColorIntensity;
} Scene;
