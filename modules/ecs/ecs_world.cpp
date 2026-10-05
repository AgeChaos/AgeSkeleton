#include "ecs_profile_scope.h"
// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

#include <cmath>
#include <cstring>

bool ECSWorld::is_vector_component(const StringName &name) {
	return name == StringName("position") || name == StringName("velocity") || name == StringName("rotation") || name == StringName("scale") || name == StringName("shear");
}
ECSWorld::ECSWorld() {
	register_component("audio", 1);
	register_component("navigation", 1);
	register_component("camera", 1);
	register_component("light", 1);
	register_component("particles", 1);
	register_component("bone_2d", 1);
	register_component("skeleton_2d", 1);
	register_component("polygon_2d", 1);
	register_component("physics_body", 1);
	register_component("physics_body_2d", 1);
	register_component("area", 1);
	register_component("area_2d", 1);
	register_component("pin_joint", 1);
	register_component("joint_3d", 1);
	register_component("joint_2d", 1);
	register_component("animation", 1);
	register_component("skeleton", 1);
	register_component("ui_layout", sizeof(ECSUILayout));
	register_component("ui_style", sizeof(ECSUIStyle));
	register_component("ui_input", sizeof(ECSUIInput));
	register_component("ui_text", 1);
	register_component("rotation", sizeof(real_t) * 3);
	register_component("scale", sizeof(real_t) * 3);
	register_component("shear", sizeof(real_t) * 3);
	register_component("position", sizeof(real_t) * 3);
	register_component("velocity", sizeof(real_t) * 3);
}
uint64_t ECSWorld::create_entity() {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	uint32_t index;
	if (free_slots.is_empty()) {
		index = slots.size();
		slots.push_back(Slot());
	} else {
		index = free_slots[free_slots.size() - 1];
		free_slots.resize(free_slots.size() - 1);
	}
	slots.write[index].alive = true;
	slots.write[index].active_self = true;
	slots.write[index].active = true;
	living++;
	transforms_dirty = true;
	return (uint64_t(slots[index].generation) << 32) | index;
}
PackedInt64Array ECSWorld::create_entities(int count) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread || count < 0 || count > 1000000, PackedInt64Array());
	PackedInt64Array result;
	result.resize(count);
	for (int i = 0; i < count; i++) {
		result.set(i, create_entity());
	}
	return result;
}
bool ECSWorld::set_vectors(const PackedInt64Array &entities, const StringName &name, const PackedVector3Array &values) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (entities.size() != values.size() || !is_vector_component(name)) {
		return false;
	}
	for (int i = 0; i < entities.size(); i++) {
		if (!is_alive(entities[i]) || !values[i].is_finite()) {
			return false;
		}
	}
	for (int i = 0; i < entities.size(); i++) {
		if (!set_vector(entities[i], name, values[i])) {
			return false;
		}
	}
	return true;
}
bool ECSWorld::is_alive(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	uint32_t index = uint32_t(id);
	return index < uint32_t(slots.size()) && slots[index].alive && slots[index].generation == uint32_t(id >> 32);
}
void ECSWorld::remove_row(Pool &pool, uint32_t index) {
	const int *found = pool.rows.getptr(index);
	if (!found) {
		return;
	}
	int row = *found, last = pool.entities.size() - 1;
	if (row != last) {
		memcpy(pool.bytes.ptrw() + row * pool.stride, pool.bytes.ptr() + last * pool.stride, pool.stride);
		uint32_t moved = pool.entities[last];
		pool.entities.write[row] = moved;
		pool.rows[moved] = row;
	}
	pool.entities.resize(last);
	pool.bytes.resize(last * pool.stride);
	pool.rows.erase(index);
}
bool ECSWorld::destroy_entity(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	uint32_t index = uint32_t(id);
	// Deleting a parent detaches immediate children and preserves their local transform.
	if (const HashSet<uint32_t> *attached = children.getptr(index)) {
		for (uint32_t child : *attached) {
			parents.erase(child);
			refresh_activation(child);
		}
		children.erase(index);
	}
	set_parent(id, 0);
	visuals.erase(index);
	ui_texts.erase(index);
	remove_pin_joint(id);
	remove_joint_3d(id);
	remove_joint_2d(id);
	remove_area(id);
	remove_area_2d(id);
	remove_physics_2d(id);
	remove_physics(id);
	remove_audio(id);
	remove_navigation(id);
	navigation_followers.erase(uint32_t(id));
	remove_camera(id);
	remove_light(id);
	remove_particles(id);
	remove_tilemap_2d(id);
	remove_skeletal_2d(id, "polygon_2d");
	remove_skeletal_2d(id, "skeleton_2d");
	remove_skeletal_2d(id, "bone_2d");
	remove_animation_references(id);
	ui_revision++;
	transforms_dirty = true;
	for (KeyValue<StringName, Pool> &entry : pools) {
		remove_row(entry.value, index);
	}
	Slot &slot = slots.write[index];
	slot.alive = false;
	living--;
	// Retire exhausted slots instead of wrapping a generation and reviving stale IDs.
	if (slot.generation < 0x7fffffffU) {
		slot.generation++;
		free_slots.push_back(index);
	}
	return true;
}
void ECSWorld::defer_destroy(uint64_t id) {
	if (is_alive(id)) {
		pending_destroy.push_back(id);
	}
}
void ECSWorld::flush_commands() {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	for (uint64_t id : pending_destroy) {
		destroy_entity(id);
	}
	pending_destroy.clear();
}
bool ECSWorld::register_component(const StringName &name, int stride) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (name == StringName() || stride < 1 || stride > 65536) {
		return false;
	}
	if (pools.has(name)) {
		return pools[name].stride == stride;
	}
	Pool pool;
	pool.stride = stride;
	pools.insert(name, pool);
	return true;
}
bool ECSWorld::set_component(uint64_t id, const StringName &name, const PackedByteArray &bytes) {
	if (is_ui_component(name) || (name == StringName("pin_joint") || name == StringName("joint_3d") || name == StringName("joint_2d") || name == StringName("area") || name == StringName("area_2d") || name == StringName("physics_body_2d") || name == StringName("physics_body") || name == StringName("animation") || name == StringName("skeleton") || name == StringName("audio") || name == StringName("navigation") || name == StringName("camera") || name == StringName("bone_2d") || name == StringName("skeleton_2d") || name == StringName("polygon_2d") || name == StringName("particles") || name == StringName("light"))) {
		return false;
	} // Use validated set_ui; references never enter POD buffers.
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Pool *pool = pools.getptr(name);
	if (!pool || bytes.size() != pool->stride) {
		return false;
	}
	if (is_vector_component(name)) {
		real_t values[3];
		memcpy(values, bytes.ptr(), sizeof(values));
		if (!Vector3(values[0], values[1], values[2]).is_finite()) {
			return false;
		}
	}
	uint32_t index = uint32_t(id);
	int row;
	if (const int *existing = pool->rows.getptr(index)) {
		row = *existing;
	} else {
		row = pool->entities.size();
		if (int64_t(row + 1) * pool->stride > INT32_MAX) {
			return false;
		}
		pool->entities.push_back(index);
		pool->rows.insert(index, row);
		pool->bytes.resize((row + 1) * pool->stride);
	}
	memcpy(pool->bytes.ptrw() + row * pool->stride, bytes.ptr(), pool->stride);
	transforms_dirty = true;
	return true;
}
PackedByteArray ECSWorld::get_component(uint64_t id, const StringName &name) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedByteArray());
	PackedByteArray result;
	if (!is_alive(id)) {
		return result;
	}
	const Pool *pool = pools.getptr(name);
	if (!pool) {
		return result;
	}
	const int *row = pool->rows.getptr(uint32_t(id));
	if (!row) {
		return result;
	}
	result.resize(pool->stride);
	memcpy(result.ptrw(), pool->bytes.ptr() + *row * pool->stride, pool->stride);
	return result;
}
bool ECSWorld::remove_component(uint64_t id, const StringName &name) {
	if (name == StringName("tilemap_2d")) { return remove_tilemap_2d(id); }
	if (name == StringName("bone_2d") || name == StringName("skeleton_2d") || name == StringName("polygon_2d")) { return remove_skeletal_2d(id, name); }
	if (name == StringName("particles")) { return remove_particles(id); }
	if (name == StringName("camera")) {
		return remove_camera(id);
	}
	if (name == StringName("light")) {
		return remove_light(id);
	}
	if (name == StringName("navigation")) {
		return remove_navigation(id);
	}
	if (name == StringName("audio")) {
		return remove_audio(id);
	}
	if (name == StringName("animation")) {
		return remove_animation(id);
	}
	if (name == StringName("skeleton")) {
		return remove_skeleton(id);
	}
	if (name == StringName("joint_2d")) {
		return remove_joint_2d(id);
	}
	if (name == StringName("joint_3d")) {
		return remove_joint_3d(id);
	}
	if (name == StringName("pin_joint")) {
		return remove_pin_joint(id);
	}
	if (name == StringName("area_2d")) {
		return remove_area_2d(id);
	}
	if (name == StringName("area")) {
		return remove_area(id);
	}
	if (name == StringName("physics_body_2d")) {
		return remove_physics_2d(id);
	}
	if (name == StringName("physics_body")) {
		return remove_physics(id);
	}
	if (is_ui_component(name)) {
		return remove_ui(id);
	}
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	Pool *pool = pools.getptr(name);
	if (!is_alive(id) || !pool || !pool->rows.has(uint32_t(id))) {
		return false;
	}
	remove_row(*pool, uint32_t(id));
	transforms_dirty = true;
	return true;
}
PackedInt64Array ECSWorld::query(const PackedStringArray &names, bool include_inactive) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	PackedInt64Array result;
	const Pool *smallest = nullptr;
	for (const String &name : names) {
		const Pool *pool = pools.getptr(name);
		if (!pool) {
			return result;
		}
		if (!smallest || pool->entities.size() < smallest->entities.size()) {
			smallest = pool;
		}
	}
	if (!smallest) {
		for (int i = 0; i < slots.size(); i++) {
			if (slots[i].alive && (include_inactive || slots[i].active)) {
				result.push_back((uint64_t(slots[i].generation) << 32) | uint32_t(i));
			}
		}
		return result;
	}
	for (uint32_t index : smallest->entities) {
		if (!include_inactive && !slots[index].active) { continue; }
		bool matches = true;
		for (const String &name : names) {
			if (!pools[name].rows.has(index)) {
				matches = false;
				break;
			}
		}
		if (matches) {
			result.push_back((uint64_t(slots[index].generation) << 32) | index);
		}
	}
	return result;
}
Vector3 ECSWorld::read_vector(const Pool &pool, int row) const {
	real_t values[3];
	memcpy(values, pool.bytes.ptr() + row * pool.stride, sizeof(values));
	return Vector3(values[0], values[1], values[2]);
}
void ECSWorld::write_vector(Pool &pool, int row, const Vector3 &v) {
	real_t values[3] = { v.x, v.y, v.z };
	memcpy(pool.bytes.ptrw() + row * pool.stride, values, sizeof(values));
}
bool ECSWorld::set_vector(uint64_t id, const StringName &name, const Vector3 &v) {
	if (!is_vector_component(name) || !v.is_finite()) {
		return false;
	}
	PackedByteArray bytes;
	bytes.resize(sizeof(real_t) * 3);
	real_t values[3] = { v.x, v.y, v.z };
	memcpy(bytes.ptrw(), values, sizeof(values));
	return set_component(id, name, bytes);
}
Vector3 ECSWorld::get_vector(uint64_t id, const StringName &name) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Vector3());
	if (!is_alive(id) || !is_vector_component(name)) {
		return Vector3();
	}
	const Pool &pool = pools[name];
	const int *row = pool.rows.getptr(uint32_t(id));
	return row ? read_vector(pool, *row) : (name == StringName("scale") ? Vector3(1, 1, 1) : Vector3());
}
bool ECSWorld::add_system(const StringName &name, const Callable &callback) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (stepping || name == StringName() || !callback.is_valid()) {
		return false;
	}
	for (const System &system : systems) {
		if (system.name == name) {
			return false;
		}
	}
	systems.push_back({ name, callback });
	return true;
}
bool ECSWorld::remove_system(const StringName &name) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (stepping) {
		return false;
	}
	for (int i = 0; i < systems.size(); i++) {
		if (systems[i].name == name) {
			systems.remove_at(i);
			return true;
		}
	}
	return false;
}
void ECSWorld::step(double delta) {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	ERR_FAIL_COND(stepping || !std::isfinite(delta) || delta < 0);
	stepping = true;
	{ ECSProfileScope profile("UI events"); dispatch_ui_events(); }
	// User systems execute once per step, in registration order, before native movement.
	Variant argument = delta;
	const Variant *arguments[] = { &argument };
	for (const System &system : systems) {
		Variant result;
		Callable::CallError error;
		CharString profile_name;
		if (ECSProfileScope::is_active()) profile_name = (String("System: ") + String(system.name)).utf8();
		{ ECSProfileScope profile(profile_name.get_data()); system.callback.callp(arguments, 1, result, error); }
		if (error.error != Callable::CallError::CALL_OK) {
			ERR_PRINT("ECS system callback failed: " + String(system.name));
		}
	}

	{ ECSProfileScope profile("Animation"); step_animations(delta); }
	{ ECSProfileScope profile("Particles"); step_particles(delta); }
	{ ECSProfileScope profile("Navigation sync"); sync_navigation(); }
	{ ECSProfileScope profile("Navigation movement"); step_navigation_followers(delta); }
	{ ECSProfileScope profile("Movement");
	Pool &positions = pools["position"];
	const Pool &velocities = pools["velocity"];
	// One native loop, no entity callbacks and no managed/native calls per entity.
	for (int row = 0; row < velocities.entities.size(); row++) {
		const int *target = positions.rows.getptr(velocities.entities[row]);
		if (slots[velocities.entities[row]].active && target && !navigation_followers.has(velocities.entities[row]) && !physics_bodies.has(velocities.entities[row]) && !physics_bodies_2d.has(velocities.entities[row])) {
			write_vector(positions, *target, read_vector(positions, *target) + read_vector(velocities, row) * delta);
		}
	}
	}
	transforms_dirty = true;
	{ ECSProfileScope profile("Audio"); step_audio(delta); }
	{ ECSProfileScope profile("Commands"); flush_commands(); }
	stepping = false;
}
namespace {
void append_transform(Vector<float> &buffer, const Transform3D &transform) {
	int offset = buffer.size();
	buffer.resize(offset + 12);
	float *out = buffer.ptrw() + offset;
	for (int row = 0; row < 3; row++) {
		for (int column = 0; column < 3; column++) {
			out[row * 4 + column] = transform.basis[row][column];
		}
		out[row * 4 + 3] = transform.origin[row];
	}
}
} //namespace
bool ECSWorld::set_parent(uint64_t id, uint64_t parent) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id) || (parent && !is_alive(parent))) {
		return false;
	}
	if (parent && physics_bodies_2d.has(uint32_t(id)) && physics_bodies_2d[uint32_t(id)].mode != 0) {
		return false;
	}
	if (parent && physics_bodies.has(uint32_t(id)) && physics_bodies[uint32_t(id)].mode != 0) {
		return false;
	}
	for (uint64_t ancestor = parent; ancestor; ancestor = get_parent(ancestor)) {
		if (ancestor == id) {
			return false;
		}
	}
	uint64_t previous = get_parent(id);
	if (previous == parent) {
		return true;
	}
	if (previous) {
		HashSet<uint32_t> &siblings = children[uint32_t(previous)];
		siblings.erase(uint32_t(id));
		if (siblings.is_empty()) {
			children.erase(uint32_t(previous));
		}
	}
	if (parent) {
		children[uint32_t(parent)].insert(uint32_t(id));
	}
	if (parent) {
		parents[uint32_t(id)] = parent;
	} else {
		parents.erase(uint32_t(id));
	}
	transforms_dirty = true;
	refresh_activation(uint32_t(id));
	ui_revision++;
	return true;
}
uint64_t ECSWorld::get_parent(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	if (!is_alive(id)) {
		return 0;
	}
	const uint64_t *parent = parents.getptr(uint32_t(id));
	return parent ? *parent : 0;
}
void ECSWorld::update_transforms() const {
	if (!transforms_dirty) {
		return;
	}
	global_transforms.resize(slots.size());
	Vector<uint8_t> resolved;
	resolved.resize(slots.size());
	resolved.fill(0);
	Vector<uint32_t> chain;
	const Pool &positions = pools["position"], &rotations = pools["rotation"], &scales = pools["scale"], &shears = pools["shear"];
	auto local_transform = [&](uint32_t index) {
		const int *rotation = rotations.rows.getptr(index);
		const int *scale = scales.rows.getptr(index);
		const int *position = positions.rows.getptr(index);
		Basis basis = rotation ? Basis::from_euler(read_vector(rotations, *rotation)) : Basis();
		if (const int *row = shears.rows.getptr(index)) {
			Vector3 shear=read_vector(shears,*row);
			basis *= Basis(Vector3(Math::cos(shear.x),Math::sin(shear.x),0),Vector3(-Math::sin(shear.y),Math::cos(shear.y),0),Vector3(0,0,1));
		}
		if (scale) {
			basis.scale_local(read_vector(scales, *scale));
		}
		return Transform3D(basis, position ? read_vector(positions, *position) : Vector3());
	};
	for (int i = 0; i < slots.size(); i++) {
		if (!slots[i].alive || resolved[i]) {
			continue;
		}
		chain.clear();
		uint32_t current = i;
		while (!resolved[current]) {
			chain.push_back(current);
			const uint64_t *parent = parents.getptr(current);
			if (!parent) {
				break;
			}
			current = uint32_t(*parent);
		}
		for (int j = chain.size() - 1; j >= 0; j--) {
			uint32_t index = chain[j];
			Transform3D transform = local_transform(index);
			const uint64_t *parent = parents.getptr(index);
			global_transforms.write[index] = parent ? global_transforms[uint32_t(*parent)] * transform : transform;
			resolved.write[index] = 1;
		}
	}
	transforms_dirty = false;
}
Transform3D ECSWorld::get_global_transform(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Transform3D());
	if (!is_alive(id)) {
		return Transform3D();
	}
	update_transforms();
	return global_transforms[uint32_t(id)];
}
bool ECSWorld::set_mesh(uint64_t id, const Ref<Mesh> &mesh, const Ref<Material> &material) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	if (mesh.is_null()) {
		visuals.erase(uint32_t(id));
	} else {
		visuals[uint32_t(id)] = { mesh, material };
	}
	return true;
}
Ref<Mesh> ECSWorld::get_mesh(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Ref<Mesh>());
	const Visual *visual = is_alive(id) ? visuals.getptr(uint32_t(id)) : nullptr;
	return visual ? visual->mesh : Ref<Mesh>();
}
Ref<Material> ECSWorld::get_material(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Ref<Material>());
	const Visual *visual = is_alive(id) ? visuals.getptr(uint32_t(id)) : nullptr;
	return visual ? visual->material : Ref<Material>();
}
Vector<ECSWorld::RenderBatch> ECSWorld::get_render_batches(const Ref<Mesh> &default_mesh) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Vector<RenderBatch>());
	update_transforms();
	Vector<RenderBatch> batches;
	HashMap<ObjectID, HashMap<ObjectID, int>> groups;
	for (uint32_t index : pools["position"].entities) {
		if (!slots[index].active) { continue; }
		const Visual *visual = visuals.getptr(index);
		Ref<Mesh> mesh = visual ? visual->mesh : default_mesh;
		if (mesh.is_null()) {
			continue;
		}
		Ref<Material> material = visual ? visual->material : Ref<Material>();
		if (const SkeletonState *rig = skeletons.getptr(index)) {
			if (Math::is_zero_approx(global_transforms[index].basis.determinant()) || rig->skin->get_bind_count() != rig->bones.size()) {
				continue;
			}
			Transform3D inverse = global_transforms[index].affine_inverse();
			for (int i = 0; i < rig->bones.size(); i++) {
				RenderingServer::get_singleton()->skeleton_bone_set_transform(rig->rid, i, inverse * global_transforms[uint32_t(rig->bones[i])] * rig->skin->get_bind_pose(i));
			}
			RenderBatch batch;
			batch.entity = (uint64_t(slots[index].generation) << 32) | index;
			batch.mesh = mesh;
			batch.material = material;
			batch.skeleton = rig->rid;
			batch.transform = global_transforms[index];
			batches.push_back(batch);
			continue;
		}
		ObjectID material_id = material.is_valid() ? material->get_instance_id() : ObjectID();
		auto &materials = groups[mesh->get_instance_id()];
		int *existing = materials.getptr(material_id);
		int group;
		if (existing) {
			group = *existing;
		} else {
			group = batches.size();
			materials[material_id] = group;
			RenderBatch batch;
			batch.mesh = mesh;
			batch.material = material;
			batches.push_back(batch);
		}
		append_transform(batches.write[group].transforms, global_transforms[index]);
	}
	return batches;
}
Vector<float> ECSWorld::get_transform_buffer() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Vector<float>());
	update_transforms();
	Vector<float> result;
	for (uint32_t index : pools["position"].entities) {
		if (!slots[index].active) { continue; }
		append_transform(result, global_transforms[index]);
	}
	return result;
}
Dictionary ECSWorld::serialize() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	Dictionary result, layouts;
	Array entities;
	HashMap<uint32_t, int> indices;
	for (int i = 0; i < slots.size(); i++) {
		if (slots[i].alive) {
			indices[i] = indices.size();
		}
	}
	for (const KeyValue<StringName, Pool> &entry : pools) {
		if (!is_vector_component(entry.key) && !is_ui_component(entry.key) && entry.key != StringName("pin_joint") && entry.key != StringName("joint_3d") && entry.key != StringName("joint_2d") && entry.key != StringName("area") && entry.key != StringName("area_2d") && entry.key != StringName("physics_body_2d") && entry.key != StringName("physics_body") && entry.key != StringName("animation") && entry.key != StringName("skeleton") && entry.key != StringName("audio") && entry.key != StringName("navigation") && entry.key != StringName("camera") && entry.key != StringName("bone_2d") && entry.key != StringName("skeleton_2d") && entry.key != StringName("polygon_2d") && entry.key != StringName("particles") && entry.key != StringName("light")) {
			layouts[entry.key] = entry.value.stride;
		}
	}
	for (int i = 0; i < slots.size(); i++) {
		if (!slots[i].alive) {
			continue;
		}
		uint64_t id = (uint64_t(slots[i].generation) << 32) | uint32_t(i);
		Dictionary entity, components;
		entity["active"] = slots[i].active_self;
		for (const KeyValue<StringName, Pool> &entry : pools) {
			if (is_ui_component(entry.key) || entry.key == StringName("pin_joint") || entry.key == StringName("joint_3d") || entry.key == StringName("joint_2d") || entry.key == StringName("area") || entry.key == StringName("area_2d") || entry.key == StringName("physics_body_2d") || entry.key == StringName("physics_body") || entry.key == StringName("animation") || entry.key == StringName("skeleton") || entry.key == StringName("audio") || entry.key == StringName("navigation") || entry.key == StringName("camera") || entry.key == StringName("bone_2d") || entry.key == StringName("skeleton_2d") || entry.key == StringName("polygon_2d") || entry.key == StringName("particles") || entry.key == StringName("light") || !entry.value.rows.has(i)) {
				continue;
			}
			if (is_vector_component(entry.key)) {
				entity[entry.key] = get_vector(id, entry.key);
			} else {
				components[entry.key] = get_component(id, entry.key);
			}
		}
		if (animations.has(i)) {
			Dictionary animation = get_animation(id);
			PackedInt64Array targets = animation["targets"];
			for (int j = 0; j < targets.size(); j++) {
				targets.set(j, indices[uint32_t(targets[j])]);
			}
			animation["targets"] = targets;
			entity["animation"] = animation;
		}
		if (skeletons.has(i)) {
			Dictionary skeleton = get_skeleton(id);
			PackedInt64Array bones = skeleton["bones"];
			for (int j = 0; j < bones.size(); j++) {
				bones.set(j, indices[uint32_t(bones[j])]);
			}
			skeleton["bones"] = bones;
			entity["skeleton"] = skeleton;
		}
		entity["components"] = components;
		if (cameras.has(i)) {
			entity["camera"] = get_camera(id);
		}
		if (tilemaps_2d.has(i)) { entity["tilemap_2d"]=get_tilemap_2d(id); }
		if (bones_2d.has(i)) { entity["bone_2d"] = get_bone_2d(id); }
		if (skeletons_2d.has(i)) {
			Dictionary rig = get_skeleton_2d(id); PackedInt64Array bones = rig["bones"];
			for (int j = 0; j < bones.size(); j++) { ERR_FAIL_COND_V(!is_alive(bones[j]) || !bones_2d.has(uint32_t(bones[j])), Dictionary()); bones.set(j, indices[uint32_t(bones[j])]); }
			rig["bones"] = bones;
			HashMap<uint64_t,uint64_t> slot_map; for(const auto &mapped:indices) { slot_map[(uint64_t(slots[mapped.key].generation)<<32)|mapped.key]=mapped.value; }
			ERR_FAIL_COND_V(!remap_skeleton_slots(rig,slot_map),Dictionary()); entity["skeleton_2d"] = rig;
		}
		if (polygons_2d.has(i)) {
			Dictionary polygon = get_polygon_2d(id); uint64_t rig = int64_t(polygon["skeleton"]);
			ERR_FAIL_COND_V(rig && (!is_alive(rig) || !skeletons_2d.has(uint32_t(rig))), Dictionary());
			polygon["skeleton"] = rig ? indices[uint32_t(rig)] : -1; entity["polygon_2d"] = polygon;
		}
		if (particles.has(i)) { entity["particles"] = get_particles(id); }
		if (lights.has(i)) {
			entity["light"] = get_light(id);
		}
		if (navigation_regions.has(i)) {
			entity["navigation"] = get_navigation(id);
		}
		if (audio_sources.has(i)) {
			entity["audio"] = get_audio(id);
		}
		if (pin_joints.has(i)) {
			Dictionary joint = get_pin_joint(id);
			joint["body_a"] = indices[uint32_t(int64_t(joint["body_a"]))];
			joint["body_b"] = indices[uint32_t(int64_t(joint["body_b"]))];
			entity["pin_joint"] = joint;
		}

		if (joints_3d.has(i)) {
			Dictionary joint = get_joint_3d(id);
			joint["body_a"] = indices[uint32_t(int64_t(joint["body_a"]))];
			joint["body_b"] = indices[uint32_t(int64_t(joint["body_b"]))];
			entity["joint_3d"] = joint;
		}

		if (joints_2d.has(i)) {
			Dictionary joint = get_joint_2d(id);
			joint["body_a"] = indices[uint32_t(int64_t(joint["body_a"]))];
			joint["body_b"] = indices[uint32_t(int64_t(joint["body_b"]))];
			entity["joint_2d"] = joint;
		}
		if (areas_2d.has(i)) {
			entity["area_2d"] = get_area_2d(id);
		}
		if (areas.has(i)) {
			entity["area"] = get_area(id);
		}
		if (physics_bodies_2d.has(i)) {
			entity["physics_2d"] = get_physics_2d(id);
		}
		if (physics_bodies.has(i)) {
			entity["physics"] = get_physics(id);
		}
		if (ui_texts.has(i)) {
			entity["ui"] = get_ui(id);
		}
		uint64_t parent = get_parent(id);
		entity["parent"] = parent ? indices[uint32_t(parent)] : -1;
		if (get_mesh(id).is_valid()) {
			entity["mesh"] = get_mesh(id);
		}
		if (get_material(id).is_valid()) {
			entity["material"] = get_material(id);
		}
		entities.push_back(entity);
	}
	result["layouts"] = layouts;
	result["entities"] = entities;
	return result;
}
Dictionary ECSWorld::get_statistics() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	Dictionary result, counts, component_memory;
	int64_t bytes = 0, index_bytes = 0;
	for (const KeyValue<StringName, Pool> &entry : pools) {
		counts[entry.key] = entry.value.entities.size();
		bytes += entry.value.bytes.size();
		index_bytes += entry.value.entities.size() * sizeof(uint32_t);
		component_memory[entry.key] = entry.value.bytes.size();
	}
	result["entities"] = living;
	result["parent_links"] = parents.size();
	result["mesh_components"] = visuals.size();
	result["physics_bodies"] = physics_bodies.size();
	result["physics_bodies_2d"] = physics_bodies_2d.size();
	result["areas"] = areas.size();
	result["areas_2d"] = areas_2d.size();
	result["pin_joints"] = pin_joints.size();
	result["joints_3d"] = joints_3d.size();
	result["joints_2d"] = joints_2d.size();
	result["animations"] = animations.size();
	result["audio_sources"] = audio_sources.size();
	result["navigation_regions"] = navigation_regions.size();
	result["lights"] = lights.size();
	result["particle_emitters"] = particles.size();
	result["cameras"] = cameras.size();
	result["skeletons"] = skeletons.size();
	result["physics_contacts"] = physics_contacts.size();
	result["physics_contacts_2d"] = physics_contacts_2d.size();
	result["ui_entities"] = ui_texts.size();
	result["ui_events_dispatched"] = ui_events_dispatched;
	result["last_ui_event"] = last_ui_event.duplicate(true);
	result["components"] = counts;
	result["component_bytes"] = bytes;
	result["component_memory"] = component_memory;
	result["dense_index_bytes"] = index_bytes;
	result["slot_bytes"] = int64_t(slots.size()) * sizeof(Slot);
	result["transform_cache_bytes"] = int64_t(global_transforms.size()) * sizeof(Transform3D);
	result["systems"] = systems.size() + 1;
	result["pending_destroy"] = pending_destroy.size();
	result["storage"] = "native_sparse_set_pod";
	return result;
}
Array ECSWorld::inspect_entities(int offset, int limit) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Array());
	Array result;
	if (offset < 0 || limit < 1) {
		return result;
	}
	limit = MIN(limit, 256);
	int skipped = 0;
	for (int i = 0; i < slots.size() && result.size() < limit; i++) {
		if (!slots[i].alive) {
			continue;
		}
		if (skipped++ < offset) {
			continue;
		}
		uint64_t id = (uint64_t(slots[i].generation) << 32) | uint32_t(i);
		Dictionary entity, components;
		entity["active"] = slots[i].active_self;
		entity["id"] = id;
		entity["parent"] = get_parent(id);
		for (const KeyValue<StringName, Pool> &entry : pools) {
			const int *row = entry.value.rows.getptr(i);
			if (!row) {
				continue;
			}
			if (is_vector_component(entry.key)) {
				components[entry.key] = read_vector(entry.value, *row);
			} else {
				components[entry.key] = vformat("POD %d bytes", entry.value.stride);
			}
		}
		if (ui_texts.has(i)) {
			Dictionary ui = get_ui(id);
			const ECSUIText &content = ui_texts[i];
			if (content.font.is_valid()) {
				ui["font"] = content.font->get_path();
			}
			if (content.texture.is_valid()) {
				ui["texture"] = content.texture->get_path();
			}
			components["ui"] = ui;
		}
		entity["components"] = components;
		if (audio_sources.has(i)) {
			Dictionary audio = get_audio(id);
			audio["stream"] = audio_sources[i].stream->get_path();
			components["audio"] = audio;
		}
		if (physics_bodies.has(i)) {
			Dictionary physics = get_physics(id);
			if (physics.has("shape")) {
				Ref<Shape3D> shape = physics["shape"];
				physics["shape"] = shape->get_path();
			} else {
				Array shapes = physics["shapes"];
				for (int j = 0; j < shapes.size(); j++) {
					Dictionary entry = shapes[j];
					Ref<Shape3D> shape = entry["shape"];
					entry["shape"] = shape->get_path();
				}
			}
			components["physics"] = physics;
		}
		if (tilemaps_2d.has(i)) { components["tilemap_2d"]=get_tilemap_2d(id); }
		if (bones_2d.has(i)) { components["bone_2d"] = get_bone_2d(id); }
		if (skeletons_2d.has(i)) { components["skeleton_2d"] = get_skeleton_2d(id); }
		if (polygons_2d.has(i)) { components["polygon_2d"] = get_polygon_2d(id); }
		if (particles.has(i)) { components["particles"] = get_particles(id); }
		if (animations.has(i)) {
			Dictionary animation = get_animation(id);
			animation["clip"] = animations[i].clip->get_path();
			components["animation"] = animation;
		}
		result.push_back(entity);
	}
	return result;
}
namespace {
class ECSSystemTest : public RefCounted {
public:
	int calls = 0;
	void tick(double delta) { calls++; }
};
} //namespace
bool ECSWorld::self_test() {
	Ref<ECSWorld> w;
	w.instantiate();
	uint64_t a = w->create_entity(), b = w->create_entity();
	if (!w->set_vector(a, "position", Vector3(1, 2, 3)) || !w->set_vector(a, "velocity", Vector3(2, 0, 0))) {
		return false;
	}
	w->set_vector(b, "position", Vector3(9, 8, 7));
	w->step(.5);
	if (w->get_vector(a, "position") != Vector3(2, 2, 3) || w->get_vector(b, "position") != Vector3(9, 8, 7)) {
		return false;
	}
	PackedStringArray names;
	names.push_back("position");
	names.push_back("velocity");
	if (w->query(names).size() != 1) {
		return false;
	}
	if (!w->destroy_entity(a) || w->is_alive(a) || w->destroy_entity(a)) {
		return false;
	}
	uint64_t c = w->create_entity();
	if (c == a || w->set_vector(a, "position", Vector3())) {
		return false;
	}
	if (w->get_vector(b, "position") != Vector3(9, 8, 7)) {
		return false; // swap-remove preserves moved row
	}
	if (!w->register_component("health", 4) || w->register_component("health", 8)) {
		return false;
	}
	PackedByteArray bytes;
	bytes.resize(4);
	bytes.set(0, 100);
	if (!w->set_component(c, "health", bytes) || w->get_component(c, "health") != bytes) {
		return false;
	}
	if (!w->remove_component(c, "health") || w->remove_component(c, "health")) {
		return false;
	}
	w->defer_destroy(b);
	w->defer_destroy(b);
	if (!w->is_alive(b)) {
		return false;
	}
	w->flush_commands();
	if (w->is_alive(b) || w->query(PackedStringArray()).size() != 1) {
		return false;
	}
	Ref<ECSSystemTest> system;
	system.instantiate();
	Callable callback = callable_mp(system.ptr(), &ECSSystemTest::tick);
	if (!w->add_system("test", callback) || w->add_system("test", callback)) {
		return false;
	}
	w->step(.01);
	w->step(.01);
	if (system->calls != 2 || !w->remove_system("test") || w->remove_system("test")) {
		return false;
	}
	w->step(.01);
	if (system->calls != 2) {
		return false;
	}
	Vector<uint64_t> ids;
	for (int i = 0; i < 20000; i++) {
		uint64_t id = w->create_entity();
		ids.push_back(id);
		w->set_vector(id, "position", Vector3(i, 1, 2));
		w->set_vector(id, "velocity", Vector3(2, 3, 4));
	}
	w->step(.5);
	for (int i = 0; i < 20000; i++) {
		if (w->get_vector(ids[i], "position") != Vector3(i + 1, 2.5, 4)) {
			return false;
		}
	}
	for (int i = 0; i < 20000; i += 2) {
		w->defer_destroy(ids[i]);
	}
	w->flush_commands();
	w->step(.5);
	for (int i = 1; i < 20000; i += 2) {
		if (w->get_vector(ids[i], "position") != Vector3(i + 2, 4, 6)) {
			return false;
		}
	}
	return w->query(names).size() == 10000 && w->get_transform_buffer().size() == 10000 * 12;
}
void ECSWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_component_batch", "name"), &ECSWorld::get_component_batch);
	ClassDB::bind_method(D_METHOD("set_component_batch", "name", "entities", "bytes"), &ECSWorld::set_component_batch);
	ClassDB::bind_method(D_METHOD("set_camera", "entity", "definition"), &ECSWorld::set_camera);
	ClassDB::bind_method(D_METHOD("get_camera", "entity"), &ECSWorld::get_camera);
	ClassDB::bind_method(D_METHOD("get_active_camera"), &ECSWorld::get_active_camera);
	ClassDB::bind_method(D_METHOD("remove_camera", "entity"), &ECSWorld::remove_camera);
	ClassDB::bind_method(D_METHOD("control_particles", "entity", "action", "time"), &ECSWorld::control_particles, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("get_particles_statistics"), &ECSWorld::get_particles_statistics);
	ClassDB::bind_method(D_METHOD("trigger_effect", "entity", "event"), &ECSWorld::trigger_effect, DEFVAL("play"));
	ClassDB::bind_method(D_METHOD("set_bone_2d", "entity", "definition"), &ECSWorld::set_bone_2d);
	ClassDB::bind_method(D_METHOD("get_bone_2d", "entity"), &ECSWorld::get_bone_2d);
	ClassDB::bind_method(D_METHOD("set_skeleton_2d", "entity", "definition"), &ECSWorld::set_skeleton_2d);
	ClassDB::bind_method(D_METHOD("get_skeleton_2d", "entity"), &ECSWorld::get_skeleton_2d);
	ClassDB::bind_method(D_METHOD("set_skeleton_skin", "entity", "skin"), &ECSWorld::set_skeleton_skin);
	ClassDB::bind_method(D_METHOD("set_skeleton_slot_attachment", "entity", "slot", "attachment"), &ECSWorld::set_skeleton_slot_attachment);
	ClassDB::bind_method(D_METHOD("is_skeleton_attachment_visible", "entity"), &ECSWorld::is_skeleton_attachment_visible);
    ClassDB::bind_method(D_METHOD("set_tilemap_2d","entity","definition"),&ECSWorld::set_tilemap_2d);
    ClassDB::bind_method(D_METHOD("get_tilemap_2d","entity"),&ECSWorld::get_tilemap_2d);
    ClassDB::bind_method(D_METHOD("remove_tilemap_2d","entity"),&ECSWorld::remove_tilemap_2d);
    ClassDB::bind_method(D_METHOD("tilemap_statistics"),&ECSWorld::tilemap_statistics);
    ClassDB::bind_method(D_METHOD("tilemap_navigation_path","start","end","layers"),&ECSWorld::tilemap_navigation_path,DEFVAL(1));
	ClassDB::bind_method(D_METHOD("set_polygon_2d", "entity", "definition"), &ECSWorld::set_polygon_2d);
	ClassDB::bind_method(D_METHOD("get_polygon_2d", "entity"), &ECSWorld::get_polygon_2d);
	ClassDB::bind_method(D_METHOD("get_deformed_polygon_2d", "entity"), &ECSWorld::get_deformed_polygon_2d);
	ClassDB::bind_method(D_METHOD("set_particles", "entity", "definition"), &ECSWorld::set_particles);
	ClassDB::bind_method(D_METHOD("get_particles", "entity"), &ECSWorld::get_particles);
	ClassDB::bind_method(D_METHOD("remove_particles", "entity"), &ECSWorld::remove_particles);
	ClassDB::bind_method(D_METHOD("set_light", "entity", "definition"), &ECSWorld::set_light);
	ClassDB::bind_method(D_METHOD("get_light", "entity"), &ECSWorld::get_light);
	ClassDB::bind_method(D_METHOD("remove_light", "entity"), &ECSWorld::remove_light);
	ClassDB::bind_method(D_METHOD("set_navigation", "entity", "mesh", "layers"), &ECSWorld::set_navigation, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("get_navigation", "entity"), &ECSWorld::get_navigation);
	ClassDB::bind_method(D_METHOD("remove_navigation", "entity"), &ECSWorld::remove_navigation);
	ClassDB::bind_method(D_METHOD("navigate_to", "entity", "target", "speed", "tolerance", "layers"), &ECSWorld::navigate_to, DEFVAL(1.0), DEFVAL(0.1), DEFVAL(1));
	ClassDB::bind_method(D_METHOD("cancel_navigation", "entity"), &ECSWorld::cancel_navigation);
	ClassDB::bind_method(D_METHOD("get_navigation_follow", "entity"), &ECSWorld::get_navigation_follow);
	ADD_SIGNAL(MethodInfo("navigation_reached", PropertyInfo(Variant::INT, "entity")));
	ADD_SIGNAL(MethodInfo("navigation_failed", PropertyInfo(Variant::INT, "entity")));
	ClassDB::bind_method(D_METHOD("is_navigation_ready"), &ECSWorld::is_navigation_ready);
	ClassDB::bind_method(D_METHOD("find_path", "from", "to", "layers"), &ECSWorld::find_path, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("set_audio_listener", "transform"), &ECSWorld::set_audio_listener);
	ClassDB::bind_method(D_METHOD("get_audio_pitch", "entity"), &ECSWorld::get_audio_pitch);
	ClassDB::bind_method(D_METHOD("get_audio_gain", "entity"), &ECSWorld::get_audio_gain);
	ClassDB::bind_method(D_METHOD("set_audio", "entity", "definition"), &ECSWorld::set_audio);
	ClassDB::bind_method(D_METHOD("get_audio", "entity"), &ECSWorld::get_audio);
	ClassDB::bind_method(D_METHOD("remove_audio", "entity"), &ECSWorld::remove_audio);
	ADD_SIGNAL(MethodInfo("audio_finished", PropertyInfo(Variant::INT, "entity")));
	ClassDB::bind_method(D_METHOD("intersect_ray", "from", "to", "mask", "exclude"), &ECSWorld::intersect_ray, DEFVAL(UINT32_MAX), DEFVAL(PackedInt64Array()));
	ClassDB::bind_method(D_METHOD("intersect_shape", "shape", "transform", "mask", "exclude", "max_results"), &ECSWorld::intersect_shape, DEFVAL(UINT32_MAX), DEFVAL(PackedInt64Array()), DEFVAL(32));
	ClassDB::bind_method(D_METHOD("migrate_component", "name", "stride", "entities", "bytes"), &ECSWorld::migrate_component);
	ClassDB::bind_method(D_METHOD("set_animation", "entity", "definition"), &ECSWorld::set_animation);
	ClassDB::bind_method(D_METHOD("get_animation", "entity"), &ECSWorld::get_animation);
	ClassDB::bind_method(D_METHOD("set_animation_layer_weight", "entity", "weight"), &ECSWorld::set_animation_layer_weight);
	ClassDB::bind_method(D_METHOD("set_animation_blend_position", "entity", "position"), &ECSWorld::set_animation_blend_position);
	ClassDB::bind_method(D_METHOD("get_root_motion", "entity"), &ECSWorld::get_root_motion);
	ClassDB::bind_method(D_METHOD("travel_animation", "entity", "state", "blend_duration"), &ECSWorld::travel_animation, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("remove_animation", "entity"), &ECSWorld::remove_animation);
	ClassDB::bind_method(D_METHOD("set_skeleton", "entity", "skin", "bones"), &ECSWorld::set_skeleton);
	ClassDB::bind_method(D_METHOD("get_skeleton", "entity"), &ECSWorld::get_skeleton);
	ClassDB::bind_method(D_METHOD("remove_skeleton", "entity"), &ECSWorld::remove_skeleton);
	ADD_SIGNAL(MethodInfo("animation_finished", PropertyInfo(Variant::INT, "entity")));
	ADD_SIGNAL(MethodInfo("animation_event", PropertyInfo(Variant::INT,"entity"), PropertyInfo(Variant::STRING,"name"), PropertyInfo(Variant::DICTIONARY,"data")));
	ClassDB::bind_method(D_METHOD("set_pin_joint", "entity", "definition"), &ECSWorld::set_pin_joint);
	ClassDB::bind_method(D_METHOD("get_pin_joint", "entity"), &ECSWorld::get_pin_joint);
	ClassDB::bind_method(D_METHOD("remove_pin_joint", "entity"), &ECSWorld::remove_pin_joint);
	ClassDB::bind_method(D_METHOD("set_joint_3d", "entity", "definition"), &ECSWorld::set_joint_3d);
	ClassDB::bind_method(D_METHOD("get_joint_3d", "entity"), &ECSWorld::get_joint_3d);
	ClassDB::bind_method(D_METHOD("remove_joint_3d", "entity"), &ECSWorld::remove_joint_3d);
	ClassDB::bind_method(D_METHOD("set_joint_2d", "entity", "definition"), &ECSWorld::set_joint_2d);
	ClassDB::bind_method(D_METHOD("get_joint_2d", "entity"), &ECSWorld::get_joint_2d);
	ClassDB::bind_method(D_METHOD("remove_joint_2d", "entity"), &ECSWorld::remove_joint_2d);
	ClassDB::bind_method(D_METHOD("set_area", "entity", "definition"), &ECSWorld::set_area);
	ClassDB::bind_method(D_METHOD("get_area", "entity"), &ECSWorld::get_area);
	ClassDB::bind_method(D_METHOD("remove_area", "entity"), &ECSWorld::remove_area);
	ClassDB::bind_method(D_METHOD("get_area_overlaps", "entity"), &ECSWorld::get_area_overlaps);
	ADD_SIGNAL(MethodInfo("area_body_entered", PropertyInfo(Variant::INT, "area"), PropertyInfo(Variant::INT, "body")));
	ADD_SIGNAL(MethodInfo("area_body_exited", PropertyInfo(Variant::INT, "area"), PropertyInfo(Variant::INT, "body")));
	ClassDB::bind_method(D_METHOD("set_area_2d", "entity", "definition"), &ECSWorld::set_area_2d);
	ClassDB::bind_method(D_METHOD("get_area_2d", "entity"), &ECSWorld::get_area_2d);
	ClassDB::bind_method(D_METHOD("remove_area_2d", "entity"), &ECSWorld::remove_area_2d);
	ClassDB::bind_method(D_METHOD("get_area_overlaps_2d", "entity"), &ECSWorld::get_area_overlaps_2d);
	ADD_SIGNAL(MethodInfo("area_2d_body_entered", PropertyInfo(Variant::INT, "area_2d"), PropertyInfo(Variant::INT, "body")));
	ADD_SIGNAL(MethodInfo("area_2d_body_exited", PropertyInfo(Variant::INT, "area_2d"), PropertyInfo(Variant::INT, "body")));
	ClassDB::bind_method(D_METHOD("set_physics_2d", "entity", "definition"), &ECSWorld::set_physics_2d);
	ClassDB::bind_method(D_METHOD("get_physics_2d", "entity"), &ECSWorld::get_physics_2d);
	ClassDB::bind_method(D_METHOD("remove_physics_2d", "entity"), &ECSWorld::remove_physics_2d);
	ClassDB::bind_method(D_METHOD("apply_impulse_2d", "entity", "impulse"), &ECSWorld::apply_impulse_2d);
	ClassDB::bind_method(D_METHOD("move_and_collide_2d", "entity", "motion"), &ECSWorld::move_and_collide_2d);
	ClassDB::bind_method(D_METHOD("move_and_slide_2d", "entity", "velocity", "delta", "max_slides"), &ECSWorld::move_and_slide_2d, DEFVAL(4));
	ClassDB::bind_method(D_METHOD("intersect_ray_2d", "from", "to", "mask", "exclude"), &ECSWorld::intersect_ray_2d, DEFVAL(UINT32_MAX), DEFVAL(PackedInt64Array()));
	ClassDB::bind_method(D_METHOD("set_physics", "entity", "definition"), &ECSWorld::set_physics);
	ClassDB::bind_method(D_METHOD("get_physics", "entity"), &ECSWorld::get_physics);
	ClassDB::bind_method(D_METHOD("remove_physics", "entity"), &ECSWorld::remove_physics);
	ClassDB::bind_method(D_METHOD("apply_impulse", "entity", "impulse"), &ECSWorld::apply_impulse);
	ClassDB::bind_method(D_METHOD("apply_torque_impulse", "entity", "impulse"), &ECSWorld::apply_torque_impulse);
	ClassDB::bind_method(D_METHOD("apply_torque_impulse_2d", "entity", "impulse"), &ECSWorld::apply_torque_impulse_2d);
	ClassDB::bind_method(D_METHOD("move_and_collide", "entity", "motion"), &ECSWorld::move_and_collide);
	ClassDB::bind_method(D_METHOD("move_and_slide", "entity", "velocity", "delta", "max_slides"), &ECSWorld::move_and_slide, DEFVAL(4));
	ClassDB::bind_method(D_METHOD("carry_platform", "entity", "platform", "previous", "stop_platform"), &ECSWorld::carry_platform, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("move_grounded", "entity", "velocity", "delta", "step_height", "snap", "floor_angle"), &ECSWorld::move_grounded, DEFVAL(0.35), DEFVAL(0.4), DEFVAL(0.785398));
	ClassDB::bind_method(D_METHOD("get_physics_contacts_2d"), &ECSWorld::get_physics_contacts_2d);
	ADD_SIGNAL(MethodInfo("body_2d_contact_started", PropertyInfo(Variant::INT, "entity"), PropertyInfo(Variant::INT, "other")));
	ADD_SIGNAL(MethodInfo("body_2d_contact_ended", PropertyInfo(Variant::INT, "entity"), PropertyInfo(Variant::INT, "other")));
	ClassDB::bind_method(D_METHOD("get_physics_contacts"), &ECSWorld::get_physics_contacts);
	ClassDB::bind_method(D_METHOD("set_ui", "entity", "definition"), &ECSWorld::set_ui);
	ClassDB::bind_method(D_METHOD("get_ui", "entity"), &ECSWorld::get_ui);
	ClassDB::bind_method(D_METHOD("remove_ui", "entity"), &ECSWorld::remove_ui);
	ClassDB::bind_method(D_METHOD("get_ui_events"), &ECSWorld::get_ui_events);
	ADD_SIGNAL(MethodInfo("ui_event", PropertyInfo(Variant::INT, "entity"), PropertyInfo(Variant::STRING_NAME, "type"), PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT)));
	ClassDB::bind_method(D_METHOD("set_parent", "entity", "parent"), &ECSWorld::set_parent);
	ClassDB::bind_method(D_METHOD("get_parent", "entity"), &ECSWorld::get_parent);
	ClassDB::bind_method(D_METHOD("get_global_transform", "entity"), &ECSWorld::get_global_transform);
	ClassDB::bind_method(D_METHOD("set_mesh", "entity", "mesh", "material"), &ECSWorld::set_mesh, DEFVAL(Ref<Material>()));
	ClassDB::bind_method(D_METHOD("get_mesh", "entity"), &ECSWorld::get_mesh);
	ClassDB::bind_method(D_METHOD("get_material", "entity"), &ECSWorld::get_material);
	ClassDB::bind_method(D_METHOD("serialize"), &ECSWorld::serialize);
	ClassDB::bind_method(D_METHOD("create_entities", "count"), &ECSWorld::create_entities);
	ClassDB::bind_method(D_METHOD("set_vectors", "entities", "name", "values"), &ECSWorld::set_vectors);
	ClassDB::bind_method(D_METHOD("create_entity"), &ECSWorld::create_entity);
	ClassDB::bind_method(D_METHOD("is_alive", "entity"), &ECSWorld::is_alive);
	ClassDB::bind_method(D_METHOD("destroy_entity", "entity"), &ECSWorld::destroy_entity);
	ClassDB::bind_method(D_METHOD("defer_destroy", "entity"), &ECSWorld::defer_destroy);
	ClassDB::bind_method(D_METHOD("flush_commands"), &ECSWorld::flush_commands);
	ClassDB::bind_method(D_METHOD("register_component", "name", "stride"), &ECSWorld::register_component);
	ClassDB::bind_method(D_METHOD("set_component", "entity", "name", "bytes"), &ECSWorld::set_component);
	ClassDB::bind_method(D_METHOD("get_component", "entity", "name"), &ECSWorld::get_component);
	ClassDB::bind_method(D_METHOD("remove_component", "entity", "name"), &ECSWorld::remove_component);
	ClassDB::bind_method(D_METHOD("query", "components", "include_inactive"), &ECSWorld::query, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_active", "entity", "active"), &ECSWorld::set_active);
	ClassDB::bind_method(D_METHOD("is_active_self", "entity"), &ECSWorld::is_active_self);
	ClassDB::bind_method(D_METHOD("is_active_in_hierarchy", "entity"), &ECSWorld::is_active_in_hierarchy);
	ClassDB::bind_method(D_METHOD("set_vector", "entity", "name", "value"), &ECSWorld::set_vector);
	ClassDB::bind_method(D_METHOD("get_vector", "entity", "name"), &ECSWorld::get_vector);
	ClassDB::bind_method(D_METHOD("add_system", "name", "callback"), &ECSWorld::add_system);
	ClassDB::bind_method(D_METHOD("remove_system", "name"), &ECSWorld::remove_system);
	ClassDB::bind_method(D_METHOD("step", "delta"), &ECSWorld::step);
	ClassDB::bind_method(D_METHOD("get_statistics"), &ECSWorld::get_statistics);
	ClassDB::bind_method(D_METHOD("inspect_entities", "offset", "limit"), &ECSWorld::inspect_entities);
}

bool ECSWorld::migrate_component(const StringName &name, int stride, const PackedInt64Array &ids, const PackedByteArray &bytes) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (stepping || physics_phase || stride < 1 || stride > 65536 || is_vector_component(name) || is_ui_component(name) || name == StringName("pin_joint") || name == StringName("joint_3d") || name == StringName("joint_2d") || name == StringName("area") || name == StringName("area_2d") || name == StringName("physics_body_2d") || name == StringName("physics_body") || name == StringName("animation") || name == StringName("skeleton") || name == StringName("audio") || name == StringName("navigation") || name == StringName("camera") || name == StringName("light")) {
		return false;
	}
	const Pool *original = pools.getptr(name);
	if (!original || ids.size() != original->entities.size() || int64_t(stride) * ids.size() != bytes.size()) {
		return false;
	}
	Pool replacement;
	replacement.stride = stride;
	replacement.entities.resize(ids.size());
	for (int i = 0; i < ids.size(); i++) {
		uint32_t slot = uint32_t(ids[i]);
		if (!is_alive(ids[i]) || !original->rows.has(slot) || replacement.rows.has(slot)) {
			return false;
		}
		replacement.entities.write[i] = slot;
		replacement.rows[slot] = i;
	}
	replacement.bytes.resize(bytes.size());
	if (!bytes.is_empty()) {
		memcpy(replacement.bytes.ptrw(), bytes.ptr(), bytes.size());
	}
	// Commit after validation; a rejected schema change leaves every old row intact.
	pools[name] = replacement;
	return true;
}

Transform3D ECSWorld::calculate_global_transform(uint64_t id) const {
	// Sparse server queries must not rebuild transforms for unrelated entities.
	Transform3D result;
	const Pool &positions = pools["position"], &rotations = pools["rotation"], &scales = pools["scale"], &shears = pools["shear"];
	while (id) {
		uint32_t index = uint32_t(id);
		const int *rotation = rotations.rows.getptr(index), *scale = scales.rows.getptr(index), *position = positions.rows.getptr(index);
		Basis basis = rotation ? Basis::from_euler(read_vector(rotations, *rotation)) : Basis();
		if (const int *row = shears.rows.getptr(index)) {
			Vector3 shear=read_vector(shears,*row);
			basis *= Basis(Vector3(Math::cos(shear.x),Math::sin(shear.x),0),Vector3(-Math::sin(shear.y),Math::cos(shear.y),0),Vector3(0,0,1));
		}
		if (scale) {
			basis.scale_local(read_vector(scales, *scale));
		}
		result = Transform3D(basis, position ? read_vector(positions, *position) : Vector3()) * result;
		const uint64_t *parent = parents.getptr(index);
		id = parent ? *parent : 0;
	}
	return result;
}

Dictionary ECSWorld::get_component_batch(const StringName &name) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const Pool *pool = pools.getptr(name);
	if (!pool || is_ui_component(name) || name == StringName("pin_joint") || name == StringName("joint_3d") || name == StringName("joint_2d") || name == StringName("area") || name == StringName("area_2d") || name == StringName("physics_body_2d") || name == StringName("physics_body") || name == StringName("animation") || name == StringName("skeleton") || name == StringName("audio") || name == StringName("navigation") || name == StringName("camera") || name == StringName("light")) {
		return Dictionary();
	}
	PackedInt64Array ids;
	ids.resize(pool->entities.size());
	for (int i = 0; i < ids.size(); i++) {
		uint32_t index = pool->entities[i];
		ids.set(i, (uint64_t(slots[index].generation) << 32) | index);
	}
	PackedByteArray bytes;
	bytes.resize(pool->bytes.size());
	if (!bytes.is_empty()) {
		memcpy(bytes.ptrw(), pool->bytes.ptr(), bytes.size());
	}
	Dictionary result;
	result["entities"] = ids;
	result["bytes"] = bytes;
	result["stride"] = pool->stride;
	return result;
}
bool ECSWorld::set_component_batch(const StringName &name, const PackedInt64Array &ids, const PackedByteArray &bytes) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	Pool *pool = pools.getptr(name);
	if (!pool || int64_t(pool->stride) * ids.size() != bytes.size() || is_ui_component(name) || name == StringName("pin_joint") || name == StringName("joint_3d") || name == StringName("joint_2d") || name == StringName("area") || name == StringName("area_2d") || name == StringName("physics_body_2d") || name == StringName("physics_body") || name == StringName("animation") || name == StringName("skeleton") || name == StringName("audio") || name == StringName("navigation") || name == StringName("camera") || name == StringName("light")) {
		return false;
	}
	HashSet<uint32_t> seen;
	int added = 0;
	for (int i = 0; i < ids.size(); i++) {
		uint32_t index = uint32_t(ids[i]);
		if (!is_alive(ids[i]) || seen.has(index)) {
			return false;
		}
		seen.insert(index);
		if (!pool->rows.has(index)) {
			added++;
		}
		if (is_vector_component(name)) {
			real_t values[3];
			memcpy(values, bytes.ptr() + i * pool->stride, sizeof(values));
			if (!Vector3(values[0], values[1], values[2]).is_finite()) {
				return false;
			}
		}
	}
	if ((int64_t(pool->entities.size()) + added) * pool->stride > INT32_MAX) {
		return false;
	}
	// Validate the entire batch before changing any row.
	for (int i = 0; i < ids.size(); i++) {
		uint32_t index = uint32_t(ids[i]);
		int row;
		if (const int *existing = pool->rows.getptr(index)) {
			row = *existing;
		} else {
			row = pool->entities.size();
			pool->entities.push_back(index);
			pool->rows[index] = row;
		}
	}
	pool->bytes.resize(pool->entities.size() * pool->stride);
	for (int i = 0; i < ids.size(); i++) {
		int row = pool->rows[uint32_t(ids[i])];
		memcpy(pool->bytes.ptrw() + row * pool->stride, bytes.ptr() + i * pool->stride, pool->stride);
	}
	transforms_dirty = true;
	return true;
}
