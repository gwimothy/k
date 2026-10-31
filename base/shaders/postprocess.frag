#version 450 core
// HDR scene -> display: exposure, tonemap, gamma.

in vec2 v_uv;
uniform sampler2D u_scene;
uniform float u_exposure;
uniform int u_tonemap;
out vec4 o_color;

// Krzysztof Narkowicz's ACES filmic curve fit.
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(u_scene, v_uv).rgb * u_exposure;
    vec3 ldr = u_tonemap == 1 ? aces(hdr) : clamp(hdr, 0.0, 1.0);
    o_color = vec4(pow(ldr, vec3(1.0 / 2.2)), 1.0);
}
