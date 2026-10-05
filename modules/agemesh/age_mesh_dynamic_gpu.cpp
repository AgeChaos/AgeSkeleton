// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_dynamic_gpu.h"
#include "age_mesh_dynamic_preparation.h"
#include "age_mesh_occlusion.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/os/mutex.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/rendering_device.h"
#include <algorithm>

namespace AgeMeshDynamicGpu {
namespace {
constexpr uint32_t CLUSTER_INDICES = 384;
struct Source {
    int references = 0;
    Array original_indices;
    Vector<RID> indices;
    Vector<RID> cluster_vertices, cluster_ranges;
    Vector<uint32_t> counts;
    Vector<uint32_t> surface_offsets, surface_lod_counts;
    Vector<AABB> aabbs;
    uint64_t bytes = 0;
    uint64_t revision = 0, generation = 0;
    Ref<AgeMeshDynamicPreparation> work;
    Vector<RID> download_buffers;
    Vector<uint32_t> download_offsets;
    uint32_t pending_reads = 0;
    int upload_surface = 0, upload_component = 0;
    uint32_t upload_offset = 0;
    bool failed = false, ready = false, prepared = false;
    bool deformed = false;
};
struct State {
    RID mesh, vertices, source_indices, visible, indices, command, counts, offsets, uniforms;
    RID bounds, camera, occlusion_uniforms, occlusion_texture;
    uint8_t push_constants[112] = {};
    uint32_t count = 0, clusters = 0;
    uint32_t capacity = 0, lod = 0;
    int surface_index = 0;
    uint64_t bytes = 0;
    bool ready = false;
    bool bounds_ready = false, occlusion_applied = false;
    Transform3D decode_transform;
};
HashMap<RID, Source> sources;
HashMap<uint64_t, State> states;
Mutex uploaded_mutex;
HashSet<RID> uploaded_meshes;
RID shader, pipelines[3];
RID occlusion_shader, occlusion_pipeline;
uint64_t used_bytes = 0, budget_bytes = 256 * 1024 * 1024;
bool failed = false;
Vector<Ref<AgeMeshDynamicPreparation>> retired;
uint64_t next_generation = 0, preparation_frame = UINT64_MAX;
WorkerThreadPool::TaskID shader_task = WorkerThreadPool::INVALID_TASK_ID;
bool shader_ready = false;
bool preserve_shared_source(const Source &p_source) {
    return !p_source.deformed && p_source.references > 1 && int(GLOBAL_GET("rendering/agemesh/dynamic_gpu_policy")) == 0 && !bool(GLOBAL_GET("rendering/agemesh/gpu_occlusion"));
}
// Each cluster uses current post-deformation positions. The parallel prefix
// preserves the original index order, including within transparent surfaces.
const char *code = R"GLSL(#version 450
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) readonly buffer Vertices { uint vertices[]; };
layout(set=0,binding=1,std430) readonly buffer Source { uint source_indices[]; };
layout(set=0,binding=2,std430) writeonly buffer Visible { uint visible_indices[]; };
layout(set=0,binding=3,std430) buffer Command { uint index_count; uint instance_count; uint first_index; int vertex_offset; uint first_instance; };
layout(set=0,binding=4,std430) buffer Counts { uint counts[]; };
layout(set=0,binding=5,std430) buffer Offsets { uint offsets[]; };
layout(set=0,binding=6,std430) readonly buffer ClusterVertices { uint cluster_vertices[]; };
layout(set=0,binding=7,std430) readonly buffer ClusterRanges { uvec2 cluster_ranges[]; };
layout(set=0,binding=8,std430) writeonly buffer Bounds { vec4 bounds[]; };
layout(push_constant,std430) uniform Params { vec4 planes[6]; uvec4 sizes; } params;
layout(constant_id=0) const uint phase=0;
shared vec3 lows[64];
shared vec3 highs[64];
shared uint scan[64];
void main() {
    uint lane=gl_LocalInvocationIndex;
    uint cluster=gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x;
    if (phase==0) {
        if (cluster>=params.sizes.y) return;
        uint start=cluster*384u, n=min(384u,params.sizes.x-start);
        vec3 lo=vec3(3.402823e38), hi=-lo;
        uvec2 range=cluster_ranges[cluster];
        for(uint i=lane;i<range.y;i+=64u) {
            uint v=cluster_vertices[range.x+i];
            // Invalid/nonfinite data must not cause an out-of-bounds read or
            // false rejection. Mesh validation normally rejects these indices.
            vec3 p=vec3(0);
            if(v<params.sizes.z) {
                if((params.sizes.w&2u)!=0u) p=vec3(unpackUnorm2x16(vertices[v*2u]),unpackUnorm2x16(vertices[v*2u+1u]).x);
                else p=uintBitsToFloat(uvec3(vertices[v*3u],vertices[v*3u+1u],vertices[v*3u+2u]));
            }
            if(v>=params.sizes.z || any(isnan(p)) || any(isinf(p))) { lo=vec3(-3.402823e38); hi=-lo; }
            else { lo=min(lo,p); hi=max(hi,p); }
        }
        lows[lane]=lo; highs[lane]=hi; barrier();
        for(uint step=32u;step>0u;step>>=1u) {
            if(lane<step) { lows[lane]=min(lows[lane],lows[lane+step]); highs[lane]=max(highs[lane],highs[lane+step]); } barrier();
        }
        if(lane==0u) {
            if((params.sizes.w&1u)!=0u) { bounds[cluster*2u]=vec4(lows[0],0); bounds[cluster*2u+1u]=vec4(highs[0],0); }
            bool visible=true;
            for(uint p=0u;p<6u;p++) {
                vec3 support=mix(lows[0],highs[0],lessThan(params.planes[p].xyz,vec3(0)));
                if(dot(params.planes[p].xyz,support)-params.planes[p].w>0.0001) visible=false;
            }
            counts[cluster]=visible?n:0u;
        }
    } else if(phase==1) {
        uint chunk=(params.sizes.y+63u)/64u;
        uint begin=lane*chunk, end=min(begin+chunk,params.sizes.y), sum=0u;
        for(uint i=begin;i<end;i++) { offsets[i]=sum; sum+=counts[i]; }
        scan[lane]=sum; barrier();
        for(uint step=1u;step<64u;step<<=1u) {
            uint add=lane>=step?scan[lane-step]:0u; barrier();
            scan[lane]+=add; barrier();
        }
        uint base=lane>0u?scan[lane-1u]:0u;
        for(uint i=begin;i<end;i++) offsets[i]+=base;
        if(lane==63u) { index_count=scan[63]; instance_count=1u; first_index=0u; vertex_offset=0; first_instance=0u; }
    } else {
        if(cluster>=params.sizes.y) return;
        uint n=counts[cluster], dst=offsets[cluster], src=cluster*384u;
        for(uint i=lane;i<n;i+=64u) visible_indices[dst+i]=source_indices[src+i];
    }
}
)GLSL";
const char *occlusion_code = R"GLSL(#version 450
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) readonly buffer Bounds { vec4 bounds[]; };
layout(set=0,binding=1,std430) buffer Counts { uint counts[]; };
layout(set=0,binding=2,std430) readonly buffer Camera { mat4 local_to_clip; } camera;
layout(set=0,binding=3) uniform sampler2D depth_pyramid;
layout(push_constant,std430) uniform Params { uint clusters; uint levels; } params;
void main() {
    uint cluster=(gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x)*64u+gl_LocalInvocationIndex;
    if(cluster>=params.clusters || counts[cluster]==0u) return;
    vec3 lo=bounds[cluster*2u].xyz, hi=bounds[cluster*2u+1u].xyz;
    vec2 lower=vec2(1), upper=vec2(0);
    float closest=0.0;
    for(uint corner=0u;corner<8u;corner++) {
        vec3 p=mix(lo,hi,bvec3((corner&1u)!=0u,(corner&2u)!=0u,(corner&4u)!=0u));
        vec4 clip=camera.local_to_clip*vec4(p,1);
        if(clip.w<=0.0 || any(isnan(clip)) || any(isinf(clip))) return;
        vec3 ndc=clip.xyz/clip.w;
        // A box crossing the near plane cannot be conservatively projected.
        if(ndc.z>=1.0) return;
        vec2 uv=ndc.xy*0.5+0.5;
        lower=min(lower,uv); upper=max(upper,uv); closest=max(closest,ndc.z);
    }
    vec2 dimensions=vec2(textureSize(depth_pyramid,0));
    lower=clamp(lower-2.0/dimensions,vec2(0),vec2(1));
    upper=clamp(upper+2.0/dimensions,vec2(0),vec2(1));
    vec2 footprint=(upper-lower)*dimensions;
    // Long thin clusters otherwise sample a very coarse square that includes
    // unrelated background. Cover at most four texels along the long axis.
    float span=max(min(footprint.x,footprint.y),max(footprint.x,footprint.y)*0.25);
    int mip=clamp(int(ceil(log2(max(1.0,span)))),0,int(params.levels)-1);
    ivec2 size=textureSize(depth_pyramid,mip);
    ivec2 first=clamp(ivec2(floor(lower*vec2(size))),ivec2(0),size-1);
    ivec2 last=clamp(ivec2(floor(upper*vec2(size))),ivec2(0),size-1);
    float farthest=1.0;
    for(int y=first.y;y<=last.y;y++) for(int x=first.x;x<=last.x;x++)
        farthest=min(farthest,texelFetch(depth_pyramid,ivec2(x,y),mip).r);
    if(closest<farthest-0.00002) counts[cluster]=0u;
}
)GLSL";
bool initialize() {
    if (shader.is_valid()) { return true; }
    RD *rd = RD::get_singleton();
    if (!rd || failed) { return false; }
    String error;
    RD::ShaderStageSPIRVData stage;
    stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
    stage.spirv = rd->shader_compile_spirv_from_source(stage.shader_stage, code, RD::SHADER_LANGUAGE_GLSL, &error);
    if (stage.spirv.is_empty()) { failed = true; ERR_PRINT("AgeMesh dynamic GPU: " + error); return false; }
    Vector<RD::ShaderStageSPIRVData> stages; stages.push_back(stage);
    shader = rd->shader_create_from_spirv(stages, "AgeMesh stable dynamic cluster culling");
    for (uint32_t i = 0; i < 3; i++) {
        RD::PipelineSpecializationConstant constant;
        constant.constant_id = 0; constant.type = RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT; constant.int_value = i;
        Vector<RD::PipelineSpecializationConstant> constants; constants.push_back(constant);
        pipelines[i] = rd->compute_pipeline_create(shader, constants);
    }
    stage.spirv = rd->shader_compile_spirv_from_source(stage.shader_stage, occlusion_code, RD::SHADER_LANGUAGE_GLSL, &error);
    if (stage.spirv.is_empty()) { failed = true; ERR_PRINT("AgeMesh dynamic HZB: " + error); return false; }
    stages.clear(); stages.push_back(stage);
    occlusion_shader = rd->shader_create_from_spirv(stages, "AgeMesh dynamic current depth occlusion");
    if (occlusion_shader.is_valid()) { occlusion_pipeline = rd->compute_pipeline_create(occlusion_shader); }
    bool success = pipelines[0].is_valid() && pipelines[1].is_valid() && pipelines[2].is_valid() && occlusion_pipeline.is_valid();
    failed = !success;
    return success;
}
}

void release(const void *p_surface) {
    uint64_t key = uint64_t(p_surface);
    State *s = states.getptr(key);
    if (!s) { return; }
    RD *rd = RD::get_singleton();
    if (rd) {
        if (rd->uniform_set_is_valid(s->uniforms)) { rd->free_rid(s->uniforms); }
        if (rd->uniform_set_is_valid(s->occlusion_uniforms)) { rd->free_rid(s->occlusion_uniforms); }
        for (RID rid : {s->indices, s->visible, s->command, s->counts, s->offsets, s->bounds, s->camera}) {
            if (rid.is_valid()) { rd->free_rid(rid); }
        }
    }
    used_bytes -= s->bytes;
    states.erase(key);
}

namespace {
void compile_shaders(void *) { shader_ready = initialize(); }
bool shaders_available() {
    if (shader_task != WorkerThreadPool::INVALID_TASK_ID) {
        if (!WorkerThreadPool::get_singleton()->is_task_completed(shader_task)) { return false; }
        WorkerThreadPool::get_singleton()->wait_for_task_completion(shader_task);
        shader_task = WorkerThreadPool::INVALID_TASK_ID;
    }
    return shader_ready;
}
void readback_complete(PackedByteArray p_bytes, RID p_mesh, int p_surface, uint32_t p_offset, uint64_t p_generation) {
    Source *source = sources.getptr(p_mesh);
    if (!source || source->generation != p_generation || source->failed) { return; }
    PackedByteArray &target = source->work->inputs.write[p_surface].bytes;
    if (uint64_t(p_offset) + p_bytes.size() > uint64_t(target.size())) { source->failed = true; return; }
    memcpy(target.ptrw() + p_offset, p_bytes.ptr(), p_bytes.size());
    source->pending_reads--;
}
void pump_preparation() {
    uint64_t frame = RendererCompositor::get_singleton()->get_frame_number();
    if (frame == preparation_frame) { return; }
    preparation_frame = frame;
    for (KeyValue<uint64_t, State> &entry : states) { entry.value.ready = false; }
    for (int i = retired.size() - 1; i >= 0; i--) {
        if (retired[i]->completed()) { retired[i]->finish(); retired.remove_at(i); }
    }
    bool shaders = shaders_available();
    RD *rd = RD::get_singleton();
    // Bound readback separately; requesting a huge staging allocation can itself
    // stall the device, even when the completion callback is asynchronous.
    uint32_t download_budget = 1024 * 1024;
    for (KeyValue<RID, Source> &entry : sources) {
        Source &source = entry.value;
        if (source.ready || source.failed || preserve_shared_source(source)) { continue; }
        Ref<AgeMeshDynamicPreparation> work = source.work;
        if (!source.prepared && work->task == WorkerThreadPool::INVALID_TASK_ID) {
            bool submitted = true;
            for (int surface = 0; surface < source.download_buffers.size(); surface++) {
                const PackedByteArray &bytes = work->inputs[surface].bytes;
                uint32_t &offset = source.download_offsets.write[surface];
                if (offset == bytes.size()) { continue; }
                if (download_budget == 0) { submitted = false; break; }
                uint32_t count = MIN(download_budget, uint32_t(bytes.size()) - offset);
                source.pending_reads++;
                Error error = rd->buffer_get_data_async(source.download_buffers[surface], callable_mp_static(&readback_complete).bind(entry.key, surface, offset, source.generation), offset, count);
                if (error != OK) { source.failed = true; source.pending_reads--; break; }
                offset += count; download_budget -= count;
                submitted = submitted && offset == bytes.size();
            }
            if (!source.failed && submitted && source.pending_reads == 0) { work->start(); }
            continue;
        }
        if (!source.prepared) {
            if (!work->completed()) { continue; }
            work->finish();
            if (!work->error.is_empty()) { source.failed = true; WARN_PRINT("AgeMesh dynamic preparation: " + work->error); continue; }
            source.prepared = true; work->inputs.clear();
            source.original_indices.resize(work->outputs.size());
            for (int i = 0; i < work->outputs.size(); i++) { source.original_indices[i] = work->outputs[i].indices; }
        }
        if (!shaders) { continue; }
        while (source.upload_surface < work->outputs.size()) {
            int surface = source.upload_surface;
            const AgeMeshDynamicPreparation::Output &output = work->outputs[surface];
            if (output.indices.is_empty()) { source.upload_surface++; continue; }
            if (source.indices[surface].is_null()) {
                uint64_t bytes = uint64_t(output.indices.size() + output.vertices.size() + output.ranges.size()) * 4;
                if (used_bytes + AgeMeshGpu::get_working_bytes() + bytes > budget_bytes) { source.upload_surface++; continue; }
                source.indices.write[surface] = rd->storage_buffer_create(output.indices.size() * 4);
                source.cluster_vertices.write[surface] = rd->storage_buffer_create(output.vertices.size() * 4);
                source.cluster_ranges.write[surface] = rd->storage_buffer_create(output.ranges.size() * 4);
                source.bytes += bytes; used_bytes += bytes;
                if (source.indices[surface].is_null() || source.cluster_vertices[surface].is_null() || source.cluster_ranges[surface].is_null()) { source.failed = true; break; }
            }
            const PackedInt32Array &array = source.upload_component == 0 ? output.indices : source.upload_component == 1 ? output.vertices : output.ranges;
            RID buffer = source.upload_component == 0 ? source.indices[surface] : source.upload_component == 1 ? source.cluster_vertices[surface] : source.cluster_ranges[surface];
            uint32_t count = AgeMeshGpu::claim_upload_bytes(array.size() * 4 - source.upload_offset);
            if (count == 0) { break; }
            rd->buffer_update(buffer, source.upload_offset, count, reinterpret_cast<const uint8_t *>(array.ptr()) + source.upload_offset);
            source.upload_offset += count;
            if (source.upload_offset == array.size() * 4) {
                source.upload_offset = 0; source.upload_component++;
                if (source.upload_component == 3) {
                    source.counts.write[surface] = output.indices.size();
                    source.upload_component = 0; source.upload_surface++;
                }
            }
        }
        if (source.upload_surface == work->outputs.size()) {
            source.ready = true; source.work.unref();
            bool any = false; for (uint32_t count : source.counts) { any = any || count > 0; }
            if (any) { MutexLock lock(uploaded_mutex); uploaded_meshes.insert(entry.key); }
        }
    }
}
}
void register_mesh(RID p_mesh, uint64_t p_revision, int p_budget_mb) {
    int references = 1;
    if (Source *s = sources.getptr(p_mesh)) {
        if (s->revision == p_revision) { s->references++; return; }
        references += s->references; s->references = 1; unregister_mesh(p_mesh);
    }
    if (!RD::get_singleton()) { return; }
    AgeMeshOcclusion::prewarm();
    if (shader_task == WorkerThreadPool::INVALID_TASK_ID && !shader_ready && !failed) {
        shader_task = WorkerThreadPool::get_singleton()->add_native_task(compile_shaders, nullptr, false, "AgeMesh dynamic shader compilation");
    }
    budget_bytes = uint64_t(CLAMP(p_budget_mb, 1, 4096)) * 1024 * 1024;
    Source source; source.references = references; source.revision = p_revision; source.generation = ++next_generation;
    source.work.instantiate();
    auto *storage = RendererRD::MeshStorage::get_singleton();
    source.deformed = storage->mesh_needs_instance(p_mesh, true);
    uint32_t surfaces = 0; storage->mesh_get_surface_count_and_materials(p_mesh, surfaces);
    source.surface_offsets.resize(surfaces); source.surface_lod_counts.resize(surfaces);
    uint32_t slots = 0;
    for (uint32_t i = 0; i < surfaces; i++) {
        source.surface_offsets.write[i] = slots;
        source.surface_lod_counts.write[i] = 1 + storage->mesh_surface_get_source_lod_count(storage->mesh_get_surface(p_mesh, i));
        slots += source.surface_lod_counts[i];
    }
    source.indices.resize(slots); source.cluster_vertices.resize(slots); source.cluster_ranges.resize(slots); source.counts.resize(slots); source.counts.fill(0);
    source.download_buffers.resize(slots); source.download_offsets.resize(slots); source.download_offsets.fill(0); source.work->inputs.resize(slots);
    source.aabbs.resize(slots);
    for (uint32_t i = 0; i < surfaces; i++) {
        void *surface = storage->mesh_get_surface(p_mesh, i);
        uint64_t format = storage->mesh_surface_get_format(surface);
        if (storage->mesh_surface_get_primitive(surface) != RSE::PRIMITIVE_TRIANGLES || (format & RSE::ARRAY_FLAG_USE_2D_VERTICES)) { continue; }
        for (uint32_t lod = 0; lod < source.surface_lod_counts[i]; lod++) {
            uint32_t slot = source.surface_offsets[i] + lod;
            source.aabbs.write[slot] = storage->mesh_surface_get_aabb(surface);
            auto &input = source.work->inputs.write[slot];
            input.vertices = storage->mesh_surface_get_vertex_count(surface); input.indices = storage->mesh_surface_get_source_index_count(surface, lod);
            input.index16 = input.vertices > 0 && input.vertices <= 65536;
            if ((input.indices ? input.indices : input.vertices) < CLUSTER_INDICES) { input.vertices = input.indices = 0; continue; }
            source.download_buffers.write[slot] = storage->mesh_surface_get_source_index_buffer(surface, lod);
            input.bytes.resize(uint64_t(input.indices) * (input.index16 ? 2 : 4));
        }
    }
    sources.insert(p_mesh, source);
}
void unregister_mesh(RID p_mesh) {
    Source *source = sources.getptr(p_mesh);
    if (!source || --source->references > 0) { return; }
    { MutexLock lock(uploaded_mutex); uploaded_meshes.erase(p_mesh); }
    if (source->work.is_valid() && source->work->task != WorkerThreadPool::INVALID_TASK_ID) { source->work->cancelled.set(); retired.push_back(source->work); }
    Vector<uint64_t> keys;
    for (const KeyValue<uint64_t, State> &entry : states) { if (entry.value.mesh == p_mesh) { keys.push_back(entry.key); } }
    for (uint64_t key : keys) { release(reinterpret_cast<const void *>(key)); }
    if (RD *rd = RD::get_singleton()) {
        for (const Vector<RID> *buffers : {&source->indices, &source->cluster_vertices, &source->cluster_ranges}) {
            for (RID rid : *buffers) { if (rid.is_valid()) { rd->free_rid(rid); } }
        }
    }
    used_bytes -= source->bytes; sources.erase(p_mesh);
}
bool has_mesh(RID p_mesh) { return sources.has(p_mesh); }
bool is_uploaded(RID p_mesh) { MutexLock lock(uploaded_mutex); return uploaded_meshes.has(p_mesh); }
bool should_cull(RID p_mesh, uint32_t p_triangles, bool p_alpha, bool p_deformed, bool p_occlusion) {
    const Source *source = sources.getptr(p_mesh);
    if (!source) { return false; }
    if (int(GLOBAL_GET("rendering/agemesh/dynamic_gpu_policy")) == 1 || p_occlusion) { return true; }
    // Conservative measured thresholds, not a promise that arbitrary scenes
    // become faster. Force mode remains available for profiling/correctness.
    if (p_alpha && p_triangles < uint32_t(MAX(0, int(GLOBAL_GET("rendering/agemesh/dynamic_gpu_alpha_min_triangles"))))) { return false; }
    // Separate indirect outputs prevent the renderer from instancing identical
    // non-deformed meshes. Retain the native path for shared live meshes.
    if (!p_deformed && source->references > 1) { return false; }
    return true;
}
bool has_meshes() {
    pump_preparation();
    for (const KeyValue<RID, Source> &entry : sources) { if (!preserve_shared_source(entry.value)) { return true; } }
    return false;
}
uint64_t get_working_bytes() { return used_bytes; }

void prepare(const void *p_surface, RID p_mesh, int p_surface_index, RID p_vertices, uint32_t p_vertex_count, const Vector<Plane> &p_planes, const Transform3D &p_transform, bool p_occlusion, bool p_compressed, uint32_t p_lod) {
    pump_preparation();
    uint64_t key = uint64_t(p_surface);
    State *state = states.getptr(key);
    if (state) { state->ready = false; state->bounds_ready = false; state->occlusion_applied = false; }
    Source *source = sources.getptr(p_mesh);
    if (!source || !source->ready || !shaders_available() || p_vertices.is_null() || p_planes.size() != 6 || p_surface_index < 0 || p_surface_index >= source->surface_offsets.size() || p_lod >= source->surface_lod_counts[p_surface_index]) { return; }
    uint32_t base_slot = source->surface_offsets[p_surface_index];
    uint32_t slot = base_slot + p_lod;
    if (!source->counts[slot] || source->indices[slot].is_null()) { return; }
    RD *rd = RD::get_singleton();
    uint32_t count = source->counts[slot];
    if (state && (state->mesh != p_mesh || state->capacity < count)) { release(p_surface); state = nullptr; }
    if (!state) {
        uint32_t capacity = count;
        for (uint32_t lod = 0; lod < source->surface_lod_counts[p_surface_index]; lod++) { capacity = MAX(capacity, source->counts[base_slot + lod]); }
        State s; s.mesh = p_mesh; s.capacity = capacity;
        uint32_t capacity_clusters = (capacity + CLUSTER_INDICES - 1) / CLUSTER_INDICES;
        s.bytes = uint64_t(capacity) * 4 + capacity_clusters * 40 + 84;
        if (used_bytes + AgeMeshGpu::get_working_bytes() + s.bytes > budget_bytes) { return; }
        s.visible = rd->index_buffer_create(capacity, RD::INDEX_BUFFER_FORMAT_UINT32, {}, false, RD::BUFFER_CREATION_AS_STORAGE_BIT);
        s.indices = rd->index_array_create(s.visible, 0, capacity);
        s.command = rd->storage_buffer_create(20, {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
        s.counts = rd->storage_buffer_create(capacity_clusters * 4);
        s.offsets = rd->storage_buffer_create(capacity_clusters * 4);
        s.bounds = rd->storage_buffer_create(capacity_clusters * 32);
        s.camera = rd->storage_buffer_create(64);
        used_bytes += s.bytes; states.insert(key, s); state = states.getptr(key);
        if (s.visible.is_null() || s.indices.is_null() || s.command.is_null() || s.counts.is_null() || s.offsets.is_null() || s.bounds.is_null() || s.camera.is_null()) { release(p_surface); return; }
    }
    state->count = count; state->clusters = (count + CLUSTER_INDICES - 1) / CLUSTER_INDICES;
    state->surface_index = slot; state->lod = p_lod;
    if (state->vertices != p_vertices || state->source_indices != source->indices[slot] || !rd->uniform_set_is_valid(state->uniforms)) {
        if (rd->uniform_set_is_valid(state->uniforms)) { rd->free_rid(state->uniforms); }
        Vector<RD::Uniform> uniforms;
        for (RID rid : {p_vertices, source->indices[slot], state->visible, state->command, state->counts, state->offsets, source->cluster_vertices[slot], source->cluster_ranges[slot], state->bounds}) {
            RD::Uniform uniform; uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
            uniform.binding = uniforms.size(); uniform.append_id(rid); uniforms.push_back(uniform);
        }
        state->uniforms = rd->uniform_set_create(uniforms, shader, 0); state->vertices = p_vertices;
        state->source_indices = source->indices[slot];
    }
    if (!rd->uniform_set_is_valid(state->uniforms)) { return; }
    struct Push { float planes[24]; uint32_t sizes[4]; } push = {};
    push.sizes[0] = count; push.sizes[1] = state->clusters; push.sizes[2] = p_vertex_count;
    push.sizes[3] = uint32_t(p_occlusion) | (p_compressed ? 2u : 0u);
    state->decode_transform = Transform3D();
    if (p_compressed) {
        const AABB &aabb = source->aabbs[slot];
        state->decode_transform = Transform3D(Basis::from_scale(aabb.size), aabb.position);
    }
    Transform3D transform = p_transform * state->decode_transform;
    for (int i = 0; i < 6; i++) {
        Vector3 normal = transform.basis.transposed().xform(p_planes[i].normal);
        for (int a = 0; a < 3; a++) { push.planes[i * 4 + a] = normal[a]; }
        push.planes[i * 4 + 3] = p_planes[i].d - p_planes[i].normal.dot(transform.origin);
    }
    uint32_t width = MIN(state->clusters, uint32_t(rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X)));
    RD::ComputeListID list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(list, pipelines[0]);
    rd->compute_list_bind_uniform_set(list, state->uniforms, 0);
    rd->compute_list_set_push_constant(list, &push, sizeof(push));
    for (int phase = 0; phase < 3; phase++) {
        rd->compute_list_bind_compute_pipeline(list, pipelines[phase]);
        rd->compute_list_dispatch(list, phase == 1 ? 1 : width, phase == 1 ? 1 : (state->clusters + width - 1) / width, 1);
        if (phase != 2) { rd->compute_list_add_barrier(list); }
    }
    rd->compute_list_end(); state->ready = true; state->bounds_ready = p_occlusion;
    memcpy(state->push_constants, &push, sizeof(push));
}
void occlude(const void *p_surface, RID p_pyramid, const Projection &p_local_to_clip) {
    State *s = states.getptr(uint64_t(p_surface));
    if (!s || !s->ready || !s->bounds_ready || p_pyramid.is_null()) { return; }
    RD *rd = RD::get_singleton();
    if (s->occlusion_texture != p_pyramid || !rd->uniform_set_is_valid(s->occlusion_uniforms)) {
        if (rd->uniform_set_is_valid(s->occlusion_uniforms)) { rd->free_rid(s->occlusion_uniforms); }
        Vector<RD::Uniform> bindings;
        for (RID rid : {s->bounds, s->counts, s->camera}) {
            RD::Uniform uniform; uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
            uniform.binding = bindings.size(); uniform.append_id(rid); bindings.push_back(uniform);
        }
        RD::Uniform texture; texture.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE; texture.binding = 3;
        texture.append_id(AgeMeshOcclusion::get_sampler()); texture.append_id(p_pyramid); bindings.push_back(texture);
        s->occlusion_uniforms = rd->uniform_set_create(bindings, occlusion_shader, 0); s->occlusion_texture = p_pyramid;
    }
    if (!rd->uniform_set_is_valid(s->occlusion_uniforms)) { return; }
    float matrix[16];
    Projection local_to_clip = p_local_to_clip * Projection(s->decode_transform);
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) { matrix[c * 4 + r] = local_to_clip.columns[c][r]; }
    rd->buffer_update(s->camera, 0, sizeof(matrix), matrix);
    uint32_t push[2] = {s->clusters, AgeMeshOcclusion::get_levels()};
    uint32_t groups = (s->clusters + 63) / 64;
    uint32_t width = MIN(groups, uint32_t(rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X)));
    RD::ComputeListID list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(list, occlusion_pipeline);
    rd->compute_list_bind_uniform_set(list, s->occlusion_uniforms, 0);
    rd->compute_list_set_push_constant(list, push, sizeof(push));
    rd->compute_list_dispatch(list, width, (groups + width - 1) / width, 1);
    rd->compute_list_add_barrier(list);
    rd->compute_list_bind_compute_pipeline(list, pipelines[1]);
    rd->compute_list_bind_uniform_set(list, s->uniforms, 0);
    rd->compute_list_set_push_constant(list, s->push_constants, sizeof(s->push_constants));
    rd->compute_list_dispatch(list, 1, 1, 1); rd->compute_list_add_barrier(list);
    rd->compute_list_bind_compute_pipeline(list, pipelines[2]);
    width = MIN(s->clusters, uint32_t(rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X)));
    rd->compute_list_dispatch(list, width, (s->clusters + width - 1) / width, 1);
    rd->compute_list_end(); s->occlusion_applied = true;
}
AgeMeshGpu::Draw get_draw(const void *p_surface, uint32_t p_lod) {
    State *s = states.getptr(uint64_t(p_surface));
    return s && s->ready && s->lod == p_lod ? AgeMeshGpu::Draw{s->indices, s->command} : AgeMeshGpu::Draw{};
}
void debug_readback(RID p_mesh) {
    int active = 0;
    for (const KeyValue<uint64_t, State> &entry : states) {
        const State &s = entry.value;
        if (s.mesh != p_mesh || !s.ready) { continue; }
        active++;
        PackedByteArray bytes = RD::get_singleton()->buffer_get_data(s.command);
        uint32_t count = 0; if (bytes.size() == 20) { memcpy(&count, bytes.ptr(), 4); }
        PackedByteArray counts = RD::get_singleton()->buffer_get_data(s.counts);
        PackedByteArray visible = RD::get_singleton()->buffer_get_data(s.visible);
        const Source *source = sources.getptr(p_mesh);
        PackedInt32Array original = source->original_indices[s.surface_index];
        uint32_t destination = 0;
        bool ordered = count <= s.count && count % 3 == 0 && counts.size() >= s.clusters * 4;
        for (uint32_t c = 0; ordered && c < s.clusters; c++) {
            uint32_t n; memcpy(&n, counts.ptr() + c * 4, 4);
            uint32_t size = MIN(CLUSTER_INDICES, s.count - c * CLUSTER_INDICES);
            ordered = (n == 0 || n == size) && destination + n <= count;
            if (ordered && n) { ordered = memcmp(visible.ptr() + destination * 4, original.ptr() + c * CLUSTER_INDICES, n * 4) == 0; }
            destination += n;
        }
        ordered = ordered && destination == count;
        ERR_FAIL_COND_MSG(!ordered, "AgeMesh dynamic GPU lost original primitive order.");
        print_line("AGEMESH_DYNAMIC_GPU indices=" + itos(count) + " capacity=" + itos(s.count) + " clusters=" + itos(s.clusters) + " working_bytes=" + itos(used_bytes));
        print_line("AGEMESH_DYNAMIC_GPU_ORDER_VERIFIED");
        print_line("AGEMESH_DYNAMIC_GPU_HZB " + itos(s.occlusion_applied));
        print_line("AGEMESH_DYNAMIC_GPU_LOD " + itos(s.lod));
    }
    if (!active) { print_line("AGEMESH_DYNAMIC_GPU direct_fallback working_bytes=" + itos(used_bytes)); }
}
void shutdown() {
    if (shader_task != WorkerThreadPool::INVALID_TASK_ID) {
        WorkerThreadPool::get_singleton()->wait_for_task_completion(shader_task); shader_task = WorkerThreadPool::INVALID_TASK_ID;
    }
    while (!states.is_empty()) { release(reinterpret_cast<const void *>(states.begin()->key)); }
    while (!sources.is_empty()) { sources.begin()->value.references = 1; unregister_mesh(sources.begin()->key); }
    for (const Ref<AgeMeshDynamicPreparation> &work : retired) { work->finish(); }
    retired.clear();
    if (RD *rd = RD::get_singleton()) {
        for (RID &rid : pipelines) { if (rid.is_valid()) { rd->free_rid(rid); rid = RID(); } }
        if (shader.is_valid()) { rd->free_rid(shader); }
        if (occlusion_pipeline.is_valid()) { rd->free_rid(occlusion_pipeline); }
        if (occlusion_shader.is_valid()) { rd->free_rid(occlusion_shader); }
    }
    shader = RID(); failed = false; shader_ready = false; preparation_frame = UINT64_MAX;
    occlusion_shader = occlusion_pipeline = RID();
}
}
