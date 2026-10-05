// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh.h"
#include "scene/resources/material.h"

namespace {
template <typename T>
void append_array(Array &r_arrays, const Array &p_source, int p_slot) {
    if (p_source[p_slot].get_type() == Variant::NIL) { return; }
    Vector<T> values = r_arrays[p_slot];
    Vector<T> source = p_source[p_slot];
    values.append_array(source);
    r_arrays[p_slot] = values;
}

void append_vertices(Array &r_arrays, const Array &p_source) {
    append_array<Vector3>(r_arrays, p_source, Mesh::ARRAY_VERTEX);
    append_array<Vector3>(r_arrays, p_source, Mesh::ARRAY_NORMAL);
    append_array<float>(r_arrays, p_source, Mesh::ARRAY_TANGENT);
    append_array<Color>(r_arrays, p_source, Mesh::ARRAY_COLOR);
    append_array<Vector2>(r_arrays, p_source, Mesh::ARRAY_TEX_UV);
    append_array<Vector2>(r_arrays, p_source, Mesh::ARRAY_TEX_UV2);
    append_array<int>(r_arrays, p_source, Mesh::ARRAY_BONES);
    append_array<float>(r_arrays, p_source, Mesh::ARRAY_WEIGHTS);
}

bool can_merge(const Ref<Mesh> &p_source, int p_surface) {
    if (p_source->surface_get_primitive_type(p_surface) != Mesh::PRIMITIVE_TRIANGLES) { return false; }
    uint64_t format = p_source->surface_get_format(p_surface);
    if (format & (Mesh::ARRAY_FLAG_USE_DYNAMIC_UPDATE | Mesh::ARRAY_FLAG_USE_2D_VERTICES |
            Mesh::ARRAY_FORMAT_CUSTOM0 | Mesh::ARRAY_FORMAT_CUSTOM1 | Mesh::ARRAY_FORMAT_CUSTOM2 | Mesh::ARRAY_FORMAT_CUSTOM3)) { return false; }
    // Do not silently remove source LODs when merging animated geometry.
    if (!p_source->surface_get_lods(p_surface).is_empty()) { return false; }
    Ref<Material> material = p_source->surface_get_material(p_surface);
    if (material.is_null()) { return true; }
    Ref<BaseMaterial3D> base = material;
    // Alpha blend order, VERTEX_ID-dependent shaders and extra passes must retain
    // their original surface boundaries, indices and renderer sorting behavior.
    return base.is_valid() && base->get_transparency() == BaseMaterial3D::TRANSPARENCY_DISABLED &&
            base->get_next_pass().is_null() && !base->is_grow_enabled() &&
            base->get_billboard_mode() == BaseMaterial3D::BILLBOARD_DISABLED;
}
}

bool AgeMeshData::requires_native_geometry(const Ref<Mesh> &p_mesh) {
    if (p_mesh.is_null()) { return true; }
    if (!get_unsupported_reason(p_mesh).is_empty()) { return true; }
    for (int s = 0; s < p_mesh->get_surface_count(); s++) {
        if (p_mesh->surface_get_format(s) & Mesh::ARRAY_FLAG_USE_DYNAMIC_UPDATE) { return true; }
    }
    return false;
}

Ref<Mesh> AgeMeshData::build_native_mesh(bool p_allow_merge) const {
    if (source.is_null() || !p_allow_merge) { return source; }
    Ref<ArrayMesh> array_source = source;
    // A shadow mesh has its own surface layout. Preserve it rather than silently
    // replacing an authored/optimized shadow path with merged color geometry.
    if (array_source.is_valid() && array_source->get_shadow_mesh().is_valid()) { return source; }
    // Region updates must remain on the exact source buffers; never read them
    // back or duplicate/upload the entire mesh in response to a small update.
    for (int s = 0; s < source->get_surface_count(); s++) {
        if (source->surface_get_format(s) & Mesh::ARRAY_FLAG_USE_DYNAMIC_UPDATE) { return source; }
    }
    bool merge = false;
    for (int s = 1; s < source->get_surface_count(); s++) {
        if (can_merge(source, s - 1) && can_merge(source, s) &&
                source->surface_get_material(s) == source->surface_get_material(s - 1) &&
                source->surface_get_format(s) == source->surface_get_format(s - 1)) { merge = true; break; }
    }
    if (!merge) { return source; }
    Ref<ArrayMesh> result;
    result.instantiate();
    for (int b = 0; b < source->get_blend_shape_count(); b++) { result->add_blend_shape(source->get_blend_shape_name(b)); }
    if (array_source.is_valid()) { result->set_blend_shape_mode(array_source->get_blend_shape_mode()); }
    for (int first = 0; first < source->get_surface_count();) {
        int end = first + 1;
        while (end < source->get_surface_count() && can_merge(source, first) && can_merge(source, end) &&
                source->surface_get_material(end) == source->surface_get_material(first) &&
                source->surface_get_format(end) == source->surface_get_format(first)) { end++; }
        Array arrays = source->surface_get_arrays(first);
        Array shapes = source->surface_get_blend_shape_arrays(first);
        if (end > first + 1) {
            arrays = Array(); arrays.resize(Mesh::ARRAY_MAX);
            shapes = Array(); shapes.resize(source->get_blend_shape_count());
            for (int b = 0; b < shapes.size(); b++) { Array a; a.resize(Mesh::ARRAY_MAX); shapes[b] = a; }
            Vector<int> indices;
            int offset = 0;
            for (int s = first; s < end; s++) {
                Array local = source->surface_get_arrays(s);
                Vector<Vector3> vertices = local[Mesh::ARRAY_VERTEX];
                Vector<int> local_indices = local[Mesh::ARRAY_INDEX];
                if (local_indices.is_empty()) {
                    for (int i = 0; i < vertices.size(); i++) { indices.push_back(offset + i); }
                } else {
                    for (int index : local_indices) { indices.push_back(offset + index); }
                }
                append_vertices(arrays, local);
                Array local_shapes = source->surface_get_blend_shape_arrays(s);
                ERR_FAIL_COND_V(local_shapes.size() != shapes.size(), source);
                for (int b = 0; b < shapes.size(); b++) {
                    Array combined = shapes[b]; append_vertices(combined, local_shapes[b]); shapes[b] = combined;
                }
                offset += vertices.size();
            }
            arrays[Mesh::ARRAY_INDEX] = indices;
        }
        uint64_t flags = source->surface_get_format(first) & ~(uint64_t(Mesh::ARRAY_FLAG_FORMAT_VERSION_MASK) << Mesh::ARRAY_FLAG_FORMAT_VERSION_SHIFT);
        result->add_surface_from_arrays(source->surface_get_primitive_type(first), arrays, shapes,
                end == first + 1 ? source->surface_get_lods(first) : Dictionary(), flags);
        result->surface_set_material(result->get_surface_count() - 1, source->surface_get_material(first));
        first = end;
    }
    // Keep the original object-wide sorting/bounds domain, especially for mixed
    // opaque and transparent meshes. Skeleton/shape processing stays native.
    if (array_source.is_valid()) { result->set_custom_aabb(array_source->get_custom_aabb()); }
    return result;
}
