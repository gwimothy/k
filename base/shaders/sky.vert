#version 450 core

layout(location = 0) in vec3 a_position;

uniform mat4 u_viewProj; // view rotation only, no translation

out vec3 v_dir;

void main() {
    v_dir = a_position;
    vec4 p = u_viewProj * vec4(a_position, 1.0);
    gl_Position = p.xyww; // depth = 1: always behind everything
}
