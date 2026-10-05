// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_preparation.h"
#include "age_mesh_gpu_layout.h"
#include "servers/rendering/rendering_server.h"
#include <cmath>
namespace { Vector<AgeMeshPreparation *> retired_jobs; }

AgeMeshPreparation::AgeMeshPreparation(const Ref<AgeMeshData> &p_data) {
    payloads = p_data->get_gpu_payloads();
    expected_triangles = p_data->get_source_triangles();
    Ref<Mesh> source = p_data->get_source();
    for (int i = 0; i < source->get_surface_count(); i++) {
        int indices = source->surface_get_array_index_len(i);
        surface_triangles.push_back((indices ? indices : source->surface_get_array_len(i)) / 3);
    }
    task = WorkerThreadPool::get_singleton()->add_native_task(&AgeMeshPreparation::run, this, false, "AgeMesh packed geometry validation");
}
void AgeMeshPreparation::run(void *p_self) {
    auto *self = static_cast<AgeMeshPreparation *>(p_self);
    self->error = self->validate();
    if (!self->error.is_empty() || self->cancelled.is_set()) { return; }
    self->packed_surfaces.resize(self->payloads.size());
    for (int i = 0; i < self->payloads.size(); i++) {
        if (self->cancelled.is_set()) { return; }
        Dictionary item = self->payloads[i];
        Error result = RenderingServer::get_singleton()->mesh_create_surface_data_from_arrays(&self->packed_surfaces.write[i], RSE::PRIMITIVE_TRIANGLES, item["arrays"]);
        if (result != OK) { self->error = "could not prepare packed render surface"; return; }
    }
}
bool AgeMeshPreparation::completed() const { return WorkerThreadPool::get_singleton()->is_task_completed(task); }
AgeMeshPreparation::~AgeMeshPreparation() {
    cancelled.set();
    WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
}
void AgeMeshPreparation::retire(AgeMeshPreparation *p_job) {
    if (!p_job) { return; }
    p_job->cancelled.set(); retired_jobs.push_back(p_job);
}
void AgeMeshPreparation::poll_retired() {
    for (int i = retired_jobs.size() - 1; i >= 0; i--) {
        if (retired_jobs[i]->completed()) { memdelete(retired_jobs[i]); retired_jobs.remove_at(i); }
    }
}
void AgeMeshPreparation::shutdown_retired() {
    for (AgeMeshPreparation *job : retired_jobs) { memdelete(job); }
    retired_jobs.clear();
}
String AgeMeshPreparation::validate() const {
    Vector<int64_t> totals; totals.resize(surface_triangles.size()); totals.fill(0);
    int64_t total = 0;
    if (payloads.is_empty()) { return "empty packed geometry"; }
    for (int p = 0; p < payloads.size(); p++) {
        if (cancelled.is_set()) { return "cancelled"; }
        if (payloads[p].get_type() != Variant::DICTIONARY) { return "invalid packed surface"; }
        Dictionary item = payloads[p];
        if (int(item.get("layout", 0)) != 2 || item.get("surface", Variant()).get_type() != Variant::INT ||
                item.get("arrays", Variant()).get_type() != Variant::ARRAY ||
                item.get("clusters", Variant()).get_type() != Variant::PACKED_BYTE_ARRAY ||
                item.get("indices", Variant()).get_type() != Variant::PACKED_BYTE_ARRAY) { return "packed layout mismatch; reimport"; }
        int surface = item["surface"];
        if (surface < 0 || surface >= totals.size()) { return "packed surface out of range"; }
        Array arrays = item["arrays"];
        if (arrays.size() != Mesh::ARRAY_MAX || arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY || arrays[Mesh::ARRAY_INDEX].get_type() != Variant::PACKED_INT32_ARRAY) { return "invalid packed arrays"; }
        Vector<Vector3> vertices = arrays[Mesh::ARRAY_VERTEX]; Vector<int> indices = arrays[Mesh::ARRAY_INDEX];
        if (vertices.is_empty() || indices.is_empty() || indices.size() % 3) { return "empty packed triangles"; }
        for (int a = 1; a < Mesh::ARRAY_INDEX; a++) {
            if (arrays[a].get_type() == Variant::NIL) { continue; }
            int count = -1;
            if (a == Mesh::ARRAY_NORMAL && arrays[a].get_type() == Variant::PACKED_VECTOR3_ARRAY) { Vector<Vector3> v = arrays[a]; count = v.size(); }
            if (a == Mesh::ARRAY_COLOR && arrays[a].get_type() == Variant::PACKED_COLOR_ARRAY) { Vector<Color> v = arrays[a]; count = v.size(); }
            if ((a == Mesh::ARRAY_TEX_UV || a == Mesh::ARRAY_TEX_UV2) && arrays[a].get_type() == Variant::PACKED_VECTOR2_ARRAY) { Vector<Vector2> v = arrays[a]; count = v.size(); }
            if (a == Mesh::ARRAY_TANGENT && arrays[a].get_type() == Variant::PACKED_FLOAT32_ARRAY) { Vector<float> v = arrays[a]; if (v.size() % 4 == 0) { count = v.size() / 4; } }
            if (count != vertices.size()) { return "packed vertex attribute mismatch"; }
        }
        PackedByteArray bytes = item["indices"], metadata = item["clusters"];
        if (bytes.size() < indices.size() * int64_t(sizeof(int)) || bytes.size() % 12 || uint64_t(bytes.size()) > UINT32_MAX || memcmp(bytes.ptr(), indices.ptr(), indices.size() * sizeof(int))) { return "packed index buffer mismatch"; }
        using Cluster = AgeMeshGpuCluster;
        if (metadata.is_empty() || metadata.size() % sizeof(Cluster)) { return "invalid packed cluster records"; }
        uint64_t next = 0, next_vertex = 0, next_lod = indices.size();
        for (int64_t offset = 0; offset < metadata.size(); offset += sizeof(Cluster)) {
            if (cancelled.is_set()) { return "cancelled"; }
            Cluster cluster; memcpy(&cluster, metadata.ptr() + offset, sizeof(cluster));
            if (cluster.range[0] != next || !cluster.range[1] || cluster.range[1] % 3 || cluster.range[1] > 768 || next + cluster.range[1] > uint64_t(indices.size())) { return "invalid packed cluster range"; }
            if (cluster.range[2] != next_vertex || !cluster.range[3] || cluster.range[3] > 256 || next_vertex + cluster.range[3] > uint64_t(vertices.size())) { return "invalid packed vertex range"; }
            for (int axis = 0; axis < 3; axis++) {
                if (!std::isfinite(cluster.center[axis]) || !std::isfinite(cluster.extent[axis]) || cluster.extent[axis] < 0) { return "invalid packed bounds"; }
            }
            for (uint32_t i = 0; i < cluster.range[1]; i++) {
                int index = indices[next + i];
                if (index < int64_t(next_vertex) || index >= int64_t(next_vertex + cluster.range[3])) { return "packed index outside cluster vertices"; }
                const Vector3 &v = vertices[index];
                for (int axis = 0; axis < 3; axis++) {
                    if (!std::isfinite(v[axis]) || Math::abs(v[axis] - cluster.center[axis]) > cluster.extent[axis] + MAX(0.0001f, Math::abs(cluster.center[axis]) * 0.000001f)) { return "packed cluster bounds do not contain vertices"; }
                }
            }
            next += cluster.range[1];
            float previous_error = 0;
            uint32_t previous_count = cluster.range[1];
            bool ended = false;
            for (const Cluster::Level &level : cluster.lod) {
                if (!level.count) { ended = true; continue; }
                if (ended || level.offset != next_lod || level.count % 3 || level.count >= previous_count || !std::isfinite(level.error) || level.error <= previous_error || next_lod + level.count > uint64_t(bytes.size() / 4)) { return "invalid packed LOD range"; }
                for (uint32_t i = 0; i < level.count; i++) {
                    uint32_t index; memcpy(&index, bytes.ptr() + (next_lod + i) * 4, 4);
                    if (index < next_vertex || index >= next_vertex + cluster.range[3]) { return "packed LOD index outside cluster"; }
                }
                next_lod += level.count; previous_count = level.count; previous_error = level.error;
            }
            next_vertex += cluster.range[3];
        }
        if (next != uint64_t(indices.size()) || next_vertex != uint64_t(vertices.size()) || next_lod * 4 != uint64_t(bytes.size())) { return "packed cluster coverage mismatch"; }
        totals.write[surface] += indices.size() / 3; total += indices.size() / 3;
    }
    if (total != expected_triangles) { return "packed triangle total mismatch"; }
    for (int i = 0; i < totals.size(); i++) { if (totals[i] != surface_triangles[i]) { return "packed/source surface mismatch"; } }
    return String();
}
