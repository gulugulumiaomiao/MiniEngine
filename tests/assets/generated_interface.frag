void FragmentMain(MiniVaryings inValue, out MiniFragmentOutput outValue)
{
    outValue.color = vec4(inValue.uv, inValue.objectId, 1.0);
}
