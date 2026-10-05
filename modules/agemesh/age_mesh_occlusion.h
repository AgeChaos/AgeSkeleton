// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "core/templates/rid.h"
namespace AgeMeshOcclusion {
void prewarm();
RID build(RID p_depth);
RID get_sampler();
uint32_t get_levels();
void shutdown();
}
