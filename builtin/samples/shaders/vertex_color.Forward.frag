#include "include/scene.glsl"

void FragmentMain(MiniVaryings inValue, out MiniFragmentOutput outValue)
{
    float diffuse = max(dot(vec3(0.0, 0.0, 1.0),
                            -Scene.directionalLightDirection.xyz), 0.0);
    vec3 directional = Scene.directionalLightColorIntensity.rgb *
                       Scene.directionalLightColorIntensity.a *
                       (0.15 + 0.85 * diffuse);
    outValue.color = vec4(inValue.color * directional, 1.0) * Material.BaseColor;
}
