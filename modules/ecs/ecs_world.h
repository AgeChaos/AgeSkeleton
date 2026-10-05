// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "ecs_ui_types.h"

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"
#include "scene/resources/3d/shape_3d.h"
#include "scene/resources/3d/skin.h"
#include "scene/resources/animation.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/navigation_mesh.h"
#include "servers/audio/audio_stream.h"

// Single-owner World. Systems execute on the owning thread; structural commands
// are applied only at explicit barriers. POD columns never contain managed pointers.
class ECSWorld : public RefCounted {
	GDCLASS(ECSWorld, RefCounted);
	struct Slot {
		uint32_t generation = 1;
		bool alive = false;
		bool active_self = true;
		bool active = true;
	};
	struct Pool {
		int stride = 0;
		Vector<uint32_t> entities;
		HashMap<uint32_t, int> rows;
		Vector<uint8_t> bytes;
	};
	struct Visual {
		Ref<Mesh> mesh;
		Ref<Material> material;
	};
	void refresh_activation(uint32_t p_root);
	void apply_activation(uint32_t p_index);
	void sync_joint_activation();
	HashMap<uint32_t, uint64_t> parents;
	HashMap<uint32_t, HashSet<uint32_t>> children;
	HashMap<uint32_t, Visual> visuals;
	struct PhysicsBody {
		RID rid;
		Dictionary definition;
		int mode = 0;
		Transform3D submitted_transform;
		Vector3 submitted_velocity;
	};
	struct AnimationState {
        bool event_start_pending=true, has_event_tracks=false;
		Ref<Animation> clip;
		Ref<Animation> secondary_clip;
		Dictionary states;
		Dictionary blend_space;
		Dictionary graph;
		Array effect_events;
		int root_motion_track = -1;
		Ref<Animation> root_clip, root_secondary_clip;
		Vector3 root_motion;
		StringName current_state;
		double secondary_time = 0, secondary_speed = 1, secondary_weight = 0;
		PackedFloat32Array secondary_weights;
		bool secondary_additive = false;
		double secondary_reference_time = 0;
		Vector<Variant> secondary_reference;
		PackedInt64Array targets;
		double time = 0, speed = 1;
		double blend_duration = 0, blend_elapsed = 0;
		Vector<Variant> blend_from;
		bool playing = true;
	};
	struct SkeletonState {
		Ref<Skin> skin;
		PackedInt64Array bones;
		RID rid;
	};
	HashMap<uint32_t, AnimationState> animations;
	HashMap<uint32_t, SkeletonState> skeletons;
	struct AudioState {
		Ref<AudioStream> stream;
		Ref<AudioStreamPlayback> playback;
		Dictionary definition;
		Vector3 previous_source, previous_listener;
		uint64_t listener_entity = 0;
		bool motion_valid = false;
		double effective_pitch = 1;
	};
	HashMap<uint32_t, AudioState> audio_sources;
	Transform3D audio_listener;
	Vector2 calculate_audio_gain(uint64_t p_entity, const Dictionary &p_definition) const;
	struct NavigationRegion {
		Ref<NavigationMesh> mesh;
		RID rid;
		uint32_t layers = 1;
		Transform3D transform;
	};
	RID navigation_map;
	real_t navigation_cell_size = 0, navigation_cell_height = 0;
	HashMap<uint32_t, NavigationRegion> navigation_regions;
	struct ParticlePoint { Vector3 position, velocity; double age = 0; };
	struct ParticleState {
		Dictionary definition;
		Vector<ParticlePoint> points;
		PackedFloat32Array render_buffer;
		Ref<Mesh> mesh;
		Ref<Material> material;
		RID multimesh, instance;
		uint32_t random = 1;
		double time = 0;
		bool paused = false;
	};
	HashMap<uint32_t, ParticleState> particles;
	struct Skeleton2DState { Dictionary definition; RID skeleton; HashMap<uint64_t,Dictionary> attachments; };
	bool prepare_skeleton_slots(Dictionary &p_definition,HashMap<uint64_t,Dictionary> &r_attachments) const;
	Ref<Material> skeleton_slot_materials[4];
	struct Polygon2DState { Dictionary definition; RID item; RID mesh; Ref<Material> tint_material; };
	HashMap<uint32_t, Dictionary> bones_2d;
	HashMap<uint32_t, Skeleton2DState> skeletons_2d;
	HashMap<uint32_t, Polygon2DState> polygons_2d;
	struct TilemapChunk { PackedInt32Array cells; Vector<RID> items, bodies, regions; Vector<Transform2D> region_transforms; };
    struct TilemapState { Dictionary definition; HashMap<Vector2i, TilemapChunk> chunks; bool dirty=true, rebuild_all=true, submitted=false, submitted_active=false; Transform2D submitted_transform; };
    HashMap<uint32_t, TilemapState> tilemaps_2d;
    RID tilemap_canvas, tilemap_viewport, tilemap_navigation_map;
    void tilemap_resource_changed(uint64_t p_entity);
    void free_tilemap_chunk(TilemapChunk &p_chunk);
    void rebuild_tilemap(uint64_t p_entity);
    void sync_tilemaps_2d(RID p_viewport);
    void clear_tilemaps_2d();
	RID skeleton_canvas;
	RID skeleton_viewport;
	void step_particles(double p_delta);
	uint64_t particles_step_usec = 0, particles_upload_usec = 0;
	HashMap<uint32_t, Dictionary> cameras;
	struct LightState {
		Dictionary definition;
		RID light, instance, scenario;
		Transform3D transform;
	};
	HashMap<uint32_t, LightState> lights;
	struct NavigationFollow {
		Vector3 target;
		PackedVector3Array path;
		int waypoint = 0;
		double speed = 1, tolerance = 0.1, waiting_time = 0, blocked_time = 0;
		uint32_t layers = 1;
		uint64_t iteration = 0;
		String status = "waiting";
	};
	HashMap<uint32_t, NavigationFollow> navigation_followers;
	void step_navigation_followers(double p_delta);
	void sync_navigation();
	void step_audio(double p_delta);
	void step_animations(double p_delta);
	void remove_animation_references(uint64_t p_entity);
	RID physics_space;
	HashMap<uint32_t, PhysicsBody> physics_bodies;
	HashMap<RID, uint64_t> physics_entities;
	struct PhysicsBody2D {
		RID rid;
		Dictionary definition;
		int mode = 0;
		Transform2D submitted_transform;
		Vector2 submitted_velocity;
	};
	RID physics_space_2d;
	HashMap<uint32_t, PhysicsBody2D> physics_bodies_2d;
	HashMap<RID, uint64_t> physics_entities_2d;
	Transform2D calculate_transform_2d(uint64_t p_entity) const;
	void sync_physics_2d();
	void submit_physics_2d();
	void clear_physics_2d();
	struct PinJointState {
		RID rid;
		Dictionary definition;
		uint64_t body_a = 0, body_b = 0;
		bool suspended = false;
	};
	HashMap<uint32_t, PinJointState> pin_joints;
	void remove_joint_references(uint64_t p_body);
	void clear_joints();
	HashMap<uint32_t, PinJointState> joints_3d;
	void remove_joint_references_3d(uint64_t p_body);
	void clear_joints_3d();
	HashMap<uint32_t, PinJointState> joints_2d;
	void remove_joint_references_2d(uint64_t p_body);
	void clear_joints_2d();
	struct AreaState {
		RID rid;
		Dictionary definition;
		Transform3D submitted_transform;
		HashMap<uint64_t, int> overlaps;
	};
	struct AreaEvent {
		uint64_t owner = 0;
		RID area, body;
		bool entered = false;
		uint64_t removed_entity = 0;
	};
	HashMap<uint32_t, AreaState> areas;
	Mutex area_event_mutex;
	Vector<AreaEvent> area_events;
	void area_body_changed(int p_status, const RID &p_body, ObjectID p_instance, int p_body_shape, int p_area_shape, uint64_t p_owner, RID p_area);
	void sync_areas();
	void submit_areas();
	void clear_areas();
	struct AreaState2D {
		RID rid;
		Dictionary definition;
		Transform2D submitted_transform;
		HashMap<uint64_t, int> overlaps;
	};
	struct AreaEvent2D {
		uint64_t owner = 0;
		RID area, body;
		bool entered = false;
		uint64_t removed_entity = 0;
	};
	HashMap<uint32_t, AreaState2D> areas_2d;
	Mutex area_event_mutex_2d;
	Vector<AreaEvent2D> area_events_2d;
	void area_body_changed_2d(int p_status, const RID &p_body, ObjectID p_instance, int p_body_shape, int p_area_shape, uint64_t p_owner, RID p_area);
	void sync_areas_2d();
	void submit_areas_2d();
	void clear_areas_2d();
	Array physics_contacts;
	Array physics_contacts_2d;
	HashMap<uint64_t, HashSet<uint64_t>> contact_pairs_2d;
	bool physics_phase = false;
	HashMap<uint32_t, ECSUIText> ui_texts;
	uint64_t ui_revision = 1;
	Array pending_ui_events, current_ui_events;
	uint64_t ui_events_dispatched = 0;
	Dictionary last_ui_event;
	static bool is_ui_component(const StringName &p_name);
	void store_ui_column(uint32_t p_index, const StringName &p_name, const void *p_data);
	void dispatch_ui_events();
	mutable Vector<Transform3D> global_transforms;
	mutable bool transforms_dirty = true;
	void update_transforms() const;
	Transform3D calculate_global_transform(uint64_t p_entity) const;
	static bool is_vector_component(const StringName &p_name);
	Vector<Slot> slots;
	Vector<uint32_t> free_slots;
	HashMap<StringName, Pool> pools;
	Vector<uint64_t> pending_destroy;
	struct System {
		StringName name;
		Callable callback;
	};
	Vector<System> systems;
	bool stepping = false;
	int living = 0;
	Thread::ID owner_thread = Thread::get_caller_id();
	void remove_row(Pool &p_pool, uint32_t p_slot);
	Vector3 read_vector(const Pool &p_pool, int p_row) const;
	void write_vector(Pool &p_pool, int p_row, const Vector3 &p_value);

protected:
	static void _bind_methods();

public:
	bool set_physics_2d(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_physics_2d(uint64_t p_entity) const;
	bool remove_physics_2d(uint64_t p_entity);
	bool apply_impulse_2d(uint64_t p_entity, const Vector2 &p_impulse);
	bool apply_torque_impulse_2d(uint64_t p_entity, double p_impulse);
	Dictionary move_and_collide_2d(uint64_t p_entity, const Vector2 &p_motion);
	Dictionary move_and_slide_2d(uint64_t p_entity, const Vector2 &p_velocity, double p_delta, int p_max_slides);
	Dictionary intersect_ray_2d(const Vector2 &p_from, const Vector2 &p_to, uint32_t p_mask, const PackedInt64Array &p_exclude) const;
	bool set_audio_listener(const Transform3D &p_transform);
	Vector2 get_audio_gain(uint64_t p_entity) const;
	double get_audio_pitch(uint64_t p_entity) const;
	bool set_pin_joint(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_pin_joint(uint64_t p_entity) const;
	bool remove_pin_joint(uint64_t p_entity);
	bool set_joint_3d(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_joint_3d(uint64_t p_entity) const;
	bool remove_joint_3d(uint64_t p_entity);
	bool set_joint_2d(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_joint_2d(uint64_t p_entity) const;
	bool remove_joint_2d(uint64_t p_entity);
	bool set_area(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_area(uint64_t p_entity) const;
	bool remove_area(uint64_t p_entity);
	PackedInt64Array get_area_overlaps(uint64_t p_entity) const;
	bool set_area_2d(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_area_2d(uint64_t p_entity) const;
	bool remove_area_2d(uint64_t p_entity);
	PackedInt64Array get_area_overlaps_2d(uint64_t p_entity) const;
	Dictionary get_component_batch(const StringName &p_name) const;
	bool set_component_batch(const StringName &p_name, const PackedInt64Array &p_entities, const PackedByteArray &p_bytes);
	bool set_particles(uint64_t p_entity, const Dictionary &p_definition);
	void advance_animation_preview(double p_delta) { ERR_FAIL_COND(Thread::get_caller_id()!=owner_thread || !Math::is_finite(p_delta) || p_delta<0); step_animations(p_delta); }
	bool set_bone_2d(uint64_t p_entity,const Dictionary &p_definition);
	Dictionary get_bone_2d(uint64_t p_entity) const;
	void solve_ik_2d();
	bool set_skeleton_2d(uint64_t p_entity,const Dictionary &p_definition);
	bool set_skeleton_skin(uint64_t p_entity,const String &p_skin);
	bool set_skeleton_slot_attachment(uint64_t p_entity,const String &p_slot,const String &p_attachment);
	Dictionary get_skeleton_attachment_style(uint64_t p_entity) const;
	bool is_skeleton_attachment_visible(uint64_t p_entity) const;
	static bool skeleton_slots_self_test();
	static bool skeleton_animation_channels_self_test();
    Variant get_skeleton_slot_value(uint64_t p_entity,const String &p_path) const;
    bool set_skeleton_slot_value(uint64_t p_entity,const String &p_path,const Variant &p_value);
    bool valid_skeletal_animation_value(uint64_t p_entity,const String &p_path,const Variant &p_value) const;
    bool valid_skeletal_animation_track(uint64_t p_entity,const Ref<Animation> &p_clip,int p_track) const;
    static Array sample_animation_events(const Ref<Animation> &p_clip,double p_from,double p_to,bool p_include_start=false);
	static bool remap_skeleton_slots(Dictionary &p_definition,const HashMap<uint64_t,uint64_t> &p_map);
	Dictionary get_skeleton_2d(uint64_t p_entity) const;
    bool set_tilemap_2d(uint64_t p_entity,const Dictionary &p_definition);
    Dictionary get_tilemap_2d(uint64_t p_entity) const;
    bool remove_tilemap_2d(uint64_t p_entity);
    Dictionary tilemap_statistics() const;
    PackedVector2Array tilemap_navigation_path(Vector2 p_start,Vector2 p_end,uint32_t p_layers=1) const;
    static bool tilemap_2d_self_test(const String &p_path);
	bool set_polygon_2d(uint64_t p_entity,const Dictionary &p_definition);
	Dictionary get_polygon_2d(uint64_t p_entity) const;
	bool remove_skeletal_2d(uint64_t p_entity,const StringName &p_kind);
	void clear_skeletal_2d();
	void sync_skeletal_2d(RID p_viewport);
#ifdef TOOLS_ENABLED
	void hide_authoring_polygon(uint64_t p_entity);
#endif
	PackedVector2Array get_deformed_polygon_2d(uint64_t p_entity) const;
	static bool skeletal_2d_self_test(const String &p_path);
	Dictionary get_particles(uint64_t p_entity) const;
	bool remove_particles(uint64_t p_entity);
	void sync_particles(RID p_scenario);
	bool control_particles(uint64_t p_entity, const String &p_action, double p_time = 0);
	Dictionary get_particles_statistics() const;
	bool trigger_effect(uint64_t p_entity, const String &p_event = "play");
	void clear_particles();
	static bool particles_self_test(const String &p_demo_path);
#ifdef TOOLS_ENABLED
	Dictionary particle_preview(uint64_t p_entity) const;
	void apply_particle_preview(uint64_t p_entity, const Dictionary &p_data, RID p_scenario, const Transform3D &p_transform, bool p_visible);
#endif
	bool set_camera(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_camera(uint64_t p_entity) const;
	Dictionary get_active_camera() const;
	bool remove_camera(uint64_t p_entity);
	bool set_light(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_light(uint64_t p_entity) const;
	bool remove_light(uint64_t p_entity);
	void sync_lights(RID p_scenario);
	void clear_lights();
	bool set_navigation(uint64_t p_entity, const Ref<NavigationMesh> &p_mesh, uint32_t p_layers = 1);
	Dictionary get_navigation(uint64_t p_entity) const;
	bool remove_navigation(uint64_t p_entity);
	void clear_navigation();
	PackedVector3Array find_path(const Vector3 &p_from, const Vector3 &p_to, uint32_t p_layers = 1) const;
	bool navigate_to(uint64_t p_entity, const Vector3 &p_target, double p_speed = 1.0, double p_tolerance = 0.1, uint32_t p_layers = 1);
	bool cancel_navigation(uint64_t p_entity);
	Dictionary get_navigation_follow(uint64_t p_entity) const;
	bool is_navigation_ready() const;
	bool set_audio(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_audio(uint64_t p_entity) const;
	bool remove_audio(uint64_t p_entity);
	void clear_audio();
	Dictionary intersect_ray(const Vector3 &p_from, const Vector3 &p_to, uint32_t p_mask, const PackedInt64Array &p_exclude) const;
	PackedInt64Array intersect_shape(const Ref<Shape3D> &p_shape, const Transform3D &p_transform, uint32_t p_mask, const PackedInt64Array &p_exclude, int p_max_results) const;
	bool migrate_component(const StringName &p_name, int p_stride, const PackedInt64Array &p_entities, const PackedByteArray &p_bytes);
	struct RenderBatch {
		Ref<Mesh> mesh;
		Ref<Material> material;
		Vector<float> transforms;
		uint64_t entity = 0;
		RID skeleton;
		Transform3D transform;
	};
	ECSWorld();
	~ECSWorld();
	bool set_animation(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_animation(uint64_t p_entity) const;
	bool remove_animation(uint64_t p_entity);
	bool set_animation_layer_weight(uint64_t p_entity, double p_weight);
	bool set_animation_blend_position(uint64_t p_entity, double p_position);
	Vector3 get_root_motion(uint64_t p_entity) const;
	bool travel_animation(uint64_t p_entity, const StringName &p_state, double p_blend_duration = 0.0);
	bool set_skeleton(uint64_t p_entity, const Ref<Skin> &p_skin, const PackedInt64Array &p_bones);
	Dictionary get_skeleton(uint64_t p_entity) const;
	bool remove_skeleton(uint64_t p_entity);
	void clear_skeletons();
	bool set_physics(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_physics(uint64_t p_entity) const;
	bool remove_physics(uint64_t p_entity);
	bool apply_impulse(uint64_t p_entity, const Vector3 &p_impulse);
	bool apply_torque_impulse(uint64_t p_entity, const Vector3 &p_impulse);
	Dictionary move_and_collide(uint64_t p_entity, const Vector3 &p_motion);
	Dictionary carry_platform(uint64_t p_entity, uint64_t p_platform, const Transform3D &p_previous, bool p_stop_platform = false);
	Dictionary move_grounded(uint64_t p_entity, const Vector3 &p_velocity, double p_delta, double p_step_height = 0.35, double p_snap = 0.4, double p_floor_angle = 0.785398);
	Dictionary move_and_slide(uint64_t p_entity, const Vector3 &p_velocity, double p_delta, int p_max_slides = 4);
	Array get_physics_contacts() const;
	Array get_physics_contacts_2d() const;
	void sync_physics();
	void submit_physics();
	void clear_physics();
	bool set_ui(uint64_t p_entity, const Dictionary &p_definition);
	Dictionary get_ui(uint64_t p_entity) const;
	bool remove_ui(uint64_t p_entity);
	Vector<ECSUIItem> get_ui_items() const;
	uint64_t get_ui_revision() const { return ui_revision; }
	Array get_ui_events() const;
	void queue_ui_event(uint64_t p_entity, const StringName &p_type, const Variant &p_value);
	bool set_parent(uint64_t p_entity, uint64_t p_parent);
	uint64_t get_parent(uint64_t p_entity) const;
	Transform3D get_global_transform(uint64_t p_entity) const;
	bool set_mesh(uint64_t p_entity, const Ref<Mesh> &p_mesh, const Ref<Material> &p_material = Ref<Material>());
	Ref<Mesh> get_mesh(uint64_t p_entity) const;
	Ref<Material> get_material(uint64_t p_entity) const;
	Vector<RenderBatch> get_render_batches(const Ref<Mesh> &p_default_mesh) const;
	Dictionary serialize() const;
	uint64_t create_entity();
	PackedInt64Array create_entities(int p_count);
	bool set_vectors(const PackedInt64Array &p_entities, const StringName &p_name, const PackedVector3Array &p_values);
	bool is_alive(uint64_t p_entity) const;
	bool destroy_entity(uint64_t p_entity);
	void defer_destroy(uint64_t p_entity);
	void flush_commands();
	bool register_component(const StringName &p_name, int p_stride);
	bool set_component(uint64_t p_entity, const StringName &p_name, const PackedByteArray &p_bytes);
	PackedByteArray get_component(uint64_t p_entity, const StringName &p_name) const;
	bool remove_component(uint64_t p_entity, const StringName &p_name);
	bool set_active(uint64_t p_entity, bool p_active);
	bool is_active_self(uint64_t p_entity) const;
	bool is_active_in_hierarchy(uint64_t p_entity) const;
	PackedInt64Array query(const PackedStringArray &p_components, bool p_include_inactive = false) const;
	bool set_vector(uint64_t p_entity, const StringName &p_name, const Vector3 &p_value);
	Vector3 get_vector(uint64_t p_entity, const StringName &p_name) const;
	bool add_system(const StringName &p_name, const Callable &p_callback);
	bool remove_system(const StringName &p_name);
	void step(double p_delta);
	Vector<float> get_transform_buffer() const;
	Dictionary get_statistics() const;
	Array inspect_entities(int p_offset, int p_limit) const;
	bool self_test();
	static bool activation_self_test();
};
