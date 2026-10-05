// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/variant/variant.h"

// Immutable inputs, CPU-only work; cancellation never waits on the render thread.
class AgeMeshDynamicPreparation : public RefCounted {
public:
    struct Input {
        PackedByteArray bytes;
        uint32_t vertices = 0, indices = 0;
        bool index16 = false;
    };
    struct Output {
        PackedInt32Array indices, vertices, ranges;
    };
    Vector<Input> inputs;
    Vector<Output> outputs;
    SafeFlag cancelled;
    String error;
    WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
    void start();
    bool completed() const;
    void finish();
    static void run(void *p_self);
};
