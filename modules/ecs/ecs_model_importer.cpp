#ifdef TOOLS_ENABLED
#include "ecs_model_importer.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/resources/packed_scene.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/animation/animation_player.h"
#include "scene/resources/mesh.h"

namespace {
struct ModelConversion {
	Array entities;
	HashMap<Node *, int> nodes;
	HashMap<Skeleton3D *, PackedInt64Array> bones;
	Vector<Node *> ordered;
	String error;

	bool transform(Dictionary &entity, const Transform3D &value) {
		if (!value.is_finite() || Math::is_zero_approx(value.basis.determinant())) {
			error = "Model contains a non-finite or singular transform.";
			return false;
		}
		Vector3 scale = value.basis.get_scale();
		Basis rotation = value.basis.orthonormalized();
		if (rotation.determinant() < 0) rotation = rotation.scaled(Vector3(-1, -1, -1));
		if (!rotation.scaled_local(scale).is_equal_approx(value.basis)) {
			error = "Model contains shear; bake the object transform before import.";
			return false;
		}
		entity["position"] = value.origin;
		entity["rotation"] = rotation.get_euler();
		entity["scale"] = scale;
		return true;
	}

	bool visit(Node *node, int parent) {
		if (entities.size() >= 100000) { error = "Model exceeds the entity import limit."; return false; }
		Dictionary entity;
		entity["name"] = String(node->get_name());
		entity["parent"] = parent;
		Node3D *spatial = Object::cast_to<Node3D>(node);
		if (!transform(entity, spatial ? spatial->get_transform() : Transform3D())) return false;
		entity["active"] = !spatial || spatial->is_visible();
		const int index = entities.size();
		nodes[node] = index;
		ordered.push_back(node);
		entities.push_back(entity);
		if (auto *rig = Object::cast_to<Skeleton3D>(node)) {
			if (entities.size() + rig->get_bone_count() > 100000) { error = "Model exceeds the entity import limit."; return false; }
			PackedInt64Array indices;
			for (int b = 0; b < rig->get_bone_count(); b++) { indices.push_back(entities.size()); entities.push_back(Dictionary()); }
			bones[rig] = indices;
			for (int b = 0; b < indices.size(); b++) {
				Dictionary bone;
				bone["name"] = String(rig->get_bone_name(b));
				int parent_bone = rig->get_bone_parent(b);
				bone["parent"] = parent_bone < 0 ? index : int(indices[parent_bone]);
				if (!transform(bone, rig->get_bone_pose(b))) return false;
				entities[int(indices[b])] = bone;
			}
		}
		for (int c = 0; c < node->get_child_count(); c++) if (!visit(node->get_child(c), index)) return false;
		return true;
	}

	bool convert() {
		for (Node *node : ordered) {
			Dictionary entity = entities[nodes[node]];
			if (auto *instance = Object::cast_to<MeshInstance3D>(node)) {
				Ref<Mesh> mesh = instance->get_mesh();
				if (mesh.is_null()) continue;
				if (mesh->get_blend_shape_count()) { error = "Blend shape animation is not supported by ECS model import yet."; return false; }
				bool overrides = false;
				for (int s = 0; s < mesh->get_surface_count(); s++) overrides |= instance->get_surface_override_material(s).is_valid();
				if (overrides) {
					Ref<ArrayMesh> copy = mesh->duplicate();
					if (copy.is_null()) { error = "Cannot copy mesh surface materials."; return false; }
					for (int s = 0; s < mesh->get_surface_count(); s++) if (instance->get_surface_override_material(s).is_valid()) copy->surface_set_material(s, instance->get_surface_override_material(s));
					mesh = copy;
				}
				entity["mesh"] = mesh;
				if (instance->get_material_override().is_valid()) entity["material"] = instance->get_material_override();
				Ref<Skin> skin = instance->get_skin();
				if (skin.is_valid()) {
					auto *rig = Object::cast_to<Skeleton3D>(instance->get_node_or_null(instance->get_skeleton_path()));
					if (!rig || !bones.has(rig)) { error = "Cannot resolve model skeleton."; return false; }
					PackedInt64Array indices;
					for (int b = 0; b < skin->get_bind_count(); b++) {
						int bone = skin->get_bind_name(b) != StringName() ? rig->find_bone(skin->get_bind_name(b)) : skin->get_bind_bone(b);
						if (bone < 0 || bone >= bones[rig].size()) { error = "Invalid skin bone binding."; return false; }
						indices.push_back(bones[rig][bone]);
					}
					Dictionary skeleton;
					skeleton["skin"] = skin;
					skeleton["bones"] = indices;
					entity["skeleton"] = skeleton;
				}
			}
			entities[nodes[node]] = entity;
			if (auto *player = Object::cast_to<AnimationPlayer>(node)) {
				LocalVector<StringName> names;
				player->get_animation_list(&names);
				bool first = true;
				Node *animation_root = player->get_node_or_null(player->get_root_node());
				for (const StringName &name : names) {
					if (name == StringName("RESET")) continue;
					Ref<Animation> source = player->get_animation(name), clip = source->duplicate();
					PackedInt64Array targets;
					for (int t = clip->get_track_count() - 1; t >= 0; t--) if (!clip->track_is_enabled(t)) clip->remove_track(t);
					for (int t = 0; t < clip->get_track_count(); t++) {
						auto type = clip->track_get_type(t);
						if (type != Animation::TYPE_POSITION_3D && type != Animation::TYPE_ROTATION_3D && type != Animation::TYPE_SCALE_3D) { error = "Unsupported model animation track: " + String(name); return false; }
						NodePath path = clip->track_get_path(t);
						Node *target = animation_root ? animation_root->get_node_or_null(NodePath(path.get_concatenated_names())) : nullptr;
						if (!target || !nodes.has(target)) { error = "Cannot resolve animation target: " + String(path); return false; }
						int index = nodes[target];
						if (path.get_subname_count()) {
							auto *rig = Object::cast_to<Skeleton3D>(target);
							int bone = rig ? rig->find_bone(path.get_subname(0)) : -1;
							if (!rig || bone < 0 || path.get_subname_count() != 1) { error = "Invalid animation bone target."; return false; }
							index = bones[rig][bone];
						}
						targets.push_back(index);
						clip->track_set_path(t, NodePath(type == Animation::TYPE_POSITION_3D ? ":position" : type == Animation::TYPE_ROTATION_3D ? ":rotation" : ":scale"));
					}
					if (targets.is_empty()) continue;
					Dictionary animation, owner;
					animation["clip"] = clip; animation["targets"] = targets; animation["playing"] = first;
					owner["name"] = String(name); owner["parent"] = nodes[node]; owner["animation"] = animation;
					entities.push_back(owner);
					first = false;
				}
			}
		}
		return true;
	}
};
}

bool ECSModelImporter::accepts(const Variant &drag) {
	if (drag.get_type() != Variant::DICTIONARY) return false;
	Dictionary data = drag;
	if (String(data.get("type", "")) != "files") return false;
	PackedStringArray files = data.get("files", PackedStringArray());
	if (files.is_empty()) return false;
	for (const String &path : files) if (!PackedStringArray({ "blend", "glb", "gltf", "fbx" }).has(path.get_extension().to_lower())) return false;
	return true;
}

bool ECSModelImporter::append(const String &path, Array &output, int parent, String &error, const Ref<ECSScene> &context) {
	if (parent < -1 || parent >= output.size()) { error = "Invalid destination parent."; return false; }
	Ref<PackedScene> packed = ResourceLoader::load(path);
	if (packed.is_null()) { error = "Model resource is not imported: " + path; return false; }
	Node *root = packed->instantiate(PackedScene::GEN_EDIT_STATE_DISABLED);
	if (!root) { error = "Cannot read model scene."; return false; }
	ModelConversion conversion;
	bool ok = conversion.visit(root, -1) && conversion.convert();
	memdelete(root);
	if (!ok) { error = conversion.error; return false; }
	int offset = output.size();
	Array next = output.duplicate(true);
	for (int i = 0; i < conversion.entities.size(); i++) {
		Dictionary entity = conversion.entities[i];
		int local_parent = entity.get("parent", -1);
		entity["parent"] = local_parent < 0 ? parent : local_parent + offset;
		for (const String &kind : { String("skeleton"), String("animation") }) {
			if (!entity.has(kind)) continue;
			Dictionary component = entity[kind];
			String key = kind == "skeleton" ? "bones" : "targets";
			PackedInt64Array references = component[key];
			for (int r = 0; r < references.size(); r++) references.set(r, references[r] + offset);
			component[key] = references; entity[kind] = component;
		}
		next.push_back(entity);
	}
	Ref<ECSScene> validation;
	validation.instantiate();
	if (context.is_valid()) { validation->set_custom_schemas(context->get_custom_schemas()); validation->set_layouts(context->get_layouts()); }
	validation->set_entities(next);
	if (validation->instantiate().is_null()) { error = "Converted model failed ECS validation."; return false; }
	output = next;
	return true;
}
bool ECSModelImporter::test() {
	for (const String &extension : { String("blend"), String("glb") }) {
		Array entities;
		Dictionary parent;
		parent["name"] = "ImportedModels";
		entities.push_back(parent);
		String error;
		if (!append("res://Assets/BlenderProbe." + extension, entities, 0, error)) { print_error(error); return false; }
		int meshes = 0, skins = 0, animations = 0;
		for (int i = 1; i < entities.size(); i++) {
			Dictionary entity = entities[i];
			if (int(entity.get("parent", -1)) < 0) return false;
			if (entity.has("mesh")) { meshes++; Ref<Mesh> mesh = entity["mesh"]; if (mesh->get_surface_count() == 0 || mesh->surface_get_material(0).is_null()) return false; }
			if (entity.has("skeleton")) skins++;
			if (entity.has("animation")) animations++;
		}
		if (meshes != 3 || skins != 1 || animations < 1) { print_error(vformat("Unexpected components %d %d %d", meshes, skins, animations)); return false; }
		Ref<ECSScene> scene;
		scene.instantiate(); scene->set_entities(entities);
		String saved = "res://Converted_" + extension + ".tres";
		if (ResourceSaver::save(scene, saved) != OK) return false;
		Ref<ECSScene> loaded = ResourceLoader::load(saved, "ECSScene", ResourceLoader::CACHE_MODE_IGNORE);
		if (loaded.is_null()) return false;
		Ref<ECSWorld> world = loaded->instantiate();
		if (world.is_null() || world->query(PackedStringArray({ "skeleton" })).size() != 1) return false;
		PackedInt64Array ids = world->query(PackedStringArray());
		Vector<Transform3D> before;
		for (int64_t id : ids) before.push_back(world->get_global_transform(id));
		world->advance_animation_preview(0.45);
		bool moved = false;
		for (int i = 0; i < ids.size(); i++) moved |= !before[i].is_equal_approx(world->get_global_transform(ids[i]));
		if (!moved) { print_error("Imported animation did not move any entity."); return false; }
		Array unchanged = entities.duplicate(true);
		if (append("res://Assets/BlenderProbe." + extension, entities, entities.size(), error) || entities != unchanged) return false;
		print_line("ECS_MODEL_FIXTURE_PASS " + extension + " materials hierarchy skin animation save_reload atomic_failure");
	}
	return true;
}
#endif
