#include "include/scene.glsl"

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
    color += EvaluateBlinnPhong(
        normal, viewDirection, directionalDirection,
        Scene.directionalLightColorIntensity.rgb,
        baseColor.rgb,
        Scene.directionalLightColorIntensity.a);

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
