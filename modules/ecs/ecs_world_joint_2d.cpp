// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "servers/physics_2d/physics_server_2d.h"

bool ECSWorld::set_joint_2d(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Dictionary data = get_joint_2d(id);
	data.merge(definition, true);
	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "body_a" && field != "body_b" && field != "anchor_a" && field != "anchor_b" && field != "exclude_collision" && field != "type" && field != "groove_a" && field != "groove_b" && field != "rest_length" && field != "stiffness" && field != "damping" && field != "angular_limit_enabled" && field != "angular_lower" && field != "angular_upper" && field != "motor_enabled" && field != "motor_speed" && field != "softness") {
			return false;
		}
	}
	Variant a = data.get("body_a", Variant()), b = data.get("body_b", Variant());
	if (a.get_type() != Variant::INT || b.get_type() != Variant::INT || !is_alive(uint64_t(int64_t(a))) || !is_alive(uint64_t(int64_t(b))) || int64_t(a) == int64_t(b)) {
		return false;
	}
	const PhysicsBody2D *body_a = physics_bodies_2d.getptr(uint32_t(int64_t(a))), *body_b = physics_bodies_2d.getptr(uint32_t(int64_t(b)));
	if (!body_a || !body_b || (body_a->mode != 2 && body_b->mode != 2)) {
		return false;
	}
	for (const auto &entry : joints_2d) {
		if (entry.key != uint32_t(id) && ((entry.value.body_a == uint64_t(int64_t(a)) && entry.value.body_b == uint64_t(int64_t(b))) || (entry.value.body_a == uint64_t(int64_t(b)) && entry.value.body_b == uint64_t(int64_t(a))))) {
			return false;
		}
	}
	for (const String &key : { String("anchor_a"), String("anchor_b") }) {
		Variant value = data.get(key, Vector2());
		if (value.get_type() != Variant::VECTOR2 || !Vector2(value).is_finite()) {
			return false;
		}
		data[key] = value;
	}
	Variant kind = data.get("type", "pin");
	if (kind.get_type() != Variant::STRING) {
		return false;
	}
	String type = kind;
	if (type != "pin" && type != "groove" && type != "spring") {
		return false;
	}
	data["type"] = type;
	for (const String &key : { String("groove_a"), String("groove_b") }) {
		Variant value = data.get(key, key == "groove_a" ? Vector2(0, -25) : Vector2(0, 25));
		if (value.get_type() != Variant::VECTOR2 || !Vector2(value).is_finite()) {
			return false;
		}
		data[key] = value;
	}
	if (type == "groove" && Vector2(data["groove_a"]).is_equal_approx(Vector2(data["groove_b"]))) {
		return false;
	}
	for (const String &key : { String("rest_length"), String("stiffness"), String("damping") }) {
		Variant value = data.get(key, key == "stiffness" ? 20.0 : key == "damping" ? 1.0
																				   : 50.0);
		if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value)) || double(value) < 0) {
			return false;
		}
		data[key] = double(value);
	}
	for (const String &key : { String("angular_limit_enabled"), String("motor_enabled") }) {
		Variant value = data.get(key, false);
		if (value.get_type() != Variant::BOOL) {
			return false;
		}
		data[key] = value;
	}
	for (const String &key : { String("angular_lower"), String("angular_upper"), String("motor_speed"), String("softness") }) {
		Variant value = data.get(key, 0.0);
		if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value)) || (key == "softness" && double(value) < 0) || ((key == "angular_lower" || key == "angular_upper") && Math::abs(double(value)) > Math::PI)) {
			return false;
		}
		data[key] = double(value);
	}
	if (double(data["angular_lower"]) > double(data["angular_upper"])) {
		return false;
	}
	Transform2D transform_a = calculate_transform_2d(uint64_t(int64_t(a)));
	Transform2D transform_b = calculate_transform_2d(uint64_t(int64_t(b)));
	if (!transform_a.is_finite() || !transform_b.is_finite() || Math::is_zero_approx(transform_a.determinant()) || Math::is_zero_approx(transform_b.determinant())) {
		return false;
	}
	Variant exclude = data.get("exclude_collision", true);
	if (exclude.get_type() != Variant::BOOL) {
		return false;
	}
	data["exclude_collision"] = exclude;
	auto *server = PhysicsServer2D::get_singleton();
	if (!server) {
		return false;
	}
	// Reconfigure the existing RID: freeing an older joint after enabling collision
	// exclusion on a new one can otherwise remove the new body's exclusion pair.
	PinJointState *previous = joints_2d.getptr(uint32_t(id));
	RID rid = previous ? previous->rid : server->joint_create();
	if (previous) {
		server->joint_disable_collisions_between_bodies(rid, false);
		for (uint64_t body_id : { previous->body_a, previous->body_b }) {
			const PhysicsBody2D *old_body = physics_bodies_2d.getptr(uint32_t(body_id));
			if (old_body && old_body->mode == 2) {
				server->body_set_state(old_body->rid, PhysicsServer2D::BODY_STATE_SLEEPING, false);
			}
		}
	}
	if (type == "pin") {
		server->joint_make_pin(rid, transform_a.xform(Vector2(data["anchor_a"])), body_a->rid, body_b->rid);
		server->pin_joint_set_param(rid, PhysicsServer2D::PIN_JOINT_SOFTNESS, data["softness"]);
		server->pin_joint_set_param(rid, PhysicsServer2D::PIN_JOINT_LIMIT_LOWER, data["angular_lower"]);
		server->pin_joint_set_param(rid, PhysicsServer2D::PIN_JOINT_LIMIT_UPPER, data["angular_upper"]);
		server->pin_joint_set_param(rid, PhysicsServer2D::PIN_JOINT_MOTOR_TARGET_VELOCITY, data["motor_speed"]);
		server->pin_joint_set_flag(rid, PhysicsServer2D::PIN_JOINT_FLAG_ANGULAR_LIMIT_ENABLED, data["angular_limit_enabled"]);
		server->pin_joint_set_flag(rid, PhysicsServer2D::PIN_JOINT_FLAG_MOTOR_ENABLED, data["motor_enabled"]);
	} else if (type == "groove") {
		server->joint_make_groove(rid, transform_a.xform(Vector2(data["groove_a"])), transform_a.xform(Vector2(data["groove_b"])), transform_b.xform(Vector2(data["anchor_b"])), body_a->rid, body_b->rid);
	} else {
		server->joint_make_damped_spring(rid, transform_a.xform(Vector2(data["anchor_a"])), transform_b.xform(Vector2(data["anchor_b"])), body_a->rid, body_b->rid);
		server->damped_spring_joint_set_param(rid, PhysicsServer2D::DAMPED_SPRING_REST_LENGTH, data["rest_length"]);
		server->damped_spring_joint_set_param(rid, PhysicsServer2D::DAMPED_SPRING_STIFFNESS, data["stiffness"]);
		server->damped_spring_joint_set_param(rid, PhysicsServer2D::DAMPED_SPRING_DAMPING, data["damping"]);
	}
	server->joint_disable_collisions_between_bodies(rid, exclude);
	if (body_a->mode == 2) {
		server->body_set_state(body_a->rid, PhysicsServer2D::BODY_STATE_SLEEPING, false);
	}
	if (body_b->mode == 2) {
		server->body_set_state(body_b->rid, PhysicsServer2D::BODY_STATE_SLEEPING, false);
	}
	joints_2d[uint32_t(id)] = { rid, data.duplicate(true), uint64_t(int64_t(a)), uint64_t(int64_t(b)) };
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "joint_2d", &marker);
	sync_joint_activation();
	return true;
}
Dictionary ECSWorld::get_joint_2d(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const PinJointState *joint = is_alive(id) ? joints_2d.getptr(uint32_t(id)) : nullptr;
	return joint ? joint->definition.duplicate(true) : Dictionary();
}
bool ECSWorld::remove_joint_2d(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	PinJointState *joint = is_alive(id) ? joints_2d.getptr(uint32_t(id)) : nullptr;
	if (!joint) {
		return false;
	}
	PhysicsServer2D::get_singleton()->free_rid(joint->rid);
	// Godot Physics does not automatically wake a sleeping body on constraint
	// removal. Without this a released hanging body stays suspended forever.
	for (uint64_t body_id : { joint->body_a, joint->body_b }) {
		const PhysicsBody2D *body = physics_bodies_2d.getptr(uint32_t(body_id));
		if (body && body->mode == 2) {
			PhysicsServer2D::get_singleton()->body_set_state(body->rid, PhysicsServer2D::BODY_STATE_SLEEPING, false);
		}
	}
	joints_2d.erase(uint32_t(id));
	remove_row(pools["joint_2d"], uint32_t(id));
	return true;
}
void ECSWorld::remove_joint_references_2d(uint64_t body) {
	Vector<uint64_t> owners;
	for (const auto &entry : joints_2d) {
		if (entry.value.body_a == body || entry.value.body_b == body) {
			owners.push_back((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		}
	}
	for (uint64_t owner : owners) {
		remove_joint_2d(owner);
	}
}
void ECSWorld::clear_joints_2d() {
	if (auto *server = PhysicsServer2D::get_singleton()) {
		for (const auto &entry : joints_2d) {
			server->free_rid(entry.value.rid);
		}
	}
	joints_2d.clear();
	if (Pool *pool = pools.getptr("joint_2d")) {
		pool->bytes.clear();
		pool->entities.clear();
		pool->rows.clear();
	}
}
