#version 450
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normal;
void main() {
    color = vec4(0.25, 0.5, 0.75, 1.0);
    normal = vec4(0.0, 0.0, 1.0, 1.0);
}
