#version 450 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec2 a_lightmapUV;

uniform mat4 u_viewProj;
uniform mat4 u_model;
uniform float u_uvScale;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;

void main() {
    vec4 world = u_model * vec4(a_position, 1.0);
    v_worldPos = world.xyz;
    v_normal = transpose(inverse(mat3(u_model))) * a_normal;
    v_uv = a_uv * u_uvScale;
    gl_Position = u_viewProj * world;
}
