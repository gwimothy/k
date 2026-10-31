#version 450 core
// u_mode 0: soft round particle. u_mode 1: impact decal (dark disc with a ragged edge;
// v_color.a carries a per-decal random seed).

in vec2 v_uv;
in vec4 v_color;
uniform int u_mode;
out vec4 o_color;

float hash(float n) { return fract(sin(n) * 43758.5453); }

void main() {
    vec2 p = v_uv * 2.0 - 1.0;
    float r = length(p);
    if (u_mode == 0) {
        float a = smoothstep(1.0, 0.3, r);
        o_color = vec4(v_color.rgb, v_color.a * a);
    } else {
        float angle = atan(p.y, p.x);
        float seed = v_color.a;
        float edge = 0.55 + 0.15 * sin(angle * 7.0 + seed) + 0.1 * sin(angle * 13.0 + seed * 3.1);
        float core = smoothstep(edge, edge - 0.25, r);
        float ring = smoothstep(edge + 0.25, edge, r) * 0.35;
        float a = max(core, ring) * (0.9 + 0.1 * hash(floor(angle * 8.0) + seed));
        if (a < 0.01) discard;
        o_color = vec4(v_color.rgb, a);
    }
}
