// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "servers/rendering/rendering_server.h"
namespace {
bool number(const Variant &v, double minimum, double maximum) {
	return (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) && Math::is_finite(double(v)) && double(v) >= minimum && double(v) <= maximum;
}
} //namespace
bool ECSWorld::set_camera(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Dictionary data = get_camera(id);
	data.merge(definition, true);
	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "projection" && field != "fov" && field != "size" && field != "near" && field != "far" && field != "priority" && field != "enabled") {
			return false;
		}
	}
	Variant projection = data.get("projection", "perspective"), priority = data.get("priority", 0), enabled = data.get("enabled", true);
	if (projection.get_type() != Variant::STRING || (String(projection) != "perspective" && String(projection) != "orthogonal") || priority.get_type() != Variant::INT || enabled.get_type() != Variant::BOOL) {
		return false;
	}
	Variant fov = data.get("fov", 60.0), size = data.get("size", 10.0), near_plane = data.get("near", 0.1), far_plane = data.get("far", 2000.0);
	if (!number(fov, 1, 179) || !number(size, .001, 1e9) || !number(near_plane, .001, 1e9) || !number(far_plane, .002, 1e12) || double(far_plane) <= double(near_plane)) {
		return false;
	}
	data["projection"] = projection;
	data["priority"] = priority;
	data["enabled"] = enabled;
	data["fov"] = fov;
	data["size"] = size;
	data["near"] = near_plane;
	data["far"] = far_plane;
	cameras[uint32_t(id)] = data.duplicate(true);
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "camera", &marker);
	return true;
}
Dictionary ECSWorld::get_camera(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const Dictionary *data = is_alive(id) ? cameras.getptr(uint32_t(id)) : nullptr;
	return data ? data->duplicate(true) : Dictionary();
}
bool ECSWorld::remove_camera(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id) || !cameras.erase(uint32_t(id))) {
		return false;
	}
	remove_row(pools["camera"], uint32_t(id));
	return true;
}
Dictionary ECSWorld::get_active_camera() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	uint64_t selected = 0;
	int64_t priority = INT64_MIN;
	Transform3D transform;
	for (const auto &entry : cameras) {
		if (!slots[entry.key].active || !bool(entry.value["enabled"])) {
			continue;
		}
		uint64_t id = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
		int64_t rank = entry.value["priority"];
		if (selected && (rank < priority || (rank == priority && id > selected))) {
			continue;
		}
		Transform3D candidate = calculate_global_transform(id);
		if (!candidate.is_finite() || Math::is_zero_approx(candidate.basis.determinant())) {
			continue;
		}
		selected = id;
		priority = rank;
		transform = candidate.orthonormalized();
	}
	Dictionary result = get_camera(selected);
	if (selected) {
		result["entity"] = selected;
		result["transform"] = transform;
	}
	return result;
}
bool ECSWorld::set_light(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Dictionary data = get_light(id);
	data.merge(definition, true);
	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "type" && field != "color" && field != "energy" && field != "range" && field != "angle" && field != "shadow" && field != "enabled") {
			return false;
		}
	}
	Variant type = data.get("type", "omni"), color = data.get("color", Color(1, 1, 1)), energy = data.get("energy", 1.0), range = data.get("range", 5.0), angle = data.get("angle", 45.0), shadow = data.get("shadow", false), enabled = data.get("enabled", true);
	if (type.get_type() != Variant::STRING || (String(type) != "omni" && String(type) != "directional" && String(type) != "spot") || color.get_type() != Variant::COLOR || !number(energy, 0, 1e6) || !number(range, .001, 1e9) || !number(angle, .1, 89.9) || shadow.get_type() != Variant::BOOL || enabled.get_type() != Variant::BOOL) {
		return false;
	}
	Color light_color = color;
	for (int i = 0; i < 4; i++) {
		if (!Math::is_finite(light_color[i])) {
			return false;
		}
	}
	auto *server = RenderingServer::get_singleton();
	LightState light;
	light.light = String(type) == "directional" ? server->directional_light_create() : String(type) == "spot" ? server->spot_light_create()
																											  : server->omni_light_create();
	server->light_set_color(light.light, light_color);
	server->light_set_param(light.light, RSE::LIGHT_PARAM_ENERGY, energy);
	server->light_set_param(light.light, RSE::LIGHT_PARAM_RANGE, range);
	server->light_set_param(light.light, RSE::LIGHT_PARAM_SPOT_ANGLE, angle);
	server->light_set_shadow(light.light, shadow);
	light.instance = server->instance_create();
	server->instance_set_base(light.instance, light.light);
	server->instance_set_visible(light.instance, enabled);
	data["type"] = type;
	data["color"] = color;
	data["energy"] = energy;
	data["range"] = range;
	data["angle"] = angle;
	data["shadow"] = shadow;
	data["enabled"] = enabled;
	light.definition = data.duplicate(true);
	remove_light(id);
	lights[uint32_t(id)] = light;
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "light", &marker);
	if (!slots[uint32_t(id)].active) { apply_activation(uint32_t(id)); }
	return true;
}
Dictionary ECSWorld::get_light(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const LightState *state = is_alive(id) ? lights.getptr(uint32_t(id)) : nullptr;
	return state ? state->definition.duplicate(true) : Dictionary();
}
bool ECSWorld::remove_light(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	LightState *state = is_alive(id) ? lights.getptr(uint32_t(id)) : nullptr;
	if (!state) {
		return false;
	}
	if (auto *server = RenderingServer::get_singleton()) {
		server->free_rid(state->instance);
		server->free_rid(state->light);
	}
	lights.erase(uint32_t(id));
	remove_row(pools["light"], uint32_t(id));
	return true;
}
void ECSWorld::sync_lights(RID scenario) {
	auto *server = RenderingServer::get_singleton();
	for (auto &entry : lights) {
		LightState &light = entry.value;
		Transform3D transform = calculate_global_transform((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		if (!transform.is_finite() || Math::is_zero_approx(transform.basis.determinant())) {
			server->instance_set_visible(light.instance, false);
			continue;
		}
		server->instance_set_visible(light.instance, slots[entry.key].active && bool(light.definition["enabled"]));
		if (light.scenario != scenario) {
			server->instance_set_scenario(light.instance, scenario);
			light.scenario = scenario;
		}
		if (!light.transform.is_equal_approx(transform)) {
			server->instance_set_transform(light.instance, transform);
			light.transform = transform;
		}
	}
}
void ECSWorld::clear_lights() {
	if (auto *server = RenderingServer::get_singleton()) {
		for (const auto &entry : lights) {
			server->free_rid(entry.value.instance);
			server->free_rid(entry.value.light);
		}
	}
	lights.clear();
	if (Pool *pool = pools.getptr("light")) {
		pool->entities.clear();
		pool->rows.clear();
		pool->bytes.clear();
	}
}
