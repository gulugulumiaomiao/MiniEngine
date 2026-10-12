#version 450
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput gbuffer;
layout(location = 0) out vec4 color;
void main() { color = subpassLoad(gbuffer).bgra; }
