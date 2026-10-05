// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "age_mesh.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/mesh_instance_3d.h"
class AgeMeshPreparation;

// Importer-owned helper preserves the original MeshInstance3D and its animation paths.
class AgeMeshInstance3D : public Node3D {
    GDCLASS(AgeMeshInstance3D, Node3D);
    Ref<AgeMeshData> data;
    Vector<MeshInstance3D *> parts;
    ObjectID owner_id;
    bool active = false;
    bool source_dirty = false;
    bool material_supported = true;
    uint64_t material_state = 0;
    bool cache_checked = false;
    bool gpu_cache_checked = false;
    AgeMeshPreparation *preparation = nullptr;
    Vector<RenderingServerTypes::SurfaceData> prepared_gpu_data;
    int observed_cluster_count = -1;
    Vector<Ref<Material>> watched_materials;
    Ref<Mesh> watched_source;
    Vector<Ref<ArrayMesh>> watched_clusters;
    Array synced_properties;
    Array render_batches;
    int batch_size = 0;
    bool gpu_mode = false;
    Vector<RID> gpu_meshes;
    bool dynamic_updates = false;
    bool native_active = false;
    bool native_dirty = true;
    bool native_merge_allowed = false;
    uint64_t native_rebuilds = 0;
    Ref<Mesh> native_render_mesh;
    RID dynamic_gpu_mesh;
    uint64_t native_topology_revision = 0;
    void refresh_native(MeshInstance3D *p_owner);
    void owner_render_state_changed();
    void restore_owner_base(MeshInstance3D *p_owner);
    void disconnect_resources();
    void material_changed();
    void source_changed();
    void refresh();
    void deactivate();
    void settings_changed();
    void sync_properties(MeshInstance3D *p_source);
    uint64_t compute_material_state() const;
protected:
    static void _bind_methods();
    void _notification(int p_what);
public:
    void set_dynamic_updates(bool p_enabled);
    bool get_dynamic_updates() const { return dynamic_updates; }
    void set_data(const Ref<AgeMeshData> &p_data);
    Ref<AgeMeshData> get_data() const { return data; }
    bool is_active() const { return active; }
    Dictionary get_statistics() const;
    void request_gpu_debug_readback();
};
