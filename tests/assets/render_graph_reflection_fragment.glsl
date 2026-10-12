layout(input_attachment_index = 0, set = 3, binding = 0) uniform subpassInput gbuffer;
void FragmentMain(MiniVaryings inputValue, out MiniFragmentOutput outputValue) {
    outputValue.color = subpassLoad(gbuffer);
}
