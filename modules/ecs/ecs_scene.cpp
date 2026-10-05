#include "ecs_custom_components.h"
#include "ecs_ui_components.h"
// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_scene.h"
#include "ecs_tool_adapters.h"

#include "core/object/class_db.h"
#include "scene/resources/3d/navigation_mesh_source_geometry_data_3d.h"
#include "servers/navigation_3d/navigation_server_3d.h"

Ref<ECSWorld> ECSScene::instantiate() const {
	Array entities = get_entities();
	String adapter_error;
	ERR_FAIL_COND_V_MSG(!ECSToolAdapters::compile(entities, adapter_error), Ref<ECSWorld>(), adapter_error);
	ERR_FAIL_COND_V_MSG(entities.size() > 1000000, Ref<ECSWorld>(), "ECS scene exceeds one million entities.");
	// Construct privately. Invalid input never exposes a partially loaded world.
	Ref<ECSWorld> world;
	world.instantiate();
	Array names = layouts.keys();
	for (const Variant &key : names) {
		ERR_FAIL_COND_V_MSG(key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME, Ref<ECSWorld>(), "ECS component name must be a string.");
		Variant stride = layouts[key];
		ERR_FAIL_COND_V_MSG(stride.get_type() != Variant::INT || int64_t(stride) < 1 || int64_t(stride) > 65536, Ref<ECSWorld>(), "Invalid ECS component layout.");
		ERR_FAIL_COND_V_MSG(!world->register_component(key, int(stride)), Ref<ECSWorld>(), "Conflicting ECS component layout.");
	}
	Dictionary schemas = custom_schemas.duplicate(true);
	schemas.merge(ECSCustomComponents::registry(), true);
	PackedInt64Array ids = world->create_entities(entities.size());
	for (int i = 0; i < entities.size(); i++) {
		ERR_FAIL_COND_V_MSG(entities[i].get_type() != Variant::DICTIONARY, Ref<ECSWorld>(), vformat("ECS entity %d must be a dictionary.", i));
		Dictionary entity = entities[i];
		for (const Variant &key : entity.keys()) {
			String name = key;
			if (!ECSCustomComponents::is_component(name)) {
				continue;
			}
			ERR_FAIL_COND_V_MSG(!schemas.has(name) || entity[key].get_type() != Variant::DICTIONARY, Ref<ECSWorld>(), "Missing custom component schema: " + name);
			Dictionary schema = schemas[name];
			PackedByteArray bytes;
			ERR_FAIL_COND_V_MSG(!ECSCustomComponents::pack(schema, entity[key], bytes) || !world->register_component(name, bytes.size()) || !world->set_component(ids[i], name, bytes), Ref<ECSWorld>(), "Invalid custom component: " + name);
		}
		Dictionary ui_definition;
		ERR_FAIL_COND_V_MSG(!ECSUIComponents::compose(entity, ui_definition), Ref<ECSWorld>(), "Invalid ECS UI component combination.");
		if (!ui_definition.is_empty()) {
			ERR_FAIL_COND_V_MSG(!world->set_ui(ids[i], ui_definition), Ref<ECSWorld>(), vformat("Invalid ECS entity %d UI components.", i));
		}
		for (const String &component_name : { String("position"), String("velocity"), String("rotation"), String("scale"), String("shear") }) {
			if (!entity.has(component_name)) {
				continue;
			}
			ERR_FAIL_COND_V_MSG(entity[component_name].get_type() != Variant::VECTOR3 || !world->set_vector(ids[i], component_name, entity[component_name]), Ref<ECSWorld>(), vformat("Invalid ECS entity %d vector %s.", i, component_name));
		}
		Variant raw_components = entity.get("components", Dictionary());
		ERR_FAIL_COND_V(raw_components.get_type() != Variant::DICTIONARY, Ref<ECSWorld>());
		Dictionary components = raw_components;
		for (const Variant &key : components.keys()) {
			ERR_FAIL_COND_V(key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME, Ref<ECSWorld>());
			ERR_FAIL_COND_V_MSG(components[key].get_type() != Variant::PACKED_BYTE_ARRAY || !world->set_component(ids[i], key, components[key]), Ref<ECSWorld>(), vformat("Invalid ECS entity %d POD component.", i));
		}
		Ref<Mesh> mesh;
		Ref<Material> material;
		if (entity.has("mesh")) {
			ERR_FAIL_COND_V(entity["mesh"].get_type() != Variant::OBJECT, Ref<ECSWorld>());
			mesh = entity["mesh"];
			ERR_FAIL_COND_V_MSG(mesh.is_null(), Ref<ECSWorld>(), "ECS mesh must reference a Mesh resource.");
		}
		if (entity.has("material")) {
			ERR_FAIL_COND_V(entity["material"].get_type() != Variant::OBJECT, Ref<ECSWorld>());
			material = entity["material"];
			ERR_FAIL_COND_V_MSG(material.is_null() || mesh.is_null(), Ref<ECSWorld>(), "ECS material requires a Mesh and a Material resource.");
		}
		if (mesh.is_valid()) {
			world->set_mesh(ids[i], mesh, material);
		}
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		Variant parent = entity.get("parent", -1);
		ERR_FAIL_COND_V_MSG(parent.get_type() != Variant::INT || int64_t(parent) < -1 || int64_t(parent) >= entities.size(), Ref<ECSWorld>(), "ECS parent must be -1 or a local entity index.");
		if (int64_t(parent) >= 0) {
			ERR_FAIL_COND_V_MSG(!world->set_parent(ids[i], ids[int(parent)]), Ref<ECSWorld>(), "ECS scene contains a parent cycle.");
		}
	}
	// Resolve references only after all transforms and parent links exist.
	for (const String &kind : { String("bone_2d"), String("skeleton_2d"), String("polygon_2d") }) {
		for (int i = 0; i < entities.size(); i++) {
			Dictionary entity = entities[i]; if (!entity.has(kind)) { continue; }
			ERR_FAIL_COND_V(entity[kind].get_type() != Variant::DICTIONARY, Ref<ECSWorld>());
			Dictionary data = Dictionary(entity[kind]).duplicate(true);
			if (kind == "bone_2d") { ERR_FAIL_COND_V(!world->set_bone_2d(ids[i], data), Ref<ECSWorld>()); }
			else if (kind == "skeleton_2d") {
				ERR_FAIL_COND_V(data.get("bones", Variant()).get_type() != Variant::PACKED_INT64_ARRAY, Ref<ECSWorld>());
				PackedInt64Array bones = data["bones"];
				for (int j = 0; j < bones.size(); j++) { ERR_FAIL_COND_V(bones[j] < 0 || bones[j] >= ids.size(), Ref<ECSWorld>()); bones.set(j, ids[bones[j]]); }
				data["bones"] = bones;
				HashMap<uint64_t,uint64_t> slot_map; for(int k=0;k<ids.size();k++) { slot_map[k]=ids[k]; }
				ERR_FAIL_COND_V(!ECSWorld::remap_skeleton_slots(data,slot_map),Ref<ECSWorld>());
				ERR_FAIL_COND_V(!world->set_skeleton_2d(ids[i], data), Ref<ECSWorld>());
			} else {
				Variant rig = data.get("skeleton", -1);
				ERR_FAIL_COND_V(rig.get_type() != Variant::INT || int64_t(rig) < -1 || int64_t(rig) >= ids.size(), Ref<ECSWorld>());
				data["skeleton"] = int64_t(rig) == -1 ? int64_t(0) : ids[int64_t(rig)];
				ERR_FAIL_COND_V(!world->set_polygon_2d(ids[i], data), Ref<ECSWorld>());
			}
		}
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (entity.has("tilemap_2d")) { ERR_FAIL_COND_V(entity["tilemap_2d"].get_type()!=Variant::DICTIONARY || !world->set_tilemap_2d(ids[i],entity["tilemap_2d"]),Ref<ECSWorld>()); }
		if (entity.has("area_2d")) {
			ERR_FAIL_COND_V(entity["area_2d"].get_type() != Variant::DICTIONARY || !world->set_area_2d(ids[i], entity["area_2d"]), Ref<ECSWorld>());
		}
		if (entity.has("area")) {
			ERR_FAIL_COND_V(entity["area"].get_type() != Variant::DICTIONARY || !world->set_area(ids[i], entity["area"]), Ref<ECSWorld>());
		}
		if (entity.has("physics_2d")) {
			ERR_FAIL_COND_V(entity["physics_2d"].get_type() != Variant::DICTIONARY || !world->set_physics_2d(ids[i], entity["physics_2d"]), Ref<ECSWorld>());
		}
		if (entity.has("physics")) {
			ERR_FAIL_COND_V_MSG(entity["physics"].get_type() != Variant::DICTIONARY || !world->set_physics(ids[i], entity["physics"]), Ref<ECSWorld>(), "Invalid ECS physics component (moving bodies must be roots).");
		}
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (!entity.has("pin_joint")) {
			continue;
		}
		ERR_FAIL_COND_V(entity["pin_joint"].get_type() != Variant::DICTIONARY, Ref<ECSWorld>());
		Dictionary joint = Dictionary(entity["pin_joint"]).duplicate(true);
		for (const String &key : { String("body_a"), String("body_b") }) {
			Variant index = joint.get(key, Variant());
			ERR_FAIL_COND_V(index.get_type() != Variant::INT || int64_t(index) < 0 || int64_t(index) >= ids.size(), Ref<ECSWorld>());
			joint[key] = ids[int64_t(index)];
		}
		ERR_FAIL_COND_V(!world->set_pin_joint(ids[i], joint), Ref<ECSWorld>());
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (!entity.has("joint_3d")) {
			continue;
		}
		ERR_FAIL_COND_V(entity["joint_3d"].get_type() != Variant::DICTIONARY, Ref<ECSWorld>());
		Dictionary joint = Dictionary(entity["joint_3d"]).duplicate(true);
		for (const String &key : { String("body_a"), String("body_b") }) {
			Variant index = joint.get(key, Variant());
			ERR_FAIL_COND_V(index.get_type() != Variant::INT || int64_t(index) < 0 || int64_t(index) >= ids.size(), Ref<ECSWorld>());
			joint[key] = ids[int64_t(index)];
		}
		ERR_FAIL_COND_V(!world->set_joint_3d(ids[i], joint), Ref<ECSWorld>());
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (!entity.has("joint_2d")) {
			continue;
		}
		ERR_FAIL_COND_V(entity["joint_2d"].get_type() != Variant::DICTIONARY, Ref<ECSWorld>());
		Dictionary joint = Dictionary(entity["joint_2d"]).duplicate(true);
		for (const String &key : { String("body_a"), String("body_b") }) {
			Variant index = joint.get(key, Variant());
			ERR_FAIL_COND_V(index.get_type() != Variant::INT || int64_t(index) < 0 || int64_t(index) >= ids.size(), Ref<ECSWorld>());
			joint[key] = ids[int64_t(index)];
		}
		ERR_FAIL_COND_V(!world->set_joint_2d(ids[i], joint), Ref<ECSWorld>());
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		for (const String &kind : { String("animation"), String("skeleton") }) {
			if (!entity.has(kind)) {
				continue;
			}
			ERR_FAIL_COND_V(entity[kind].get_type() != Variant::DICTIONARY, Ref<ECSWorld>());
			Dictionary definition = Dictionary(entity[kind]).duplicate(true);
			String key = kind == "animation" ? "targets" : "bones";
			if (definition.has(key)) {
				ERR_FAIL_COND_V(definition[key].get_type() != Variant::PACKED_INT64_ARRAY, Ref<ECSWorld>());
				PackedInt64Array targets = definition[key];
				for (int j = 0; j < targets.size(); j++) {
					ERR_FAIL_COND_V(targets[j] < 0 || targets[j] >= ids.size(), Ref<ECSWorld>());
					targets.set(j, ids[targets[j]]);
				}
				definition[key] = targets;
			}
			if (kind == "animation") {
				ERR_FAIL_COND_V(!world->set_animation(ids[i], definition), Ref<ECSWorld>());
			} else {
				ERR_FAIL_COND_V(!definition.has("skin") || definition["skin"].get_type() != Variant::OBJECT || !definition.has("bones") || !world->set_skeleton(ids[i], definition["skin"], definition["bones"]), Ref<ECSWorld>());
			}
		}
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (entity.has("audio")) {
			ERR_FAIL_COND_V(entity["audio"].get_type() != Variant::DICTIONARY || !world->set_audio(ids[i], entity["audio"]), Ref<ECSWorld>());
		}
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (!entity.has("navigation")) {
			continue;
		}
		ERR_FAIL_COND_V(entity["navigation"].get_type() != Variant::DICTIONARY, Ref<ECSWorld>());
		Dictionary data = entity["navigation"];
		Variant layers = data.get("layers", 1);
		ERR_FAIL_COND_V(!data.has("mesh") || data["mesh"].get_type() != Variant::OBJECT || layers.get_type() != Variant::INT || int64_t(layers) < 0 || uint64_t(int64_t(layers)) > UINT32_MAX, Ref<ECSWorld>());
		ERR_FAIL_COND_V(!world->set_navigation(ids[i], data["mesh"], uint32_t(int64_t(layers))), Ref<ECSWorld>());
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (entity.has("camera")) {
			ERR_FAIL_COND_V(entity["camera"].get_type() != Variant::DICTIONARY || !world->set_camera(ids[i], entity["camera"]), Ref<ECSWorld>());
		}
		if (entity.has("particles")) {
			ERR_FAIL_COND_V(entity["particles"].get_type() != Variant::DICTIONARY || !world->set_particles(ids[i], entity["particles"]), Ref<ECSWorld>());
		}
		if (entity.has("light")) {
			ERR_FAIL_COND_V(entity["light"].get_type() != Variant::DICTIONARY || !world->set_light(ids[i], entity["light"]), Ref<ECSWorld>());
		}
	}
	for (int i = 0; i < entities.size(); i++) {
		Variant active = Dictionary(entities[i]).get("active", true);
		ERR_FAIL_COND_V_MSG(active.get_type() != Variant::BOOL, Ref<ECSWorld>(), "Entity active must be boolean.");
		world->set_active(ids[i], active);
	}
	return world;
}
bool ECSScene::capture(const Ref<ECSWorld> &world) {
	ERR_FAIL_COND_V(world.is_null(), false);
	Dictionary data = world->serialize();
	if (!data.has("entities") || !data.has("layouts")) {
		return false;
	}
	entities = data["entities"];
	layouts = data["layouts"];
	emit_changed();
	return true;
}
Ref<NavigationMesh> ECSScene::prepare_navigation(int region, const PackedInt32Array &sources, Ref<NavigationMeshSourceGeometryData3D> &geometry) const {
	if (region < 0 || region >= entities.size() || entities[region].get_type() != Variant::DICTIONARY) {
		return Ref<NavigationMesh>();
	}
	Dictionary owner = entities[region];
	Variant definition = owner.get("navigation", Dictionary());
	if (definition.get_type() != Variant::DICTIONARY) {
		return Ref<NavigationMesh>();
	}
	Variant settings = Dictionary(definition).get("mesh", Variant());
	Ref<NavigationMesh> mesh;
	if (settings.get_type() == Variant::NIL) {
		mesh.instantiate();
	} else if (settings.get_type() == Variant::OBJECT && Ref<NavigationMesh>(settings).is_valid()) {
		mesh = Ref<NavigationMesh>(settings)->duplicate();
	} else {
		return Ref<NavigationMesh>();
	}
	// Build only transform data. Baking must not start scene physics or audio.
	Ref<ECSWorld> world;
	world.instantiate();
	PackedInt64Array ids = world->create_entities(entities.size());
	for (int i = 0; i < entities.size(); i++) {
		if (entities[i].get_type() != Variant::DICTIONARY) {
			return Ref<NavigationMesh>();
		}
		Dictionary entity = entities[i];
		for (const String &key : { String("position"), String("rotation"), String("scale"), String("shear") }) {
			if (entity.has(key) && (entity[key].get_type() != Variant::VECTOR3 || !world->set_vector(ids[i], key, entity[key]))) {
				return Ref<NavigationMesh>();
			}
		}
	}
	for (int i = 0; i < entities.size(); i++) {
		Variant parent = Dictionary(entities[i]).get("parent", -1);
		if (parent.get_type() != Variant::INT || int64_t(parent) < -1 || int64_t(parent) >= ids.size()) {
			return Ref<NavigationMesh>();
		}
		if (int64_t(parent) >= 0 && !world->set_parent(ids[i], ids[int64_t(parent)])) {
			return Ref<NavigationMesh>();
		}
	}
	Transform3D region_transform = world->get_global_transform(ids[region]);
	if (!region_transform.is_finite() || Math::is_zero_approx(region_transform.basis.determinant())) {
		return Ref<NavigationMesh>();
	}
	Transform3D inverse = region_transform.affine_inverse();
	HashSet<int> selected;
	for (int index : sources) {
		if (index < 0 || index >= ids.size()) {
			return Ref<NavigationMesh>();
		}
		selected.insert(index);
	}
	geometry.instantiate();
	for (int i = 0; i < entities.size(); i++) {
		if (!sources.is_empty() && !selected.has(i)) {
			continue;
		}
		Variant value = Dictionary(entities[i]).get("mesh", Variant());
		if (value.get_type() == Variant::NIL) {
			continue;
		}
		if (value.get_type() != Variant::OBJECT || Ref<Mesh>(value).is_null()) {
			return Ref<NavigationMesh>();
		}
		Transform3D source_transform = inverse * world->get_global_transform(ids[i]);
		if (!source_transform.is_finite() || Math::is_zero_approx(source_transform.basis.determinant())) {
			return Ref<NavigationMesh>();
		}
		geometry->add_mesh(value, source_transform);
	}
	auto *server = NavigationServer3D::get_singleton();
	if (!server || !geometry->has_data()) {
		return Ref<NavigationMesh>();
	}
	mesh->clear_polygons();
	return mesh;
}
Ref<NavigationMesh> ECSScene::bake_navigation(int region, const PackedInt32Array &sources) const {
	Ref<NavigationMeshSourceGeometryData3D> geometry;
	Ref<NavigationMesh> mesh = prepare_navigation(region, sources, geometry);
	if (mesh.is_null()) {
		return mesh;
	}
	NavigationServer3D::get_singleton()->bake_from_source_geometry_data(mesh, geometry);
	return mesh->get_polygon_count() > 0 ? mesh : Ref<NavigationMesh>();
}
int64_t ECSScene::start_navigation_bake(int region, const PackedInt32Array &sources) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), 0);
	auto *server = NavigationServer3D::get_singleton();
	if (!server || (pending_navigation_mesh.is_valid() && server->is_baking_navigation_mesh(pending_navigation_mesh))) {
		return 0;
	}
	Ref<NavigationMeshSourceGeometryData3D> geometry;
	Ref<NavigationMesh> mesh = prepare_navigation(region, sources, geometry);
	if (mesh.is_null() || navigation_bake_id == INT64_MAX) {
		return 0;
	}
	pending_navigation_mesh = mesh;
	navigation_bake_canceled = false;
	navigation_bake_id++;
	server->bake_from_source_geometry_data_async(mesh, geometry);
	return navigation_bake_id;
}
Dictionary ECSScene::get_navigation_bake(int64_t request) const {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), Dictionary());
	Dictionary result;
	if (request <= 0 || request != navigation_bake_id || pending_navigation_mesh.is_null()) {
		return result;
	}
	auto *server = NavigationServer3D::get_singleton();
	bool running = server && server->is_baking_navigation_mesh(pending_navigation_mesh);
	result["request"] = request;
	result["running"] = running;
	result["status"] = navigation_bake_canceled ? "canceled" : running ? "baking"
			: pending_navigation_mesh->get_polygon_count() > 0		   ? "ready"
																	   : "failed";
	if (!navigation_bake_canceled && !running && pending_navigation_mesh->get_polygon_count() > 0) {
		result["mesh"] = pending_navigation_mesh;
	}
	return result;
}
bool ECSScene::cancel_navigation_bake(int64_t request) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), false);
	if (request <= 0 || request != navigation_bake_id || pending_navigation_mesh.is_null() || navigation_bake_canceled) {
		return false;
	}
	navigation_bake_canceled = true;
	return true;
}
void ECSScene::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_custom_schemas", "schemas"), &ECSScene::set_custom_schemas);
	ClassDB::bind_method(D_METHOD("get_custom_schemas"), &ECSScene::get_custom_schemas);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "custom_schemas"), "set_custom_schemas", "get_custom_schemas");
	ClassDB::bind_method(D_METHOD("start_navigation_bake", "region", "sources"), &ECSScene::start_navigation_bake, DEFVAL(PackedInt32Array()));
	ClassDB::bind_method(D_METHOD("get_navigation_bake", "request"), &ECSScene::get_navigation_bake);
	ClassDB::bind_method(D_METHOD("cancel_navigation_bake", "request"), &ECSScene::cancel_navigation_bake);
	ClassDB::bind_method(D_METHOD("bake_navigation", "region", "sources"), &ECSScene::bake_navigation, DEFVAL(PackedInt32Array()));
	ClassDB::bind_method(D_METHOD("set_camera_transform", "transform"), &ECSScene::set_camera_transform);
	ClassDB::bind_method(D_METHOD("get_camera_transform"), &ECSScene::get_camera_transform);
	ClassDB::bind_method(D_METHOD("set_camera_fov", "fov"), &ECSScene::set_camera_fov);
	ClassDB::bind_method(D_METHOD("get_camera_fov"), &ECSScene::get_camera_fov);
	ClassDB::bind_method(D_METHOD("set_environment", "environment"), &ECSScene::set_environment);
	ClassDB::bind_method(D_METHOD("get_environment"), &ECSScene::get_environment);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "camera_transform"), "set_camera_transform", "get_camera_transform");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "camera_fov", PROPERTY_HINT_RANGE, "1,179,0.1"), "set_camera_fov", "get_camera_fov");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "environment", PROPERTY_HINT_RESOURCE_TYPE, "Environment"), "set_environment", "get_environment");
	ClassDB::bind_method(D_METHOD("set_entities", "entities"), &ECSScene::set_entities);
	ClassDB::bind_method(D_METHOD("get_entities"), &ECSScene::get_entities);
	ClassDB::bind_method(D_METHOD("set_layouts", "layouts"), &ECSScene::set_layouts);
	ClassDB::bind_method(D_METHOD("get_layouts"), &ECSScene::get_layouts);
	ClassDB::bind_method(D_METHOD("instantiate"), &ECSScene::instantiate);
	ClassDB::bind_method(D_METHOD("capture", "world"), &ECSScene::capture);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "entities"), "set_entities", "get_entities");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "layouts"), "set_layouts", "get_layouts");
}
