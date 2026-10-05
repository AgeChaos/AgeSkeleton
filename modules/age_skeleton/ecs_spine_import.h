#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene.h"
Ref<ECSScene> ecs_import_spine_json(const String &p_path,String &r_report);
bool ecs_spine_resolve_linked_meshes(Dictionary &p_skins,String &r_error);
#endif
