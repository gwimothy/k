// glTF 2.0 loading (CPU only, no GL), so tests and tools can use it.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "render/model.h"

#include <glm/gtc/type_ptr.hpp>

namespace k {

std::optional<MeshData> loadGltf(const std::vector<uint8_t>& fileData, std::string_view debugName, std::string* error) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    auto fail = [&](const std::string& msg) -> std::optional<MeshData> {
        if (error) *error = msg;
        if (data) cgltf_free(data);
        return std::nullopt;
    };
    if (cgltf_parse(&options, fileData.data(), fileData.size(), &data) != cgltf_result_success)
        return fail("not a valid glTF file");
    // Only embedded (.glb) or data-URI buffers for now; external .bin files need file callbacks.
    if (cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success)
        return fail("couldn't load buffers (external .bin files aren't supported yet; use .glb)");

    MeshData out;
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh) continue;
        float world[16];
        cgltf_node_transform_world(&node, world);
        Mat4 xf = glm::make_mat4(world);
        glm::mat3 nxf = glm::transpose(glm::inverse(glm::mat3(xf)));

        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node.mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const auto& a = prim.attributes[ai];
                if (a.type == cgltf_attribute_type_position) pos = a.data;
                else if (a.type == cgltf_attribute_type_normal) nrm = a.data;
                else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
            }
            if (!pos) continue;

            auto base = static_cast<uint32_t>(out.vertices.size());
            for (cgltf_size v = 0; v < pos->count; ++v) {
                Vertex vert{};
                float tmp[4] = {};
                cgltf_accessor_read_float(pos, v, tmp, 3);
                vert.position = Vec3(xf * Vec4(tmp[0], tmp[1], tmp[2], 1.0f));
                if (nrm) {
                    cgltf_accessor_read_float(nrm, v, tmp, 3);
                    vert.normal = glm::normalize(nxf * Vec3(tmp[0], tmp[1], tmp[2]));
                } else {
                    vert.normal = Vec3(0, 0, 1);
                }
                if (uv) {
                    cgltf_accessor_read_float(uv, v, tmp, 2);
                    vert.uv = Vec2(tmp[0], tmp[1]);
                }
                out.vertices.push_back(vert);
            }

            std::string matName = prim.material && prim.material->name ? prim.material->name : "__error";
            auto& indices = out.sectionFor(matName);
            if (prim.indices) {
                for (cgltf_size i = 0; i < prim.indices->count; ++i)
                    indices.push_back(base + static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
            } else {
                for (cgltf_size i = 0; i < pos->count; ++i) indices.push_back(base + static_cast<uint32_t>(i));
            }
        }
    }
    cgltf_free(data);
    if (out.vertices.empty()) {
        if (error) *error = "no triangle meshes in file";
        return std::nullopt;
    }
    (void)debugName;
    return out;
}

} // namespace k
