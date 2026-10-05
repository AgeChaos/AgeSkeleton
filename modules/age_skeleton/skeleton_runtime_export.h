#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene.h"
bool pack_skeleton_atlas_entities(Array &p_entities, String &r_error);
// Portable, sampled mesh animation. No engine objects or proprietary runtime dependencies.
Dictionary export_skeleton_runtime(const Ref<ECSScene> &p_scene, int p_rig, const String &p_directory, int p_fps);
#endif
