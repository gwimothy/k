#version 450 core
// Placeholder world shader: dev-texture grid + sun + hemisphere ambient + fog.
// Replaced by lightmapped materials in Phase 3.

in vec3 v_worldPos;
in vec3 v_normal;
in vec3 v_color;

uniform vec3 u_cameraPos;
uniform vec3 u_sunDir;
uniform vec3 u_fogColor;
uniform float u_fogDensity;
uniform int u_fullbright;

out vec4 o_color;

// Anti-aliased grid lines every `spacing` units.
float gridLine(vec2 p, float spacing, float width) {
    vec2 g = abs(fract(p / spacing - 0.5) - 0.5) * spacing;
    vec2 fw = fwidth(p) * 1.5;
    vec2 l = 1.0 - smoothstep(vec2(width) - fw, vec2(width) + fw, g);
    return max(l.x, l.y);
}

void main() {
    vec3 n = normalize(v_normal);

    // Project world position onto the plane the face mostly lies in.
    vec3 an = abs(n);
    vec2 uv = an.z > an.x && an.z > an.y ? v_worldPos.xy : (an.x > an.y ? v_worldPos.yz : v_worldPos.xz);

    // Dev texture: 64-unit checker, 16-unit minor lines, 256-unit major lines.
    vec2 cell = floor(uv / 64.0);
    float checker = mod(cell.x + cell.y, 2.0);
    vec3 albedo = v_color * mix(0.92, 1.0, checker);
    albedo *= 1.0 - 0.10 * gridLine(uv, 16.0, 0.25);
    albedo *= 1.0 - 0.30 * gridLine(uv, 64.0, 0.5);
    albedo *= 1.0 - 0.45 * gridLine(uv, 256.0, 1.0);

    vec3 lit = albedo;
    if (u_fullbright == 0) {
        float sun = max(dot(n, u_sunDir), 0.0);
        vec3 sky = vec3(0.50, 0.56, 0.66), ground = vec3(0.30, 0.27, 0.24);
        vec3 ambient = mix(ground, sky, n.z * 0.5 + 0.5);
        lit = albedo * (ambient + vec3(1.0, 0.95, 0.85) * sun * 0.75);
    }

    float dist = length(v_worldPos - u_cameraPos);
    float fog = 1.0 - exp(-pow(dist * u_fogDensity, 2.0));
    o_color = vec4(mix(lit, u_fogColor, fog), 1.0);
}
