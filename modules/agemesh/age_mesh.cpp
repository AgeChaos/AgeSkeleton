// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh.h"
#include "core/object/class_db.h"
#include "scene/resources/material.h"
#include "thirdparty/meshoptimizer/meshoptimizer.h"
#include <cmath>
#include <vector>

String AgeMeshData::get_unsupported_reason(const Ref<Mesh> &p_mesh) {
    if (p_mesh.is_null() || p_mesh->get_surface_count() == 0) { return "empty mesh"; }
    if (p_mesh->get_blend_shape_count()) { return "blend shapes require the ordinary mesh path"; }
    for (int s = 0; s < p_mesh->get_surface_count(); s++) {
        if (p_mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) { return "non-triangle surface"; }
        Array arrays = p_mesh->surface_get_arrays(s);
        if (arrays.size() != Mesh::ARRAY_MAX || arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY) { return "invalid 3D vertex array"; }
        Vector<Vector3> vertices = arrays[Mesh::ARRAY_VERTEX];
        if (vertices.is_empty()) { return "empty vertices"; }
        for (const Vector3 &v : vertices) {
            for (int axis = 0; axis < 3; axis++) { if (!std::isfinite(float(v[axis]))) { return "vertex outside finite float range"; } }
        }
        for (int a = Mesh::ARRAY_CUSTOM0; a <= Mesh::ARRAY_WEIGHTS; a++) {
            if (arrays[a].get_type() != Variant::NIL) { return "custom vertex attributes or skinning are not supported yet"; }
        }
        if (arrays[Mesh::ARRAY_INDEX].get_type() != Variant::NIL && arrays[Mesh::ARRAY_INDEX].get_type() != Variant::PACKED_INT32_ARRAY) { return "invalid index array type"; }
        Vector<int> indices = arrays[Mesh::ARRAY_INDEX];
        if ((indices.is_empty() ? vertices.size() : indices.size()) % 3) { return "triangle index count is not divisible by three"; }
        for (int index : indices) { if (index < 0 || index >= vertices.size()) { return "index outside vertex array"; } }
        for (int a : { Mesh::ARRAY_NORMAL, Mesh::ARRAY_COLOR, Mesh::ARRAY_TEX_UV, Mesh::ARRAY_TEX_UV2, Mesh::ARRAY_TANGENT }) {
            if (arrays[a].get_type() == Variant::NIL) { continue; }
            Variant::Type expected = a == Mesh::ARRAY_NORMAL ? Variant::PACKED_VECTOR3_ARRAY :
                    a == Mesh::ARRAY_COLOR ? Variant::PACKED_COLOR_ARRAY :
                    a == Mesh::ARRAY_TANGENT ? Variant::PACKED_FLOAT32_ARRAY : Variant::PACKED_VECTOR2_ARRAY;
            if (arrays[a].get_type() != expected) { return "invalid vertex attribute type"; }
            int size = 0;
            switch (a) {
                case Mesh::ARRAY_NORMAL: { Vector<Vector3> v = arrays[a]; size = v.size(); } break;
                case Mesh::ARRAY_COLOR: { Vector<Color> v = arrays[a]; size = v.size(); } break;
                case Mesh::ARRAY_TANGENT: { Vector<float> v = arrays[a]; size = v.size() / 4; if (v.size() % 4) { return "invalid tangent array"; } } break;
                default: { Vector<Vector2> v = arrays[a]; size = v.size(); } break;
            }
            if (size != vertices.size()) { return "vertex attribute count mismatch"; }
        }

    }
    return get_material_unsupported_reason(p_mesh);
}

String AgeMeshData::get_material_unsupported_reason(const Ref<Mesh> &p_mesh) {
    if (p_mesh.is_null()) { return "empty mesh"; }
    for (int s = 0; s < p_mesh->get_surface_count(); s++) {
        Ref<Material> material = p_mesh->surface_get_material(s);
        if (material.is_valid()) {
            Ref<BaseMaterial3D> base = material;
            if (base.is_null() || base->get_transparency() != BaseMaterial3D::TRANSPARENCY_DISABLED || base->get_next_pass().is_valid() || base->is_grow_enabled() || base->get_billboard_mode() != BaseMaterial3D::BILLBOARD_DISABLED) {
                return "only opaque built-in materials without extra passes are supported";
            }
        }
    }
    return String();
}

Ref<AgeMeshData> AgeMeshData::build(const Ref<Mesh> &p_mesh, int p_triangles_per_cluster) {
    ERR_FAIL_COND_V(p_triangles_per_cluster < 32 || p_triangles_per_cluster > 256 || p_triangles_per_cluster % 4, Ref<AgeMeshData>());
    ERR_FAIL_COND_V(p_mesh.is_null() || p_mesh->get_surface_count() == 0, Ref<AgeMeshData>());
    Ref<AgeMeshData> data;
    data.instantiate();
    data->cache_version = 1;
    data->source = p_mesh;
    if (requires_native_geometry(p_mesh)) {
        data->native_geometry = true;
        for (int s = 0; s < p_mesh->get_surface_count(); s++) {
            int count = p_mesh->surface_get_array_index_len(s);
            if (!count) { count = p_mesh->surface_get_array_len(s); }
            if (p_mesh->surface_get_primitive_type(s) == Mesh::PRIMITIVE_TRIANGLES) { data->source_triangles += count / 3; }
        }
        return data;
    }
    String unsupported = get_unsupported_reason(p_mesh);
    ERR_FAIL_COND_V_MSG(!unsupported.is_empty(), Ref<AgeMeshData>(), "AgeMesh: " + unsupported);
    for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
        Array arrays = p_mesh->surface_get_arrays(surface);
        Vector<Vector3> vertices = arrays[Mesh::ARRAY_VERTEX];
        Vector<int> index_array = arrays[Mesh::ARRAY_INDEX];
        std::vector<unsigned int> indices(index_array.is_empty() ? vertices.size() : index_array.size());
        for (size_t i = 0; i < indices.size(); i++) { indices[i] = index_array.is_empty() ? i : index_array[i]; }
        std::vector<float> positions(vertices.size() * 3);
        for (int i = 0; i < vertices.size(); i++) { for (int axis = 0; axis < 3; axis++) { positions[i * 3 + axis] = vertices[i][axis]; } }
        size_t bound = meshopt_buildMeshletsBound(indices.size(), 256, p_triangles_per_cluster);
        std::vector<meshopt_Meshlet> meshlets(bound);
        std::vector<unsigned int> remap(bound * 256);
        std::vector<unsigned char> triangles(bound * p_triangles_per_cluster * 3);
        size_t count = meshopt_buildMeshlets(meshlets.data(), remap.data(), triangles.data(), indices.data(), indices.size(), positions.data(), vertices.size(), 12, 256, p_triangles_per_cluster, 0.0f);
        data->source_triangles += indices.size() / 3;
        for (size_t m = 0; m < count; m++) {
            const meshopt_Meshlet &cluster = meshlets[m];
            Array local;
            local.resize(Mesh::ARRAY_MAX);
            for (int a : { Mesh::ARRAY_VERTEX, Mesh::ARRAY_NORMAL, Mesh::ARRAY_COLOR, Mesh::ARRAY_TEX_UV, Mesh::ARRAY_TEX_UV2, Mesh::ARRAY_TANGENT }) {
                if (arrays[a].get_type() == Variant::NIL) { continue; }
                switch (a) {
                    case Mesh::ARRAY_VERTEX:
                    case Mesh::ARRAY_NORMAL: {
                        Vector<Vector3> old = arrays[a], selected;
                        selected.resize(cluster.vertex_count);
                        for (unsigned int v = 0; v < cluster.vertex_count; v++) { selected.write[v] = old[remap[cluster.vertex_offset + v]]; }
                        local[a] = selected;
                    } break;
                    case Mesh::ARRAY_COLOR: {
                        Vector<Color> old = arrays[a], selected;
                        selected.resize(cluster.vertex_count);
                        for (unsigned int v = 0; v < cluster.vertex_count; v++) { selected.write[v] = old[remap[cluster.vertex_offset + v]]; }
                        local[a] = selected;
                    } break;
                    case Mesh::ARRAY_TANGENT: {
                        Vector<float> old = arrays[a], selected;
                        selected.resize(cluster.vertex_count * 4);
                        for (unsigned int v = 0; v < cluster.vertex_count; v++) { for (int c = 0; c < 4; c++) { selected.write[v * 4 + c] = old[remap[cluster.vertex_offset + v] * 4 + c]; } }
                        local[a] = selected;
                    } break;
                    default: {
                        Vector<Vector2> old = arrays[a], selected;
                        selected.resize(cluster.vertex_count);
                        for (unsigned int v = 0; v < cluster.vertex_count; v++) { selected.write[v] = old[remap[cluster.vertex_offset + v]]; }
                        local[a] = selected;
                    } break;
                }
            }
            Vector<int> local_indices;
            local_indices.resize(cluster.triangle_count * 3);
            for (unsigned int i = 0; i < cluster.triangle_count * 3; i++) { local_indices.write[i] = triangles[cluster.triangle_offset + i]; }
            local[Mesh::ARRAY_INDEX] = local_indices;
            // Simplify each cluster with locked boundaries: adjacent clusters retain
            // identical edge vertices at every level, avoiding LOD cracks.
            Vector<Vector3> cluster_positions = local[Mesh::ARRAY_VERTEX];
            std::vector<float> positions_f32(cluster.vertex_count * 3);
            for (unsigned int v = 0; v < cluster.vertex_count; v++) { for (int axis = 0; axis < 3; axis++) { positions_f32[v * 3 + axis] = cluster_positions[v][axis]; } }
            std::vector<unsigned int> original(local_indices.size());
            for (int i = 0; i < local_indices.size(); i++) { original[i] = local_indices[i]; }
            // Keep UVs, normals and colors in the simplification metric, not only geometry.
            const int attribute_count = 11;
            std::vector<float> attributes(cluster.vertex_count * attribute_count, 0.0f);
            float weights[attribute_count] = {};
            Vector<Vector3> normals = local[Mesh::ARRAY_NORMAL];
            Vector<Vector2> uv = local[Mesh::ARRAY_TEX_UV], uv2 = local[Mesh::ARRAY_TEX_UV2];
            Vector<Color> colors = local[Mesh::ARRAY_COLOR];
            for (unsigned int v = 0; v < cluster.vertex_count; v++) {
                if (!normals.is_empty()) { for (int c = 0; c < 3; c++) { attributes[v * attribute_count + c] = normals[v][c]; weights[c] = 1.0f; } }
                if (!uv.is_empty()) { for (int c = 0; c < 2; c++) { attributes[v * attribute_count + 3 + c] = uv[v][c]; weights[3 + c] = 1.0f; } }
                if (!uv2.is_empty()) { for (int c = 0; c < 2; c++) { attributes[v * attribute_count + 5 + c] = uv2[v][c]; weights[5 + c] = 1.0f; } }
                if (!colors.is_empty()) { for (int c = 0; c < 4; c++) { attributes[v * attribute_count + 7 + c] = colors[v][c]; weights[7 + c] = 1.0f; } }
            }
            Dictionary lods;
            float scale = meshopt_simplifyScale(positions_f32.data(), cluster.vertex_count, 12);
            int previous_count = local_indices.size();
            double previous_error = 0;
            for (int divisor = 2; divisor <= 8; divisor *= 2) {
                std::vector<unsigned int> simplified(original.size());
                float error = 0;
                size_t selected = meshopt_simplifyWithAttributes(simplified.data(), original.data(), original.size(), positions_f32.data(), cluster.vertex_count, 12,
                        attributes.data(), attribute_count * sizeof(float), weights, attribute_count, nullptr, MAX(size_t(12), original.size() / divisor / 3 * 3), 0.01f, meshopt_SimplifyLockBorder, &error);
                if (selected >= size_t(previous_count) || selected == 0) { continue; }
                Vector<int> level;
                level.resize(selected);
                for (size_t i = 0; i < selected; i++) { level.write[i] = simplified[i]; }
                double distance_error = MAX(double(error * scale), previous_error + MAX(double(scale) * 0.00001, 0.000001));
                lods[distance_error] = level;
                previous_error = distance_error;
                previous_count = selected;
            }
            Ref<ArrayMesh> mesh;
            mesh.instantiate();
            mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, local, Array(), lods);
            mesh->surface_set_material(0, p_mesh->surface_get_material(surface));
            Dictionary entry;
            entry["mesh"] = mesh;
            entry["surface"] = surface;
            entry["triangles"] = int(cluster.triangle_count);
            data->clusters.push_back(entry);
        }
    }
    data->gpu_payloads = data->bake_gpu_payloads();
    return data;
}

String AgeMeshData::get_cache_error() const {
    if (cache_version != 1) { return "cache version mismatch; reimport the source model"; }
    if (native_geometry) { return source.is_valid() && source->get_surface_count() ? String() : String("empty native mesh"); }
    String unsupported = get_unsupported_reason(source);
    if (!unsupported.is_empty()) { return unsupported; }
    if (clusters.is_empty()) { return "empty cluster cache"; }
    int64_t total = 0;
    for (int i = 0; i < clusters.size(); i++) {
        Ref<ArrayMesh> mesh = get_cluster_mesh(i);
        int surface = get_cluster_surface(i);
        if (mesh.is_null() || mesh->get_surface_count() != 1 || surface < 0 || surface >= source->get_surface_count()) { return "invalid cluster surface"; }
        unsupported = get_unsupported_reason(mesh);
        if (!unsupported.is_empty()) { return unsupported; }
        Array arrays = mesh->surface_get_arrays(0);
        Vector<Vector3> vertices = arrays[Mesh::ARRAY_VERTEX];
        Vector<int> indices = arrays[Mesh::ARRAY_INDEX];
        if (vertices.size() > 256 || indices.is_empty() || indices.size() > 768) { return "cluster size outside supported bounds"; }
        Dictionary entry = clusters[i];
        if (entry.get("triangles", Variant()).get_type() != Variant::INT || int(entry["triangles"]) != indices.size() / 3) { return "cluster triangle count mismatch"; }
        if (mesh->surface_get_material(0) != source->surface_get_material(surface)) { return "cluster material differs from source"; }
        Dictionary lods = mesh->surface_get_lods(0);
        for (const Variant *key = lods.next(); key; key = lods.next(key)) {
            if ((key->get_type() != Variant::FLOAT && key->get_type() != Variant::INT) || !std::isfinite(double(*key)) || double(*key) <= 0 || lods[*key].get_type() != Variant::PACKED_INT32_ARRAY) { return "invalid LOD entry"; }
            Vector<int> level = lods[*key];
            if (level.is_empty() || level.size() % 3 || level.size() >= indices.size()) { return "invalid LOD triangle count"; }
            for (int index : level) { if (index < 0 || index >= vertices.size()) { return "LOD index outside vertex array"; } }
        }
        total += indices.size() / 3;
    }
    int64_t original = 0;
    for (int s = 0; s < source->get_surface_count(); s++) {
        Array arrays = source->surface_get_arrays(s);
        Vector<int> indices = arrays[Mesh::ARRAY_INDEX];
        Vector<Vector3> vertices = arrays[Mesh::ARRAY_VERTEX];
        original += (indices.is_empty() ? vertices.size() : indices.size()) / 3;
    }
    if (total != source_triangles || total != original) { return "cache/source triangle count mismatch"; }
    return String();
}

Ref<ArrayMesh> AgeMeshData::get_cluster_mesh(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, clusters.size(), Ref<ArrayMesh>());
    if (clusters[p_index].get_type() != Variant::DICTIONARY) { return Ref<ArrayMesh>(); }
    Dictionary item = clusters[p_index];
    if (item.get("mesh", Variant()).get_type() != Variant::OBJECT) { return Ref<ArrayMesh>(); }
    return item.get("mesh", Variant());
}
int AgeMeshData::get_cluster_surface(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, clusters.size(), -1);
    if (clusters[p_index].get_type() != Variant::DICTIONARY) { return -1; }
    Dictionary item = clusters[p_index];
    if (item.get("surface", Variant()).get_type() != Variant::INT) { return -1; }
    return item.get("surface", -1);
}
void AgeMeshData::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_native_geometry", "enabled"), &AgeMeshData::set_native_geometry);
    ClassDB::bind_method(D_METHOD("is_native_geometry"), &AgeMeshData::is_native_geometry);
    ClassDB::bind_method(D_METHOD("build_native_mesh", "allow_merge"), &AgeMeshData::build_native_mesh, DEFVAL(true));
    ClassDB::bind_static_method("AgeMeshData", D_METHOD("requires_native_geometry", "mesh"), &AgeMeshData::requires_native_geometry);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "native_geometry", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_native_geometry", "is_native_geometry");
    ClassDB::bind_static_method("AgeMeshData", D_METHOD("build", "mesh", "triangles_per_cluster"), &AgeMeshData::build, DEFVAL(128));
    ClassDB::bind_static_method("AgeMeshData", D_METHOD("get_unsupported_reason", "mesh"), &AgeMeshData::get_unsupported_reason);
    ClassDB::bind_method(D_METHOD("set_source", "mesh"), &AgeMeshData::set_source);
    ClassDB::bind_method(D_METHOD("get_source"), &AgeMeshData::get_source);
    ClassDB::bind_method(D_METHOD("set_clusters", "clusters"), &AgeMeshData::set_clusters);
    ClassDB::bind_method(D_METHOD("get_clusters"), &AgeMeshData::get_clusters);
    ClassDB::bind_method(D_METHOD("set_source_triangles", "count"), &AgeMeshData::set_source_triangles);
    ClassDB::bind_method(D_METHOD("get_source_triangles"), &AgeMeshData::get_source_triangles);
    ClassDB::bind_method(D_METHOD("get_cluster_count"), &AgeMeshData::get_cluster_count);
    ClassDB::bind_method(D_METHOD("get_cluster_mesh", "index"), &AgeMeshData::get_cluster_mesh);
    ClassDB::bind_method(D_METHOD("get_cluster_surface", "index"), &AgeMeshData::get_cluster_surface);
    ClassDB::bind_method(D_METHOD("set_cache_version", "version"), &AgeMeshData::set_cache_version);
    ClassDB::bind_method(D_METHOD("get_cache_version"), &AgeMeshData::get_cache_version);
    ClassDB::bind_method(D_METHOD("get_cache_error"), &AgeMeshData::get_cache_error);
    ClassDB::bind_method(D_METHOD("set_gpu_payloads", "payloads"), &AgeMeshData::set_gpu_payloads);
    ClassDB::bind_method(D_METHOD("get_gpu_payloads"), &AgeMeshData::get_gpu_payloads);
    ClassDB::bind_method(D_METHOD("build_render_batches", "clusters_per_batch"), &AgeMeshData::build_render_batches, DEFVAL(8));
    ADD_PROPERTY(PropertyInfo(Variant::INT, "cache_version", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_cache_version", "get_cache_version");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "source", PROPERTY_HINT_RESOURCE_TYPE, "Mesh", PROPERTY_USAGE_STORAGE), "set_source", "get_source");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "clusters", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_clusters", "get_clusters");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "gpu_payloads", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_gpu_payloads", "get_gpu_payloads");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "source_triangles", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_source_triangles", "get_source_triangles");
}
