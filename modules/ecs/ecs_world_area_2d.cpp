// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "scene/resources/2d/shape_2d.h"
#include "servers/physics_2d/physics_server_2d.h"

bool ECSWorld::set_area_2d(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Dictionary data = get_area_2d(id);
	data.merge(definition, true);
	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "shape" && field != "layer" && field != "mask" && field != "enabled" && field != "gravity" && field != "linear_damp" && field != "angular_damp") {
			return false;
		}
	}
	Variant shape_value = data.get("shape", Variant());
	if (shape_value.get_type() != Variant::OBJECT) {
		return false;
	}
	Ref<Shape2D> shape = shape_value;
	if (shape.is_null()) {
		return false;
	}
	for (const String &key : { String("layer"), String("mask") }) {
		Variant value = data.get(key, 1);
		if (value.get_type() != Variant::INT || int64_t(value) < 0 || uint64_t(int64_t(value)) > UINT32_MAX) {
			return false;
		}
		data[key] = value;
	}
	Variant enabled = data.get("enabled", true);
	if (enabled.get_type() != Variant::BOOL) {
		return false;
	}
	data["enabled"] = enabled;
	Variant gravity_value = data.get("gravity", Dictionary());
	if (gravity_value.get_type() != Variant::DICTIONARY) {
		return false;
	}
	Dictionary gravity = Dictionary(gravity_value).duplicate(true);
	for (const Variant &key : gravity.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "mode" && field != "strength" && field != "vector" && field != "point" && field != "unit_distance" && field != "priority") {
			return false;
		}
	}
	Variant mode = gravity.get("mode", "disabled");
	if (mode.get_type() != Variant::STRING) {
		return false;
	}
	const char *modes[] = { "disabled", "combine", "combine_replace", "replace", "replace_combine" };
	int override_mode = -1;
	for (int i = 0; i < 5; i++) {
		if (String(mode) == modes[i]) {
			override_mode = i;
		}
	}
	if (override_mode < 0) {
		return false;
	}
	gravity["mode"] = mode;
	for (const String &field : { String("strength"), String("unit_distance"), String("priority") }) {
		Variant value = gravity.get(field, field == "strength" ? 980.0 : 0.0);
		if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value)) || (field != "priority" && double(value) < 0)) {
			return false;
		}
		gravity[field] = double(value);
	}
	Variant vector = gravity.get("vector", Vector2(0, 1));
	Variant point = gravity.get("point", false);
	if (vector.get_type() != Variant::VECTOR2 || !Vector2(vector).is_finite() || point.get_type() != Variant::BOOL) {
		return false;
	}
	gravity["vector"] = vector;
	gravity["point"] = point;
	data["gravity"] = gravity;
	int damping_modes[2] = {};
	const char *damping_keys[] = { "linear_damp", "angular_damp" };
	for (int i = 0; i < 2; i++) {
		Variant raw = data.get(damping_keys[i], Dictionary());
		if (raw.get_type() != Variant::DICTIONARY) {
			return false;
		}
		Dictionary damping = Dictionary(raw).duplicate(true);
		for (const Variant &key : damping.keys()) {
			if ((key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) || (String(key) != "mode" && String(key) != "value")) {
				return false;
			}
		}
		Variant damping_mode = damping.get("mode", "disabled");
		Variant value = damping.get("value", 0.0);
		if (damping_mode.get_type() != Variant::STRING || (value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value)) || double(value) < 0) {
			return false;
		}
		int selected = -1;
		for (int j = 0; j < 5; j++) {
			if (String(damping_mode) == modes[j]) {
				selected = j;
			}
		}
		if (selected < 0) {
			return false;
		}
		damping_modes[i] = selected;
		damping["mode"] = damping_mode;
		damping["value"] = double(value);
		data[damping_keys[i]] = damping;
	}

	Transform2D transform = calculate_transform_2d(id);
	if (!transform.is_finite() || Math::is_zero_approx(transform.determinant())) {
		return false;
	}
	auto *server = PhysicsServer2D::get_singleton();
	if (!server) {
		return false;
	}
	if (!physics_space_2d.is_valid()) {
		physics_space_2d = server->space_create();
		server->space_set_active(physics_space_2d, true);
		server->area_set_param(physics_space_2d, PhysicsServer2D::AREA_PARAM_GRAVITY, GLOBAL_GET("physics/2d/default_gravity"));
		server->area_set_param(physics_space_2d, PhysicsServer2D::AREA_PARAM_GRAVITY_VECTOR, GLOBAL_GET("physics/2d/default_gravity_vector"));
	}
	AreaState2D area;
	area.rid = server->area_create();
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_LINEAR_DAMP_OVERRIDE_MODE, damping_modes[0]);
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_LINEAR_DAMP, Dictionary(data["linear_damp"])["value"]);
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_ANGULAR_DAMP_OVERRIDE_MODE, damping_modes[1]);
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_ANGULAR_DAMP, Dictionary(data["angular_damp"])["value"]);
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_GRAVITY_OVERRIDE_MODE, override_mode);
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_GRAVITY, gravity["strength"]);
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_GRAVITY_VECTOR, vector);
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_GRAVITY_IS_POINT, point);
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_GRAVITY_POINT_UNIT_DISTANCE, gravity["unit_distance"]);
	server->area_set_param(area.rid, PhysicsServer2D::AREA_PARAM_PRIORITY, gravity["priority"]);
	area.definition = data.duplicate(true);
	area.submitted_transform = transform;
	server->area_add_shape(area.rid, shape->get_rid(), Transform2D(), !bool(enabled));
	server->area_set_transform(area.rid, transform);
	server->area_set_collision_layer(area.rid, uint32_t(int64_t(data["layer"])));
	server->area_set_collision_mask(area.rid, uint32_t(int64_t(data["mask"])));
	// Godot Physics otherwise keeps the Area in the static broadphase tree,
	// which prevents detecting static bodies even with a monitoring callback.
	server->area_set_monitorable(area.rid, true);
	server->area_set_monitor_callback(area.rid, callable_mp(this, &ECSWorld::area_body_changed_2d).bind(id, area.rid));
	server->area_set_space(area.rid, physics_space_2d);
	PackedInt64Array previous = get_area_overlaps_2d(id);
	remove_area_2d(id);
	areas_2d[uint32_t(id)] = area;
	{
		MutexLock lock(area_event_mutex_2d);
		for (int64_t other : previous) {
			area_events_2d.push_back({ id, area.rid, RID(), false, uint64_t(other) });
		}
	}
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "area_2d", &marker);
	if (!slots[uint32_t(id)].active) { apply_activation(uint32_t(id)); }
	return true;
}
Dictionary ECSWorld::get_area_2d(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const AreaState2D *area = is_alive(id) ? areas_2d.getptr(uint32_t(id)) : nullptr;
	return area ? area->definition.duplicate(true) : Dictionary();
}
bool ECSWorld::remove_area_2d(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	AreaState2D *area = is_alive(id) ? areas_2d.getptr(uint32_t(id)) : nullptr;
	if (!area) {
		return false;
	}
	PhysicsServer2D::get_singleton()->free_rid(area->rid);
	areas_2d.erase(uint32_t(id));
	remove_row(pools["area_2d"], uint32_t(id));
	return true;
}
PackedInt64Array ECSWorld::get_area_overlaps_2d(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	PackedInt64Array result;
	const AreaState2D *area = is_alive(id) ? areas_2d.getptr(uint32_t(id)) : nullptr;
	if (area) {
		for (const auto &entry : area->overlaps) {
			if (is_alive(entry.key)) {
				result.push_back(entry.key);
			}
		}
	}
	result.sort();
	return result;
}
void ECSWorld::area_body_changed_2d(int status, const RID &body, ObjectID instance, int body_shape, int area_shape, uint64_t owner, RID area) {
	// Physics callbacks only enqueue native data, never invoke managed gameplay.
	MutexLock lock(area_event_mutex_2d);
	area_events_2d.push_back({ owner, area, body, status == PhysicsServer2D::AREA_BODY_ADDED });
}
void ECSWorld::sync_areas_2d() {
	Vector<AreaEvent2D> pending;
	{
		MutexLock lock(area_event_mutex_2d);
		pending = area_events_2d;
		area_events_2d.clear();
	}
	struct Notification {
		uint64_t owner, other;
		RID area;
		bool entered;
	};
	Vector<Notification> notifications;
	for (const AreaEvent2D &event : pending) {
		AreaState2D *area = is_alive(event.owner) ? areas_2d.getptr(uint32_t(event.owner)) : nullptr;
		if (event.removed_entity) {
			if (area && area->rid == event.area) {
				notifications.push_back({ event.owner, event.removed_entity, event.area, false });
			}
			continue;
		}
		const uint64_t *other = physics_entities_2d.getptr(event.body);
		if (!area || area->rid != event.area || !other || !is_alive(*other) || *other == event.owner) {
			continue;
		}
		int *count = area->overlaps.getptr(*other);
		if (event.entered && (!is_active_in_hierarchy(event.owner) || !is_active_in_hierarchy(*other))) { continue; }
		if (event.entered) {
			if (count) {
				++*count;
			} else {
				area->overlaps[*other] = 1;
				notifications.push_back({ event.owner, *other, event.area, true });
			}
		} else if (count && --*count == 0) {
			area->overlaps.erase(*other);
			notifications.push_back({ event.owner, *other, event.area, false });
		}
	}
	for (auto &entry : areas_2d) {
		Vector<uint64_t> removed;
		for (const auto &overlap : entry.value.overlaps) {
			if (!slots[entry.key].active || !is_active_in_hierarchy(overlap.key) || !physics_bodies_2d.has(uint32_t(overlap.key))) {
				removed.push_back(overlap.key);
			}
		}
		uint64_t owner = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
		for (uint64_t other : removed) {
			entry.value.overlaps.erase(other);
			notifications.push_back({ owner, other, entry.value.rid, false });
		}
	}
	for (const auto &event : notifications) {
		const AreaState2D *area = is_alive(event.owner) ? areas_2d.getptr(uint32_t(event.owner)) : nullptr;
		if (area && area->rid == event.area && (!event.entered || (is_active_in_hierarchy(event.owner) && is_active_in_hierarchy(event.other)))) {
			emit_signal(event.entered ? "area_2d_body_entered" : "area_2d_body_exited", event.owner, event.other);
		}
	}
}
void ECSWorld::submit_areas_2d() {
	auto *server = PhysicsServer2D::get_singleton();
	for (auto &entry : areas_2d) {
		uint64_t id = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
		Transform2D transform = calculate_transform_2d(id);
		if (transform.is_finite() && !Math::is_zero_approx(transform.determinant()) && !transform.is_equal_approx(entry.value.submitted_transform)) {
			server->area_set_transform(entry.value.rid, transform);
			entry.value.submitted_transform = transform;
		}
	}
}
void ECSWorld::clear_areas_2d() {
	if (auto *server = PhysicsServer2D::get_singleton()) {
		for (const auto &entry : areas_2d) {
			server->free_rid(entry.value.rid);
		}
	}
	areas_2d.clear();
	if (Pool *pool = pools.getptr("area_2d")) {
		pool->bytes.clear();
		pool->entities.clear();
		pool->rows.clear();
	}
	MutexLock lock(area_event_mutex_2d);
	area_events_2d.clear();
}
