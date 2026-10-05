// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "core/config/project_settings.h"
#include "scene/resources/2d/shape_2d.h"
#include "servers/physics_2d/physics_server_2d.h"

Transform2D ECSWorld::calculate_transform_2d(uint64_t id) const {
	Transform3D value = calculate_global_transform(id);
	Vector3 x = value.basis.get_column(0), y = value.basis.get_column(1);
	return Transform2D(Vector2(x.x, x.y), Vector2(y.x, y.y), Vector2(value.origin.x, value.origin.y));
}
bool ECSWorld::set_physics_2d(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id) || physics_bodies.has(uint32_t(id))) {
		return false;
	}
	Dictionary data = get_physics_2d(id);
	// Explicitly switching representation replaces the previous shape list.
	if (definition.has("shape") && definition.has("shapes")) {
		return false;
	}
	if (definition.has("shape")) {
		data.erase("shapes");
	} else if (definition.has("shapes")) {
		data.erase("shape");
	}
	data.merge(definition, true);
	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "shape" && field != "shapes" && field != "ccd" && field != "max_contacts" && field != "mode" && field != "mass" && field != "friction" && field != "bounce" && field != "gravity_scale" && field != "layer" && field != "mask") {
			return false;
		}
	}
	Array shapes;
	if (data.has("shapes")) {
		if (data["shapes"].get_type() != Variant::ARRAY) {
			return false;
		}
		shapes = data["shapes"];
	} else if (data.has("shape")) {
		Dictionary entry;
		entry["shape"] = data["shape"];
		shapes.push_back(entry);
	}
	if (shapes.is_empty() || shapes.size() > 256) {
		return false;
	}
	// Validate the complete input before allocating or replacing any RID.
	for (const Variant &value : shapes) {
		if (value.get_type() != Variant::DICTIONARY) {
			return false;
		}
		Dictionary entry = value;
		for (const Variant &key : entry.keys()) {
			if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
				return false;
			}
			String field = key;
			if (field != "shape" && field != "transform" && field != "disabled") {
				return false;
			}
		}
		Variant resource = entry.get("shape", Variant());
		if (resource.get_type() != Variant::OBJECT || Ref<Shape2D>(resource).is_null()) {
			return false;
		}
		Variant local = entry.get("transform", Transform2D());
		Variant disabled = entry.get("disabled", false);
		if (local.get_type() != Variant::TRANSFORM2D || disabled.get_type() != Variant::BOOL) {
			return false;
		}
		Transform2D transform = local;
		if (!transform.is_finite() || Math::is_zero_approx(transform.determinant())) {
			return false;
		}
	}
	Variant raw_mode = data.get("mode", "static");
	if (raw_mode.get_type() != Variant::STRING) {
		return false;
	}
	String mode = raw_mode;
	int body_mode = mode == "static" ? 0 : mode == "kinematic" ? 1
			: mode == "rigid"								   ? 2
															   : -1;
	if (body_mode < 0 || (body_mode != 0 && get_parent(id))) {
		return false;
	}
	struct JointRestore {
		uint64_t owner;
		Dictionary definition;
	};
	Vector<JointRestore> attached_joints;
	for (const auto &entry : joints_2d) {
		if (entry.value.body_a != id && entry.value.body_b != id) {
			continue;
		}
		uint64_t other_id = entry.value.body_a == id ? entry.value.body_b : entry.value.body_a;
		const PhysicsBody2D *other = physics_bodies_2d.getptr(uint32_t(other_id));
		if (!other || (body_mode != 2 && other->mode != 2)) {
			return false;
		}
		attached_joints.push_back({ (uint64_t(slots[entry.key].generation) << 32) | entry.key, entry.value.definition.duplicate(true) });
	}
	for (const String &key : { String("mass"), String("friction"), String("bounce"), String("gravity_scale") }) {
		Variant value = data.get(key, key == "bounce" ? 0.0 : 1.0);
		if ((value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) || !Math::is_finite(double(value)) || double(value) < 0 || (key == "mass" && double(value) <= 0) || (key == "bounce" && double(value) > 1)) {
			return false;
		}
		data[key] = double(value);
	}
	for (const String &key : { String("layer"), String("mask") }) {
		Variant value = data.get(key, 1);
		if (value.get_type() != Variant::INT || int64_t(value) < 0 || uint64_t(int64_t(value)) > UINT32_MAX) {
			return false;
		}
		data[key] = value;
	}
	Variant max_contacts = data.get("max_contacts", 0);
	if (max_contacts.get_type() != Variant::INT || int64_t(max_contacts) < 0 || int64_t(max_contacts) > 256) {
		return false;
	}
	data["max_contacts"] = max_contacts;
	Variant ccd = data.get("ccd", "disabled");
	if (ccd.get_type() != Variant::STRING) {
		return false;
	}
	String ccd_name = ccd;
	int ccd_mode = ccd_name == "disabled" ? 0 : ccd_name == "ray" ? 1
			: ccd_name == "shape"								  ? 2
																  : -1;
	if (ccd_mode < 0) {
		return false;
	}
	data["ccd"] = ccd_name;
	data["mode"] = mode;
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
	PhysicsBody2D body;
	body.rid = server->body_create();
	body.definition = data.duplicate(true);
	body.mode = body_mode;
	body.submitted_transform = transform;
	Vector3 velocity = get_vector(id, "velocity");
	body.submitted_velocity = Vector2(velocity.x, velocity.y);
	server->body_set_mode(body.rid, PhysicsServer2D::BodyMode(body_mode));
	server->body_set_max_contacts_reported(body.rid, int64_t(max_contacts));
	for (const Variant &value : shapes) {
		Dictionary entry = value;
		Ref<Shape2D> shape = entry["shape"];
		server->body_add_shape(body.rid, shape->get_rid(), entry.get("transform", Transform2D()), entry.get("disabled", false));
	}
	server->body_set_continuous_collision_detection_mode(body.rid, PhysicsServer2D::CCDMode(ccd_mode));
	server->body_set_state(body.rid, PhysicsServer2D::BODY_STATE_TRANSFORM, transform);
	server->body_set_state(body.rid, PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY, body.submitted_velocity);
	server->body_set_param(body.rid, PhysicsServer2D::BODY_PARAM_MASS, data["mass"]);
	server->body_set_param(body.rid, PhysicsServer2D::BODY_PARAM_FRICTION, data["friction"]);
	server->body_set_param(body.rid, PhysicsServer2D::BODY_PARAM_BOUNCE, data["bounce"]);
	server->body_set_param(body.rid, PhysicsServer2D::BODY_PARAM_GRAVITY_SCALE, data["gravity_scale"]);
	server->body_set_collision_layer(body.rid, uint32_t(int64_t(data["layer"])));
	server->body_set_collision_mask(body.rid, uint32_t(int64_t(data["mask"])));
	server->body_set_space(body.rid, physics_space_2d);
	remove_physics_2d(id);
	physics_bodies_2d[uint32_t(id)] = body;
	physics_entities_2d[body.rid] = id;
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "physics_body_2d", &marker);
	for (const JointRestore &joint : attached_joints) {
		set_joint_2d(joint.owner, joint.definition);
	}
	if (!slots[uint32_t(id)].active) { apply_activation(uint32_t(id)); }
	return true;
}
Dictionary ECSWorld::get_physics_2d(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const PhysicsBody2D *body = is_alive(id) ? physics_bodies_2d.getptr(uint32_t(id)) : nullptr;
	return body ? body->definition.duplicate(true) : Dictionary();
}
bool ECSWorld::remove_physics_2d(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	PhysicsBody2D *body = is_alive(id) ? physics_bodies_2d.getptr(uint32_t(id)) : nullptr;
	if (!body) {
		return false;
	}
	{
		MutexLock lock(area_event_mutex_2d);
		for (auto &entry : areas_2d) {
			if (entry.value.overlaps.erase(id)) {
				uint64_t owner = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
				area_events_2d.push_back({ owner, entry.value.rid, body->rid, false, id });
			}
		}
	}
	remove_joint_references_2d(id);
	physics_entities_2d.erase(body->rid);
	PhysicsServer2D::get_singleton()->free_rid(body->rid);
	physics_bodies_2d.erase(uint32_t(id));
	remove_row(pools["physics_body_2d"], uint32_t(id));
	return true;
}
void ECSWorld::clear_physics_2d() {
	for(auto &entry:tilemaps_2d) { entry.value.dirty=true; entry.value.rebuild_all=true; }
	clear_areas_2d();
	clear_joints_2d();
	if (auto *server = PhysicsServer2D::get_singleton()) {
		for (const auto &entry : physics_bodies_2d) {
			server->free_rid(entry.value.rid);
		}
		if (physics_space_2d.is_valid()) {
			server->free_rid(physics_space_2d);
		}
	}
	physics_space_2d = RID();
	physics_bodies_2d.clear();
	physics_entities_2d.clear();
	physics_contacts_2d.clear();
	contact_pairs_2d.clear();
	if (Pool *pool = pools.getptr("physics_body_2d")) {
		pool->bytes.clear();
		pool->entities.clear();
		pool->rows.clear();
	}
}
Array ECSWorld::get_physics_contacts_2d() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Array());
	return physics_contacts_2d.duplicate(true);
}
void ECSWorld::sync_physics_2d() {
	physics_contacts_2d.clear();
	HashMap<uint64_t, HashSet<uint64_t>> current;
	auto *server = PhysicsServer2D::get_singleton();
	for (auto &entry : physics_bodies_2d) {
		if (!slots[entry.key].active) { continue; }
		PhysicsBody2D &body = entry.value;
		if (body.mode != 2) {
			continue;
		}
		uint64_t id = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
		auto *state = server->body_get_direct_state(body.rid);
		if (!state) {
			continue;
		}
		if (calculate_transform_2d(id).is_equal_approx(body.submitted_transform)) {
			Transform2D transform = state->get_transform();
			Vector3 position = get_vector(id, "position");
			position.x = transform.get_origin().x;
			position.y = transform.get_origin().y;
			set_vector(id, "position", position);
			Vector3 rotation = get_vector(id, "rotation");
			rotation.z = transform.get_rotation();
			set_vector(id, "rotation", rotation);
			body.submitted_transform = transform;
		}
		Vector3 velocity = get_vector(id, "velocity");
		if (Vector2(velocity.x, velocity.y).is_equal_approx(body.submitted_velocity)) {
			body.submitted_velocity = state->get_linear_velocity();
			velocity.x = body.submitted_velocity.x;
			velocity.y = body.submitted_velocity.y;
			set_vector(id, "velocity", velocity);
		}
		for (int i = 0; i < state->get_contact_count(); i++) {
			const uint64_t *other = physics_entities_2d.getptr(state->get_contact_collider(i));
			if (!other || !is_alive(*other) || *other == id) {
				continue;
			}
			Dictionary contact;
			contact["entity"] = id;
			contact["other"] = *other;
			contact["position"] = state->get_contact_collider_position(i);
			contact["normal"] = state->get_contact_local_normal(i);
			contact["impulse"] = state->get_contact_impulse(i);
			contact["shape"] = state->get_contact_local_shape(i);
			contact["other_shape"] = state->get_contact_collider_shape(i);
			physics_contacts_2d.push_back(contact);
			current[id].insert(*other);
		}
	}
	struct ContactEvent {
		uint64_t owner, other;
		bool entered;
	};
	Vector<ContactEvent> events;
	for (const auto &entry : current) {
		const HashSet<uint64_t> *previous = contact_pairs_2d.getptr(entry.key);
		for (uint64_t other : entry.value) {
			if (!previous || !previous->has(other)) {
				events.push_back({ entry.key, other, true });
			}
		}
	}
	for (const auto &entry : contact_pairs_2d) {
		const HashSet<uint64_t> *next = current.getptr(entry.key);
		for (uint64_t other : entry.value) {
			if (!next || !next->has(other)) {
				events.push_back({ entry.key, other, false });
			}
		}
	}
	contact_pairs_2d = current;
	// Dispatch only after sampling every body, so callbacks may change the world.
	for (const ContactEvent &event : events) {
		if (is_alive(event.owner) && physics_bodies_2d.has(uint32_t(event.owner)) && (!event.entered || (is_alive(event.other) && physics_bodies_2d.has(uint32_t(event.other))))) {
			emit_signal(event.entered ? "body_2d_contact_started" : "body_2d_contact_ended", event.owner, event.other);
		}
	}
}
void ECSWorld::submit_physics_2d() {
	auto *server = PhysicsServer2D::get_singleton();
	for (auto &entry : physics_bodies_2d) {
		if (!slots[entry.key].active) { continue; }
		PhysicsBody2D &body = entry.value;
		uint64_t id = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
		Transform2D transform = calculate_transform_2d(id);
		if (!transform.is_finite() || Math::is_zero_approx(transform.determinant())) {
			continue;
		}
		if (!transform.is_equal_approx(body.submitted_transform)) {
			server->body_set_state(body.rid, PhysicsServer2D::BODY_STATE_TRANSFORM, transform);
			body.submitted_transform = transform;
		}
		Vector3 v = get_vector(id, "velocity");
		Vector2 velocity(v.x, v.y);
		if (body.mode == 2 && velocity != body.submitted_velocity) {
			server->body_set_state(body.rid, PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY, velocity);
			body.submitted_velocity = velocity;
		}
	}
}
bool ECSWorld::apply_impulse_2d(uint64_t id, const Vector2 &impulse) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	const PhysicsBody2D *body = is_alive(id) ? physics_bodies_2d.getptr(uint32_t(id)) : nullptr;
	if (!body || body->mode != 2 || !impulse.is_finite()) {
		return false;
	}
	PhysicsServer2D::get_singleton()->body_apply_central_impulse(body->rid, impulse);
	return true;
}
bool ECSWorld::apply_torque_impulse_2d(uint64_t id, double impulse) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	const PhysicsBody2D *body = is_alive(id) ? physics_bodies_2d.getptr(uint32_t(id)) : nullptr;
	if (!body || body->mode != 2 || !Math::is_finite(impulse)) {
		return false;
	}
	PhysicsServer2D::get_singleton()->body_apply_torque_impulse(body->rid, impulse);
	return true;
}
Dictionary ECSWorld::move_and_collide_2d(uint64_t id, const Vector2 &motion) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || !physics_phase, Dictionary());
	PhysicsBody2D *body = is_alive(id) ? physics_bodies_2d.getptr(uint32_t(id)) : nullptr;
	if (!body || body->mode != 1 || !motion.is_finite()) {
		return Dictionary();
	}
	auto *server = PhysicsServer2D::get_singleton();
	Transform2D transform = calculate_transform_2d(id);
	PhysicsServer2D::MotionResult hit;
	bool collided = server->body_test_motion(body->rid, PhysicsServer2D::MotionParameters(transform, motion), &hit);
	Vector2 travel = collided ? hit.travel : motion;
	transform.set_origin(transform.get_origin() + travel);
	Vector3 position = get_vector(id, "position");
	position.x = transform.get_origin().x;
	position.y = transform.get_origin().y;
	set_vector(id, "position", position);
	server->body_set_state(body->rid, PhysicsServer2D::BODY_STATE_TRANSFORM, transform);
	body->submitted_transform = transform;
	Dictionary result;
	result["collided"] = collided;
	result["travel"] = travel;
	result["remainder"] = collided ? hit.remainder : Vector2();
	if (collided) {
		const uint64_t *other = physics_entities_2d.getptr(hit.collider);
		result["other"] = other ? *other : 0;
		result["normal"] = hit.collision_normal;
		result["position"] = hit.collision_point;
	}
	return result;
}
Dictionary ECSWorld::move_and_slide_2d(uint64_t id, const Vector2 &velocity, double delta, int max_slides) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || !physics_phase, Dictionary());
	if (!velocity.is_finite() || !Math::is_finite(delta) || delta < 0 || max_slides < 1 || max_slides > 16) {
		return Dictionary();
	}
	Vector2 remaining = velocity * delta, output = velocity, travel;
	Array collisions;
	for (int i = 0; i < max_slides; i++) {
		Dictionary hit = move_and_collide_2d(id, remaining);
		if (hit.is_empty()) {
			return Dictionary();
		}
		travel += Vector2(hit["travel"]);
		if (!bool(hit["collided"])) {
			break;
		}
		collisions.push_back(hit);
		Vector2 normal = hit.get("normal", Vector2());
		if (normal.is_zero_approx()) {
			break;
		}
		remaining = Vector2(hit["remainder"]).slide(normal);
		output = output.slide(normal);
		if (remaining.is_zero_approx()) {
			break;
		}
	}
	Vector3 v = get_vector(id, "velocity");
	v.x = output.x;
	v.y = output.y;
	set_vector(id, "velocity", v);
	Dictionary result;
	result["velocity"] = output;
	result["travel"] = travel;
	result["collisions"] = collisions;
	return result;
}
Dictionary ECSWorld::intersect_ray_2d(const Vector2 &from, const Vector2 &to, uint32_t mask, const PackedInt64Array &exclude) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || !physics_phase, Dictionary());
	if (!physics_space_2d.is_valid() || !from.is_finite() || !to.is_finite()) {
		return Dictionary();
	}
	auto *space = PhysicsServer2D::get_singleton()->space_get_direct_state(physics_space_2d);
	if (!space) {
		return Dictionary();
	}
	PhysicsDirectSpaceState2D::RayParameters parameters;
	parameters.from = from;
	parameters.to = to;
	parameters.collision_mask = mask;
	for (int64_t id : exclude) {
		if (is_alive(id)) {
			if (const PhysicsBody2D *body = physics_bodies_2d.getptr(uint32_t(id))) {
				parameters.exclude.insert(body->rid);
			}
            if(const TilemapState *map=tilemaps_2d.getptr(uint32_t(id))) { for(const auto &chunk:map->chunks) { for(RID body:chunk.value.bodies) { parameters.exclude.insert(body); } } }
		}
	}
	PhysicsDirectSpaceState2D::RayResult hit;
	if (!space->intersect_ray(parameters, hit)) {
		return Dictionary();
	}
	Dictionary result;
	const uint64_t *id = physics_entities_2d.getptr(hit.rid);
	result["entity"] = id ? *id : 0;
	result["position"] = hit.position;
	result["normal"] = hit.normal;
	result["shape"] = hit.shape;
	return result;
}
