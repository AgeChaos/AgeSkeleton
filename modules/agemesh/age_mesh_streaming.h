// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "age_mesh_page_set.h"
#include "age_mesh_instance.h"
#include "age_mesh_page_pool.h"
#include "core/templates/hash_set.h"
#include "core/templates/hash_map.h"
#include "scene/3d/camera_3d.h"

class AgeMeshStreamingInstance3D : public Node3D {
    GDCLASS(AgeMeshStreamingInstance3D, Node3D);
    struct Resident { Ref<AgeMeshData> data; Ref<AgeMeshPageJob> shared_page; MeshInstance3D *node = nullptr; };
    Ref<AgeMeshPageSet> page_set;
    Array pages;
    PackedInt32Array roots;
    struct PageInfo {
        AABB bounds;
        PackedInt32Array children;
        double error = 0;
        uint64_t bytes = 0;
        double projected = 0;
        uint64_t frame = 0;
    };
    Vector<PageInfo> page_info;
    struct SelectionView {
        Vector<Plane> frustum;
        Vector3 origin, forward;
        double scale = 0;
        bool orthogonal = false;
    };
    Vector<SelectionView> selection_views;
    Transform3D selection_transform;
    uint64_t selection_frame = 0, projection_evaluations = 0;
    HashMap<int, Resident *> resident;
    HashMap<int, Ref<AgeMeshPageJob>> pending;
    HashSet<int> failed;
    uint64_t resident_bytes = 0, pending_bytes = 0, minimum_bytes = 0, peak_bytes = 0;
    int budget_mb = 64;
    double lod_pixels = 1.0;
    uint64_t requests = 0, evictions = 0;
    uint64_t selection_deadline = 0;
    int changes_remaining = 0, changes_this_frame = 0;
    bool budget_limited = false;
    String error;
    void clear_pages();
    void initialize_roots();
    void page_set_changed();
    void poll_pages();
    uint64_t bytes(int p_id) const;
    uint64_t budget() const;
    void show_page(int p_id);
    void hide_page(int p_id);
    bool coarsen_page(int p_id, bool p_visible = true);
    bool claim_changes(int p_count);
    void drop_page(int p_id);
    bool request_children(const PackedInt32Array &p_children);
    void prepare_selection();
    double projected_error(int p_id);
    void select_pages(int p_id);
protected:
    static void _bind_methods();
    void _notification(int p_what);
public:
    void set_page_set(const Ref<AgeMeshPageSet> &p_value);
    Ref<AgeMeshPageSet> get_page_set() const { return page_set; }
    void set_memory_budget_mb(int p_value) { budget_mb = CLAMP(p_value, 1, 4096); }
    int get_memory_budget_mb() const { return budget_mb; }
    void set_lod_pixels(double p_value) { lod_pixels = CLAMP(p_value, 0.01, 100.0); }
    double get_lod_pixels() const { return lod_pixels; }
    Dictionary get_statistics() const;
};
