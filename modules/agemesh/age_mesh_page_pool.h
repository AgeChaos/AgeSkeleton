// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "age_mesh.h"
#include "core/object/worker_thread_pool.h"

// Main-thread consumers share checksum validation, disk requests and resources.
// The pool retains jobs until worker/ResourceLoader completion, so destroying a
// streaming node never joins an in-flight job on the frame thread.
class AgeMeshPageJob : public RefCounted {
    String path, digest;
    SafeFlag cancelled;
    WorkerThreadPool::TaskID task;
    bool loading = false, finished = false;
    static void run(void *p_self);
public:
    uint64_t reserved_bytes = 0;
    Ref<AgeMeshData> data;
    String error;
    AgeMeshPageJob(const String &p_path, const String &p_digest, uint64_t p_bytes);
    bool completed();
    void cancel() { cancelled.set(); }
    bool is_cancelled() const { return cancelled.is_set(); }
    ~AgeMeshPageJob();
};

namespace AgeMeshPagePool {
Ref<AgeMeshPageJob> request(const String &p_path, const String &p_digest, uint64_t p_bytes);
uint64_t extra_reservation(const String &p_path, const String &p_digest, uint64_t p_bytes);
bool fits(uint64_t p_bytes);
bool over_budget();
void poll();
Dictionary get_statistics();
void shutdown();
}
