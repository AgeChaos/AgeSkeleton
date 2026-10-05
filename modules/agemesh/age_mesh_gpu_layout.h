// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include <cstdint>
struct AgeMeshGpuCluster {
    float center[4];
    float extent[4];
    uint32_t range[4]; // Base indices: offset/count; vertex offset/count.
    struct Level { uint32_t offset, count; float error; uint32_t padding; } lod[3];
};
static_assert(sizeof(AgeMeshGpuCluster) == 96);
