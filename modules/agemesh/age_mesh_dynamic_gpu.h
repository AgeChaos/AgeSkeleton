// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "age_mesh_gpu.h"

// Render-thread only. Source indices are shared; output is per render surface,
// so instances with different skeletons and different cameras never alias.
namespace AgeMeshDynamicGpu {
void register_mesh(RID p_mesh, uint64_t p_revision, int p_budget_mb);
void unregister_mesh(RID p_mesh);
bool has_mesh(RID p_mesh);
bool is_uploaded(RID p_mesh);
bool should_cull(RID p_mesh, uint32_t p_triangles, bool p_alpha, bool p_deformed, bool p_occlusion);
bool has_meshes();
uint64_t get_working_bytes();
void release(const void *p_surface);
void prepare(const void *p_surface, RID p_mesh, int p_surface_index, RID p_vertices, uint32_t p_vertex_count, const Vector<Plane> &p_planes, const Transform3D &p_transform, bool p_occlusion = false, bool p_compressed = false, uint32_t p_lod = 0);
void occlude(const void *p_surface, RID p_pyramid, const Projection &p_local_to_clip);
AgeMeshGpu::Draw get_draw(const void *p_surface, uint32_t p_lod = 0);
void debug_readback(RID p_mesh);
void shutdown();
}
