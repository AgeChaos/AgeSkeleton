// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_streaming.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include <algorithm>
#include <vector>


uint64_t AgeMeshStreamingInstance3D::bytes(int p_id) const { return page_info[p_id].bytes; }
uint64_t AgeMeshStreamingInstance3D::budget() const { return MAX(minimum_bytes, uint64_t(budget_mb) * 1024 * 1024); }
void AgeMeshStreamingInstance3D::clear_pages() {
    pending.clear();
    for (const KeyValue<int, Resident *> &entry : resident) {
        if (entry.value->node) { memdelete(entry.value->node); }
        memdelete(entry.value);
    }
    resident.clear(); failed.clear(); resident_bytes = pending_bytes = 0;
}
void AgeMeshStreamingInstance3D::set_page_set(const Ref<AgeMeshPageSet> &p_value) {
    Ref<AgeMeshPageSet> keep = p_value;
    if (page_set.is_valid()) { page_set->disconnect_changed(callable_mp(this, &AgeMeshStreamingInstance3D::page_set_changed)); }
    clear_pages(); page_set = keep; pages.clear(); roots.clear(); page_info.clear(); error = String(); minimum_bytes = peak_bytes = requests = evictions = 0;
    if (page_set.is_valid()) {
        page_set->connect_changed(callable_mp(this, &AgeMeshStreamingInstance3D::page_set_changed));
        error = page_set->get_validation_error();
        if (!error.is_empty()) { WARN_PRINT("AgeMesh page hierarchy: " + error); return; }
        pages = page_set->get_pages(); roots = page_set->get_roots();
        page_info.resize(pages.size());
        for (int i = 0; i < pages.size(); i++) {
            Dictionary info = pages[i];
            PageInfo &entry = page_info.write[i];
            entry.bounds = info["bounds"]; entry.children = info["children"];
            entry.error = info["error"]; entry.bytes = int64_t(info["geometry_bytes"]);
        }
        for (int id : roots) { minimum_bytes += bytes(id); }
        initialize_roots();
    }
}
void AgeMeshStreamingInstance3D::page_set_changed() { set_page_set(page_set); }
void AgeMeshStreamingInstance3D::initialize_roots() {
    if (page_set.is_null() || roots.is_empty() || !resident.is_empty()) { return; }
    Array root_geometry = page_set->get_root_data();
    for (int i = 0; i < roots.size(); i++) {
        auto *entry = memnew(Resident); entry->data = root_geometry[i]; resident.insert(roots[i], entry); resident_bytes += bytes(roots[i]); show_page(roots[i]);
    }
    peak_bytes = MAX(peak_bytes, resident_bytes);
}
void AgeMeshStreamingInstance3D::show_page(int p_id) {
    Resident **entry = resident.getptr(p_id); if (!entry || (*entry)->node) { return; }
    auto *node = memnew(MeshInstance3D); node->set_mesh((*entry)->data->get_source()); node->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
    Dictionary info = pages[p_id]; node->set_extra_cull_margin(float(info["error"]));
    auto *helper = memnew(AgeMeshInstance3D); helper->set_data((*entry)->data); node->add_child(helper, false, Node::INTERNAL_MODE_BACK);
    (*entry)->node = node; add_child(node, false, Node::INTERNAL_MODE_BACK);
}
void AgeMeshStreamingInstance3D::hide_page(int p_id) {
    Resident **entry = resident.getptr(p_id);
    if (entry && (*entry)->node) { memdelete((*entry)->node); (*entry)->node = nullptr; }
}
void AgeMeshStreamingInstance3D::drop_page(int p_id) {
    if (pending.has(p_id)) { pending.erase(p_id); pending_bytes -= bytes(p_id); }
    Resident **entry = resident.getptr(p_id);
    if (!entry) { return; }
    hide_page(p_id); memdelete(*entry); resident.erase(p_id); resident_bytes -= bytes(p_id); evictions++;
}
bool AgeMeshStreamingInstance3D::claim_changes(int p_count) {
    if (p_count == 0) { return true; }
    if (p_count > changes_remaining || OS::get_singleton()->get_ticks_usec() >= selection_deadline) { return false; }
    changes_remaining -= p_count; changes_this_frame += p_count;
    return true;
}
bool AgeMeshStreamingInstance3D::coarsen_page(int p_id, bool p_visible) {
    // Collapse bottom-up, one complete sibling group at a time. Until the
    // atomic swap fits this frame's budget, the previous covering set stays.
    // This avoids holes and avoids destroying hundreds of pages after a teleport.
    Resident **entry = resident.getptr(p_id);
    if (!entry) { return true; }
    bool expanded = (*entry)->node == nullptr;
    const PackedInt32Array &children = page_info[p_id].children;
    for (int child : children) {
        if (!coarsen_page(child, p_visible && expanded)) { return false; }
    }
    int changes = p_visible && expanded ? 1 : 0;
    for (int child : children) { if (resident.has(child) || pending.has(child)) { changes++; } }
    if (!claim_changes(changes)) { return false; }
    if (p_visible) { show_page(p_id); }
    for (int child : children) { drop_page(child); }
    return true;
}
void AgeMeshStreamingInstance3D::poll_pages() {
    Vector<int> completed;
    for (const KeyValue<int, Ref<AgeMeshPageJob>> &entry : pending) { if (entry.value->completed()) { completed.push_back(entry.key); } }
    for (int id : completed) {
        Ref<AgeMeshPageJob> job = pending[id]; pending_bytes -= bytes(id);
        if (!job->is_cancelled() && job->error.is_empty() && job->data.is_valid()) {
            auto *entry = memnew(Resident); entry->data = job->data; entry->shared_page = job; resident.insert(id, entry); resident_bytes += bytes(id);
        } else if (!job->is_cancelled()) { failed.insert(id); WARN_PRINT("AgeMesh streaming fallback: " + job->error); }
        pending.erase(id);
    }
}
bool AgeMeshStreamingInstance3D::request_children(const PackedInt32Array &p_children) {
    uint64_t needed = 0; int missing = 0; bool ready = true;
    for (int child : p_children) {
        if (failed.has(child)) { return false; }
        if (!resident.has(child)) { ready = false; if (!pending.has(child)) { needed += bytes(child); missing++; } }
    }
    if (ready) { return true; }
    if (resident_bytes + pending_bytes + needed > budget()) { budget_limited = true; return false; }
    if (pending.size() + missing > 8) { return false; }
    uint64_t shared_needed = 0;
    for (int child : p_children) {
        if (resident.has(child) || pending.has(child)) { continue; }
        Dictionary info = pages[child];
        uint64_t extra = AgeMeshPagePool::extra_reservation(info["path"], info["sha256"], bytes(child));
        if (extra == UINT64_MAX) { return false; }
        shared_needed += extra;
    }
    // Reserve complete sibling groups, avoiding a full pool of incomplete
    // groups that cannot replace any parent page.
    if (!AgeMeshPagePool::fits(shared_needed)) { budget_limited = true; return false; }
    for (int child : p_children) {
        if (resident.has(child) || pending.has(child)) { continue; }
        Dictionary info = pages[child];
        Ref<AgeMeshPageJob> job = AgeMeshPagePool::request(info["path"], info["sha256"], bytes(child));
        if (job.is_null()) { budget_limited = true; continue; }
        pending_bytes += bytes(child); requests++; pending.insert(child, job);
    }
    peak_bytes = MAX(peak_bytes, resident_bytes + pending_bytes);
    return false;
}
void AgeMeshStreamingInstance3D::prepare_selection() {
    selection_frame++;
    projection_evaluations = 0;
    selection_views.clear();
    selection_transform = get_global_transform();
    Ref<World3D> world = get_world_3d();
    if (world.is_null()) { return; }
    double model_scale = Math::sqrt(selection_transform.basis.get_column(0).length_squared() + selection_transform.basis.get_column(1).length_squared() + selection_transform.basis.get_column(2).length_squared());
    for (Camera3D *camera : world->get_cameras()) {
        Viewport *viewport = camera->get_viewport();
        if (viewport->get_camera_3d() != camera) { continue; }
        SubViewport *sub = Object::cast_to<SubViewport>(viewport);
        if (sub && sub->get_update_mode() == SubViewport::UPDATE_DISABLED) { continue; }
        SelectionView view; view.frustum = camera->get_frustum();
        Projection projection = camera->get_camera_projection();
        Size2 size = viewport->get_visible_rect().size;
        view.scale = MAX(Math::abs(projection.columns[0][0]) * size.x, Math::abs(projection.columns[1][1]) * size.y) * 0.5 * model_scale;
        view.orthogonal = camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL;
        Transform3D transform = camera->get_camera_transform();
        view.origin = transform.origin; view.forward = -transform.basis.get_column(2);
        selection_views.push_back(view);
    }
}
double AgeMeshStreamingInstance3D::projected_error(int p_id) {
    PageInfo &info = page_info.write[p_id];
    if (info.frame == selection_frame) { return info.projected; }
    info.frame = selection_frame; info.projected = 0; projection_evaluations++;
    if (selection_views.is_empty() || info.error <= 0) { return 0; }
    AABB bounds = selection_transform.xform(info.bounds);
    for (const SelectionView &view : selection_views) {
        bool visible = true;
        for (const Plane &plane : view.frustum) {
            if (plane.distance_to(bounds.get_center()) > plane.normal.abs().dot(bounds.size * 0.5)) { visible = false; break; }
        }
        if (!visible) { continue; }
        double projected = info.error * view.scale;
        if (!view.orthogonal) {
            double distance = view.forward.dot(bounds.get_center() - view.origin) - view.forward.abs().dot(bounds.size * 0.5);
            projected /= MAX(0.001, distance);
        }
        info.projected = MAX(info.projected, projected);
    }
    return info.projected;
}
void AgeMeshStreamingInstance3D::select_pages(int p_id) {
    if (changes_remaining == 0 || OS::get_singleton()->get_ticks_usec() >= selection_deadline) { return; }
    PackedInt32Array children = page_info[p_id].children;
    for (int child : children) {
        if (failed.has(child)) { coarsen_page(p_id); return; }
    }
    if (children.is_empty() || projected_error(p_id) <= lod_pixels) {
        coarsen_page(p_id); return;
    }
    if (!request_children(children)) { return; }
    Resident **entry = resident.getptr(p_id);
    if (entry && (*entry)->node) {
        if (!claim_changes(children.size() + 1)) { return; }
        // Publish the whole child covering set before removing the parent.
        for (int child : children) { show_page(child); }
        hide_page(p_id);
    }
    std::vector<int> ordered;
    for (int child : children) { ordered.push_back(child); }
    std::sort(ordered.begin(), ordered.end(), [&](int a, int b) { return projected_error(a) > projected_error(b); });
    for (int child : ordered) { select_pages(child); }
}
void AgeMeshStreamingInstance3D::_notification(int p_what) {
    if (p_what == NOTIFICATION_ENTER_TREE) { initialize_roots(); set_process_internal(true); }
    else if (p_what == NOTIFICATION_INTERNAL_PROCESS) {
        if (!error.is_empty() || roots.is_empty()) { return; }
        poll_pages(); budget_limited = false;
        changes_remaining = CLAMP(int(GLOBAL_GET("rendering/agemesh/streaming_page_changes_per_frame")), 16, 1024);
        changes_this_frame = 0;
        selection_deadline = OS::get_singleton()->get_ticks_usec() + uint64_t(CLAMP(double(GLOBAL_GET("rendering/agemesh/streaming_selection_budget_ms")), 0.1, 100.0) * 1000.0);
        if (!is_visible_in_tree() || resident_bytes + pending_bytes > budget() || AgeMeshPagePool::over_budget()) {
            budget_limited = is_visible_in_tree();
            for (int id : roots) { coarsen_page(id); }
            return;
        }
        prepare_selection();
        for (int id : roots) { select_pages(id); }
    } else if (p_what == NOTIFICATION_EXIT_TREE) { clear_pages(); }
    else if (p_what == NOTIFICATION_PREDELETE) {
        if (page_set.is_valid()) { page_set->disconnect_changed(callable_mp(this, &AgeMeshStreamingInstance3D::page_set_changed)); }
        clear_pages();
    }
}
Dictionary AgeMeshStreamingInstance3D::get_statistics() const {
    int active = 0; int64_t triangles = 0;
    for (const KeyValue<int, Resident *> &entry : resident) {
        if (entry.value->node) { active++; triangles += entry.value->data->get_source_triangles(); }
    }
    Dictionary result;
    result["total_pages"] = pages.size(); result["resident_pages"] = resident.size(); result["pending_pages"] = pending.size(); result["active_pages"] = active;
    result["active_triangles"] = triangles; result["resident_geometry_bytes"] = int64_t(resident_bytes); result["reserved_geometry_bytes"] = int64_t(resident_bytes + pending_bytes);
    result["peak_reserved_geometry_bytes"] = int64_t(peak_bytes); result["effective_budget_bytes"] = int64_t(budget()); result["minimum_root_bytes"] = int64_t(minimum_bytes);
    result["disk_requests"] = int64_t(requests); result["evictions"] = int64_t(evictions); result["failed_pages"] = failed.size(); result["budget_limited"] = budget_limited; result["error"] = error;
    result["projection_evaluations"] = int64_t(projection_evaluations);
    result["selection_view_count"] = selection_views.size();
    result["page_changes_this_frame"] = changes_this_frame;
    result["shared_page_pool"] = AgeMeshPagePool::get_statistics();
    return result;
}
void AgeMeshStreamingInstance3D::_bind_methods() {
    ClassDB::bind_static_method("AgeMeshStreamingInstance3D", D_METHOD("get_shared_pool_statistics"), &AgeMeshPagePool::get_statistics);
    ClassDB::bind_method(D_METHOD("set_page_set", "pages"), &AgeMeshStreamingInstance3D::set_page_set); ClassDB::bind_method(D_METHOD("get_page_set"), &AgeMeshStreamingInstance3D::get_page_set);
    ClassDB::bind_method(D_METHOD("set_memory_budget_mb", "megabytes"), &AgeMeshStreamingInstance3D::set_memory_budget_mb); ClassDB::bind_method(D_METHOD("get_memory_budget_mb"), &AgeMeshStreamingInstance3D::get_memory_budget_mb);
    ClassDB::bind_method(D_METHOD("set_lod_pixels", "pixels"), &AgeMeshStreamingInstance3D::set_lod_pixels); ClassDB::bind_method(D_METHOD("get_lod_pixels"), &AgeMeshStreamingInstance3D::get_lod_pixels);
    ClassDB::bind_method(D_METHOD("get_statistics"), &AgeMeshStreamingInstance3D::get_statistics);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "page_set", PROPERTY_HINT_RESOURCE_TYPE, "AgeMeshPageSet"), "set_page_set", "get_page_set");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "memory_budget_mb", PROPERTY_HINT_RANGE, "1,4096,1"), "set_memory_budget_mb", "get_memory_budget_mb");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_pixels", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_lod_pixels", "get_lod_pixels");
}
