// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "age_mesh.h"
#include "core/object/worker_thread_pool.h"

// Immutable packed arrays only: the worker never accesses scene nodes or RD.
class AgeMeshPreparation {
    Array payloads;
    Vector<int> surface_triangles;
    int expected_triangles = 0;
    SafeFlag cancelled;
    WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
    static void run(void *p_self);
    String validate() const;
public:
    String error;
    Vector<RenderingServerTypes::SurfaceData> packed_surfaces;
    explicit AgeMeshPreparation(const Ref<AgeMeshData> &p_data);
    bool completed() const;
    ~AgeMeshPreparation();
    static void retire(AgeMeshPreparation *p_job);
    static void poll_retired();
    static void shutdown_retired();
};
