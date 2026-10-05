#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene.h"

#include "scene/gui/control.h"
#include "scene/resources/3d/primitive_meshes.h"

// Transitional editor shell only. All preview objects are native RenderingServer RIDs.
class Button;

class ECSSpatialEditor : public Control {
	GDCLASS(ECSSpatialEditor, Control);
	Ref<ECSScene> scene;
	Ref<ECSWorld> preview;
	bool runtime_preview = false;
	bool animation_preview = false;
	Array runtime_entities;
	int64_t runtime_frame = -1;
	void apply_runtime_entities();
	PackedInt64Array ids;
	Array preview_definitions;
	Vector<RID> instances;
	RID viewport, scenario, camera, light, light_instance;
	Ref<Environment> default_environment;
	Ref<PlaneMesh> grid_mesh;
	Ref<ShaderMaterial> grid_material;
	RID grid_instance;
	Vector2i viewport_size;
	Vector3 grid_center = Vector3(1e20, 0, 1e20);
	float grid_step = 0;
	void update_grid();
	bool grid_visible = true;
	Transform3D camera_transform;
	Vector3 focus;
	float yaw = 0.6, pitch = 0.45, distance = 12;
	int selected = -1, mode = 0, axis = 0;
	bool orbit = false, panning = false, dragging = false;
	bool hand_panning = false;
	int tool_mode = 0;
	bool orthographic = false, view_rotation_locked = false;
	Vector2 navigation_ends[6];
	void draw_navigation();
	bool navigation_click(const Vector2 &p_point);
	void snap_view(int p_axis);
	int drag_plane = -1;
	Vector3 drag_world_origin;
	struct GizmoPlane {
		PackedVector2Array points;
		int normal = 0;
	};
	Vector<GizmoPlane> gizmo_planes;
	Button *tool_buttons[6] = {};
	struct GizmoHandle {
		Vector2 start, end;
		int operation = 0, axis = 0;
	};
	Vector<GizmoHandle> gizmo_handles;
	void set_tool(int p_tool);
	void draw_gizmo();
	bool pick_gizmo(const Vector2 &p_point);
	Vector2 drag_start;
	Vector3 original;
	Array before;
	void clear_preview();
	void update_model_skeletons();
	void update_camera();
	Vector2 project(const Vector3 &p_position) const;
	void finish_drag(bool p_commit);
	void capture_visual();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	bool preview_animation(int p_owner, double p_time, bool p_playing);
	void pause_animation_preview(int p_owner) { if (preview.is_valid() && p_owner >= 0 && p_owner < ids.size()) { Dictionary patch; patch["playing"] = false; preview->set_animation(ids[p_owner], patch); } }
	void update_runtime_definition(int p_index,const Dictionary &p_definition);
	bool test_runtime_drag();
	void update_runtime_preview(const Array &p_entities, int64_t p_frame);
	void stop_runtime_preview();
	Dictionary runtime_preview_status() const;
	Dictionary effect_preview(int p_entity, const String &p_action, double p_time);
	bool is_grid_visible() const { return grid_visible; }
	void set_grid_visible(bool p_visible);
	void edit_scene(const Ref<ECSScene> &p_scene, int p_selected);
	void gui_input(const Ref<InputEvent> &p_event) override;
	bool run_self_test();
	void start_visual_test();
	ECSSpatialEditor();
	~ECSSpatialEditor();
};
#endif
