// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"
#include "ecs_animation_graph.h"
#include "ecs_animation_path.h"

#include "servers/rendering/rendering_server.h"

bool ECSWorld::set_animation(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Dictionary data = get_animation(id);
	// Changing the primary clip must not retain an unrelated secondary layer.
	if (definition.has("clip") && data.get("clip", Variant()) != definition["clip"] && !definition.has("secondary")) {
		data.erase("secondary");
	}
	data.merge(definition, true);
	Dictionary blend_space;
	if (data.has("blend_space") && data["blend_space"].get_type() != Variant::NIL) {
		if (data["blend_space"].get_type() != Variant::DICTIONARY || definition.has("secondary") || definition.has("states") || definition.has("state")) {
			return false;
		}
		blend_space = Dictionary(data["blend_space"]).duplicate(true);
		for (const Variant &key : blend_space.keys()) {
			if ((key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) || (String(key) != "points" && String(key) != "position")) {
				return false;
			}
		}
		Variant raw_points = blend_space.get("points", Variant());
		Variant parameter = blend_space.get("position", 0.0);
		if (raw_points.get_type() != Variant::ARRAY || (parameter.get_type() != Variant::INT && parameter.get_type() != Variant::FLOAT) || !Math::is_finite(double(parameter))) {
			return false;
		}
		Array points = raw_points;
		if (points.size() < 2) {
			return false;
		}
		double last = -INFINITY;
		Ref<Animation> layout;
		for (int i = 0; i < points.size(); i++) {
			if (points[i].get_type() != Variant::DICTIONARY) {
				return false;
			}
			Dictionary point = points[i];
			if (point.size() != 2 || !point.has("position") || !point.has("clip")) {
				return false;
			}
			Variant coordinate = point["position"];
			if ((coordinate.get_type() != Variant::INT && coordinate.get_type() != Variant::FLOAT) || !Math::is_finite(double(coordinate)) || double(coordinate) <= last || point["clip"].get_type() != Variant::OBJECT) {
				return false;
			}
			last = coordinate;
			Ref<Animation> candidate = point["clip"];
			if (candidate.is_null() || candidate->get_length() <= 0 || !Math::is_finite(candidate->get_length()) || candidate->get_loop_mode() != Animation::LOOP_LINEAR) {
				return false;
			}
			if (layout.is_null()) {
				layout = candidate;
			}
			if (candidate->get_track_count() != layout->get_track_count()) {
				return false;
			}
			for (int t = 0; t < layout->get_track_count(); t++) {
				if (candidate->track_get_type(t) != layout->track_get_type(t) || candidate->track_get_path(t) != layout->track_get_path(t)) {
					return false;
				}
			}
		}
		int left = 0;
		while (left < points.size() - 2 && double(parameter) > double(Dictionary(points[left + 1])["position"])) {
			left++;
		}
		Dictionary a = points[left], b = points[left + 1];
		Ref<Animation> first = a["clip"], second = b["clip"];
		double weight = CLAMP((double(parameter) - double(a["position"])) / (double(b["position"]) - double(a["position"])), 0.0, 1.0);
		const AnimationState *old = animations.getptr(uint32_t(id));
		double phase = old && !old->blend_space.is_empty() ? old->time / old->clip->get_length() : 0.0;
		if (definition.has("time")) {
			Variant time = definition["time"];
			if ((time.get_type() != Variant::INT && time.get_type() != Variant::FLOAT) || !Math::is_finite(double(time))) {
				return false;
			}
			phase = double(time) / first->get_length();
		}
		Variant speed_value = data.get("speed", 1.0);
		if ((speed_value.get_type() != Variant::INT && speed_value.get_type() != Variant::FLOAT) || !Math::is_finite(double(speed_value))) {
			return false;
		}
		data["clip"] = first;
		data["time"] = phase * first->get_length();
		Dictionary secondary;
		secondary["clip"] = second;
		secondary["time"] = phase * second->get_length();
		secondary["speed"] = double(speed_value) * second->get_length() / first->get_length();
		secondary["weight"] = weight;
		data["secondary"] = secondary;
		data.erase("states");
		data.erase("state");
		blend_space["position"] = double(parameter);
	}

	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "clip" && field != "targets" && field != "time" && field != "speed" && field != "playing" && field != "blend_duration" && field != "transition" && field != "secondary" && field != "states" && field != "state" && field != "blend_space" && field != "root_motion_track" && field != "effect_events" && field != "graph") {
			return false;
		}
	}
	if (!data.has("clip") || data["clip"].get_type() != Variant::OBJECT) {
		return false;
	}
	Ref<Animation> clip = data["clip"];
	if (clip.is_null()) {
		return false;
	}
	AnimationState state;
	Variant raw_events=data.get("effect_events",Array());
	if(raw_events.get_type()!=Variant::ARRAY || Array(raw_events).size()>256) { return false; }
	state.effect_events=Array(raw_events).duplicate(true);
	for(const Variant &raw:state.effect_events) {
		if(raw.get_type()!=Variant::DICTIONARY) { return false; }
		Dictionary event=raw; Variant at=event.get("time",Variant()), name=event.get("event",Variant());
		if(event.size()!=2 || (at.get_type()!=Variant::INT && at.get_type()!=Variant::FLOAT) || !Math::is_finite(double(at)) || double(at)<0 || double(at)>clip->get_length() || name.get_type()!=Variant::STRING || String(name).is_empty()) { return false; }
	}
	state.clip = clip;
	if (data.has("graph") && data["graph"].get_type()!=Variant::NIL) {
		if(data["graph"].get_type()!=Variant::DICTIONARY) { return false; }
		state.graph=Dictionary(data["graph"]).duplicate(true);
		if(!state.graph.is_empty() && (!ECSAnimationGraph::validate(state.graph,clip) || data.has("secondary") || data.has("blend_space") || int(data.get("root_motion_track",-1))>=0 || data.has("states"))) { return false; }
	}
	state.blend_space = blend_space;
	if (data.has("targets")) {
		if (data["targets"].get_type() != Variant::PACKED_INT64_ARRAY) {
			return false;
		}
		state.targets = data["targets"];
	} else {
		state.targets.resize(clip->get_track_count());
		state.targets.fill(id);
	}
	if (state.targets.size() != clip->get_track_count()) {
		return false;
	}
	for (int i = 0; i < state.targets.size(); i++) {
		if (!is_alive(state.targets[i])) {
			return false;
		}
		if (!valid_skeletal_animation_track(state.targets[i],clip,i)) { return false; }
		auto type = clip->track_get_type(i);
		if (type != Animation::TYPE_POSITION_3D && type != Animation::TYPE_ROTATION_3D && type != Animation::TYPE_SCALE_3D && type != Animation::TYPE_VALUE) {
			return false;
		}
		if (type == Animation::TYPE_VALUE) {
			String property = ecs_animation_subnames(clip->track_get_path(i));
			state.has_event_tracks |= property.get_slice(":",0)=="event";
			if (property != "position" && property != "rotation" && property != "scale" && property != "shear" && !property.begins_with("ui:") && !property.begins_with("slot:") && property!="polygon:frame" && property.get_slice(":",0)!="event") {
				return false;
			}
		}
	}
	for (const String &field : { String("time"), String("speed") }) {
		Variant value = data.get(field, field == "time" ? 0.0 : 1.0);
		if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value))) {
			return false;
		}
		if (field == "time") {
			state.time = value;
		} else {
			state.speed = value;
		}
	}
	if (data.has("secondary") && data["secondary"].get_type() != Variant::NIL) {
		if (data["secondary"].get_type() != Variant::DICTIONARY) {
			return false;
		}
		Dictionary secondary = data["secondary"];
		for (const Variant &key : secondary.keys()) {
			if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
				return false;
			}
			String field = key;
			if (field != "clip" && field != "time" && field != "speed" && field != "weight" && field != "track_weights" && field != "additive" && field != "reference_time") {
				return false;
			}
		}
		Variant resource = secondary.get("clip", Variant());
		if (resource.get_type() != Variant::OBJECT) {
			return false;
		}
		state.secondary_clip = resource;
		if (state.secondary_clip.is_null() || state.secondary_clip->get_track_count() != clip->get_track_count()) {
			return false;
		}
		for (int i = 0; i < clip->get_track_count(); i++) {
			if (!valid_skeletal_animation_track(state.targets[i],state.secondary_clip,i) || state.secondary_clip->track_get_type(i) != clip->track_get_type(i) || state.secondary_clip->track_get_path(i) != clip->track_get_path(i)) {
				return false;
			}
		}
		Variant mask = secondary.get("track_weights", PackedFloat32Array());
		Variant additive = secondary.get("additive", false);
		Variant reference_time = secondary.get("reference_time", 0.0);
		if (mask.get_type() != Variant::PACKED_FLOAT32_ARRAY || additive.get_type() != Variant::BOOL || (reference_time.get_type() != Variant::INT && reference_time.get_type() != Variant::FLOAT) || !Math::is_finite(double(reference_time)) || double(reference_time) < 0 || double(reference_time) > state.secondary_clip->get_length()) {
			return false;
		}
		state.secondary_weights = mask;
		state.secondary_additive = additive;
		state.secondary_reference_time = reference_time;
		if (!state.secondary_weights.is_empty() && state.secondary_weights.size() != clip->get_track_count()) {
			return false;
		}
		for (float weight : state.secondary_weights) {
			if (!Math::is_finite(weight) || weight < 0 || weight > 1) {
				return false;
			}
		}
		if (state.secondary_additive) {
			for (int i = 0; i < clip->get_track_count(); i++) {
				Variant reference;
				auto type = clip->track_get_type(i);
				if (type == Animation::TYPE_POSITION_3D || type == Animation::TYPE_SCALE_3D) {
					Vector3 value;
					Error error = type == Animation::TYPE_POSITION_3D ? state.secondary_clip->try_position_track_interpolate(i, state.secondary_reference_time, &value) : state.secondary_clip->try_scale_track_interpolate(i, state.secondary_reference_time, &value);
					if (error != OK || !value.is_finite()) {
						return false;
					}
					reference = value;
				} else if (type == Animation::TYPE_ROTATION_3D) {
					Quaternion value;
					if (state.secondary_clip->try_rotation_track_interpolate(i, state.secondary_reference_time, &value) != OK || !value.is_finite() || Math::is_zero_approx(value.length_squared())) {
						return false;
					}
					reference = value.normalized();
				} else {
					// Additive value/UI tracks require property-specific semantics.
					return false;
				}
				state.secondary_reference.push_back(reference);
			}
		}
		for (const String &field : { String("time"), String("speed"), String("weight") }) {
			Variant value = secondary.get(field, field == "time" ? 0.0 : 1.0);
			if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value))) {
				return false;
			}
			if (field == "weight" && (double(value) < 0 || double(value) > 1)) {
				return false;
			}
			if (field == "time") {
				state.secondary_time = value;
			} else if (field == "speed") {
				state.secondary_speed = value;
			} else {
				state.secondary_weight = value;
			}
		}
	}
	if (data.has("states")) {
		if (data["states"].get_type() != Variant::DICTIONARY) {
			return false;
		}
		Dictionary states = data["states"];
		for (const Variant &key : states.keys()) {
			if ((key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) || String(key).is_empty() || states[key].get_type() != Variant::OBJECT) {
				return false;
			}
			Ref<Animation> candidate = states[key];
			if (candidate.is_null() || candidate->get_track_count() != clip->get_track_count()) {
				return false;
			}
			for (int i = 0; i < clip->get_track_count(); i++) {
				if (!valid_skeletal_animation_track(state.targets[i],candidate,i) || candidate->track_get_type(i) != clip->track_get_type(i) || candidate->track_get_path(i) != clip->track_get_path(i)) {
					return false;
				}
			}
			state.states[StringName(key)] = candidate;
		}
	}
	Variant current = data.get("state", StringName());
	if (current.get_type() != Variant::STRING && current.get_type() != Variant::STRING_NAME) {
		return false;
	}
	state.current_state = current;
	if (!state.current_state.is_empty() && (!state.states.has(state.current_state) || Ref<Animation>(state.states[state.current_state]) != clip)) {
		return false;
	}
	Variant playing = data.get("playing", true);
	if (playing.get_type() != Variant::BOOL) {
		return false;
	}
	state.playing = playing;
	const AnimationState *previous = animations.getptr(uint32_t(id));
	state.event_start_pending = previous && previous->clip==clip && !definition.has("time") ? previous->event_start_pending : state.time==0;
	Variant duration = definition.get("blend_duration", 0.0);
	if ((duration.get_type() != Variant::FLOAT && duration.get_type() != Variant::INT) || !Math::is_finite(double(duration)) || double(duration) < 0) {
		return false;
	}
	if (double(duration) > 0) {
		state.blend_duration = duration;
		state.blend_from.resize(state.targets.size());
		for (int i = 0; i < state.targets.size(); i++) {
			auto type = clip->track_get_type(i);
			String property = type == Animation::TYPE_POSITION_3D ? "position" : type == Animation::TYPE_ROTATION_3D ? "rotation"
					: type == Animation::TYPE_SCALE_3D																 ? "scale"
																													 : ecs_animation_subnames(clip->track_get_path(i));
			state.blend_from.write[i] = property.get_slice(":",0)=="event" ? Variant() : property.begins_with("slot:") ? get_skeleton_slot_value(state.targets[i],property) : property.begins_with("ui:") ? get_ui(state.targets[i]).get(property.substr(3), Variant()) : Variant(get_vector(state.targets[i], property));
		}
	} else if (!definition.has("blend_duration") && definition.has("transition")) {
		if (definition["transition"].get_type() != Variant::DICTIONARY) {
			return false;
		}
		Dictionary transition = definition["transition"];
		Variant duration_value = transition.get("duration", Variant()), elapsed_value = transition.get("elapsed", Variant()), from_value = transition.get("from", Variant());
		if ((duration_value.get_type() != Variant::FLOAT && duration_value.get_type() != Variant::INT) || (elapsed_value.get_type() != Variant::FLOAT && elapsed_value.get_type() != Variant::INT) || from_value.get_type() != Variant::ARRAY) {
			return false;
		}
		state.blend_duration = duration_value;
		state.blend_elapsed = elapsed_value;
		Array from = from_value;
		if (!Math::is_finite(state.blend_duration) || !Math::is_finite(state.blend_elapsed) || state.blend_duration <= 0 || state.blend_elapsed < 0 || state.blend_elapsed >= state.blend_duration || from.size() != state.targets.size()) {
			return false;
		}
		for (const Variant &value : from) {
			if (value.get_type() != Variant::NIL && value.get_type() != Variant::BOOL && value.get_type() != Variant::STRING && value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT && value.get_type() != Variant::VECTOR2 && value.get_type() != Variant::VECTOR3 && value.get_type() != Variant::COLOR && value.get_type() != Variant::RECT2 && value.get_type() != Variant::VECTOR4 && value.get_type() != Variant::OBJECT) {
				return false;
			}
			if ((value.get_type() == Variant::FLOAT && !Math::is_finite(double(value))) ||
					(value.get_type() == Variant::VECTOR2 && !Vector2(value).is_finite()) ||
					(value.get_type() == Variant::VECTOR3 && !Vector3(value).is_finite()) ||
					(value.get_type() == Variant::VECTOR4 && !Vector4(value).is_finite()) ||
					(value.get_type() == Variant::RECT2 && !Rect2(value).is_finite()) ||
					(value.get_type() == Variant::OBJECT && Ref<Resource>(value).is_null())) {
				return false;
			}
			if (value.get_type() == Variant::COLOR) {
				Color color = value;
				if (!Math::is_finite(color.r) || !Math::is_finite(color.g) || !Math::is_finite(color.b) || !Math::is_finite(color.a)) {
					return false;
				}
			}
			state.blend_from.push_back(value);
		}
	} else if (!definition.has("blend_duration") && previous && previous->clip == state.clip && previous->targets == state.targets) {
		// Pausing or adjusting speed must not silently discard an active transition.
		state.blend_duration = previous->blend_duration;
		state.blend_elapsed = previous->blend_elapsed;
		state.blend_from = previous->blend_from;
	}
	Variant root = data.get("root_motion_track", -1);
	if (root.get_type() != Variant::INT || int64_t(root) < -1 || int64_t(root) >= clip->get_track_count()) {
		return false;
	}
	state.root_motion_track = int64_t(root);
	if (state.root_motion_track >= 0) {
		if (clip->track_get_type(state.root_motion_track) != Animation::TYPE_POSITION_3D || clip->track_get_key_count(state.root_motion_track) == 0 || state.secondary_additive) {
			return false;
		}
		state.root_clip = clip->duplicate();
		state.root_clip->set_loop_mode(Animation::LOOP_NONE);
		if (state.secondary_clip.is_valid()) {
			if (state.secondary_clip->track_get_key_count(state.root_motion_track) == 0) {
				return false;
			}
			state.root_secondary_clip = state.secondary_clip->duplicate();
			state.root_secondary_clip->set_loop_mode(Animation::LOOP_NONE);
		}
	}
	animations[uint32_t(id)] = state;
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "animation", &marker);
	return true;
}
bool ECSWorld::set_animation_blend_position(uint64_t id, double position) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	const AnimationState *state = is_alive(id) ? animations.getptr(uint32_t(id)) : nullptr;
	if (!state || state->blend_space.is_empty() || !Math::is_finite(position)) {
		return false;
	}
	Dictionary space = state->blend_space.duplicate(true);
	space["position"] = position;
	Dictionary update;
	update["blend_space"] = space;
	return set_animation(id, update);
}
bool ECSWorld::set_animation_layer_weight(uint64_t id, double weight) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	AnimationState *state = is_alive(id) ? animations.getptr(uint32_t(id)) : nullptr;
	if (!state || !state->blend_space.is_empty() || state->secondary_clip.is_null() || !Math::is_finite(weight) || weight < 0 || weight > 1) {
		return false;
	}
	state->secondary_weight = weight;
	return true;
}
Dictionary ECSWorld::get_animation(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	Dictionary result;
	const AnimationState *state = is_alive(id) ? animations.getptr(uint32_t(id)) : nullptr;
	if (state) {
		result["clip"] = state->clip;
		if(!state->graph.is_empty()) { result["graph"]=state->graph.duplicate(true); }
		if (!state->effect_events.is_empty()) { result["effect_events"]=state->effect_events.duplicate(true); }
		result["root_motion_track"] = state->root_motion_track;
		if (!state->blend_space.is_empty()) {
			result["blend_space"] = state->blend_space.duplicate(true);
		}
		if (!state->states.is_empty()) {
			result["states"] = state->states.duplicate();
			result["state"] = String(state->current_state);
		}
		result["targets"] = state->targets;
		result["time"] = state->time;
		result["speed"] = state->speed;
		result["playing"] = state->playing;
		if (state->secondary_clip.is_valid() && state->blend_space.is_empty()) {
			Dictionary secondary;
			secondary["clip"] = state->secondary_clip;
			secondary["time"] = state->secondary_time;
			secondary["speed"] = state->secondary_speed;
			secondary["weight"] = state->secondary_weight;
			secondary["track_weights"] = state->secondary_weights;
			secondary["additive"] = state->secondary_additive;
			secondary["reference_time"] = state->secondary_reference_time;
			result["secondary"] = secondary;
		}
		if (state->blend_elapsed < state->blend_duration) {
			Dictionary transition;
			Array from;
			for (const Variant &value : state->blend_from) {
				from.push_back(value);
			}
			transition["duration"] = state->blend_duration;
			transition["elapsed"] = state->blend_elapsed;
			transition["from"] = from;
			result["transition"] = transition;
		}
	}
	return result;
}
bool ECSWorld::travel_animation(uint64_t id, const StringName &name, double duration) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	const AnimationState *state = is_alive(id) ? animations.getptr(uint32_t(id)) : nullptr;
	if (!state || !state->states.has(name) || !Math::is_finite(duration) || duration < 0) {
		return false;
	}
	Dictionary update;
	update["clip"] = state->states[name];
	update["state"] = name;
	update["time"] = 0.0;
	update["playing"] = true;
	update["blend_duration"] = duration;
	update["secondary"] = Variant();
	return set_animation(id, update);
}
bool ECSWorld::remove_animation(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id) || !animations.erase(uint32_t(id))) {
		return false;
	}
	remove_row(pools["animation"], uint32_t(id));
	return true;
}
Vector3 ECSWorld::get_root_motion(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Vector3());
	const AnimationState *state = is_alive(id) ? animations.getptr(uint32_t(id)) : nullptr;
	return state ? state->root_motion : Vector3();
}
namespace {
Vector3 sample_root_motion(const Ref<Animation> &clip, Animation::LoopMode loop, int track, double time) {
	double length = clip->get_length();
	double sample = CLAMP(time, 0.0, length);
	double cycles = 0;
	if (loop == Animation::LOOP_LINEAR) {
		cycles = Math::floor(time / length);
		sample = Math::fposmod(time, length);
	} else if (loop == Animation::LOOP_PINGPONG) {
		sample = Math::fposmod(time, length * 2);
		if (sample > length) {
			sample = length * 2 - sample;
		}
	}
	Vector3 value;
	if (clip->try_position_track_interpolate(track, sample, &value) != OK) {
		return Vector3();
	}
	if (cycles != 0) {
		Vector3 start, end;
		if (clip->try_position_track_interpolate(track, 0, &start) != OK || clip->try_position_track_interpolate(track, length, &end) != OK) {
			return Vector3();
		}
		value += (end - start) * cycles;
	}
	return value;
}
} //namespace
void ECSWorld::step_animations(double delta) {
	Vector<uint64_t> finished;
	Array events;
	for (auto &entry : animations) {
		if (!slots[entry.key].active) { continue; }
		AnimationState &state = entry.value;
		state.root_motion = Vector3();
		if (!state.playing || state.targets.size() != state.clip->get_track_count()) {
			continue;
		}
		double next = state.time + delta * state.speed;
		if (!Math::is_finite(next)) {
			state.playing = false;
			continue;
		}
		double length = state.clip->get_length();
		if (!Math::is_finite(length) || length <= 0) {
			state.playing = false;
			continue;
		}
		if (state.root_motion_track >= 0 && state.clip->track_is_enabled(state.root_motion_track)) {
			int track = state.root_motion_track;
			state.root_motion = sample_root_motion(state.root_clip, state.clip->get_loop_mode(), track, next) - sample_root_motion(state.root_clip, state.clip->get_loop_mode(), track, state.time);
			if (state.root_secondary_clip.is_valid() && state.secondary_clip->track_is_enabled(track)) {
				double secondary_next = state.secondary_time + delta * state.secondary_speed;
				if (Math::is_finite(secondary_next)) {
					Vector3 movement = sample_root_motion(state.root_secondary_clip, state.secondary_clip->get_loop_mode(), track, secondary_next) - sample_root_motion(state.root_secondary_clip, state.secondary_clip->get_loop_mode(), track, state.secondary_time);
					double weight = state.secondary_weight * (state.secondary_weights.is_empty() ? 1.0 : double(state.secondary_weights[track]));
					state.root_motion = state.root_motion.lerp(movement, weight);
				}
			}
			if (!state.root_motion.is_finite()) {
				state.root_motion = Vector3();
				state.playing = false;
				continue;
			}
		}
		for (const Variant &raw:state.effect_events) {
			Dictionary event=raw; double at=event["time"]; bool crossed=false;
			if (state.clip->get_loop_mode()==Animation::LOOP_NONE) {
				crossed=next>state.time ? state.time<at && next>=at : next<state.time && next<=at && state.time>at;
			} else {
				double period=state.clip->get_loop_mode()==Animation::LOOP_PINGPONG?length*2:length;
				crossed=Math::floor((next-at)/period)!=Math::floor((state.time-at)/period);
				if(state.clip->get_loop_mode()==Animation::LOOP_PINGPONG && at>0 && at<length) { crossed|=Math::floor((next-(period-at))/period)!=Math::floor((state.time-(period-at))/period); }
			}
			if(crossed) { trigger_effect((uint64_t(slots[entry.key].generation)<<32)|entry.key,event["event"]); }
		}
		if(!state.graph.is_empty()) { ECSAnimationGraph::advance(state.graph,delta); }
		for (const Variant &raw : (state.has_event_tracks?(state.graph.is_empty()?sample_animation_events(state.clip,state.time,next,state.event_start_pending):ECSAnimationGraph::events(state.graph,state.time,next,state.event_start_pending)):Array())) {
			Dictionary event=raw; int track=event["track"]; event.erase("track"); event["entity"]=int64_t(state.targets[track]); events.push_back(event);
		}
		if(next!=state.time) { state.event_start_pending=false; }
		bool done = false;
		if (state.clip->get_loop_mode() == Animation::LOOP_NONE) {
			done = state.speed > 0 ? next >= length : state.speed < 0 && next <= 0;
			state.time = CLAMP(next, 0.0, length);
		} else {
			state.time = !state.graph.is_empty()?next:Math::fposmod(next, state.clip->get_loop_mode() == Animation::LOOP_PINGPONG ? length * 2 : length);
		}
		double time = state.clip->get_loop_mode() == Animation::LOOP_PINGPONG && state.time > length ? 2 * length - state.time : state.time;
		double secondary_sample_time = 0;
		if (state.secondary_clip.is_valid()) {
			double length = state.secondary_clip->get_length();
			double next = state.secondary_time + delta * state.secondary_speed;
			if (!Math::is_finite(length) || length <= 0 || !Math::is_finite(next)) {
				state.playing = false;
				continue;
			}
			auto loop = state.secondary_clip->get_loop_mode();
			state.secondary_time = loop == Animation::LOOP_NONE ? CLAMP(next, 0.0, length) : Math::fposmod(next, loop == Animation::LOOP_PINGPONG ? length * 2 : length);
			secondary_sample_time = loop == Animation::LOOP_PINGPONG && state.secondary_time > length ? 2 * length - state.secondary_time : state.secondary_time;
		}
		state.blend_elapsed = MIN(state.blend_duration, state.blend_elapsed + delta);
		double weight = state.blend_duration > 0 ? state.blend_elapsed / state.blend_duration : 1.0;
		auto blend = [&](int track, const Variant &value, bool rotation = false) -> Variant {
			if (weight >= 1 || track >= state.blend_from.size()) {
				return value;
			}
			const Variant &from = state.blend_from[track];
			if (rotation && from.get_type() == Variant::VECTOR3 && value.get_type() == Variant::VECTOR3) {
				return Quaternion::from_euler(Vector3(from)).slerp(Quaternion::from_euler(Vector3(value)), weight).get_euler();
			}
			if (from.get_type() == value.get_type()) {
				switch (value.get_type()) {
					case Variant::VECTOR3:
						return Vector3(from).lerp(Vector3(value), weight);
					case Variant::VECTOR2:
						return Vector2(from).lerp(Vector2(value), weight);
					case Variant::VECTOR4:
						return Vector4(from).lerp(Vector4(value), weight);
					case Variant::RECT2: {
						Rect2 a = from, b = value;
						return Rect2(a.position.lerp(b.position, weight), a.size.lerp(b.size, weight));
					}
					case Variant::COLOR:
						return Color(from).lerp(Color(value), weight);
					case Variant::FLOAT:
						return Math::lerp(double(from), double(value), weight);
					case Variant::INT:
						return int64_t(Math::lerp(double(from), double(value), weight));
					default:
						break;
				}
			}
			return from.get_type() == value.get_type() ? from : value;
		};
		auto secondary = [&](int track, const Variant &primary, bool rotation = false) -> Variant {
			if (!state.graph.is_empty()) { return ECSAnimationGraph::sample(state.graph,track,state.time); }
			Ref<Animation> clip = state.secondary_clip;
			if (clip.is_null() || state.secondary_weight <= 0 || (!state.secondary_weights.is_empty() && state.secondary_weights[track] <= 0) || track >= clip->get_track_count() || !clip->track_is_enabled(track) || clip->track_get_key_count(track) == 0 || clip->track_get_type(track) != state.clip->track_get_type(track) || clip->track_get_path(track) != state.clip->track_get_path(track)) {
				return primary;
			}
			Variant value;
			auto type = clip->track_get_type(track);
			if (type == Animation::TYPE_POSITION_3D) {
				Vector3 v;
				if (clip->try_position_track_interpolate(track, secondary_sample_time, &v) != OK) {
					return primary;
				}
				value = v;
			} else if (type == Animation::TYPE_SCALE_3D) {
				Vector3 v;
				if (clip->try_scale_track_interpolate(track, secondary_sample_time, &v) != OK) {
					return primary;
				}
				value = v;
			} else if (type == Animation::TYPE_ROTATION_3D) {
				Quaternion v;
				if (clip->try_rotation_track_interpolate(track, secondary_sample_time, &v) != OK) {
					return primary;
				}
				value = v.get_euler();
			} else {
				value = clip->value_track_interpolate(track, secondary_sample_time);
			}
			double w = state.secondary_weight * (state.secondary_weights.is_empty() ? 1.0 : double(state.secondary_weights[track]));
			if (state.secondary_additive) {
				if (track >= state.secondary_reference.size() || primary.get_type() != Variant::VECTOR3 || value.get_type() != Variant::VECTOR3) {
					return primary;
				}
				const Variant &reference = state.secondary_reference[track];
				if (rotation && reference.get_type() == Variant::QUATERNION) {
					Quaternion offset = Quaternion(reference).inverse() * Quaternion::from_euler(Vector3(value));
					return (Quaternion::from_euler(Vector3(primary)) * Quaternion().slerp(offset.normalized(), w)).normalized().get_euler();
				}
				if (reference.get_type() == Variant::VECTOR3) {
					return Vector3(primary) + (Vector3(value) - Vector3(reference)) * w;
				}
				return primary;
			}
			if (primary.get_type() != value.get_type()) {
				return w < .5 ? primary : value;
			}
			if (rotation && value.get_type() == Variant::VECTOR3) {
				return Quaternion::from_euler(Vector3(primary)).slerp(Quaternion::from_euler(Vector3(value)), w).get_euler();
			}
			switch (value.get_type()) {
				case Variant::VECTOR3:
					return Vector3(primary).lerp(Vector3(value), w);
				case Variant::VECTOR2:
					return Vector2(primary).lerp(Vector2(value), w);
				case Variant::VECTOR4:
					return Vector4(primary).lerp(Vector4(value), w);
				case Variant::COLOR:
					return Color(primary).lerp(Color(value), w);
				case Variant::FLOAT:
					return Math::lerp(double(primary), double(value), w);
				case Variant::INT:
					return int64_t(Math::lerp(double(primary), double(value), w));
				case Variant::RECT2: {
					Rect2 a = primary, b = value;
					return Rect2(a.position.lerp(b.position, w), a.size.lerp(b.size, w));
				}
				default:
					return w < .5 ? primary : value;
			}
		};
		for (int i = 0; i < state.targets.size(); i++) {
			if (i == state.root_motion_track || !state.clip->track_is_enabled(i) || !state.clip->track_get_key_count(i)) {
				continue;
			}
			uint64_t target = state.targets[i];
			if (!is_active_in_hierarchy(target)) { continue; }
			auto type = state.clip->track_get_type(i);
			if (((physics_bodies.has(uint32_t(target)) && physics_bodies[uint32_t(target)].mode == 2) || (physics_bodies_2d.has(uint32_t(target)) && physics_bodies_2d[uint32_t(target)].mode == 2)) && type != Animation::TYPE_VALUE) {
				continue;
			}
			if (type == Animation::TYPE_POSITION_3D) {
				Vector3 value;
				if (state.clip->try_position_track_interpolate(i, time, &value) == OK) {
					set_vector(target, "position", blend(i, secondary(i, value)));
				}
			} else if (type == Animation::TYPE_ROTATION_3D) {
				Quaternion value;
				if (state.clip->try_rotation_track_interpolate(i, time, &value) == OK) {
					set_vector(target, "rotation", blend(i, secondary(i, value.get_euler(), true), true));
				}
			} else if (type == Animation::TYPE_SCALE_3D) {
				Vector3 value;
				if (state.clip->try_scale_track_interpolate(i, time, &value) == OK) {
					set_vector(target, "scale", blend(i, secondary(i, value)));
				}
			} else if (type == Animation::TYPE_VALUE) {
				String property = ecs_animation_subnames(state.clip->track_get_path(i));
				if(property.get_slice(":",0)=="event") { continue; }
				Variant value = state.clip->value_track_interpolate(i, time);
				if(property=="polygon:frame") {
					Dictionary frame=value,existing=get_polygon_2d(target); bool changed=false; for(const Variant &key:frame.keys()) { if(existing.get(key,Variant())!=frame[key]) { changed=true; break; } } if(changed) { set_polygon_2d(target,frame); }
				} else if (property.begins_with("slot:")) {
					set_skeleton_slot_value(target,property,(property.ends_with(":color") || property.ends_with(":dark"))?blend(i,secondary(i,value)):value);
				} else if (property.begins_with("ui:")) {
					Dictionary update;
					update[property.substr(3)] = blend(i, secondary(i, value));
					set_ui(target, update);
				} else if (value.get_type() == Variant::VECTOR3 && (!physics_bodies.has(uint32_t(target)) || physics_bodies[uint32_t(target)].mode != 2) && (!physics_bodies_2d.has(uint32_t(target)) || physics_bodies_2d[uint32_t(target)].mode != 2)) {
					set_vector(target, property, blend(i, secondary(i, value, property == "rotation"), property == "rotation"));
				}
			}
		}
		if (weight >= 1) {
			state.blend_from.clear();
		}
		if (done && weight >= 1) {
			state.playing = false;
			finished.push_back((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		}
	}
	// Dispatch outside the animation map traversal: handlers may delete entities or change clips.
	for (const Variant &raw : events) {
		Dictionary event=raw; uint64_t target=int64_t(event["entity"]); event.erase("entity"); String name=event["name"]; event.erase("name");
		if(is_active_in_hierarchy(target)) { emit_signal("animation_event",target,name,event); }
	}
	for (uint64_t id : finished) {
		if (is_alive(id) && animations.has(uint32_t(id)) && !animations[uint32_t(id)].playing) {
			emit_signal("animation_finished", id);
		}
	}
}
bool ECSWorld::set_skeleton(uint64_t id, const Ref<Skin> &skin, const PackedInt64Array &bones) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id) || skin.is_null() || bones.is_empty() || bones.size() != skin->get_bind_count()) {
		return false;
	}
	for (int i = 0; i < bones.size(); i++) {
		if (!is_alive(bones[i]) || !skin->get_bind_pose(i).is_finite()) {
			return false;
		}
	}
	RID rid = RenderingServer::get_singleton()->skeleton_create();
	RenderingServer::get_singleton()->skeleton_allocate_data(rid, bones.size());
	remove_skeleton(id);
	skeletons[uint32_t(id)] = { skin, bones, rid };
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "skeleton", &marker);
	return true;
}
Dictionary ECSWorld::get_skeleton(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	Dictionary result;
	const SkeletonState *state = is_alive(id) ? skeletons.getptr(uint32_t(id)) : nullptr;
	if (state) {
		result["skin"] = state->skin;
		result["bones"] = state->bones;
	}
	return result;
}
bool ECSWorld::remove_skeleton(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	SkeletonState *state = is_alive(id) ? skeletons.getptr(uint32_t(id)) : nullptr;
	if (!state) {
		return false;
	}
	if (auto *server = RenderingServer::get_singleton()) {
		server->free_rid(state->rid);
	}
	skeletons.erase(uint32_t(id));
	remove_row(pools["skeleton"], uint32_t(id));
	return true;
}
void ECSWorld::remove_animation_references(uint64_t id) {
	Vector<uint64_t> removed;
	for (const auto &entry : animations) {
		if (entry.key == uint32_t(id) || entry.value.targets.has(id)) {
			removed.push_back((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		}
	}
	for (uint64_t owner : removed) {
		remove_animation(owner);
	}
	removed.clear();
	for (const auto &entry : skeletons) {
		if (entry.key == uint32_t(id) || entry.value.bones.has(id)) {
			removed.push_back((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		}
	}
	for (uint64_t owner : removed) {
		remove_skeleton(owner);
	}
}
void ECSWorld::clear_skeletons() {
	if (auto *server = RenderingServer::get_singleton()) {
		for (const auto &entry : skeletons) {
			server->free_rid(entry.value.rid);
		}
	}
	skeletons.clear();
	if (Pool *pool = pools.getptr("skeleton")) {
		pool->bytes.clear();
		pool->entities.clear();
		pool->rows.clear();
	}
}
