void FragmentMain(MiniVaryings inValue, out MiniFragmentOutput outValue)
{
    outValue.color = vec4(inValue.color, 1.0) * Material.BaseColor;
}
