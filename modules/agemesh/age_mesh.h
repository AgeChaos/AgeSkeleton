// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "core/io/resource.h"
#include "scene/resources/mesh.h"

// Serialized inside the normal imported scene; source assets keep their format.
class AgeMeshData : public Resource {
    GDCLASS(AgeMeshData, Resource);
    Ref<Mesh> source;
    Array clusters;
    int source_triangles = 0;
    int cache_version = 0;
    Array gpu_payloads;
    bool native_geometry = false;
protected:
    static void _bind_methods();
public:
    void set_native_geometry(bool p_enabled) { native_geometry = p_enabled; emit_changed(); }
    bool is_native_geometry() const { return native_geometry; }
    // Preserves deformation data and renderer ordering; only compatible opaque
    // adjacent surfaces can be coalesced. Live buffers stay on their source RID.
    Ref<Mesh> build_native_mesh(bool p_allow_merge = true) const;
    static bool requires_native_geometry(const Ref<Mesh> &p_mesh);
    void set_source(const Ref<Mesh> &p_source) { source = p_source; emit_changed(); }
    Ref<Mesh> get_source() const { return source; }
    void set_clusters(const Array &p_clusters) { clusters = p_clusters.duplicate(true); emit_changed(); }
    Array get_clusters() const { return clusters.duplicate(true); }
    void set_source_triangles(int p_count) { source_triangles = MAX(0, p_count); emit_changed(); }
    void set_cache_version(int p_version) { cache_version = p_version; emit_changed(); }
    int get_cache_version() const { return cache_version; }
    String get_cache_error() const;
    Array build_render_batches(int p_clusters_per_batch = 8) const;
    Array build_gpu_surfaces() const;
    Array bake_gpu_payloads() const;
    Array get_cluster_arrays(int p_begin, int p_count) const;
    void set_gpu_payloads(const Array &p_payloads) { gpu_payloads = p_payloads.duplicate(true); emit_changed(); }
    Array get_gpu_payloads() const { return gpu_payloads.duplicate(true); }
    bool has_gpu_payloads() const { return !gpu_payloads.is_empty(); }
    int get_source_triangles() const { return source_triangles; }
    int get_cluster_count() const { return clusters.size(); }
    Ref<ArrayMesh> get_cluster_mesh(int p_index) const;
    int get_cluster_surface(int p_index) const;
    static Ref<AgeMeshData> build(const Ref<Mesh> &p_mesh, int p_triangles_per_cluster = 128);
    static String get_material_unsupported_reason(const Ref<Mesh> &p_mesh);
    static String get_unsupported_reason(const Ref<Mesh> &p_mesh);
};
