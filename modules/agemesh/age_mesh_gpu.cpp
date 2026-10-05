// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_gpu.h"
#include "age_mesh_dynamic_gpu.h"
#include "age_mesh_gpu_layout.h"
#include "age_mesh_occlusion.h"
#include "core/templates/hash_map.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/renderer_compositor.h"
#include "core/os/mutex.h"
#include "core/templates/hash_set.h"
#include "core/config/project_settings.h"
#include "core/object/worker_thread_pool.h"

namespace AgeMeshGpu {
namespace {
struct State {
    RID clusters, source_indices, visible_indices, index_array, command, uniforms;
    RID camera, occlusion_uniforms, occlusion_texture;
    uint8_t push_constants[128] = {};
    bool occlusion_applied = false;
    bool ignore_occlusion = false;
    uint32_t cluster_count = 0;
    uint32_t index_count = 0;
    bool ready = false;
    bool uploaded = false;
    PackedByteArray pending_clusters, pending_indices;
    uint64_t upload_offset = 0;
    uint64_t reserved_bytes = 0;
};
HashMap<RID, State> states;
RID shader, pipeline;
RID occlusion_shader, occlusion_pipeline;
bool occlusion_failed = false;
bool failed = false;
uint64_t reserved_bytes = 0;
uint64_t upload_frame = UINT64_MAX;
uint64_t shared_upload_frame = UINT64_MAX;
uint32_t shared_upload_used = 0;
uint32_t upload_budget = 4 * 1024 * 1024;
Mutex uploaded_mutex;
HashSet<RID> uploaded_meshes;
WorkerThreadPool::TaskID pipeline_task = WorkerThreadPool::INVALID_TASK_ID;
bool pipeline_ready = false;
bool pipelines_available();

void upload_pending() {
    uint64_t frame = RendererCompositor::get_singleton()->get_frame_number();
    if (frame == upload_frame) { return; }
    upload_frame = frame;
    RD *rd = RD::get_singleton();
    bool shaders = pipelines_available();
    uint32_t remaining = upload_budget;
    for (KeyValue<RID, State> &entry : states) {
        State &state = entry.value;
        if (state.uploaded) { continue; }
        uint64_t metadata_size = state.pending_clusters.size();
        uint64_t total = metadata_size + state.pending_indices.size();
        while (remaining && state.upload_offset < total) {
            bool metadata = state.upload_offset < metadata_size;
            uint64_t offset = metadata ? state.upload_offset : state.upload_offset - metadata_size;
            const PackedByteArray &bytes = metadata ? state.pending_clusters : state.pending_indices;
            uint32_t count = claim_upload_bytes(MIN(uint64_t(remaining), uint64_t(bytes.size()) - offset));
            if (!count) { return; }
            rd->buffer_update(metadata ? state.clusters : state.source_indices, offset, count, bytes.ptr() + offset);
            remaining -= count; state.upload_offset += count;
        }
        if (state.upload_offset == total) {
            if (!shaders) { continue; }
            Vector<RD::Uniform> uniforms;
            for (RID rid : {state.clusters, state.source_indices, state.visible_indices, state.command}) {
                RD::Uniform uniform; uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
                uniform.binding = uniforms.size(); uniform.append_id(rid); uniforms.push_back(uniform);
            }
            state.uniforms = rd->uniform_set_create(uniforms, shader, 0);
            if (state.uniforms.is_null()) { continue; }
            state.uploaded = true; state.pending_clusters.clear(); state.pending_indices.clear();
            MutexLock lock(uploaded_mutex); uploaded_meshes.insert(entry.key);
        }
        if (!remaining) { break; }
    }
}
const char *compute_source = R"GLSL(#version 450
layout(local_size_x=64) in;
struct Cluster { vec4 center; vec4 extent; uvec4 range; uvec4 lod[3]; };
layout(set=0,binding=0,std430) readonly buffer Clusters { Cluster clusters[]; };
layout(set=0,binding=1,std430) readonly buffer Source { uint source_indices[]; };
layout(set=0,binding=2,std430) writeonly buffer Visible { uint visible_indices[]; };
layout(set=0,binding=3,std430) buffer Command {
    uint index_count; uint instance_count; uint first_index; int vertex_offset; uint first_instance;
};
layout(push_constant,std430) uniform Params { vec4 planes[6]; uvec4 counts; vec4 camera_depth; } params;
#ifdef AGEMESH_OCCLUSION
layout(set=0,binding=4) uniform sampler2D depth_pyramid;
layout(set=0,binding=5,std430) readonly buffer Camera { mat4 local_to_clip; } camera;
bool occluded(Cluster c) {
    vec2 lower=vec2(1.0), upper=vec2(0.0);
    float closest=0.0;
    for (uint corner=0;corner<8;corner++) {
        vec3 signs=vec3((corner&1u)!=0u?1.0:-1.0,(corner&2u)!=0u?1.0:-1.0,(corner&4u)!=0u?1.0:-1.0);
        vec4 clip=camera.local_to_clip*vec4(c.center.xyz+c.extent.xyz*signs,1.0);
        if (clip.w<=0.0 || any(isnan(clip)) || any(isinf(clip))) return false;
        vec3 ndc=clip.xyz/clip.w;
        vec2 uv=ndc.xy*0.5+0.5;
        lower=min(lower,uv); upper=max(upper,uv); closest=max(closest,ndc.z);
    }
    vec2 dimensions=vec2(textureSize(depth_pyramid,0));
    lower=clamp(lower-2.0/dimensions,vec2(0),vec2(1));
    upper=clamp(upper+2.0/dimensions,vec2(0),vec2(1));
    vec2 footprint=(upper-lower)*dimensions;
    float span=max(min(footprint.x,footprint.y),max(footprint.x,footprint.y)*0.25);
    int mip=clamp(int(ceil(log2(max(1.0,span)))),0,int(params.counts.w)-1);
    ivec2 size=textureSize(depth_pyramid,mip);
    ivec2 first=clamp(ivec2(floor(lower*vec2(size))),ivec2(0),size-1);
    ivec2 last=clamp(ivec2(floor(upper*vec2(size))),ivec2(0),size-1);
    float farthest=1.0;
    for (int y=first.y;y<=last.y;y++) for (int x=first.x;x<=last.x;x++)
        farthest=min(farthest,texelFetch(depth_pyramid,ivec2(x,y),mip).r);
    return closest<farthest-0.00002;
}
#endif
shared uint destination;
shared uint selected;
shared uint source_offset;
void main() {
    uint cluster_index=gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x;
    if (cluster_index>=params.counts.x) return;
    Cluster c = clusters[cluster_index];
    if (gl_LocalInvocationIndex == 0) {
        bool visible = true;
        for (uint p=0;p<6;p++) {
            vec4 plane=params.planes[p];
            float distance=dot(plane.xyz,c.center.xyz)-plane.w;
            float radius=dot(abs(plane.xyz),c.extent.xyz);
            if (distance>radius+0.0001) visible=false;
        }
#ifdef AGEMESH_OCCLUSION
        if (visible && occluded(c)) visible=false;
#endif
        source_offset=c.range.x;
        selected=visible?c.range.y:0;
        float scale=uintBitsToFloat(params.counts.y);
        float depth=dot(params.camera_depth.xyz,c.center.xyz)-params.camera_depth.w-dot(abs(params.camera_depth.xyz),c.extent.xyz);
        if (visible && scale>0.0 && (params.counts.z!=0 || depth>0.001)) {
            float factor=params.counts.z!=0?scale:scale/depth;
            for (uint level=0;level<3;level++) {
                if (c.lod[level].y>0 && uintBitsToFloat(c.lod[level].z)*factor<=1.0) {
                    source_offset=c.lod[level].x; selected=c.lod[level].y;
                }
            }
        }
        destination=visible?atomicAdd(index_count,selected):0;
    }
    barrier();
    for (uint i=gl_LocalInvocationIndex;i<selected;i+=64)
        visible_indices[destination+i]=source_indices[source_offset+i];
}
)GLSL";
bool initialize() {
    if (pipeline.is_valid()) { return true; }
    RD *rd = RD::get_singleton();
    if (!rd || failed) { return false; }
    String error;
    RD::ShaderStageSPIRVData stage;
    stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
    stage.spirv = rd->shader_compile_spirv_from_source(stage.shader_stage, compute_source, RD::SHADER_LANGUAGE_GLSL, &error);
    if (stage.spirv.is_empty()) { failed = true; ERR_PRINT("AgeMesh GPU shader: " + error); return false; }
    Vector<RD::ShaderStageSPIRVData> stages; stages.push_back(stage);
    shader = rd->shader_create_from_spirv(stages, "AgeMesh visible index compaction");
    if (shader.is_valid()) { pipeline = rd->compute_pipeline_create(shader); }
    if (pipeline.is_null()) { failed = true; return false; }
    return true;
}
bool initialize_occlusion() {
    if (occlusion_pipeline.is_valid()) { return true; }
    if (occlusion_failed) { return false; }
    RD *rd = RD::get_singleton(); String error;
    RD::ShaderStageSPIRVData stage; stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
    String source = String(compute_source).replace("#version 450", "#version 450\n#define AGEMESH_OCCLUSION");
    stage.spirv = rd->shader_compile_spirv_from_source(stage.shader_stage, source, RD::SHADER_LANGUAGE_GLSL, &error);
    if (stage.spirv.is_empty()) { occlusion_failed = true; ERR_PRINT("AgeMesh occlusion shader: " + error); return false; }
    Vector<RD::ShaderStageSPIRVData> stages; stages.push_back(stage);
    occlusion_shader = rd->shader_create_from_spirv(stages, "AgeMesh current depth culling");
    if (occlusion_shader.is_valid()) { occlusion_pipeline = rd->compute_pipeline_create(occlusion_shader); }
    if (occlusion_pipeline.is_null()) { occlusion_failed = true; return false; }
    return true;
}
void initialize_worker(void *) {
    pipeline_ready = initialize();
    if (pipeline_ready) { initialize_occlusion(); }
}
bool pipelines_available() {
    if (pipeline_task != WorkerThreadPool::INVALID_TASK_ID) {
        if (!WorkerThreadPool::get_singleton()->is_task_completed(pipeline_task)) { return false; }
        WorkerThreadPool::get_singleton()->wait_for_task_completion(pipeline_task);
        pipeline_task = WorkerThreadPool::INVALID_TASK_ID;
    }
    return pipeline_ready;
}
}

void unregister_mesh(RID p_mesh) {
    { MutexLock lock(uploaded_mutex); uploaded_meshes.erase(p_mesh); }
    State *state = states.getptr(p_mesh);
    if (!state) { return; }
    reserved_bytes -= state->reserved_bytes;
    if (RD *rd = RD::get_singleton()) {
        if (rd->uniform_set_is_valid(state->occlusion_uniforms)) { rd->free_rid(state->occlusion_uniforms); }
        for (RID rid : {state->uniforms, state->index_array, state->clusters, state->source_indices, state->visible_indices, state->command, state->camera}) {
            if (rid.is_valid()) { rd->free_rid(rid); }
        }
    }
    states.erase(p_mesh);
}

void register_mesh(RID p_mesh, PackedByteArray p_clusters, PackedByteArray p_indices, int p_budget_mb, int p_upload_mb) {
    unregister_mesh(p_mesh);
    if (uint64_t(p_clusters.size()) > UINT32_MAX || uint64_t(p_indices.size()) > UINT32_MAX) { WARN_PRINT("AgeMesh GPU buffers exceed supported size; using full surface."); return; }
    if (p_clusters.is_empty() || p_clusters.size() % sizeof(AgeMeshGpuCluster) || p_indices.is_empty() || p_indices.size() % 12 || !RD::get_singleton()) { return; }
    if (pipeline_task == WorkerThreadPool::INVALID_TASK_ID && !pipeline_ready && !failed) {
        pipeline_task = WorkerThreadPool::get_singleton()->add_native_task(initialize_worker, nullptr, false, "AgeMesh static culling pipelines");
    }
    AgeMeshOcclusion::prewarm();
    uint64_t base_count = 0;
    for (int64_t offset = 0; offset < p_clusters.size(); offset += sizeof(AgeMeshGpuCluster)) {
        AgeMeshGpuCluster cluster; memcpy(&cluster, p_clusters.ptr() + offset, sizeof(cluster)); base_count += cluster.range[1];
    }
    if (!base_count || base_count > UINT32_MAX) { return; }
    uint64_t reservation = p_clusters.size() + uint64_t(p_indices.size()) + base_count * 4 + 20 + 64;
    uint64_t budget = uint64_t(CLAMP(p_budget_mb, 1, 4096)) * 1024 * 1024;
    upload_budget = CLAMP(p_upload_mb, 1, 64) * 1024 * 1024;
    if (reserved_bytes + AgeMeshDynamicGpu::get_working_bytes() + reservation > budget) { WARN_PRINT("AgeMesh GPU working buffer budget exceeded; using full surface."); return; }
    RD *rd = RD::get_singleton();
    State state;
    state.reserved_bytes = reservation; reserved_bytes += reservation;
    state.pending_clusters = p_clusters; state.pending_indices = p_indices;
    state.cluster_count = p_clusters.size() / sizeof(AgeMeshGpuCluster);
    state.index_count = base_count;
    state.clusters = rd->storage_buffer_create(p_clusters.size());
    state.source_indices = rd->storage_buffer_create(p_indices.size());
    state.visible_indices = rd->index_buffer_create(state.index_count, RD::INDEX_BUFFER_FORMAT_UINT32, {}, false, RD::BUFFER_CREATION_AS_STORAGE_BIT);
    state.index_array = rd->index_array_create(state.visible_indices, 0, state.index_count);
    state.command = rd->storage_buffer_create(20, {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
    state.camera = rd->storage_buffer_create(64);
    if (state.clusters.is_null() || state.source_indices.is_null() || state.visible_indices.is_null() || state.index_array.is_null() || state.command.is_null() || state.camera.is_null()) {
        states.insert(p_mesh, state); unregister_mesh(p_mesh); return;
    }
    states.insert(p_mesh, state);
}

void prepare(RID p_mesh, const Vector<Plane> &p_world_planes, const Transform3D &p_transform, bool p_supported_view, const Plane &p_camera_depth, float p_lod_scale, bool p_orthogonal) {
    upload_pending();
    State *state = states.getptr(p_mesh);
    if (!state) { return; }
    state->ready = false;
    state->occlusion_applied = false;
    if (!state->uploaded || !p_supported_view || p_world_planes.size() != 6 || state->uniforms.is_null()) { return; }
    struct Push { float planes[24]; uint32_t counts[4]; float depth[4]; } push = {};
    static_assert(sizeof(Push) == 128);
    push.counts[0] = state->cluster_count;
    // Frobenius norm bounds scale even for sheared transforms.
    float scale = p_lod_scale * Math::sqrt(p_transform.basis.get_column(0).length_squared() + p_transform.basis.get_column(1).length_squared() + p_transform.basis.get_column(2).length_squared());
    memcpy(&push.counts[1], &scale, sizeof(scale)); push.counts[2] = p_orthogonal;
    Vector3 depth_normal = p_transform.basis.transposed().xform(p_camera_depth.normal);
    for (int axis = 0; axis < 3; axis++) { push.depth[axis] = depth_normal[axis]; }
    push.depth[3] = p_camera_depth.d - p_camera_depth.normal.dot(p_transform.origin);
    for (int i = 0; i < 6; i++) {
        // Transform plane coefficients without normalizing: valid for non-uniform
        // and negative scales; the AABB support radius uses the same coefficients.
        Vector3 normal = p_transform.basis.transposed().xform(p_world_planes[i].normal);
        push.planes[i * 4] = normal.x; push.planes[i * 4 + 1] = normal.y; push.planes[i * 4 + 2] = normal.z;
        push.planes[i * 4 + 3] = p_world_planes[i].d - p_world_planes[i].normal.dot(p_transform.origin);
    }
    RD *rd = RD::get_singleton();
    uint32_t reset[5] = {0, 1, 0, 0, 0};
    rd->buffer_update(state->command, 0, sizeof(reset), reset);
    RD::ComputeListID list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(list, pipeline);
    rd->compute_list_bind_uniform_set(list, state->uniforms, 0);
    rd->compute_list_set_push_constant(list, &push, sizeof(push));
    memcpy(state->push_constants, &push, sizeof(push));
    uint32_t width = MIN(state->cluster_count, uint32_t(rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X)));
    rd->compute_list_dispatch(list, width, (state->cluster_count + width - 1) / width, 1);
    rd->compute_list_end();
    state->ready = true;
}

Draw get_draw(RID p_mesh) {
    const State *state = states.getptr(p_mesh);
    return state && state->ready ? Draw{state->index_array, state->command} : Draw{};
}

bool has_meshes() { return !states.is_empty(); }
bool is_uploaded(RID p_mesh) { MutexLock lock(uploaded_mutex); return uploaded_meshes.has(p_mesh); }
void set_ignore_occlusion(RID p_mesh, bool p_ignore) {
    if (State *state = states.getptr(p_mesh)) { state->ignore_occlusion = p_ignore; }
}

void occlude_mesh(RID p_mesh, RID p_pyramid, const Projection &p_local_to_clip) {
    State *state = states.getptr(p_mesh);
    if (!state || !state->ready || state->ignore_occlusion || p_pyramid.is_null() || !pipelines_available() || occlusion_pipeline.is_null()) { return; }
    RD *rd = RD::get_singleton();
    if (state->occlusion_texture != p_pyramid || !rd->uniform_set_is_valid(state->occlusion_uniforms)) {
        if (rd->uniform_set_is_valid(state->occlusion_uniforms)) { rd->free_rid(state->occlusion_uniforms); }
        Vector<RD::Uniform> bindings;
        for (RID rid : {state->clusters, state->source_indices, state->visible_indices, state->command}) {
            RD::Uniform uniform; uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER; uniform.binding = bindings.size(); uniform.append_id(rid); bindings.push_back(uniform);
        }
        RD::Uniform texture; texture.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE; texture.binding = 4;
        texture.append_id(AgeMeshOcclusion::get_sampler()); texture.append_id(p_pyramid); bindings.push_back(texture);
        RD::Uniform camera; camera.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER; camera.binding = 5; camera.append_id(state->camera); bindings.push_back(camera);
        state->occlusion_uniforms = rd->uniform_set_create(bindings, occlusion_shader, 0); state->occlusion_texture = p_pyramid;
        if (state->occlusion_uniforms.is_null()) { return; }
    }
    float matrix[16];
    for (int c = 0; c < 4; c++) { for (int r = 0; r < 4; r++) { matrix[c * 4 + r] = p_local_to_clip.columns[c][r]; } }
    rd->buffer_update(state->camera, 0, sizeof(matrix), matrix);
    uint32_t reset[5] = {0, 1, 0, 0, 0}; rd->buffer_update(state->command, 0, sizeof(reset), reset);
    uint32_t levels = AgeMeshOcclusion::get_levels(); memcpy(state->push_constants + 108, &levels, sizeof(levels));
    RD::ComputeListID list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(list, occlusion_pipeline); rd->compute_list_bind_uniform_set(list, state->occlusion_uniforms, 0);
    rd->compute_list_set_push_constant(list, state->push_constants, sizeof(state->push_constants));
    uint32_t width = MIN(state->cluster_count, uint32_t(rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X)));
    rd->compute_list_dispatch(list, width, (state->cluster_count + width - 1) / width, 1); rd->compute_list_end();
    state->occlusion_applied = true;
}

void debug_readback(RID p_mesh) {
    const State *state = states.getptr(p_mesh);
    if (!state || !state->ready) { return; }
    // Explicit diagnostic only. Normal rendering never reads results back to CPU.
    Vector<uint8_t> bytes = RD::get_singleton()->buffer_get_data(state->command);
    if (bytes.size() == 20) {
        uint32_t count; memcpy(&count, bytes.ptr(), sizeof(count));
        print_line("AGEMESH_GPU_READBACK indices=" + itos(count) + " capacity=" + itos(state->index_count) + " clusters=" + itos(state->cluster_count));
        print_line("AGEMESH_GPU_WORKING_BYTES " + itos(reserved_bytes));
        print_line("AGEMESH_GPU_HZB " + itos(state->occlusion_applied));
    }
}

void shutdown() {
    if (pipeline_task != WorkerThreadPool::INVALID_TASK_ID) {
        WorkerThreadPool::get_singleton()->wait_for_task_completion(pipeline_task);
        pipeline_task = WorkerThreadPool::INVALID_TASK_ID;
    }
    AgeMeshDynamicGpu::shutdown();
    while (!states.is_empty()) { unregister_mesh(states.begin()->key); }
    if (RD *rd = RD::get_singleton()) {
        if (pipeline.is_valid()) { rd->free_rid(pipeline); }
        if (shader.is_valid()) { rd->free_rid(shader); }
        if (occlusion_pipeline.is_valid()) { rd->free_rid(occlusion_pipeline); }
        if (occlusion_shader.is_valid()) { rd->free_rid(occlusion_shader); }
    }
    AgeMeshOcclusion::shutdown(); occlusion_pipeline = occlusion_shader = RID(); occlusion_failed = false;
    pipeline = RID(); shader = RID(); failed = false; upload_frame = UINT64_MAX;
    pipeline_ready = false; shared_upload_frame = UINT64_MAX; shared_upload_used = 0;
}
uint64_t get_working_bytes() { return reserved_bytes; }
uint32_t claim_upload_bytes(uint32_t p_requested) {
    uint64_t frame = RendererCompositor::get_singleton()->get_frame_number();
    if (shared_upload_frame != frame) { shared_upload_frame = frame; shared_upload_used = 0; }
    uint32_t budget = CLAMP(int(GLOBAL_GET("rendering/agemesh/upload_mb_per_frame")), 1, 64) * 1024 * 1024;
    uint32_t count = MIN(p_requested, budget > shared_upload_used ? budget - shared_upload_used : 0);
    shared_upload_used += count; return count;
}
}
