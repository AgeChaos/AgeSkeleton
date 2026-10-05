// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_dynamic_preparation.h"
#include <algorithm>

void AgeMeshDynamicPreparation::start() {
    task = WorkerThreadPool::get_singleton()->add_native_task(run, this, false, "AgeMesh dynamic index preparation");
}
bool AgeMeshDynamicPreparation::completed() const {
    return task != WorkerThreadPool::INVALID_TASK_ID && WorkerThreadPool::get_singleton()->is_task_completed(task);
}
void AgeMeshDynamicPreparation::finish() {
    if (task != WorkerThreadPool::INVALID_TASK_ID) {
        WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
        task = WorkerThreadPool::INVALID_TASK_ID;
    }
}
void AgeMeshDynamicPreparation::run(void *p_self) {
    auto *self = static_cast<AgeMeshDynamicPreparation *>(p_self);
    self->outputs.resize(self->inputs.size());
    for (int s = 0; s < self->inputs.size(); s++) {
        const Input &input = self->inputs[s];
        Output &output = self->outputs.write[s];
        uint32_t count = input.indices ? input.indices : input.vertices;
        if (count < 384 || count % 3 || !input.vertices) { continue; }
        if (input.indices && input.bytes.size() != int64_t(count) * (input.index16 ? 2 : 4)) { self->error = "index byte count mismatch"; return; }
        output.indices.resize(count);
        int32_t *indices = output.indices.ptrw();
        for (uint32_t i = 0; i < count; i++) {
            if ((i & 4095) == 0 && self->cancelled.is_set()) { return; }
            uint32_t value = i;
            if (input.indices && input.index16) { uint16_t v; memcpy(&v, input.bytes.ptr() + i * 2, 2); value = v; }
            else if (input.indices) { memcpy(&value, input.bytes.ptr() + i * 4, 4); }
            if (value >= input.vertices) { self->error = "index outside source vertices"; return; }
            indices[i] = value;
        }
        uint32_t clusters = (count + 383) / 384;
        output.ranges.resize(clusters * 2);
        // Reserve once rather than repeatedly growing the packed array.
        output.vertices.resize(count);
        uint32_t total = 0;
        int32_t *vertices = output.vertices.ptrw(), *ranges = output.ranges.ptrw();
        for (uint32_t c = 0; c < clusters; c++) {
            if ((c & 63) == 0 && self->cancelled.is_set()) { return; }
            int32_t unique[384]; uint32_t size = MIN(384u, count - c * 384);
            memcpy(unique, indices + c * 384, size * 4);
            std::sort(unique, unique + size);
            uint32_t unique_count = std::unique(unique, unique + size) - unique;
            ranges[c * 2] = total; ranges[c * 2 + 1] = unique_count;
            memcpy(vertices + total, unique, unique_count * 4); total += unique_count;
        }
        output.vertices.resize(total);
    }
}
