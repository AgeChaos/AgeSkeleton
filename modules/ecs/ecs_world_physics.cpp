// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "core/config/project_settings.h"
#include "servers/physics_3d/physics_server_3d.h"

ECSWorld::~ECSWorld() {
	clear_tilemaps_2d();
	clear_skeletal_2d();
	clear_lights();
	clear_particles();
	clear_audio();
	clear_navigation();
	clear_skeletons();
	clear_physics();
}
bool ECSWorld::set_physics(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id) || physics_bodies_2d.has(uint32_t(id))) {
		return false;
	}
	Dictionary data = get_physics(id);
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
		String field = key;
		if (field != "shape" && field != "shapes" && field != "mode" && field != "mass" && field != "friction" && field != "bounce" && field != "gravity_scale" && field != "layer" && field != "mask" && field != "ccd") {
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
		if (resource.get_type() != Variant::OBJECT || Ref<Shape3D>(resource).is_null()) {
			return false;
		}
		Variant local = entry.get("transform", Transform3D());
		Variant disabled = entry.get("disabled", false);
		if (local.get_type() != Variant::TRANSFORM3D || disabled.get_type() != Variant::BOOL) {
			return false;
		}
		Transform3D transform = local;
		if (!transform.is_finite() || Math::is_zero_approx(transform.basis.determinant())) {
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
	for (const auto &entry : pin_joints) {
		if (entry.value.body_a != id && entry.value.body_b != id) {
			continue;
		}
		uint64_t other_id = entry.value.body_a == id ? entry.value.body_b : entry.value.body_a;
		const PhysicsBody *other = physics_bodies.getptr(uint32_t(other_id));
		if (!other || (body_mode != 2 && other->mode != 2)) {
			return false;
		}
		attached_joints.push_back({ (uint64_t(slots[entry.key].generation) << 32) | entry.key, entry.value.definition.duplicate(true) });
	}
	Vector<JointRestore> attached_generic_joints;
	for (const auto &entry : joints_3d) {
		if (entry.value.body_a != id && entry.value.body_b != id) {
			continue;
		}
		uint64_t other_id = entry.value.body_a == id ? entry.value.body_b : entry.value.body_a;
		const PhysicsBody *other = physics_bodies.getptr(uint32_t(other_id));
		if (!other || (body_mode != 2 && other->mode != 2)) {
			return false;
		}
		attached_generic_joints.push_back({ (uint64_t(slots[entry.key].generation) << 32) | entry.key, entry.value.definition.duplicate(true) });
	}
	for (const String &key : { String("mass"), String("friction"), String("bounce"), String("gravity_scale") }) {
		Variant value = data.get(key, key == "bounce" ? 0.0 : 1.0);
		if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value)) || double(value) < 0 || (key == "mass" && double(value) <= 0) || (key == "bounce" && double(value) > 1)) {
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
	Variant ccd = data.get("ccd", false);
	if (ccd.get_type() != Variant::BOOL) {
		return false;
	}
	data["mode"] = mode;
	data["ccd"] = ccd;
	auto *server = PhysicsServer3D::get_singleton();
	if (!server) {
		return false;
	}
	Transform3D transform = calculate_global_transform(id);
	if (!transform.is_finite() || Math::is_zero_approx(transform.basis.determinant())) {
		return false;
	}
	if (!physics_space.is_valid()) {
		physics_space = server->space_create();
		server->space_set_active(physics_space, true);
		server->area_set_param(physics_space, PhysicsServer3D::AREA_PARAM_GRAVITY, GLOBAL_GET("physics/3d/default_gravity"));
		server->area_set_param(physics_space, PhysicsServer3D::AREA_PARAM_GRAVITY_VECTOR, GLOBAL_GET("physics/3d/default_gravity_vector"));
	}
	PhysicsBody body;
	body.rid = server->body_create();
	body.mode = body_mode;
	// Shape resource references are retained by the definition dictionary.
	body.definition = data.duplicate(true);
	body.submitted_transform = transform;
	body.submitted_velocity = get_vector(id, "velocity");
	server->body_set_mode(body.rid, PhysicsServer3D::BodyMode(body_mode));
	for (const Variant &value : shapes) {
		Dictionary entry = value;
		Ref<Shape3D> shape = entry["shape"];
		server->body_add_shape(body.rid, shape->get_rid(), entry.get("transform", Transform3D()), entry.get("disabled", false));
	}
	server->body_set_state(body.rid, PhysicsServer3D::BODY_STATE_TRANSFORM, transform);
	server->body_set_state(body.rid, PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY, body.submitted_velocity);
	server->body_set_param(body.rid, PhysicsServer3D::BODY_PARAM_MASS, data["mass"]);
	server->body_set_param(body.rid, PhysicsServer3D::BODY_PARAM_FRICTION, data["friction"]);
	server->body_set_param(body.rid, PhysicsServer3D::BODY_PARAM_BOUNCE, data["bounce"]);
	server->body_set_param(body.rid, PhysicsServer3D::BODY_PARAM_GRAVITY_SCALE, data["gravity_scale"]);
	server->body_set_collision_layer(body.rid, uint32_t(int64_t(data["layer"])));
	server->body_set_collision_mask(body.rid, uint32_t(int64_t(data["mask"])));
	server->body_set_enable_continuous_collision_detection(body.rid, ccd);
	server->body_set_max_contacts_reported(body.rid, 16);
	server->body_set_space(body.rid, physics_space);
	remove_physics(id);
	physics_bodies[uint32_t(id)] = body;
	physics_entities[body.rid] = id;
	for (const JointRestore &joint : attached_joints) {
		set_pin_joint(joint.owner, joint.definition);
	}
	for (const JointRestore &joint : attached_generic_joints) {
		set_joint_3d(joint.owner, joint.definition);
	}
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "physics_body", &marker);
	if (!pools["position"].rows.has(uint32_t(id))) {
		set_vector(id, "position", transform.origin);
	}
	if (!slots[uint32_t(id)].active) { apply_activation(uint32_t(id)); }
	return true;
}
Dictionary ECSWorld::get_physics(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const PhysicsBody *body = is_alive(id) ? physics_bodies.getptr(uint32_t(id)) : nullptr;
	return body ? body->definition.duplicate(true) : Dictionary();
}
bool ECSWorld::remove_physics(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	PhysicsBody *body = is_alive(id) ? physics_bodies.getptr(uint32_t(id)) : nullptr;
	if (!body) {
		return false;
	}
	{
		MutexLock lock(area_event_mutex);
		for (auto &entry : areas) {
			if (entry.value.overlaps.erase(id)) {
				uint64_t owner = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
				area_events.push_back({ owner, entry.value.rid, body->rid, false, id });
			}
		}
	}
	remove_joint_references(id);
	remove_joint_references_3d(id);
	physics_entities.erase(body->rid);
	if (auto *server = PhysicsServer3D::get_singleton()) {
		server->free_rid(body->rid);
	}
	physics_bodies.erase(uint32_t(id));
	remove_row(pools["physics_body"], uint32_t(id));
	return true;
}
void ECSWorld::clear_physics() {
	clear_physics_2d();
	clear_joints();
	clear_joints_3d();
	clear_areas();
	if (auto *server = PhysicsServer3D::get_singleton()) {
		for (const auto &entry : physics_bodies) {
			server->free_rid(entry.value.rid);
		}
		if (physics_space.is_valid()) {
			server->free_rid(physics_space);
		}
	}
	physics_bodies.clear();
	physics_entities.clear();
	physics_contacts.clear();
	physics_space = RID();
	physics_phase = false;
	if (Pool *pool = pools.getptr("physics_body")) {
		pool->bytes.clear();
		pool->entities.clear();
		pool->rows.clear();
	}
}
void ECSWorld::sync_physics() {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	physics_phase = true;
	sync_physics_2d();
	physics_contacts.clear();
	auto *server = PhysicsServer3D::get_singleton();
	for (auto &entry : physics_bodies) {
		if (!slots[entry.key].active) { continue; }
		PhysicsBody &body = entry.value;
		if (body.mode != 2) {
			continue;
		}
		uint64_t id = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
		PhysicsDirectBodyState3D *state = server->body_get_direct_state(body.rid);
		if (!state) {
			continue;
		}
		// Preserve gameplay teleports/velocity changes made since the previous submission.
		if (calculate_global_transform(id).is_equal_approx(body.submitted_transform)) {
			Transform3D transform = state->get_transform();
			set_vector(id, "position", transform.origin);
			set_vector(id, "rotation", transform.basis.orthonormalized().get_euler());
			body.submitted_transform = transform;
		}
		if (get_vector(id, "velocity").is_equal_approx(body.submitted_velocity)) {
			body.submitted_velocity = state->get_linear_velocity();
			set_vector(id, "velocity", body.submitted_velocity);
		}
		for (int i = 0; i < state->get_contact_count(); i++) {
			const uint64_t *other = physics_entities.getptr(state->get_contact_collider(i));
			Dictionary contact;
			contact["entity"] = id;
			contact["other"] = other ? *other : 0;
			contact["position"] = state->get_contact_collider_position(i);
			contact["normal"] = state->get_contact_local_normal(i);
			contact["impulse"] = state->get_contact_impulse(i);
			physics_contacts.push_back(contact);
		}
	}
	sync_areas();
	sync_areas_2d();
}
void ECSWorld::submit_physics() {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	auto *server = PhysicsServer3D::get_singleton();
	for (auto &entry : physics_bodies) {
		if (!slots[entry.key].active) { continue; }
		PhysicsBody &body = entry.value;
		Transform3D transform = calculate_global_transform((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		if (!transform.is_finite() || Math::is_zero_approx(transform.basis.determinant())) {
			continue;
		}
		if (!transform.is_equal_approx(body.submitted_transform)) {
			server->body_set_state(body.rid, PhysicsServer3D::BODY_STATE_TRANSFORM, transform);
			body.submitted_transform = transform;
		}
		uint64_t id = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
		Vector3 velocity = get_vector(id, "velocity");
		if (body.mode == 2 && velocity != body.submitted_velocity) {
			server->body_set_state(body.rid, PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY, velocity);
			body.submitted_velocity = velocity;
		}
	}
	submit_physics_2d();
	submit_areas();
	submit_areas_2d();
	physics_phase = false;
}
bool ECSWorld::apply_impulse(uint64_t id, const Vector3 &impulse) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	PhysicsBody *body = is_alive(id) ? physics_bodies.getptr(uint32_t(id)) : nullptr;
	if (!body || body->mode != 2 || !impulse.is_finite()) {
		return false;
	}
	PhysicsServer3D::get_singleton()->body_apply_central_impulse(body->rid, impulse);
	return true;
}
bool ECSWorld::apply_torque_impulse(uint64_t id, const Vector3 &impulse) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	const PhysicsBody *body = is_alive(id) ? physics_bodies.getptr(uint32_t(id)) : nullptr;
	if (!body || body->mode != 2 || !impulse.is_finite()) {
		return false;
	}
	PhysicsServer3D::get_singleton()->body_apply_torque_impulse(body->rid, impulse);
	return true;
}
Dictionary ECSWorld::move_and_collide(uint64_t id, const Vector3 &motion) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || !physics_phase, Dictionary());
	PhysicsBody *body = is_alive(id) ? physics_bodies.getptr(uint32_t(id)) : nullptr;
	if (!body || body->mode != 1 || !motion.is_finite()) {
		return Dictionary();
	}
	auto *server = PhysicsServer3D::get_singleton();
	Transform3D transform = calculate_global_transform(id);
	PhysicsServer3D::MotionResult result;
	bool collided = server->body_test_motion(body->rid, PhysicsServer3D::MotionParameters(transform, motion), &result);
	Vector3 travel = collided ? result.travel : motion;
	transform.origin += travel;
	set_vector(id, "position", transform.origin);
	server->body_set_state(body->rid, PhysicsServer3D::BODY_STATE_TRANSFORM, transform);
	body->submitted_transform = transform;
	Dictionary output;
	output["collided"] = collided;
	output["travel"] = travel;
	output["remainder"] = collided ? result.remainder : Vector3();
	if (collided && result.collision_count) {
		const uint64_t *other = physics_entities.getptr(result.collisions[0].collider);
		output["other"] = other ? *other : 0;
		output["normal"] = result.collisions[0].normal;
		output["position"] = result.collisions[0].position;
	}
	return output;
}
Dictionary ECSWorld::move_and_slide(uint64_t id, const Vector3 &velocity, double delta, int max_slides) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || !physics_phase, Dictionary());
	if (!velocity.is_finite() || !Math::is_finite(delta) || delta < 0 || max_slides < 1 || max_slides > 16) {
		return Dictionary();
	}
	Vector3 remaining = velocity * delta, output_velocity = velocity;
	Array collisions;
	Vector3 travel;
	for (int i = 0; i < max_slides; i++) {
		Dictionary hit = move_and_collide(id, remaining);
		if (hit.is_empty()) {
			return Dictionary();
		}
		travel += Vector3(hit["travel"]);
		if (!bool(hit["collided"])) {
			break;
		}
		collisions.push_back(hit);
		Vector3 normal = hit.get("normal", Vector3());
		if (normal.is_zero_approx()) {
			break;
		}
		remaining = Vector3(hit["remainder"]).slide(normal);
		output_velocity = output_velocity.slide(normal);
		if (remaining.is_zero_approx()) {
			break;
		}
	}
	set_vector(id, "velocity", output_velocity);
	Dictionary result;
	result["velocity"] = output_velocity;
	result["travel"] = travel;
	result["collisions"] = collisions;
	return result;
}
Dictionary ECSWorld::carry_platform(uint64_t id, uint64_t platform, const Transform3D &previous, bool stop_platform) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || !physics_phase, Dictionary());
	PhysicsBody *body = is_alive(id) ? physics_bodies.getptr(uint32_t(id)) : nullptr;
	PhysicsBody *support = is_alive(platform) ? physics_bodies.getptr(uint32_t(platform)) : nullptr;
	if (!body || body->mode != 1 || !support || id == platform || get_parent(id) || get_parent(platform) || !previous.is_finite() || Math::is_zero_approx(previous.basis.determinant())) {
		return Dictionary();
	}
	Transform3D current = calculate_global_transform(platform), from = calculate_global_transform(id);
	// Translation and yaw only: changing scale or tilting support needs a separate shape sweep.
	if (!current.is_finite() || !current.basis.get_scale().is_equal_approx(previous.basis.get_scale()) || !current.basis.get_column(1).normalized().is_equal_approx(Vector3(0, 1, 0)) || !previous.basis.get_column(1).normalized().is_equal_approx(Vector3(0, 1, 0))) {
		return Dictionary();
	}
	Vector3 motion = current.xform(previous.affine_inverse().xform(from.origin)) - from.origin;
	if (!motion.is_finite()) {
		return Dictionary();
	}
	auto *server = PhysicsServer3D::get_singleton();
	// Submit scripted kinematic transforms before passenger queries. Motion-testing the
	// platform against its passenger first would resolve the platform in the opposite direction.
	if (support->mode == 1) {
		server->body_set_state(support->rid, PhysicsServer3D::BODY_STATE_TRANSFORM, current);
		support->submitted_transform = current;
	}
	PhysicsServer3D::MotionParameters parameters(from, motion);
	parameters.exclude_bodies.insert(support->rid);
	PhysicsServer3D::MotionResult hit;
	bool blocked = server->body_test_motion(body->rid, parameters, &hit);
	Vector3 travel = blocked ? hit.travel : motion;
	bool stopped = blocked && stop_platform && support->mode == 1;
	if (stopped) {
		// Roll back this kinematic platform step instead of continually forcing the passenger into a wall.
		set_vector(platform, "position", previous.origin);
		set_vector(platform, "rotation", previous.basis.get_euler());
		set_vector(platform, "scale", previous.basis.get_scale());
		server->body_set_state(support->rid, PhysicsServer3D::BODY_STATE_TRANSFORM, previous);
		support->submitted_transform = previous;
		travel = Vector3();
	}
	from.origin += travel;
	set_vector(id, "position", from.origin);
	server->body_set_state(body->rid, PhysicsServer3D::BODY_STATE_TRANSFORM, from);
	body->submitted_transform = from;
	Dictionary result;
	result["travel"] = travel;
	result["blocked"] = blocked;
	result["platform_stopped"] = stopped;
	return result;
}
Dictionary ECSWorld::move_grounded(uint64_t id, const Vector3 &velocity, double delta, double step_height, double snap, double floor_angle) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || !physics_phase, Dictionary());
	const PhysicsBody *body = is_alive(id) ? physics_bodies.getptr(uint32_t(id)) : nullptr;
	if (!body || body->mode != 1 || get_parent(id) || !velocity.is_finite() || !Math::is_finite(delta) || delta <= 0 || !Math::is_finite(step_height) || step_height < 0 || !Math::is_finite(snap) || snap < 0 || !Math::is_finite(floor_angle) || floor_angle <= 0 || floor_angle >= Math::PI / 2 || !(velocity * delta).is_finite()) {
		return Dictionary();
	}
	auto *server = PhysicsServer3D::get_singleton();
	Vector3 up(0, 1, 0);
	real_t minimum_normal = Math::cos(floor_angle);
	auto test = [&](const Transform3D &from, const Vector3 &motion, PhysicsServer3D::MotionResult &hit) {
		return server->body_test_motion(body->rid, PhysicsServer3D::MotionParameters(from, motion), &hit);
	};
	auto floor = [&](const PhysicsServer3D::MotionResult &hit) { return hit.collision_count > 0 && hit.collisions[0].normal.dot(up) >= minimum_normal; };
	Transform3D start = calculate_global_transform(id), current = start;
	PhysicsServer3D::MotionResult support;
	bool grounded = test(start, -up * 0.025, support) && floor(support);
	Vector3 motion = velocity * delta;
	if (grounded && velocity.y <= 0) {
		Vector3 horizontal(velocity.x, 0, velocity.z);
		Vector3 normal = support.collisions[0].normal;
		motion = Vector3(horizontal.x, -normal.dot(horizontal) / normal.y, horizontal.z) * delta;
	}
	PhysicsServer3D::MotionResult direct;
	bool blocked = test(current, motion, direct);
	bool stepped = false;
	if (blocked && grounded && velocity.y <= 0 && step_height > 0 && Vector2(motion.x, motion.z).length_squared() > 0.000001 && !floor(direct)) {
		PhysicsServer3D::MotionResult rise, forward, down;
		Transform3D raised = start;
		if (!test(raised, up * step_height, rise)) {
			raised.origin += up * step_height;
			Vector3 horizontal(motion.x, 0, motion.z);
			if (!test(raised, horizontal, forward)) {
				raised.origin += horizontal;
				if (test(raised, -up * (step_height + snap), down) && floor(down)) {
					Vector3 landing = raised.origin + down.travel;
					if (landing.y <= start.origin.y + step_height + 0.002 && landing.y >= start.origin.y - snap - 0.002) {
						current.origin = landing;
						stepped = true;
					}
				}
			}
		}
	}
	if (!stepped) {
		Vector3 remaining = motion;
		for (int i = 0; i < 4; i++) {
			PhysicsServer3D::MotionResult hit;
			if (!test(current, remaining, hit)) {
				current.origin += remaining;
				break;
			}
			current.origin += hit.travel;
			if (hit.collision_count == 0) {
				break;
			}
			Vector3 normal = hit.collisions[0].normal;
			// Too-steep slopes are walls, not automatic climbing ramps.
			if (normal.y > 0 && normal.y < minimum_normal && velocity.y <= 0) {
				normal.y = 0;
				normal.normalize();
			}
			remaining = hit.remainder.slide(normal);
			if (remaining.is_zero_approx()) {
				break;
			}
		}
	}
	grounded = false;
	uint64_t floor_entity = 0;
	Vector3 floor_normal;
	if (velocity.y <= 0 && snap > 0) {
		PhysicsServer3D::MotionResult hit;
		if (test(current, -up * snap, hit) && floor(hit)) {
			current.origin += hit.travel;
			grounded = true;
			const uint64_t *other = physics_entities.getptr(hit.collisions[0].collider);
			floor_entity = other ? *other : 0;
			floor_normal = hit.collisions[0].normal;
		}
	}
	Vector3 travel = current.origin - start.origin;
	set_vector(id, "position", current.origin);
	PhysicsBody *mutable_body = physics_bodies.getptr(uint32_t(id));
	server->body_set_state(mutable_body->rid, PhysicsServer3D::BODY_STATE_TRANSFORM, current);
	mutable_body->submitted_transform = current;
	set_vector(id, "velocity", travel / delta);
	Dictionary result;
	result["travel"] = travel;
	result["velocity"] = travel / delta;
	result["grounded"] = grounded;
	result["stepped"] = stepped;
	result["floor_entity"] = floor_entity;
	result["floor_normal"] = floor_normal;
	return result;
}
Array ECSWorld::get_physics_contacts() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Array());
	Array result;
	for (const Variant &value : physics_contacts) {
		Dictionary contact = value;
		if (is_alive(uint64_t(int64_t(contact["entity"]))) && (!int64_t(contact["other"]) || is_alive(uint64_t(int64_t(contact["other"]))))) {
			result.push_back(contact.duplicate(true));
		}
	}
	return result;
}

Dictionary ECSWorld::intersect_ray(const Vector3 &from, const Vector3 &to, uint32_t mask, const PackedInt64Array &exclude) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || !physics_phase, Dictionary());
	if (!physics_space.is_valid() || !from.is_finite() || !to.is_finite()) {
		return Dictionary();
	}
	auto *space = PhysicsServer3D::get_singleton()->space_get_direct_state(physics_space);
	if (!space) {
		return Dictionary();
	}
	PhysicsDirectSpaceState3D::RayParameters parameters;
	parameters.from = from;
	parameters.to = to;
	parameters.collision_mask = mask;
	for (int64_t id : exclude) {
		if (is_alive(id)) {
			if (const PhysicsBody *body = physics_bodies.getptr(uint32_t(id))) {
				parameters.exclude.insert(body->rid);
			}
		}
	}
	PhysicsDirectSpaceState3D::RayResult hit;
	if (!space->intersect_ray(parameters, hit)) {
		return Dictionary();
	}
	Dictionary result;
	const uint64_t *id = physics_entities.getptr(hit.rid);
	result["entity"] = id ? *id : 0;
	result["position"] = hit.position;
	result["normal"] = hit.normal;
	result["shape"] = hit.shape;
	result["face_index"] = hit.face_index;
	return result;
}
PackedInt64Array ECSWorld::intersect_shape(const Ref<Shape3D> &shape, const Transform3D &transform, uint32_t mask, const PackedInt64Array &exclude, int max_results) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || !physics_phase, PackedInt64Array());
	if (!physics_space.is_valid() || shape.is_null() || !transform.is_finite() || Math::is_zero_approx(transform.basis.determinant()) || max_results < 1 || max_results > 4096) {
		return PackedInt64Array();
	}
	auto *space = PhysicsServer3D::get_singleton()->space_get_direct_state(physics_space);
	if (!space) {
		return PackedInt64Array();
	}
	PhysicsDirectSpaceState3D::ShapeParameters parameters;
	parameters.shape_rid = shape->get_rid();
	parameters.transform = transform;
	parameters.collision_mask = mask;
	for (int64_t id : exclude) {
		if (is_alive(id)) {
			if (const PhysicsBody *body = physics_bodies.getptr(uint32_t(id))) {
				parameters.exclude.insert(body->rid);
			}
		}
	}
	Vector<PhysicsDirectSpaceState3D::ShapeResult> hits;
	hits.resize(max_results);
	int count = space->intersect_shape(parameters, hits.ptrw(), max_results);
	PackedInt64Array result;
	for (int i = 0; i < count; i++) {
		const uint64_t *id = physics_entities.getptr(hits[i].rid);
		if (id && is_alive(*id) && !result.has(*id)) {
			result.push_back(*id);
		}
	}
	return result;
}
