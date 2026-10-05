// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh.h"
#include "age_mesh_gpu_layout.h"
#include <algorithm>
#include <vector>

namespace {
template <typename T>
void append_attribute(Vector<T> &r_combined, const Array &p_arrays, int p_attribute) {
    if (p_arrays[p_attribute].get_type() == Variant::NIL) { return; }
    Vector<T> local = p_arrays[p_attribute];
    r_combined.append_array(local);
}

Ref<ArrayMesh> merge_clusters(const Vector<Ref<ArrayMesh>> &p_meshes, bool p_generate_lods = true, Array *r_arrays = nullptr) {
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    Vector<Vector3> positions, normals;
    Vector<float> tangents;
    Vector<Color> colors;
    Vector<Vector2> uv, uv2;
    Vector<int> indices;
    std::vector<int> offsets;
    std::vector<Dictionary> cluster_lods;
    // Three conservative group thresholds. A cluster with no LOD always
    // retains its base indices. Boundaries are never simplified again here.
    double thresholds[3] = {};
    int vertex_offset = 0;
    for (const Ref<ArrayMesh> &mesh : p_meshes) {
        offsets.push_back(vertex_offset);
        Array local = mesh->surface_get_arrays(0);
        append_attribute(positions, local, Mesh::ARRAY_VERTEX);
        append_attribute(normals, local, Mesh::ARRAY_NORMAL);
        append_attribute(tangents, local, Mesh::ARRAY_TANGENT);
        append_attribute(colors, local, Mesh::ARRAY_COLOR);
        append_attribute(uv, local, Mesh::ARRAY_TEX_UV);
        append_attribute(uv2, local, Mesh::ARRAY_TEX_UV2);
        Vector<int> local_indices = local[Mesh::ARRAY_INDEX];
        for (int index : local_indices) { indices.push_back(index + vertex_offset); }
        Vector<Vector3> vertices = local[Mesh::ARRAY_VERTEX];
        vertex_offset += vertices.size();
        if (!p_generate_lods) { continue; }
        Dictionary lods = mesh->surface_get_lods(0);
        cluster_lods.push_back(lods);
        std::vector<double> keys;
        for (const Variant *key = lods.next(); key; key = lods.next(key)) { keys.push_back(double(*key)); }
        std::sort(keys.begin(), keys.end());
        if (!keys.empty()) {
            for (int level = 0; level < 3; level++) {
                thresholds[level] = MAX(thresholds[level], keys[MIN(size_t(level), keys.size() - 1)]);
            }
        }
    }
    arrays[Mesh::ARRAY_INDEX] = indices;
    arrays[Mesh::ARRAY_VERTEX] = positions;
    if (!normals.is_empty()) { arrays[Mesh::ARRAY_NORMAL] = normals; }
    if (!tangents.is_empty()) { arrays[Mesh::ARRAY_TANGENT] = tangents; }
    if (!colors.is_empty()) { arrays[Mesh::ARRAY_COLOR] = colors; }
    if (!uv.is_empty()) { arrays[Mesh::ARRAY_TEX_UV] = uv; }
    if (!uv2.is_empty()) { arrays[Mesh::ARRAY_TEX_UV2] = uv2; }
    Dictionary lods;
    int previous_count = indices.size();
    double previous_threshold = 0;
    for (double threshold : thresholds) {
        if (!p_generate_lods) { break; }
        if (threshold <= previous_threshold) { continue; }
        Vector<int> selected;
        for (int m = 0; m < p_meshes.size(); m++) {
            Array local = p_meshes[m]->surface_get_arrays(0);
            Vector<int> level = local[Mesh::ARRAY_INDEX];
            double best = 0;
            const Dictionary &available = cluster_lods[m];
            for (const Variant *key = available.next(); key; key = available.next(key)) {
                double error = double(*key);
                if (error <= threshold && error > best) { level = available[*key]; best = error; }
            }
            for (int index : level) { selected.push_back(index + offsets[m]); }
        }
        if (selected.size() < previous_count) {
            lods[threshold] = selected;
            previous_count = selected.size();
            previous_threshold = threshold;
        }
    }
    if (r_arrays) { *r_arrays = arrays; return Ref<ArrayMesh>(); }
    Ref<ArrayMesh> batch;
    batch.instantiate();
    batch->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), lods);
    batch->surface_set_material(0, p_meshes[0]->surface_get_material(0));
    return batch;
}
}

Array AgeMeshData::build_render_batches(int p_clusters_per_batch) const {
    ERR_FAIL_COND_V(p_clusters_per_batch < 1 || p_clusters_per_batch > 64, Array());
    if (is_native_geometry()) { Array batches; batches.push_back(build_native_mesh()); return batches; }
    String error = get_cache_error();
    ERR_FAIL_COND_V_MSG(!error.is_empty(), Array(), "AgeMesh: " + error);
    Array batches;
    Vector<Ref<ArrayMesh>> group;
    Ref<Material> previous_material;
    uint64_t previous_format = 0;
    auto flush = [&]() {
        if (group.is_empty()) { return; }
        batches.push_back(group.size() == 1 ? group[0] : merge_clusters(group));
        group.clear();
    };
    for (int i = 0; i < get_cluster_count(); i++) {
        Ref<ArrayMesh> mesh = get_cluster_mesh(i);
        Ref<Material> material = mesh->surface_get_material(0);
        uint64_t format = mesh->surface_get_format(0);
        if (material != previous_material || format != previous_format || group.size() == p_clusters_per_batch) { flush(); }
        previous_material = material;
        previous_format = format;
        group.push_back(mesh);
    }
    flush();
    return batches;
}

Array AgeMeshData::bake_gpu_payloads() const {
    if (is_native_geometry()) { return Array(); }
    Array surfaces;
    Vector<Ref<ArrayMesh>> group;
    int previous_surface = -1;
    uint64_t previous_format = 0;
    auto flush = [&]() {
        if (group.is_empty()) { return; }
        // A unique mesh per scene instance prevents one camera/object transform
        // from overwriting another instance's GPU command and visible indices.
        Array arrays;
        merge_clusters(group, false, &arrays);
        using Cluster = AgeMeshGpuCluster;
        PackedByteArray metadata;
        metadata.resize(group.size() * sizeof(Cluster));
        uint32_t offset = 0, vertex_offset = 0;
        Vector<int> indices = arrays[Mesh::ARRAY_INDEX];
        for (int i = 0; i < group.size(); i++) {
            Cluster cluster = {};
            AABB aabb = group[i]->get_aabb();
            Vector3 center = aabb.get_center(), extent = aabb.size * 0.5;
            for (int axis = 0; axis < 3; axis++) { cluster.center[axis] = center[axis]; cluster.extent[axis] = extent[axis]; }
            uint32_t count = group[i]->surface_get_array_index_len(0);
            cluster.range[0] = offset; cluster.range[1] = count; offset += count;
            cluster.range[2] = vertex_offset; cluster.range[3] = group[i]->surface_get_array_len(0);
            Dictionary lods = group[i]->surface_get_lods(0);
            std::vector<double> errors;
            for (const Variant *key = lods.next(); key; key = lods.next(key)) { errors.push_back(double(*key)); }
            std::sort(errors.begin(), errors.end());
            for (size_t level = 0; level < MIN(size_t(3), errors.size()); level++) {
                Vector<int> selected = lods[errors[level]];
                cluster.lod[level] = {uint32_t(indices.size()), uint32_t(selected.size()), float(errors[level]), 0};
                for (int index : selected) { indices.push_back(index + vertex_offset); }
            }
            vertex_offset += cluster.range[3];
            memcpy(metadata.ptrw() + i * sizeof(Cluster), &cluster, sizeof(Cluster));
        }
        PackedByteArray index_bytes; index_bytes.resize(indices.size() * sizeof(int));
        memcpy(index_bytes.ptrw(), indices.ptr(), index_bytes.size());
        Dictionary item; item["arrays"] = arrays; item["surface"] = previous_surface; item["clusters"] = metadata; item["indices"] = index_bytes;
        item["layout"] = 2;
        surfaces.push_back(item); group.clear();
    };
    for (int i = 0; i < get_cluster_count(); i++) {
        Ref<ArrayMesh> mesh = get_cluster_mesh(i);
        int surface = get_cluster_surface(i); uint64_t format = mesh->surface_get_format(0);
        if (surface != previous_surface || format != previous_format) { flush(); }
        previous_surface = surface; previous_format = format; group.push_back(mesh);
    }
    flush(); return surfaces;
}

Array AgeMeshData::build_gpu_surfaces() const {
    Array payloads = gpu_payloads;
    if (payloads.is_empty()) {
        String error = get_cache_error();
        ERR_FAIL_COND_V_MSG(!error.is_empty(), Array(), "AgeMesh: " + error);
        payloads = bake_gpu_payloads();
    }
    Array surfaces;
    for (int i = 0; i < payloads.size(); i++) {
        Dictionary item = payloads[i];
        Ref<ArrayMesh> mesh; mesh.instantiate();
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, item["arrays"]);
        mesh->surface_set_material(0, source->surface_get_material(int(item["surface"])));
        Dictionary surface; surface["mesh"] = mesh; surface["clusters"] = item["clusters"]; surface["indices"] = item["indices"];
        surfaces.push_back(surface);
    }
    return surfaces;
}

Array AgeMeshData::get_cluster_arrays(int p_begin, int p_count) const {
    ERR_FAIL_COND_V(p_begin < 0 || p_count <= 0 || int64_t(p_begin) + p_count > get_cluster_count(), Array());
    Vector<Ref<ArrayMesh>> meshes;
    int surface = get_cluster_surface(p_begin);
    for (int i = p_begin; i < p_begin + p_count; i++) {
        ERR_FAIL_COND_V(get_cluster_surface(i) != surface, Array());
        meshes.push_back(get_cluster_mesh(i));
    }
    Array arrays; merge_clusters(meshes, false, &arrays); return arrays;
}
