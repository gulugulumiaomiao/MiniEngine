#include "include/scene.glsl"

#ifdef RECEIVE_SHADOWS
// Projects the surface into light space (offset along the normal to fight acne) and
// compares against the shadow map with a depth bias and a 3x3 PCF kernel. Samples outside
// the light volume count as lit; a zero shadow strength keeps unshadowed scenes unaffected.
float SampleShadow(vec3 worldPosition, vec3 normal)
{
    vec3 offsetPosition = worldPosition + normal * Scene.shadowParams.z;
    vec4 lightSpace = Scene.lightSpaceMatrix * vec4(offsetPosition, 1.0);
    vec3 projected = lightSpace.xyz / lightSpace.w;
    if (projected.z > 1.0 ||
        any(lessThan(projected.xy, vec2(0.0))) ||
        any(greaterThan(projected.xy, vec2(1.0)))) {
        return 1.0;
    }
    float lit = 0.0;
    vec2 texel = vec2(Scene.shadowParams.w);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float shadowDepth = texture(ShadowMap, projected.xy + vec2(float(x), float(y)) * texel).r;
            lit += (projected.z <= shadowDepth + Scene.shadowParams.y) ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}
#endif

vec3 EvaluateBlinnPhong(vec3 normal, vec3 viewDirection,
                        vec3 lightDirection, vec3 lightColor, vec3 baseColor,
                        float lightIntensity)
{
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 halfDirection = normalize(lightDirection + viewDirection);
    float specular = diffuse > 0.0
        ? pow(max(dot(normal, halfDirection), 0.0), Material.Shininess)
        : 0.0;
    return lightColor * lightIntensity *
           (baseColor * diffuse +
            Material.SpecularColor.rgb * specular);
}
void FragmentMain(MiniVaryings inValue, out MiniFragmentOutput outValue)
{
    vec4 baseColor = Material.BaseColor * texture(BaseMap, inValue.uv);
    vec3 normal = normalize(inValue.worldNormal);
    vec3 viewDirection = normalize(Scene.cameraPosition.xyz -
                                   inValue.worldPosition);
    vec3 color = baseColor.rgb * Material.AmbientColor.rgb;

    vec3 directionalDirection = normalize(
        -Scene.directionalLightDirection.xyz);
    float directionalShadow = 1.0;
#ifdef RECEIVE_SHADOWS
    directionalShadow = mix(1.0,
                            SampleShadow(inValue.worldPosition, normal),
                            Scene.shadowParams.x);
#endif
    color += EvaluateBlinnPhong(
        normal, viewDirection, directionalDirection,
        Scene.directionalLightColorIntensity.rgb,
        baseColor.rgb,
        Scene.directionalLightColorIntensity.a * directionalShadow);

    vec3 toPointLight = Scene.pointLightPositionRange.xyz -
                        inValue.worldPosition;
    float pointDistance = length(toPointLight);
    float pointRange = max(Scene.pointLightPositionRange.w, 0.0001);
    float attenuation = max(1.0 - pointDistance / pointRange, 0.0);
    attenuation *= attenuation;
    color += EvaluateBlinnPhong(
        normal, viewDirection, normalize(toPointLight),
        Scene.pointLightColorIntensity.rgb,
        baseColor.rgb,
        Scene.pointLightColorIntensity.a * attenuation);

    outValue.color = vec4(color, baseColor.a);
}