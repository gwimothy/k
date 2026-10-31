#version 450 core
// UnlitGeneric: base texture * color, no lighting or fog.

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;

uniform sampler2D u_baseTexture;
uniform vec4 u_color;
uniform float u_alphaTest;

out vec4 o_color;

void main() {
    vec4 c = texture(u_baseTexture, v_uv) * u_color;
    if (u_alphaTest >= 0.0 && c.a < u_alphaTest) discard;
    o_color = c;
}
