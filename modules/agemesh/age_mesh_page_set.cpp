// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_page_set.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "thirdparty/meshoptimizer/meshoptimizer.h"
#include <cmath>
#include <vector>

namespace {
// Weld identical attributes before simplifying; lock page borders to preserve seams.
Array simplify_page(const Array &p_arrays, float &r_error) {
    Vector<Vector3> positions = p_arrays[Mesh::ARRAY_VERTEX], normals = p_arrays[Mesh::ARRAY_NORMAL];
    Vector<float> tangents = p_arrays[Mesh::ARRAY_TANGENT]; Vector<Color> colors = p_arrays[Mesh::ARRAY_COLOR];
    Vector<Vector2> uv = p_arrays[Mesh::ARRAY_TEX_UV], uv2 = p_arrays[Mesh::ARRAY_TEX_UV2];
    Vector<int> source_indices = p_arrays[Mesh::ARRAY_INDEX];
    struct Vertex { float v[18]; };
    std::vector<Vertex> vertices(positions.size());
    for (int i = 0; i < positions.size(); i++) {
        Vertex &v = vertices[i];
        for (int c = 0; c < 3; c++) { v.v[c] = positions[i][c]; if (!normals.is_empty()) { v.v[3 + c] = normals[i][c]; } }
        for (int c = 0; c < 4; c++) { if (!tangents.is_empty()) { v.v[6 + c] = tangents[i * 4 + c]; } if (!colors.is_empty()) { v.v[10 + c] = colors[i][c]; } }
        for (int c = 0; c < 2; c++) { if (!uv.is_empty()) { v.v[14 + c] = uv[i][c]; } if (!uv2.is_empty()) { v.v[16 + c] = uv2[i][c]; } }
    }
    std::vector<unsigned int> indices(source_indices.size()), remap(vertices.size());
    for (int i = 0; i < source_indices.size(); i++) { indices[i] = source_indices[i]; }
    size_t unique_count = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));
    std::vector<Vertex> unique(unique_count);
    meshopt_remapVertexBuffer(unique.data(), vertices.data(), vertices.size(), sizeof(Vertex), remap.data());
    meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap.data());
    std::vector<unsigned int> selected(indices.size());
    float weights[15]; for (float &weight : weights) { weight = 1.0f; }
    float error = 0;
    size_t count = meshopt_simplifyWithAttributes(selected.data(), indices.data(), indices.size(), unique[0].v, unique.size(), sizeof(Vertex),
            unique[0].v + 3, sizeof(Vertex), weights, 15, nullptr, MIN(size_t(2048 * 3), indices.size()), 0.1f, meshopt_SimplifyLockBorder, &error);
    r_error = error * meshopt_simplifyScale(unique[0].v, unique.size(), sizeof(Vertex));
    if (!count) { r_error = 0; return p_arrays; }
    remap.resize(unique.size());
    size_t compact_count = meshopt_generateVertexRemap(remap.data(), selected.data(), count, unique.data(), unique.size(), sizeof(Vertex));
    std::vector<Vertex> compact(compact_count);
    meshopt_remapVertexBuffer(compact.data(), unique.data(), unique.size(), sizeof(Vertex), remap.data());
    meshopt_remapIndexBuffer(selected.data(), selected.data(), count, remap.data());
    positions.resize(compact_count);
    if (!normals.is_empty()) { normals.resize(compact_count); }
    if (!tangents.is_empty()) { tangents.resize(compact_count * 4); }
    if (!colors.is_empty()) { colors.resize(compact_count); }
    if (!uv.is_empty()) { uv.resize(compact_count); } if (!uv2.is_empty()) { uv2.resize(compact_count); }
    for (size_t i = 0; i < compact_count; i++) {
        const Vertex &v = compact[i];
        for (int c = 0; c < 3; c++) { positions.write[i][c] = v.v[c]; if (!normals.is_empty()) { normals.write[i][c] = v.v[3 + c]; } }
        for (int c = 0; c < 4; c++) { if (!tangents.is_empty()) { tangents.write[i * 4 + c] = v.v[6 + c]; } if (!colors.is_empty()) { colors.write[i][c] = v.v[10 + c]; } }
        for (int c = 0; c < 2; c++) { if (!uv.is_empty()) { uv.write[i][c] = v.v[14 + c]; } if (!uv2.is_empty()) { uv2.write[i][c] = v.v[16 + c]; } }
    }
    Vector<int> output_indices; output_indices.resize(count);
    for (size_t i = 0; i < count; i++) { output_indices.write[i] = selected[i]; }
    Array result; result.resize(Mesh::ARRAY_MAX); result[Mesh::ARRAY_VERTEX] = positions; result[Mesh::ARRAY_INDEX] = output_indices;
    if (!normals.is_empty()) { result[Mesh::ARRAY_NORMAL] = normals; } if (!tangents.is_empty()) { result[Mesh::ARRAY_TANGENT] = tangents; }
    if (!colors.is_empty()) { result[Mesh::ARRAY_COLOR] = colors; } if (!uv.is_empty()) { result[Mesh::ARRAY_TEX_UV] = uv; } if (!uv2.is_empty()) { result[Mesh::ARRAY_TEX_UV2] = uv2; }
    return result;
}
uint64_t mesh_bytes(const Ref<Mesh> &p_mesh) {
    uint64_t bytes = 0;
    for (int i = 0; i < p_mesh->get_surface_count(); i++) {
        // Conservative upper bound for supported vertex attributes, plus indices/LODs.
        bytes += uint64_t(p_mesh->surface_get_array_len(i)) * 128 + uint64_t(p_mesh->surface_get_array_index_len(i)) * 4;
        Dictionary lods = p_mesh->surface_get_lods(i);
        for (const Variant *key = lods.next(); key; key = lods.next(key)) { Vector<int> indices = lods[*key]; bytes += indices.size() * 4; }
    }
    return bytes;
}
uint64_t page_bytes(const Ref<AgeMeshData> &p_data) {
    uint64_t bytes = mesh_bytes(p_data->get_source());
    for (int i = 0; i < p_data->get_cluster_count(); i++) { bytes += mesh_bytes(p_data->get_cluster_mesh(i)); }
    Array payloads = p_data->get_gpu_payloads();
    for (int i = 0; i < payloads.size(); i++) {
        Dictionary payload = payloads[i]; Array arrays = payload["arrays"];
        Vector<Vector3> vertices = arrays[Mesh::ARRAY_VERTEX]; Vector<int> indices = arrays[Mesh::ARRAY_INDEX];
        PackedByteArray records = payload["clusters"], storage = payload["indices"];
        bytes += uint64_t(vertices.size()) * 128 + uint64_t(indices.size()) * 8 + records.size() + storage.size() + 84;
    }
    return bytes;
}
}

Ref<AgeMeshPageSet> AgeMeshPageSet::build(const Ref<AgeMeshData> &p_data, const String &p_directory, int p_clusters_per_page) {
    ERR_FAIL_COND_V(p_data.is_null() || p_clusters_per_page < 8 || p_clusters_per_page > 256 || !p_directory.begins_with("res://"), Ref<AgeMeshPageSet>());
    ERR_FAIL_COND_V_MSG(p_data->is_native_geometry(), Ref<AgeMeshPageSet>(), "AgeMesh: native dynamic/transparent geometry cannot use static geometry pages.");
    String error = p_data->get_cache_error(); ERR_FAIL_COND_V_MSG(!error.is_empty(), Ref<AgeMeshPageSet>(), error);
    ERR_FAIL_COND_V(DirAccess::make_dir_recursive_absolute(p_directory) != OK, Ref<AgeMeshPageSet>());
    Ref<AgeMeshPageSet> result; result.instantiate();
    struct Range { int first, count; };
    Vector<Range> ranges;
    Ref<AgeMeshData> last_page;
    auto create = [&](int first, int count, const PackedInt32Array &children, float minimum_error) -> int {
        Array arrays = p_data->get_cluster_arrays(first, count);
        Vector<Vector3> positions = arrays[Mesh::ARRAY_VERTEX]; AABB bounds(positions[0], Vector3());
        for (const Vector3 &p : positions) { bounds.expand_to(p); }
        // Include the stored child boxes, including their floating-point padding.
        // Rebuilding a box from vertices can round its maximum inward.
        for (int child : children) { Dictionary info = result->pages[child]; bounds.merge_with(info["bounds"]); }
        bounds.grow_by(MAX(real_t(0.00001), bounds.size.length() * real_t(0.000001)));
        float error = 0;
        if (!children.is_empty()) { arrays = simplify_page(arrays, error); error = MAX(error, minimum_error); }
        Ref<ArrayMesh> mesh; mesh.instantiate(); mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        mesh->surface_set_material(0, p_data->get_source()->surface_get_material(p_data->get_cluster_surface(first)));
        Ref<AgeMeshData> page = AgeMeshData::build(mesh, 128);
        if (page.is_null()) { return -1; }
        int id = result->pages.size(); String path = p_directory.path_join("page_" + itos(id) + ".res");
        if (ResourceSaver::save(page, path, ResourceSaver::FLAG_COMPRESS | ResourceSaver::FLAG_CHANGE_PATH) != OK) { return -1; }
        Dictionary info; info["path"] = path; info["bounds"] = bounds; info["error"] = error; info["children"] = children;
        info["geometry_bytes"] = int64_t(page_bytes(page)); info["triangles"] = page->get_source_triangles();
        info["sha256"] = FileAccess::get_sha256(path);
        result->pages.push_back(info); ranges.push_back({first, count});
        last_page = page;
        return id;
    };
    for (int first = 0; first < p_data->get_cluster_count();) {
        int end = first + 1, surface = p_data->get_cluster_surface(first);
        while (end < p_data->get_cluster_count() && p_data->get_cluster_surface(end) == surface) { end++; }
        Vector<int> level;
        for (int start = first; start < end; start += p_clusters_per_page) {
            int id = create(start, MIN(p_clusters_per_page, end - start), PackedInt32Array(), 0);
            ERR_FAIL_COND_V(id < 0, Ref<AgeMeshPageSet>()); level.push_back(id);
        }
        while (level.size() > 1) {
            Vector<int> next;
            for (int start = 0; start < level.size(); start += 8) {
                PackedInt32Array children; float minimum_error = 0; int count = 0;
                for (int i = start; i < MIN(start + 8, level.size()); i++) {
                    int id = level[i]; children.push_back(id); count += ranges[id].count;
                    Dictionary child = result->pages[id]; minimum_error = MAX(minimum_error, float(child["error"]));
                }
                int id = create(ranges[level[start]].first, count, children, minimum_error);
                ERR_FAIL_COND_V(id < 0, Ref<AgeMeshPageSet>()); next.push_back(id);
            }
            level = next;
        }
        result->roots.push_back(level[0]);
        result->root_data.push_back(last_page);
        first = end;
    }
    // Only coarse roots are strong resource references. Descendants are disk paths.
    ERR_FAIL_COND_V_MSG(!result->get_validation_error().is_empty(), Ref<AgeMeshPageSet>(), result->get_validation_error());
    return result;
}
String AgeMeshPageSet::get_validation_error() const {
    if (pages.is_empty() || roots.is_empty() || roots.size() != root_data.size()) { return "empty page hierarchy"; }
    Vector<int> parents; parents.resize(pages.size()); parents.fill(-1);
    for (int i = 0; i < pages.size(); i++) {
        if (pages[i].get_type() != Variant::DICTIONARY) { return "invalid page record"; }
        Dictionary p = pages[i];
        if (p.get("path", Variant()).get_type() != Variant::STRING || !String(p["path"]).begins_with("res://") || p.get("bounds", Variant()).get_type() != Variant::AABB || p.get("children", Variant()).get_type() != Variant::PACKED_INT32_ARRAY ||
                p.get("geometry_bytes", Variant()).get_type() != Variant::INT || int64_t(p["geometry_bytes"]) <= 0 ||
                p.get("triangles", Variant()).get_type() != Variant::INT || int64_t(p["triangles"]) <= 0 ||
                p.get("sha256", Variant()).get_type() != Variant::STRING || String(p["sha256"]).length() != 64 ||
                (p.get("error", Variant()).get_type() != Variant::FLOAT && p.get("error", Variant()).get_type() != Variant::INT)) { return "invalid page fields"; }
        double error = p.get("error", -1.0); AABB bounds = p["bounds"];
        if (!std::isfinite(error) || error < 0 || !bounds.is_finite() || bounds.size.x < 0 || bounds.size.y < 0 || bounds.size.z < 0) { return "invalid page bounds/error"; }
        PackedInt32Array children = p["children"];
        if (children.size() > 8) { return "page fanout exceeds eight"; }
        for (int child : children) {
            if (child < 0 || child >= i || parents[child] != -1) { return "cyclic or shared page child"; }
            parents.write[child] = i;
            Dictionary c = pages[child]; if (double(c["error"]) > error || !bounds.encloses(c["bounds"])) { return "non-monotonic page error or bounds"; }
        }
    }
    for (int i = 0; i < roots.size(); i++) {
        int id = roots[i]; if (id < 0 || id >= pages.size() || parents[id] != -1) { return "invalid hierarchy root"; }
        parents.write[id] = id;
        Ref<AgeMeshData> data = root_data[i]; if (data.is_null() || data->get_source().is_null()) { return "missing root geometry"; }
    }
    for (int parent : parents) { if (parent < 0) { return "unreachable page"; } }
    return String();
}
void AgeMeshPageSet::_bind_methods() {
    ClassDB::bind_static_method("AgeMeshPageSet", D_METHOD("build", "data", "directory", "clusters_per_page"), &AgeMeshPageSet::build, DEFVAL(64));
    ClassDB::bind_method(D_METHOD("set_pages", "pages"), &AgeMeshPageSet::set_pages); ClassDB::bind_method(D_METHOD("get_pages"), &AgeMeshPageSet::get_pages);
    ClassDB::bind_method(D_METHOD("set_roots", "roots"), &AgeMeshPageSet::set_roots); ClassDB::bind_method(D_METHOD("get_roots"), &AgeMeshPageSet::get_roots);
    ClassDB::bind_method(D_METHOD("set_root_data", "data"), &AgeMeshPageSet::set_root_data); ClassDB::bind_method(D_METHOD("get_root_data"), &AgeMeshPageSet::get_root_data);
    ClassDB::bind_method(D_METHOD("get_validation_error"), &AgeMeshPageSet::get_validation_error);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "pages", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_pages", "get_pages");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "roots", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_roots", "get_roots");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "root_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_root_data", "get_root_data");
}
