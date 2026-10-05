// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_page_pool.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/templates/hash_map.h"

AgeMeshPageJob::AgeMeshPageJob(const String &p_path, const String &p_digest, uint64_t p_bytes) : path(p_path), digest(p_digest), reserved_bytes(p_bytes) {
    task = WorkerThreadPool::get_singleton()->add_native_task(run, this, false, "AgeMesh shared disk page");
}
void AgeMeshPageJob::run(void *p_self) {
    auto *self = static_cast<AgeMeshPageJob *>(p_self);
    if (self->cancelled.is_set()) { return; }
    if (!FileAccess::exists(self->path) || FileAccess::get_sha256(self->path) != self->digest) { self->error = "page missing or checksum mismatch: " + self->path; }
}
bool AgeMeshPageJob::completed() {
    if (finished) { return true; }
    if (task != WorkerThreadPool::INVALID_TASK_ID) {
        if (!WorkerThreadPool::get_singleton()->is_task_completed(task)) { return false; }
        WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
        task = WorkerThreadPool::INVALID_TASK_ID;
    }
    if (!loading) {
        if (cancelled.is_set() || !error.is_empty()) { finished = true; return true; }
        Error err = ResourceLoader::load_threaded_request(path, "AgeMeshData", false, ResourceLoader::CACHE_MODE_REUSE);
        if (err != OK) { error = "could not request geometry page: " + path; finished = true; return true; }
        loading = true;
    }
    if (ResourceLoader::load_threaded_get_status(path) == ResourceLoader::THREAD_LOAD_IN_PROGRESS) { return false; }
    Error err = OK;
    data = ResourceLoader::load_threaded_get(path, &err);
    loading = false; finished = true;
    if (err != OK || data.is_null() || data->get_source().is_null()) { error = "invalid geometry page: " + path; }
    if (cancelled.is_set()) { data.unref(); }
    return true;
}
AgeMeshPageJob::~AgeMeshPageJob() {
    cancelled.set();
    if (task != WorkerThreadPool::INVALID_TASK_ID) { WorkerThreadPool::get_singleton()->wait_for_task_completion(task); }
    if (loading) { ResourceLoader::load_threaded_get(path); }
}

namespace AgeMeshPagePool {
namespace {
HashMap<String, Ref<AgeMeshPageJob>> jobs;
uint64_t reserved_bytes = 0, requests = 0, reused = 0, peak_bytes = 0;
uint64_t budget() { return uint64_t(CLAMP(int(GLOBAL_GET("rendering/agemesh/streaming_shared_page_memory_mb")), 1, 4096)) * 1024 * 1024; }
}
Ref<AgeMeshPageJob> request(const String &p_path, const String &p_digest, uint64_t p_bytes) {
    String key = p_path + ":" + p_digest;
    if (const Ref<AgeMeshPageJob> *found = jobs.getptr(key)) {
        // A canceled generation must drain before this key can be requested again.
        if ((*found)->is_cancelled()) { return Ref<AgeMeshPageJob>(); }
        reused++;
        return *found;
    }
    if (reserved_bytes + p_bytes > budget()) { return Ref<AgeMeshPageJob>(); }
    Ref<AgeMeshPageJob> job = memnew(AgeMeshPageJob(p_path, p_digest, p_bytes));
    jobs.insert(key, job); reserved_bytes += p_bytes; peak_bytes = MAX(peak_bytes, reserved_bytes); requests++;
    return job;
}
uint64_t extra_reservation(const String &p_path, const String &p_digest, uint64_t p_bytes) {
    if (const Ref<AgeMeshPageJob> *found = jobs.getptr(p_path + ":" + p_digest)) {
        return (*found)->is_cancelled() ? UINT64_MAX : 0;
    }
    return p_bytes;
}
bool fits(uint64_t p_bytes) { return p_bytes <= budget() && reserved_bytes <= budget() - p_bytes; }
bool over_budget() { return reserved_bytes > budget(); }
void poll() {
    Vector<String> retired;
    for (const KeyValue<String, Ref<AgeMeshPageJob>> &entry : jobs) {
        bool unused = entry.value->get_reference_count() == 1;
        if (unused) { entry.value->cancel(); }
        if (entry.value->completed() && unused) { retired.push_back(entry.key); }
    }
    for (const String &key : retired) { reserved_bytes -= jobs[key]->reserved_bytes; jobs.erase(key); }
}
Dictionary get_statistics() {
    Dictionary result;
    result["pages"] = jobs.size(); result["reserved_bytes"] = int64_t(reserved_bytes); result["budget_bytes"] = int64_t(budget());
    result["peak_bytes"] = int64_t(peak_bytes); result["disk_requests"] = int64_t(requests); result["reused_requests"] = int64_t(reused);
    return result;
}
void shutdown() {
    for (const KeyValue<String, Ref<AgeMeshPageJob>> &entry : jobs) { entry.value->cancel(); }
    jobs.clear(); reserved_bytes = requests = reused = peak_bytes = 0;
}
}
