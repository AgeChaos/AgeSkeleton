// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_instance.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/resources/material.h"
#include "age_mesh_gpu.h"
#include "age_mesh_dynamic_gpu.h"
#include "age_mesh_preparation.h"
#include "age_mesh_settings.h"
#include "servers/rendering/rendering_server.h"

void AgeMeshInstance3D::disconnect_resources() {
    for (const Ref<Material> &material : watched_materials) { material->disconnect_changed(callable_mp(this, &AgeMeshInstance3D::material_changed)); }
    watched_materials.clear();
    for (const Ref<ArrayMesh> &mesh : watched_clusters) { mesh->disconnect_changed(callable_mp(this, &AgeMeshInstance3D::source_changed)); }
    watched_clusters.clear();
    if (watched_source.is_valid()) { watched_source->disconnect_changed(callable_mp(this, &AgeMeshInstance3D::source_changed)); }
    watched_source.unref();
    if (data.is_valid()) { data->disconnect_changed(callable_mp(this, &AgeMeshInstance3D::source_changed)); }
}

void AgeMeshInstance3D::set_data(const Ref<AgeMeshData> &p_data) {
    deactivate();
    disconnect_resources();
    observed_cluster_count = -1;
    data = p_data;
    source_dirty = false;
    material_supported = data.is_valid() && AgeMeshData::get_material_unsupported_reason(data->get_source()).is_empty();
    material_state = compute_material_state();
    cache_checked = false;
    gpu_cache_checked = false;
    prepared_gpu_data.clear();
    render_batches.clear();
    batch_size = 0;
    if (data.is_valid()) {
        data->connect_changed(callable_mp(this, &AgeMeshInstance3D::source_changed));
        watched_source = data->get_source();
        if (watched_source.is_valid()) { watched_source->connect_changed(callable_mp(this, &AgeMeshInstance3D::source_changed)); }
        for (int i = 0; i < data->get_cluster_count(); i++) {
            Ref<ArrayMesh> mesh = data->get_cluster_mesh(i);
            if (mesh.is_valid() && mesh != watched_source && !watched_clusters.has(mesh)) {
                watched_clusters.push_back(mesh);
                mesh->connect_changed(callable_mp(this, &AgeMeshInstance3D::source_changed));
            }
        }
    }
    if (data.is_valid() && data->get_source().is_valid()) {
        for (int s = 0; s < data->get_source()->get_surface_count(); s++) {
            Ref<Material> material = data->get_source()->surface_get_material(s);
            if (material.is_valid() && !watched_materials.has(material)) { watched_materials.push_back(material); material->connect_changed(callable_mp(this, &AgeMeshInstance3D::material_changed)); }
        }
    }
    if (is_inside_tree()) { refresh(); }
}

uint64_t AgeMeshInstance3D::compute_material_state() const {
    uint64_t state = 1469598103934665603ULL;
    if (!data.is_valid() || !data->get_source().is_valid()) return state;
    Ref<Mesh> mesh = data->get_source();
    for (int i = 0; i < mesh->get_surface_count(); i++) {
        Ref<Material> m = mesh->surface_get_material(i);
        uint64_t id = m.is_valid() ? uint64_t(m->get_instance_id()) : uint64_t(0); state ^= id; state *= 1099511628211ULL;
        if (m.is_valid()) {
            state ^= m->get_next_pass().is_valid() ? uint64_t(m->get_next_pass()->get_instance_id()) : 0; state *= 1099511628211ULL;
            Ref<BaseMaterial3D> b = m;
            if (b.is_valid()) {
                state ^= uint64_t(b->get_transparency()); state *= 1099511628211ULL;
                state ^= uint64_t(b->get_billboard_mode()); state *= 1099511628211ULL;
                state ^= uint64_t(b->is_grow_enabled()); state *= 1099511628211ULL;
            }
        }
    }
    return state;
}

void AgeMeshInstance3D::material_changed() {
    // Materials are shared by the cached surfaces. Color/roughness changes do
    // not invalidate geometry. Unsupported modes fall back, and can recover
    // when the same material returns to a supported mode.
    material_supported = data.is_valid() && AgeMeshData::get_material_unsupported_reason(data->get_source()).is_empty();
    uint64_t state = compute_material_state();
    native_dirty = native_dirty || state != material_state;
    material_state = state;
    if (is_inside_tree() && ((!material_supported && !native_active) || (native_active && native_dirty))) { deactivate(); }
}

void AgeMeshInstance3D::source_changed() {
    if (native_active && native_render_mesh == data->get_source()) {
        Ref<ArrayMesh> array_mesh = native_render_mesh;
        if (array_mesh.is_valid() && array_mesh->get_topology_revision() == native_topology_revision) { source_dirty = true; return; }
    }
    source_dirty = true; native_dirty = true; render_batches.clear();
    if (native_active && native_render_mesh == data->get_source() && dynamic_gpu_mesh.is_null()) { return; }
    if (is_inside_tree()) { deactivate(); }
}

void AgeMeshInstance3D::restore_owner_base(MeshInstance3D *p_owner) {
    if (!p_owner || p_owner->get_mesh().is_null()) { return; }
    if (p_owner->get_base() == p_owner->get_mesh()->get_rid()) { return; }
    p_owner->set_base(p_owner->get_mesh()->get_rid());
    // Changing the rendering base resets weights/overrides in the render server.
    // During mesh replacement the new shape tracks may not yet be initialized.
    int count = native_render_mesh.is_valid() ? MIN(native_render_mesh->get_blend_shape_count(), p_owner->get_blend_shape_count()) : 0;
    for (int b = 0; b < count; b++) { p_owner->set_blend_shape_value(b, p_owner->get_blend_shape_value(b)); }
    for (int s = 0; s < p_owner->get_surface_override_material_count() && s < p_owner->get_mesh()->get_surface_count(); s++) {
        Ref<Material> material = p_owner->get_surface_override_material(s);
        if (material.is_valid()) { RenderingServer::get_singleton()->instance_set_surface_override_material(p_owner->get_instance(), s, material->get_rid()); }
    }
}

void AgeMeshInstance3D::owner_render_state_changed() {
    if (native_active && native_render_mesh == data->get_source()) {
        Ref<ArrayMesh> array_mesh = native_render_mesh;
        if (array_mesh.is_valid() && array_mesh->get_topology_revision() == native_topology_revision) { return; }
    }
    // Restore original surface indices BEFORE a surface override or mesh change
    // reaches the server. A merged mesh may have fewer surfaces than its source.
    if (native_active && native_render_mesh != data->get_source()) { deactivate(); }
    native_dirty = true;
}

void AgeMeshInstance3D::set_dynamic_updates(bool p_enabled) {
    if (dynamic_updates == p_enabled) { return; }
    deactivate(); dynamic_updates = p_enabled; native_dirty = true;
}

void AgeMeshInstance3D::refresh_native(MeshInstance3D *p_owner) {
    // Baked lighting is attached to the original instance RID and surface UV2.
    // Keep that instance and only replace its draw indices, never its geometry.
    bool baked = p_owner->get_gi_mode() == GeometryInstance3D::GI_MODE_STATIC;
    bool baked_static = baked && !dynamic_updates && !data->is_native_geometry();
    bool use_gpu = AgeMeshSettings::gpu_enabled() && (baked_static || bool(GLOBAL_GET("rendering/agemesh/dynamic_gpu_culling"))) && RenderingServer::get_singleton()->get_current_rendering_method() == "forward_plus";
    bool allow_merge = !dynamic_updates && p_owner->get_material_override().is_null() &&
            p_owner->get_material_overlay().is_null() && p_owner->get_transparency() == 0 &&
            p_owner->get_gi_mode() != GeometryInstance3D::GI_MODE_STATIC;
    for (int s = 0; s < p_owner->get_surface_override_material_count(); s++) {
        if (p_owner->get_surface_override_material(s).is_valid()) { allow_merge = false; }
    }
    if (native_active && !native_dirty && allow_merge == native_merge_allowed && use_gpu == dynamic_gpu_mesh.is_valid()) { return; }
    if (native_active && native_render_mesh == data->get_source() && !allow_merge && !use_gpu && dynamic_gpu_mesh.is_null()) {
        native_dirty = false; native_merge_allowed = false; return;
    }
    deactivate();
    native_render_mesh = data->build_native_mesh(allow_merge);
    if (native_render_mesh.is_null()) { return; }
    owner_id = p_owner->get_instance_id();
    if (p_owner->get_base() != native_render_mesh->get_rid()) { p_owner->set_base(native_render_mesh->get_rid()); }
    for (int b = 0; b < p_owner->get_blend_shape_count(); b++) { p_owner->set_blend_shape_value(b, p_owner->get_blend_shape_value(b)); }
    // The original MeshInstance owns all animation, skeleton, visibility, shadow,
    // transparency and shader parameters. No per-frame pose copy or readback.
    native_active = active = true; native_dirty = false; native_merge_allowed = allow_merge;
    native_rebuilds++;
    Ref<ArrayMesh> source_array = native_render_mesh;
    native_topology_revision = source_array.is_valid() ? source_array->get_topology_revision() : 0;
    if (use_gpu) {
        dynamic_gpu_mesh = native_render_mesh->get_rid();
        RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&AgeMeshDynamicGpu::register_mesh).bind(dynamic_gpu_mesh, native_topology_revision, int(GLOBAL_GET("rendering/agemesh/gpu_working_memory_mb"))));
    }
}

void AgeMeshInstance3D::deactivate() {
    if (dynamic_gpu_mesh.is_valid()) {
        RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&AgeMeshDynamicGpu::unregister_mesh).bind(dynamic_gpu_mesh));
        dynamic_gpu_mesh = RID();
    }
    if (preparation) { AgeMeshPreparation::retire(preparation); preparation = nullptr; }
    for (RID mesh : gpu_meshes) { RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&AgeMeshGpu::unregister_mesh).bind(mesh)); }
    gpu_meshes.clear();
    if (gpu_mode) { render_batches.clear(); }
    if (active) {
        auto *owner = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(owner_id));
        restore_owner_base(owner);
    }
    active = false;
    native_active = false;
    native_render_mesh.unref();
    synced_properties.clear();
    for (MeshInstance3D *part : parts) { memdelete(part); }
    parts.clear();
}

void AgeMeshInstance3D::settings_changed() {
    if (is_inside_tree()) {
        set_process_internal(AgeMeshSettings::enabled());
        refresh();
    }
}

void AgeMeshInstance3D::sync_properties(MeshInstance3D *p_source) {
    Array current;
    current.push_back(p_source->get_layer_mask());
    current.push_back(p_source->get_cast_shadows_setting());
    current.push_back(p_source->get_extra_cull_margin());
    current.push_back(p_source->get_lod_bias());
    current.push_back(p_source->get_gi_mode());
    current.push_back(p_source->is_ignoring_occlusion_culling());
    current.push_back(p_source->get_sorting_offset());
    current.push_back(p_source->get_visibility_range_begin());
    current.push_back(p_source->get_visibility_range_end());
    current.push_back(p_source->get_visibility_range_begin_margin());
    current.push_back(p_source->get_visibility_range_end_margin());
    current.push_back(p_source->get_visibility_range_fade_mode());
    if (current == synced_properties) { return; }
    synced_properties = current;
    for (RID mesh : gpu_meshes) {
        RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&AgeMeshGpu::set_ignore_occlusion).bind(mesh, p_source->is_ignoring_occlusion_culling()));
    }
    for (int i = 0; i < parts.size(); i++) {
        MeshInstance3D *part = parts[i];
        part->set_layer_mask(p_source->get_layer_mask());
        part->set_cast_shadows_setting(p_source->get_cast_shadows_setting());
        part->set_extra_cull_margin(p_source->get_extra_cull_margin());
        part->set_lod_bias(p_source->get_lod_bias());
        part->set_gi_mode(p_source->get_gi_mode());
        part->set_ignore_occlusion_culling(p_source->is_ignoring_occlusion_culling());
        part->set_sorting_offset(p_source->get_sorting_offset());
        part->set_visibility_range_begin(p_source->get_visibility_range_begin());
        part->set_visibility_range_end(p_source->get_visibility_range_end());
        part->set_visibility_range_begin_margin(p_source->get_visibility_range_begin_margin());
        part->set_visibility_range_end_margin(p_source->get_visibility_range_end_margin());
        part->set_visibility_range_fade_mode(p_source->get_visibility_range_fade_mode());
    }
}

void AgeMeshInstance3D::refresh() {
    auto *owner = Object::cast_to<MeshInstance3D>(get_parent());
    uint64_t current_material_state = compute_material_state();
    if (current_material_state != material_state) { material_changed(); }
    bool enabled = AgeMeshSettings::enabled();
    bool use_native = data.is_valid() && (dynamic_updates || data->is_native_geometry() || !material_supported || (owner && owner->get_gi_mode() == GeometryInstance3D::GI_MODE_STATIC));
    if (use_native && owner && owner->get_mesh().is_valid() && data->get_source() != owner->get_mesh()) {
        set_data(AgeMeshData::build(owner->get_mesh()));
        return;
    }
    if (use_native && enabled && owner && data->get_source().is_valid() &&
            owner->get_mesh() == data->get_source() && data->get_cache_version() == 1 && get_transform() == Transform3D()) {
        refresh_native(owner);
        return;
    }
    if (native_active) { deactivate(); }
    bool supported = !source_dirty && material_supported && owner && data.is_valid() && data->get_source().is_valid() && data->get_source() == owner->get_mesh() && data->get_cluster_count() > 0;
    if (supported) {
        supported = owner->get_skin().is_null() && owner->get_material_override().is_null() && owner->get_material_overlay().is_null() &&
                owner->get_transparency() == 0 && owner->get_custom_aabb() == AABB() && owner->get_visibility_parent().is_empty() &&
                owner->get_gi_mode() != GeometryInstance3D::GI_MODE_STATIC &&
                owner->get_visibility_range_begin() == 0 && owner->get_visibility_range_end() == 0 &&
                get_transform() == Transform3D();
        for (int s = 0; s < owner->get_surface_override_material_count(); s++) {
            if (owner->get_surface_override_material(s).is_valid()) { supported = false; }
        }
        // Imported static materials are validated at build time. A modified source
        // must be reimported before its cached cluster geometry can be used again.
    }
    if (!enabled || !supported) { deactivate(); return; }
    bool requested_gpu = AgeMeshSettings::gpu_enabled() && RenderingServer::get_singleton()->get_current_rendering_method() == "forward_plus";
    bool packed_gpu = requested_gpu && data->has_gpu_payloads();
    if (packed_gpu && !gpu_cache_checked) {
        if (data->get_cache_version() != 1) { source_dirty = true; deactivate(); return; }
        if (!preparation) { preparation = memnew(AgeMeshPreparation(data)); return; }
        if (!preparation->completed()) { return; }
        String error = preparation->error;
        if (error.is_empty()) { prepared_gpu_data = preparation->packed_surfaces; }
        memdelete(preparation); preparation = nullptr;
        if (!error.is_empty()) { source_dirty = true; deactivate(); WARN_PRINT("AgeMesh packed fallback: " + error); return; }
        gpu_cache_checked = true;
    }
    if (!packed_gpu && !cache_checked) {
        cache_checked = true;
        String error = data->get_cache_error();
        if (!error.is_empty()) {
            source_dirty = true;
            deactivate();
            WARN_PRINT("AgeMesh fallback: " + error);
            return;
        }
    }
    if (observed_cluster_count != data->get_cluster_count()) { deactivate(); observed_cluster_count = data->get_cluster_count(); }
    int limit = int(GLOBAL_GET("rendering/agemesh/prototype_cluster_limit"));
    if (data->get_cluster_count() > limit) { deactivate(); return; }
    int requested_batch_size = CLAMP(int(GLOBAL_GET("rendering/agemesh/clusters_per_batch")), 1, 64);
    if (requested_gpu != gpu_mode) { deactivate(); render_batches.clear(); gpu_mode = requested_gpu; }
    if (batch_size != requested_batch_size) { deactivate(); render_batches.clear(); batch_size = requested_batch_size; }
    if (active) { sync_properties(owner); return; }
    if (render_batches.is_empty()) {
        if (gpu_mode) {
            Array surfaces;
            if (packed_gpu && !prepared_gpu_data.is_empty()) {
                Array payloads = data->get_gpu_payloads();
                for (int i = 0; i < prepared_gpu_data.size(); i++) {
                    const RenderingServerTypes::SurfaceData &packed = prepared_gpu_data[i];
                    Ref<ArrayMesh> mesh; mesh.instantiate();
                    mesh->add_surface(packed.format, Mesh::PrimitiveType(packed.primitive), packed.vertex_data, packed.attribute_data, packed.skin_data, packed.vertex_count, packed.index_data, packed.index_count, packed.aabb, packed.blend_shape_data, packed.bone_aabbs, packed.lods, packed.uv_scale);
                    Dictionary item = payloads[i];
                    mesh->surface_set_material(0, data->get_source()->surface_get_material(int(item["surface"])));
                    Dictionary surface; surface["mesh"] = mesh; surface["clusters"] = item["clusters"]; surface["indices"] = item["indices"];
                    surfaces.push_back(surface);
                }
            } else { surfaces = data->build_gpu_surfaces(); }
            for (int i = 0; i < surfaces.size(); i++) {
                Dictionary item = surfaces[i]; Ref<ArrayMesh> mesh = item["mesh"];
                render_batches.push_back(mesh); gpu_meshes.push_back(mesh->get_rid());
                RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&AgeMeshGpu::register_mesh).bind(mesh->get_rid(), item["clusters"], item["indices"], int(GLOBAL_GET("rendering/agemesh/gpu_working_memory_mb")), int(GLOBAL_GET("rendering/agemesh/upload_mb_per_frame"))));
            }
        } else { render_batches = data->build_render_batches(batch_size); }
    }
    if (render_batches.is_empty()) { return; }
    for (int i = 0; i < render_batches.size(); i++) {
        Ref<ArrayMesh> mesh = render_batches[i];
        if (mesh.is_null() || mesh->get_surface_count() != 1) {
            deactivate();
            WARN_PRINT("AgeMesh cache is invalid; using original mesh.");
            return;
        }
        auto *part = memnew(MeshInstance3D);
        part->set_mesh(mesh);
        parts.push_back(part);
        add_child(part, false, Node::INTERNAL_MODE_BACK);
    }
    owner_id = owner->get_instance_id();
    active = true;
    sync_properties(owner);
    owner->set_base(RID());
}

void AgeMeshInstance3D::_notification(int p_what) {
    if (p_what == NOTIFICATION_ENTER_TREE) {
        auto *owner = Object::cast_to<MeshInstance3D>(get_parent());
        if (owner) { owner->connect("mesh_render_state_changed", callable_mp(this, &AgeMeshInstance3D::owner_render_state_changed)); }
        if (owner) { owner->connect("geometry_render_state_changed", callable_mp(this, &AgeMeshInstance3D::owner_render_state_changed)); }
        ProjectSettings::get_singleton()->connect("settings_changed", callable_mp(this, &AgeMeshInstance3D::settings_changed));
        set_process_internal(AgeMeshSettings::enabled());
    } else if (p_what == NOTIFICATION_INTERNAL_PROCESS) {
        if (AgeMeshSettings::enabled() || active) { refresh(); }
    } else if (p_what == NOTIFICATION_PREDELETE) {
        if (preparation) { AgeMeshPreparation::retire(preparation); preparation = nullptr; }
        disconnect_resources();
    } else if (p_what == NOTIFICATION_EXIT_TREE) {
        auto *owner = Object::cast_to<MeshInstance3D>(get_parent());
        if (owner) { owner->disconnect("mesh_render_state_changed", callable_mp(this, &AgeMeshInstance3D::owner_render_state_changed)); }
        if (owner) { owner->disconnect("geometry_render_state_changed", callable_mp(this, &AgeMeshInstance3D::owner_render_state_changed)); }
        ProjectSettings::get_singleton()->disconnect("settings_changed", callable_mp(this, &AgeMeshInstance3D::settings_changed));
        deactivate();
    }
}

Dictionary AgeMeshInstance3D::get_statistics() const {
    Dictionary result;
    result["active"] = active;
    result["source_dirty"] = source_dirty;
    result["material_supported"] = material_supported;
    result["preparing"] = preparation != nullptr;
    result["prepacked_geometry"] = data.is_valid() && data->has_gpu_payloads();
    bool uploaded = active && gpu_mode && !gpu_meshes.is_empty();
    for (RID mesh : gpu_meshes) { uploaded = uploaded && AgeMeshGpu::is_uploaded(mesh); }
    if (dynamic_gpu_mesh.is_valid()) { uploaded = AgeMeshDynamicGpu::is_uploaded(dynamic_gpu_mesh); }
    result["gpu_ready"] = uploaded;
    result["dynamic_gpu_requested"] = dynamic_gpu_mesh.is_valid();
    result["source_triangles"] = data.is_valid() ? data->get_source_triangles() : 0;
    result["clusters"] = data.is_valid() ? data->get_cluster_count() : 0;
    result["resident_clusters"] = active && data.is_valid() ? data->get_cluster_count() : 0;
    result["render_batches"] = parts.size();
    result["dynamic_updates"] = dynamic_updates;
    result["export_mode"] = AgeMeshSettings::export_mode();
    result["native_rebuilds"] = native_rebuilds;
    result["source_surfaces"] = data.is_valid() && data->get_source().is_valid() ? data->get_source()->get_surface_count() : 0;
    result["merged_surfaces"] = native_active && native_render_mesh != data->get_source();
    if (native_active) { result["render_batches"] = native_render_mesh->get_surface_count(); }
    result["clusters_per_batch"] = batch_size;
    result["backend"] = active ? (gpu_mode ? "gpu_compacted_indices" : "prototype_native_batches") : "ordinary_mesh";
    if (native_active) { result["backend"] = native_render_mesh == data->get_source() ? "native_live_mesh" : "native_deformation_batches"; }
    return result;
}

void AgeMeshInstance3D::request_gpu_debug_readback() {
    if (dynamic_gpu_mesh.is_valid()) { RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&AgeMeshDynamicGpu::debug_readback).bind(dynamic_gpu_mesh)); }
    for (RID mesh : gpu_meshes) { RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&AgeMeshGpu::debug_readback).bind(mesh)); }
}

void AgeMeshInstance3D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_dynamic_updates", "enabled"), &AgeMeshInstance3D::set_dynamic_updates);
    ClassDB::bind_method(D_METHOD("get_dynamic_updates"), &AgeMeshInstance3D::get_dynamic_updates);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dynamic_updates"), "set_dynamic_updates", "get_dynamic_updates");
    ClassDB::bind_method(D_METHOD("set_data", "data"), &AgeMeshInstance3D::set_data);
    ClassDB::bind_method(D_METHOD("get_data"), &AgeMeshInstance3D::get_data);
    ClassDB::bind_method(D_METHOD("is_active"), &AgeMeshInstance3D::is_active);
    ClassDB::bind_method(D_METHOD("get_statistics"), &AgeMeshInstance3D::get_statistics);
    ClassDB::bind_method(D_METHOD("request_gpu_debug_readback"), &AgeMeshInstance3D::request_gpu_debug_readback);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "data", PROPERTY_HINT_RESOURCE_TYPE, "AgeMeshData"), "set_data", "get_data");
}
