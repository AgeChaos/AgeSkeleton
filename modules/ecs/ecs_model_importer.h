#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene.h"

class ECSModelImporter {
public:
	static bool accepts(const Variant &p_drag);
	static bool append(const String &p_path, Array &r_entities, int p_parent, String &r_error, const Ref<ECSScene> &p_context = Ref<ECSScene>());
	static bool test();
};
#endif
