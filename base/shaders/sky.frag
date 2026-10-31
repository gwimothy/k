#version 450 core

in vec3 v_dir;
uniform samplerCube u_sky;
out vec4 o_color;

void main() {
    // Engine is Z-up; GL cubemaps are Y-up. See tools/gen_dev_assets.py for the matching convention.
    vec3 d = normalize(v_dir);
    o_color = vec4(texture(u_sky, vec3(d.x, d.z, -d.y)).rgb, 1.0);
}
