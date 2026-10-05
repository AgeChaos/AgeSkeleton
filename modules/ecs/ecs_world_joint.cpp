// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "servers/physics_3d/physics_server_3d.h"

bool ECSWorld::set_pin_joint(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Dictionary data = get_pin_joint(id);
	data.merge(definition, true);
	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "body_a" && field != "body_b" && field != "anchor_a" && field != "anchor_b" && field != "exclude_collision") {
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
	for (const auto &entry : pin_joints) {
		if (entry.key != uint32_t(id) && ((entry.value.body_a == uint64_t(int64_t(a)) && entry.value.body_b == uint64_t(int64_t(b))) || (entry.value.body_a == uint64_t(int64_t(b)) && entry.value.body_b == uint64_t(int64_t(a))))) {
			return false;
		}
	}
	for (const auto &entry : joints_3d) {
		if ((entry.value.body_a == uint64_t(int64_t(a)) && entry.value.body_b == uint64_t(int64_t(b))) || (entry.value.body_b == uint64_t(int64_t(a)) && entry.value.body_a == uint64_t(int64_t(b)))) {
			return false;
		}
	}
	for (const String &key : { String("anchor_a"), String("anchor_b") }) {
		Variant value = data.get(key, Vector3());
		if (value.get_type() != Variant::VECTOR3 || !Vector3(value).is_finite()) {
			return false;
		}
		data[key] = value;
	}
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
	PinJointState *previous = pin_joints.getptr(uint32_t(id));
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
	server->joint_make_pin(rid, body_a->rid, data["anchor_a"], body_b->rid, data["anchor_b"]);
	server->joint_disable_collisions_between_bodies(rid, exclude);
	if (body_a->mode == 2) {
		server->body_set_state(body_a->rid, PhysicsServer3D::BODY_STATE_SLEEPING, false);
	}
	if (body_b->mode == 2) {
		server->body_set_state(body_b->rid, PhysicsServer3D::BODY_STATE_SLEEPING, false);
	}
	pin_joints[uint32_t(id)] = { rid, data.duplicate(true), uint64_t(int64_t(a)), uint64_t(int64_t(b)) };
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "pin_joint", &marker);
	sync_joint_activation();
	return true;
}
Dictionary ECSWorld::get_pin_joint(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const PinJointState *joint = is_alive(id) ? pin_joints.getptr(uint32_t(id)) : nullptr;
	return joint ? joint->definition.duplicate(true) : Dictionary();
}
bool ECSWorld::remove_pin_joint(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	PinJointState *joint = is_alive(id) ? pin_joints.getptr(uint32_t(id)) : nullptr;
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
	pin_joints.erase(uint32_t(id));
	remove_row(pools["pin_joint"], uint32_t(id));
	return true;
}
void ECSWorld::remove_joint_references(uint64_t body) {
	Vector<uint64_t> owners;
	for (const auto &entry : pin_joints) {
		if (entry.value.body_a == body || entry.value.body_b == body) {
			owners.push_back((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		}
	}
	for (uint64_t owner : owners) {
		remove_pin_joint(owner);
	}
}
void ECSWorld::clear_joints() {
	if (auto *server = PhysicsServer3D::get_singleton()) {
		for (const auto &entry : pin_joints) {
			server->free_rid(entry.value.rid);
		}
	}
	pin_joints.clear();
	if (Pool *pool = pools.getptr("pin_joint")) {
		pool->bytes.clear();
		pool->entities.clear();
		pool->rows.clear();
	}
}
