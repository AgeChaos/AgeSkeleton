// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_occlusion.h"
#include "core/object/worker_thread_pool.h"
#include "servers/rendering/rendering_device.h"

namespace AgeMeshOcclusion {
namespace {
RID shader, pipeline, sampler, pyramid, input;
Vector<RID> slices, uniforms;
uint32_t width = 0, height = 0, levels = 0;
bool failed = false;
bool ready = false;
WorkerThreadPool::TaskID preparation_task = WorkerThreadPool::INVALID_TASK_ID;
const char *source = R"GLSL(#version 450
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform sampler2D source_depth;
layout(set=0,binding=1,r32f) uniform restrict writeonly image2D target_depth;
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy), target_size=imageSize(target_depth);
    if (any(greaterThanEqual(pixel,target_size))) return;
    ivec2 source_size=textureSize(source_depth,0);
    ivec2 begin=pixel*source_size/target_size;
    ivec2 end=((pixel+1)*source_size+target_size-1)/target_size;
    // Reverse Z: retain the farthest covered depth. Background zero prevents culling.
    float depth=1.0;
    for (int y=begin.y;y<end.y;y++) for (int x=begin.x;x<end.x;x++)
        depth=min(depth,texelFetch(source_depth,ivec2(x,y),0).r);
    imageStore(target_depth,pixel,vec4(depth));
}
)GLSL";
void clear_uniforms() {
    RD *rd = RD::get_singleton();
    for (RID rid : uniforms) { if (rd->uniform_set_is_valid(rid)) { rd->free_rid(rid); } }
    uniforms.clear(); input = RID();
}
void clear_texture() {
    RD *rd = RD::get_singleton(); clear_uniforms();
    for (RID rid : slices) { rd->free_rid(rid); }
    slices.clear();
    if (pyramid.is_valid()) { rd->free_rid(pyramid); }
    pyramid = RID(); width = height = levels = 0;
}
bool initialize() {
    if (pipeline.is_valid()) { return true; }
    if (failed) { return false; }
    RD *rd = RD::get_singleton();
    RD::ShaderStageSPIRVData stage; stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
    String error; stage.spirv = rd->shader_compile_spirv_from_source(stage.shader_stage, source, RD::SHADER_LANGUAGE_GLSL, &error);
    if (stage.spirv.is_empty()) { failed = true; ERR_PRINT("AgeMesh HZB shader: " + error); return false; }
    Vector<RD::ShaderStageSPIRVData> stages; stages.push_back(stage);
    shader = rd->shader_create_from_spirv(stages, "AgeMesh conservative depth pyramid");
    if (shader.is_valid()) { pipeline = rd->compute_pipeline_create(shader); }
    if (pipeline.is_null()) { failed = true; return false; }
    RD::SamplerState state; sampler = rd->sampler_create(state);
    failed = sampler.is_null();
    return !failed;
}
void initialize_worker(void *) { ready = initialize(); }
}
void prewarm() {
    if (preparation_task == WorkerThreadPool::INVALID_TASK_ID && !ready && !failed && RD::get_singleton()) {
        preparation_task = WorkerThreadPool::get_singleton()->add_native_task(initialize_worker, nullptr, false, "AgeMesh depth pyramid pipeline");
    }
}
RID build(RID p_depth) {
    prewarm();
    if (preparation_task != WorkerThreadPool::INVALID_TASK_ID) {
        if (!WorkerThreadPool::get_singleton()->is_task_completed(preparation_task)) { return RID(); }
        WorkerThreadPool::get_singleton()->wait_for_task_completion(preparation_task);
        preparation_task = WorkerThreadPool::INVALID_TASK_ID;
    }
    RD *rd = RD::get_singleton();
    if (!rd || !rd->texture_is_valid(p_depth) || !ready) { return RID(); }
    RD::TextureFormat format = rd->texture_get_format(p_depth);
    if (format.samples != RD::TEXTURE_SAMPLES_1 || !format.width || !format.height) { return RID(); }
    if (format.width != width || format.height != height) {
        clear_texture(); width = format.width; height = format.height;
        levels = 1;
        for (uint32_t w = width, h = height; w > 1 || h > 1; w = MAX(1u, w / 2), h = MAX(1u, h / 2)) { levels++; }
        RD::TextureFormat target;
        target.format = RD::DATA_FORMAT_R32_SFLOAT; target.width = width; target.height = height; target.mipmaps = levels;
        target.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
        pyramid = rd->texture_create(target, RD::TextureView());
        if (pyramid.is_null()) { clear_texture(); return RID(); }
        for (uint32_t mip = 0; mip < levels; mip++) { slices.push_back(rd->texture_create_shared_from_slice(RD::TextureView(), pyramid, 0, mip)); }
    }
    if (input != p_depth || uniforms.is_empty() || !rd->uniform_set_is_valid(uniforms[0])) {
        clear_uniforms(); input = p_depth;
        for (uint32_t mip = 0; mip < levels; mip++) {
            Vector<RD::Uniform> bindings;
            RD::Uniform texture; texture.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE; texture.binding = 0;
            texture.append_id(sampler); texture.append_id(mip ? slices[mip - 1] : p_depth); bindings.push_back(texture);
            RD::Uniform image; image.uniform_type = RD::UNIFORM_TYPE_IMAGE; image.binding = 1; image.append_id(slices[mip]); bindings.push_back(image);
            RID uniform = rd->uniform_set_create(bindings, shader, 0);
            if (uniform.is_null()) { clear_uniforms(); return RID(); }
            uniforms.push_back(uniform);
        }
    }
    uint32_t w = width, h = height;
    for (uint32_t mip = 0; mip < levels; mip++) {
        RD::ComputeListID list = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(list, pipeline); rd->compute_list_bind_uniform_set(list, uniforms[mip], 0);
        rd->compute_list_dispatch(list, (w + 7) / 8, (h + 7) / 8, 1); rd->compute_list_end();
        w = MAX(1u, w / 2); h = MAX(1u, h / 2);
    }
    return pyramid;
}
RID get_sampler() { return sampler; }
uint32_t get_levels() { return levels; }
void shutdown() {
    if (preparation_task != WorkerThreadPool::INVALID_TASK_ID) {
        WorkerThreadPool::get_singleton()->wait_for_task_completion(preparation_task);
        preparation_task = WorkerThreadPool::INVALID_TASK_ID;
    }
    if (RD *rd = RD::get_singleton()) {
        clear_texture();
        for (RID rid : {pipeline, shader, sampler}) { if (rid.is_valid()) { rd->free_rid(rid); } }
    }
    pipeline = shader = sampler = RID(); failed = false; ready = false;
}
}
