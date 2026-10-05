// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "servers/navigation_3d/navigation_server_3d.h"

bool ECSWorld::set_navigation(uint64_t id, const Ref<NavigationMesh> &mesh, uint32_t layers) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id) || mesh.is_null()) {
		return false;
	}
	auto *server = NavigationServer3D::get_singleton();
	if (!server) {
		return false;
	}
	Transform3D transform = calculate_global_transform(id);
	if (!transform.is_finite() || Math::is_zero_approx(transform.basis.determinant())) {
		return false;
	}
	if (!navigation_map.is_valid()) {
		navigation_cell_size = mesh->get_cell_size();
		navigation_cell_height = mesh->get_cell_height();
		navigation_map = server->map_create();
		server->map_set_active(navigation_map, true);
		server->map_set_cell_size(navigation_map, mesh->get_cell_size());
		server->map_set_cell_height(navigation_map, mesh->get_cell_height());
	}
	if (!Math::is_equal_approx(navigation_cell_size, mesh->get_cell_size()) || !Math::is_equal_approx(navigation_cell_height, mesh->get_cell_height())) {
		return false;
	}
	RID rid = server->region_create();
	server->region_set_navigation_mesh(rid, mesh);
	server->region_set_navigation_layers(rid, layers);
	server->region_set_transform(rid, transform);
	server->region_set_map(rid, navigation_map);
	remove_navigation(id);
	navigation_regions[uint32_t(id)] = { mesh, rid, layers, transform };
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "navigation", &marker);
	if (!slots[uint32_t(id)].active) { apply_activation(uint32_t(id)); }
	return true;
}
Dictionary ECSWorld::get_navigation(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const NavigationRegion *region = is_alive(id) ? navigation_regions.getptr(uint32_t(id)) : nullptr;
	Dictionary result;
	if (region) {
		result["mesh"] = region->mesh;
		result["layers"] = region->layers;
	}
	return result;
}
bool ECSWorld::remove_navigation(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	NavigationRegion *region = is_alive(id) ? navigation_regions.getptr(uint32_t(id)) : nullptr;
	if (!region) {
		return false;
	}
	if (auto *server = NavigationServer3D::get_singleton()) {
		server->free_rid(region->rid);
	}
	navigation_regions.erase(uint32_t(id));
	remove_row(pools["navigation"], uint32_t(id));
	return true;
}
void ECSWorld::clear_navigation() {
	if (auto *server = NavigationServer3D::get_singleton()) {
		for (const auto &entry : navigation_regions) {
			server->free_rid(entry.value.rid);
		}
		if (navigation_map.is_valid()) {
			server->free_rid(navigation_map);
		}
	}
	navigation_regions.clear();
	navigation_map = RID();
	if (Pool *pool = pools.getptr("navigation")) {
		pool->rows.clear();
		pool->entities.clear();
		pool->bytes.clear();
	}
}
void ECSWorld::sync_navigation() {
	auto *server = NavigationServer3D::get_singleton();
	for (auto &entry : navigation_regions) {
		auto &region = entry.value;
		Transform3D transform = calculate_global_transform((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		if (transform.is_finite() && !Math::is_zero_approx(transform.basis.determinant()) && !transform.is_equal_approx(region.transform)) {
			server->region_set_transform(region.rid, transform);
			region.transform = transform;
		}
	}
}
bool ECSWorld::is_navigation_ready() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	auto *server = NavigationServer3D::get_singleton();
	return server && navigation_map.is_valid() && server->map_get_iteration_id(navigation_map) > 0;
}
PackedVector3Array ECSWorld::find_path(const Vector3 &from, const Vector3 &to, uint32_t layers) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedVector3Array());
	if (!from.is_finite() || !to.is_finite() || !is_navigation_ready()) {
		return PackedVector3Array();
	}
	return NavigationServer3D::get_singleton()->map_get_path(navigation_map, from, to, true, layers);
}

// Navigation commands are transient runtime state, not persisted scene components.
bool ECSWorld::navigate_to(uint64_t id, const Vector3 &target, double speed, double tolerance, uint32_t layers) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id) || get_parent(id) || (physics_bodies.has(uint32_t(id)) && physics_bodies[uint32_t(id)].mode != 1) || physics_bodies_2d.has(uint32_t(id)) || !target.is_finite() || !Math::is_finite(speed) || speed <= 0 || !Math::is_finite(tolerance) || tolerance <= 0 || layers == 0) {
		return false;
	}
	NavigationFollow state;
	state.target = target;
	state.speed = speed;
	state.tolerance = tolerance;
	state.layers = layers;
	navigation_followers[uint32_t(id)] = state;
	return true;
}
bool ECSWorld::cancel_navigation(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	return is_alive(id) && navigation_followers.erase(uint32_t(id));
}
Dictionary ECSWorld::get_navigation_follow(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const NavigationFollow *state = is_alive(id) ? navigation_followers.getptr(uint32_t(id)) : nullptr;
	Dictionary result;
	if (state) {
		result["target"] = state->target;
		result["speed"] = state->speed;
		result["tolerance"] = state->tolerance;
		result["layers"] = state->layers;
		result["status"] = state->status;
		result["path"] = state->path;
		result["waypoint"] = state->waypoint;
	}
	return result;
}
void ECSWorld::step_navigation_followers(double delta) {
	struct Completion {
		uint64_t entity;
		bool reached;
	};
	Vector<Completion> finished;
	auto *server = NavigationServer3D::get_singleton();
	uint64_t iteration = is_navigation_ready() ? server->map_get_iteration_id(navigation_map) : 0;
	for (auto &entry : navigation_followers) {
		if (!slots[entry.key].active) { continue; }
		NavigationFollow &state = entry.value;
		if (state.status == "reached" || state.status == "failed") {
			continue;
		}
		uint64_t id = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
		if (get_parent(id) || (physics_bodies.has(entry.key) && physics_bodies[entry.key].mode != 1) || physics_bodies_2d.has(entry.key)) {
			state.status = "failed";
			finished.push_back({ id, false });
			continue;
		}
		if (!iteration) {
			continue;
		}
		bool kinematic = physics_bodies.has(entry.key);
		if (kinematic && !physics_phase) {
			continue;
		}
		Vector3 position = get_vector(id, "position");
		Vector3 previous_position = position;
		if (state.iteration != iteration || state.status == "waiting") {
			state.path = find_path(position, state.target, state.layers);
			state.waypoint = 0;
			state.iteration = iteration;
			// A map may have an initial iteration while its region update is still
			// being synchronized. Allow a bounded grace period for an empty path.
			if (state.path.is_empty() && state.waiting_time < 1.0) {
				state.waiting_time += delta;
				state.status = "waiting";
				continue;
			}
			if (state.path.is_empty() || state.path[state.path.size() - 1].distance_to(state.target) > state.tolerance) {
				state.status = "failed";
				finished.push_back({ id, false });
				continue;
			}
			state.status = "moving";
		}
		double remaining = state.speed * delta;
		if (!Math::is_finite(remaining)) {
			state.status = "failed";
			finished.push_back({ id, false });
			continue;
		}
		while (state.waypoint < state.path.size()) {
			Vector3 next = state.path[state.waypoint];
			double distance = position.distance_to(next);
			if (distance < CMP_EPSILON) {
				state.waypoint++;
				continue;
			}
			Vector3 motion = (next - position) * (MIN(distance, remaining) / distance);
			if (kinematic) {
				Dictionary result = move_and_collide(id, motion);
				position = get_vector(id, "position");
				if (result.is_empty() || bool(result.get("collided", false))) {
					break;
				}
			} else {
				position += motion;
			}
			if (distance <= remaining) {
				remaining -= distance;
				state.waypoint++;
			} else {
				break;
			}
		}
		if (kinematic && delta > 0 && position.distance_to(previous_position) < MAX(double(CMP_EPSILON), state.speed * delta * 0.1) && state.waypoint < state.path.size()) {
			state.blocked_time += delta;
			if (state.blocked_time >= 2.0) {
				state.status = "failed";
				finished.push_back({ id, false });
			}
		} else {
			state.blocked_time = 0;
		}
		set_vector(id, "position", position);
		if (state.waypoint >= state.path.size()) {
			state.status = "reached";
			finished.push_back({ id, true });
		}
	}
	// Notify after iteration: callbacks can cancel, retarget or destroy entities.
	for (const Completion &event : finished) {
		const NavigationFollow *state = is_alive(event.entity) ? navigation_followers.getptr(uint32_t(event.entity)) : nullptr;
		if (state && state->status == (event.reached ? "reached" : "failed")) {
			emit_signal(event.reached ? "navigation_reached" : "navigation_failed", event.entity);
		}
	}
}
