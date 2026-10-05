#pragma once
#include "core/object/ref_counted.h"
#include "scene/resources/animation.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// Private pose records, not ECS entities and not Nodes. One instance per actor.
class ECSCompactSkeleton : public RefCounted {
public:
	struct Part {
		String name;
		int parent = -1;
		Vector3 position, rotation, scale = Vector3(1, 1, 1), shear;
		Transform2D global;
		Dictionary polygon;
		RID item, mesh;
		Ref<Material> material;
		bool active = true;
		bool atlas_rect = false;
	};
	Vector<Part> parts;
	Vector<int> order;
	PackedInt64Array bones, targets;
	Array bind_poses, constraints;
	Dictionary definition, rig, animation, states;
	Ref<Animation> clip;
	Vector<Variant> blend_from;
	double time = 0, speed = 1, blend_duration = 0, blend_elapsed = 0;
	bool playing = true, event_start = true;
	RID skeleton;
	bool load(const Dictionary &p_definition);
	bool set_animation(const Dictionary &p_patch);
	Dictionary get_animation() const;
	bool play(const StringName &p_name, bool p_loop, double p_blend);
	Array step(double p_delta, bool &r_finished);
	void update_pose();
	void sample_pose();
	void solve_ik();
	int find_bone(const String &p_name) const;
	bool set_skin(const String &p_skin);
	bool set_attachment(const String &p_slot, const String &p_attachment);
	Variant get_channel(int p_target, const String &p_path) const;
	void set_channel(int p_target, const String &p_path, const Variant &p_value);
	PackedVector2Array polygon_points(int p_part) const;
	void draw(RID p_canvas, const Transform2D &p_root, bool p_visible);
	~ECSCompactSkeleton();
};

// Packs a standalone skeleton authoring resource. Rejects unrelated gameplay components.
Array ecs_pack_skeleton_entities(const Array &p_entities);
