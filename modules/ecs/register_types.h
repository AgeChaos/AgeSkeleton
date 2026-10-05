#pragma once
#include "modules/register_module_types.h"
void initialize_ecs_module(ModuleInitializationLevel p_level);
void uninitialize_ecs_module(ModuleInitializationLevel p_level);

#ifdef TOOLS_ENABLED
bool is_age_skeleton_editor_build();
#endif
