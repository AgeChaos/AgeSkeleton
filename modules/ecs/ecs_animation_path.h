// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once

#include "core/string/node_path.h"

// ECS transform tracks may target the owning entity without a NodePath.
// Empty paths have no property channel; keep NodePath diagnostics for other callers.
inline StringName ecs_animation_subnames(const NodePath &p_path) {
	return p_path.is_empty() ? StringName() : p_path.get_concatenated_subnames();
}
