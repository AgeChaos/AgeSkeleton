#pragma once
#ifdef TOOLS_ENABLED
#include "scene/resources/animation.h"

bool ecs_spine_channel_length(const Dictionary &p_animation, const Array &p_slots, double &r_length, String &r_error);
bool ecs_spine_import_channels(const Dictionary &p_animation, const Array &p_slots, const Dictionary &p_event_defaults, const PackedStringArray &p_events, const Ref<Animation> &p_clip, String &r_error);
#endif
