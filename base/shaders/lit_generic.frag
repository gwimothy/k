#version 450 core
// LitGeneric: base texture * color, lit by a sun and hemisphere ambient, with fog.
// Output is linear HDR; the post pass tonemaps it.

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;

uniform sampler2D u_baseTexture;
uniform vec4 u_color;
uniform float u_alphaTest;   // < 0 disables
uniform int u_selfIllum;

uniform vec3 u_cameraPos;
uniform vec3 u_sunDir;
uniform vec3 u_sunColor;
uniform vec3 u_ambientSky;
uniform vec3 u_ambientGround;
uniform vec3 u_fogColor;
uniform float u_fogDensity;
uniform int u_fullbright;

out vec4 o_color;

void main() {
    vec4 albedo = texture(u_baseTexture, v_uv) * u_color;
    if (u_alphaTest >= 0.0 && albedo.a < u_alphaTest) discard;

    vec3 n = normalize(v_normal);
    if (!gl_FrontFacing) n = -n;

    vec3 lit = albedo.rgb;
    if (u_fullbright == 0 && u_selfIllum == 0) {
        float sun = max(dot(n, u_sunDir), 0.0);
        vec3 ambient = mix(u_ambientGround, u_ambientSky, n.z * 0.5 + 0.5);
        lit = albedo.rgb * (ambient + u_sunColor * sun);
    }

    float dist = length(v_worldPos - u_cameraPos);
    float fog = 1.0 - exp(-pow(dist * u_fogDensity, 2.0));
    o_color = vec4(mix(lit, u_fogColor, fog), albedo.a);
}
