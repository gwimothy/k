#version 450 core
// World surfaces: base texture * baked lightmap, with fog. Linear HDR output.

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in vec2 v_lightmapUV;

uniform sampler2D u_baseTexture;
uniform sampler2D u_lightmap;
uniform vec4 u_color;
uniform float u_alphaTest;
uniform int u_selfIllum;
uniform vec3 u_cameraPos;
uniform vec3 u_fogColor;
uniform float u_fogDensity;

out vec4 o_color;

void main() {
    vec4 albedo = texture(u_baseTexture, v_uv) * u_color;
    if (u_alphaTest >= 0.0 && albedo.a < u_alphaTest) discard;
    vec3 light = u_selfIllum != 0 ? vec3(1.0) : texture(u_lightmap, v_lightmapUV).rgb;
    vec3 lit = albedo.rgb * light;
    float dist = length(v_worldPos - u_cameraPos);
    float fog = 1.0 - exp(-pow(dist * u_fogDensity, 2.0));
    o_color = vec4(mix(lit, u_fogColor, fog), albedo.a);
}
