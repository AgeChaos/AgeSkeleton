// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "core/math/plane.h"
#include "core/math/projection.h"
#include "core/math/transform_3d.h"
#include "core/templates/rid.h"
#include "core/variant/variant.h"

// All methods execute on the rendering thread, including registration/release.
namespace AgeMeshGpu {
struct Draw {
    RID indices;
    RID command;
};
void register_mesh(RID p_mesh, PackedByteArray p_clusters, PackedByteArray p_indices, int p_budget_mb, int p_upload_mb);
void unregister_mesh(RID p_mesh);
void prepare(RID p_mesh, const Vector<Plane> &p_world_planes, const Transform3D &p_transform, bool p_supported_view, const Plane &p_camera_depth, float p_lod_scale, bool p_orthogonal);
Draw get_draw(RID p_mesh);
bool has_meshes();
uint64_t get_working_bytes();
uint32_t claim_upload_bytes(uint32_t p_requested);
// The only query callable from the main thread; no GPU synchronization.
bool is_uploaded(RID p_mesh);
void occlude_mesh(RID p_mesh, RID p_pyramid, const Projection &p_local_to_clip);
void set_ignore_occlusion(RID p_mesh, bool p_ignore);
void debug_readback(RID p_mesh);
void shutdown();
}
