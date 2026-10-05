// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "servers/physics_3d/physics_server_3d.h"

bool ECSWorld::set_joint_3d(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Dictionary data = get_joint_3d(id);
	if (definition.has("type") && definition["type"] != data.get("type", "hinge") && !definition.has("limits")) {
		data.erase("limits");
	}
	data.merge(definition, true);
	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "body_a" && field != "body_b" && field != "frame_a" && field != "frame_b" && field != "type" && field != "limits" && field != "exclude_collision") {
			return false;
		}
	}
	Variant a = data.get("body_a", Variant()), b = data.get("body_b", Variant());
	if (a.get_type() != Variant::INT || b.get_type() != Variant::INT || !is_alive(uint64_t(int64_t(a))) || !is_alive(uint64_t(int64_t(b))) || int64_t(a) == int64_t(b)) {
		return false;
	}
	const PhysicsBody *body_a = physics_bodies.getptr(uint32_t(int64_t(a))), *body_b = physics_bodies.getptr(uint32_t(int64_t(b)));
	if (!body_a || !body_b || (body_a->mode != 2 && body_b->mode != 2)) {
		return false;
	}
	for (const auto &entry : joints_3d) {
		if (entry.key != uint32_t(id) && ((entry.value.body_a == uint64_t(int64_t(a)) && entry.value.body_b == uint64_t(int64_t(b))) || (entry.value.body_a == uint64_t(int64_t(b)) && entry.value.body_b == uint64_t(int64_t(a))))) {
			return false;
		}
	}
	// Legacy pin joints and generic joints share collision exclusion ownership.
	for (const auto &entry : pin_joints) {
		if ((entry.value.body_a == uint64_t(int64_t(a)) && entry.value.body_b == uint64_t(int64_t(b))) || (entry.value.body_b == uint64_t(int64_t(a)) && entry.value.body_a == uint64_t(int64_t(b)))) {
			return false;
		}
	}
	for (const String &key : { String("frame_a"), String("frame_b") }) {
		Variant value = data.get(key, Transform3D());
		if (value.get_type() != Variant::TRANSFORM3D) {
			return false;
		}
		Transform3D frame = value;
		if (!frame.is_finite() || !frame.basis.is_orthonormal() || frame.basis.determinant() < 0) {
			return false;
		}
		data[key] = value;
	}
	Variant kind = data.get("type", "hinge");
	if (kind.get_type() != Variant::STRING) {
		return false;
	}
	String type = kind;
	if (type != "pin" && type != "hinge" && type != "slider" && type != "cone_twist" && type != "6dof") {
		return false;
	}
	data["type"] = type;
	Variant raw_limits = data.get("limits", Dictionary());
	if (raw_limits.get_type() != Variant::DICTIONARY) {
		return false;
	}
	Dictionary limits = Dictionary(raw_limits).duplicate(true);
	auto number = [](Dictionary &values, const String &key, double fallback, double minimum, double maximum) {
		Variant value = values.get(key, fallback);
		if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value)) || double(value) < minimum || double(value) > maximum) {
			return false;
		}
		values[key] = double(value);
		return true;
	};
	auto flag = [](Dictionary &values, const String &key, bool fallback) {
		Variant value = values.get(key, fallback);
		if (value.get_type() != Variant::BOOL) {
			return false;
		}
		values[key] = value;
		return true;
	};
	for (const Variant &key : limits.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		bool accepted = type == "hinge" ? (field == "enabled" || field == "lower" || field == "upper" || field == "motor_enabled" || field == "motor_speed" || field == "motor_impulse") : type == "slider" ? (field == "linear_lower" || field == "linear_upper" || field == "angular_lower" || field == "angular_upper")
				: type == "cone_twist"																																										? (field == "swing_span" || field == "twist_span")
																																																			: type == "6dof" && (field == "x" || field == "y" || field == "z");
		if (!accepted) {
			return false;
		}
	}
	if (type == "hinge") {
		if (!flag(limits, "enabled", true) || !flag(limits, "motor_enabled", false) || !number(limits, "lower", -Math::PI / 4, -Math::PI, Math::PI) || !number(limits, "upper", Math::PI / 4, -Math::PI, Math::PI) || double(limits["lower"]) > double(limits["upper"]) || !number(limits, "motor_speed", 0, -1e6, 1e6) || !number(limits, "motor_impulse", 1, 0, 1e9)) {
			return false;
		}
	} else if (type == "slider") {
		for (const String &axis : { String("linear"), String("angular") }) {
			double range = axis == "linear" ? 1e6 : Math::PI;
			if (!number(limits, axis + "_lower", 0, -range, range) || !number(limits, axis + "_upper", 0, -range, range) || double(limits[axis + "_lower"]) > double(limits[axis + "_upper"])) {
				return false;
			}
		}
	} else if (type == "cone_twist") {
		if (!number(limits, "swing_span", Math::PI / 4, 0, Math::PI) || !number(limits, "twist_span", Math::PI / 4, 0, Math::PI)) {
			return false;
		}
	} else if (type == "6dof") {
		for (const String &axis : { String("x"), String("y"), String("z") }) {
			Variant raw_axis = limits.get(axis, Dictionary());
			if (raw_axis.get_type() != Variant::DICTIONARY) {
				return false;
			}
			Dictionary values = raw_axis;
			for (const Variant &key : values.keys()) {
				if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
					return false;
				}
				String field = key;
				if (field != "linear_lower" && field != "linear_upper" && field != "angular_lower" && field != "angular_upper" && field != "linear_enabled" && field != "angular_enabled") {
					return false;
				}
			}
			for (const String &kind : { String("linear"), String("angular") }) {
				double range = kind == "linear" ? 1e6 : Math::PI;
				if (!flag(values, kind + "_enabled", true) || !number(values, kind + "_lower", 0, -range, range) || !number(values, kind + "_upper", 0, -range, range) || double(values[kind + "_lower"]) > double(values[kind + "_upper"])) {
					return false;
				}
			}
			limits[axis] = values;
		}
	}
	data["limits"] = limits;
	Variant exclude = data.get("exclude_collision", true);
	if (exclude.get_type() != Variant::BOOL) {
		return false;
	}
	data["exclude_collision"] = exclude;
	auto *server = PhysicsServer3D::get_singleton();
	if (!server) {
		return false;
	}
	// Reconfigure the existing RID: freeing an older joint after enabling collision
	// exclusion on a new one can otherwise remove the new body's exclusion pair.
	PinJointState *previous = joints_3d.getptr(uint32_t(id));
	RID rid = previous ? previous->rid : server->joint_create();
	if (previous) {
		server->joint_disable_collisions_between_bodies(rid, false);
		for (uint64_t body_id : { previous->body_a, previous->body_b }) {
			const PhysicsBody *old_body = physics_bodies.getptr(uint32_t(body_id));
			if (old_body && old_body->mode == 2) {
				server->body_set_state(old_body->rid, PhysicsServer3D::BODY_STATE_SLEEPING, false);
			}
		}
	}
	Transform3D frame_a = data["frame_a"], frame_b = data["frame_b"];
	if (type == "pin") {
		server->joint_make_pin(rid, body_a->rid, frame_a.origin, body_b->rid, frame_b.origin);
	} else if (type == "hinge") {
		server->joint_make_hinge(rid, body_a->rid, frame_a, body_b->rid, frame_b);
		server->hinge_joint_set_param(rid, PhysicsServer3D::HINGE_JOINT_LIMIT_LOWER, limits["lower"]);
		server->hinge_joint_set_param(rid, PhysicsServer3D::HINGE_JOINT_LIMIT_UPPER, limits["upper"]);
		server->hinge_joint_set_flag(rid, PhysicsServer3D::HINGE_JOINT_FLAG_USE_LIMIT, limits["enabled"]);
		server->hinge_joint_set_param(rid, PhysicsServer3D::HINGE_JOINT_MOTOR_TARGET_VELOCITY, limits["motor_speed"]);
		server->hinge_joint_set_param(rid, PhysicsServer3D::HINGE_JOINT_MOTOR_MAX_IMPULSE, limits["motor_impulse"]);
		server->hinge_joint_set_flag(rid, PhysicsServer3D::HINGE_JOINT_FLAG_ENABLE_MOTOR, limits["motor_enabled"]);
	} else if (type == "slider") {
		server->joint_make_slider(rid, body_a->rid, frame_a, body_b->rid, frame_b);
		server->slider_joint_set_param(rid, PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_LOWER, limits["linear_lower"]);
		server->slider_joint_set_param(rid, PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_UPPER, limits["linear_upper"]);
		server->slider_joint_set_param(rid, PhysicsServer3D::SLIDER_JOINT_ANGULAR_LIMIT_LOWER, limits["angular_lower"]);
		server->slider_joint_set_param(rid, PhysicsServer3D::SLIDER_JOINT_ANGULAR_LIMIT_UPPER, limits["angular_upper"]);
	} else if (type == "cone_twist") {
		server->joint_make_cone_twist(rid, body_a->rid, frame_a, body_b->rid, frame_b);
		server->cone_twist_joint_set_param(rid, PhysicsServer3D::CONE_TWIST_JOINT_SWING_SPAN, limits["swing_span"]);
		server->cone_twist_joint_set_param(rid, PhysicsServer3D::CONE_TWIST_JOINT_TWIST_SPAN, limits["twist_span"]);
	} else {
		server->joint_make_generic_6dof(rid, body_a->rid, frame_a, body_b->rid, frame_b);
		const char *names[] = { "x", "y", "z" };
		for (int i = 0; i < 3; i++) {
			Dictionary values = limits[names[i]];
			Vector3::Axis axis = Vector3::Axis(i);
			server->generic_6dof_joint_set_param(rid, axis, PhysicsServer3D::G6DOF_JOINT_LINEAR_LOWER_LIMIT, values["linear_lower"]);
			server->generic_6dof_joint_set_param(rid, axis, PhysicsServer3D::G6DOF_JOINT_LINEAR_UPPER_LIMIT, values["linear_upper"]);
			server->generic_6dof_joint_set_param(rid, axis, PhysicsServer3D::G6DOF_JOINT_ANGULAR_LOWER_LIMIT, values["angular_lower"]);
			server->generic_6dof_joint_set_param(rid, axis, PhysicsServer3D::G6DOF_JOINT_ANGULAR_UPPER_LIMIT, values["angular_upper"]);
			server->generic_6dof_joint_set_flag(rid, axis, PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_LIMIT, values["linear_enabled"]);
			server->generic_6dof_joint_set_flag(rid, axis, PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_LIMIT, values["angular_enabled"]);
		}
	}
	server->joint_disable_collisions_between_bodies(rid, exclude);
	if (body_a->mode == 2) {
		server->body_set_state(body_a->rid, PhysicsServer3D::BODY_STATE_SLEEPING, false);
	}
	if (body_b->mode == 2) {
		server->body_set_state(body_b->rid, PhysicsServer3D::BODY_STATE_SLEEPING, false);
	}
	joints_3d[uint32_t(id)] = { rid, data.duplicate(true), uint64_t(int64_t(a)), uint64_t(int64_t(b)) };
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "joint_3d", &marker);
	sync_joint_activation();
	return true;
}
Dictionary ECSWorld::get_joint_3d(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const PinJointState *joint = is_alive(id) ? joints_3d.getptr(uint32_t(id)) : nullptr;
	return joint ? joint->definition.duplicate(true) : Dictionary();
}
bool ECSWorld::remove_joint_3d(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	PinJointState *joint = is_alive(id) ? joints_3d.getptr(uint32_t(id)) : nullptr;
	if (!joint) {
		return false;
	}
	PhysicsServer3D::get_singleton()->free_rid(joint->rid);
	// Godot Physics does not automatically wake a sleeping body on constraint
	// removal. Without this a released hanging body stays suspended forever.
	for (uint64_t body_id : { joint->body_a, joint->body_b }) {
		const PhysicsBody *body = physics_bodies.getptr(uint32_t(body_id));
		if (body && body->mode == 2) {
			PhysicsServer3D::get_singleton()->body_set_state(body->rid, PhysicsServer3D::BODY_STATE_SLEEPING, false);
		}
	}
	joints_3d.erase(uint32_t(id));
	remove_row(pools["joint_3d"], uint32_t(id));
	return true;
}
void ECSWorld::remove_joint_references_3d(uint64_t body) {
	Vector<uint64_t> owners;
	for (const auto &entry : joints_3d) {
		if (entry.value.body_a == body || entry.value.body_b == body) {
			owners.push_back((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		}
	}
	for (uint64_t owner : owners) {
		remove_joint_3d(owner);
	}
}
void ECSWorld::clear_joints_3d() {
	if (auto *server = PhysicsServer3D::get_singleton()) {
		for (const auto &entry : joints_3d) {
			server->free_rid(entry.value.rid);
		}
	}
	joints_3d.clear();
	if (Pool *pool = pools.getptr("joint_3d")) {
		pool->bytes.clear();
		pool->entities.clear();
		pool->rows.clear();
	}
}
