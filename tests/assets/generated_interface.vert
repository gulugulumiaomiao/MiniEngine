void VertexMain(MiniVertexInput inValue, out MiniVaryings outValue)
{
    gl_Position = vec4(inValue.position, 1.0);
    outValue.uv = inValue.uv;
    outValue.objectId = 7.0;
}
