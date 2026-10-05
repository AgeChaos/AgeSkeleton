// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene.h"
#include "ecs_ui_system.h"

#include "scene/gui/control.h"
#include "scene/main/viewport.h"
class EditorZoomWidget;
class Button;
class ECSUICanvasEditor : public Control {
	GDCLASS(ECSUICanvasEditor, Control);
	Ref<ECSScene> scene;
	Ref<ECSWorld> preview;
	PackedInt64Array ids;
	ECSUISystem renderer;
	SubViewport *viewport = nullptr;
	EditorZoomWidget *zoom_widget = nullptr;
	Button *fit_button = nullptr;
	Button *tool_buttons[6] = {};
	int tool_mode = 4;
	bool creating_bone=false;
	Vector2 create_bone_start,create_bone_end;
    int tilemap_source=-1,tilemap_alternative=0;
    Vector2i tilemap_atlas, tilemap_last_cell;
    bool tilemap_painting=false,tilemap_erasing=false,tilemap_last_valid=false;
    Dictionary tilemap_before,tilemap_work;
    bool tilemap_input(const Ref<InputEvent> &p_event);
    void tilemap_paint_at(const Vector2 &p_position);
    void finish_tilemap(bool p_commit);
	int axis_space=0;
	HashSet<int> authoring_hidden,authoring_locked;
	void sync_authoring_preview();
	bool show_bones=true,show_images=true,pick_bones=true,pick_images=true;
	Control *legacy_toolbar=nullptr;
	Control *zoom_bar_control=nullptr;
	Size2 authoring_view_size;
	void set_tool(int p_tool);
	void set_preview_zoom(float p_scale);
	void fit_canvas();
	Size2 canvas_size = Size2(800, 600);
	int selected = -1;
	bool dragging = false, resizing = false;
	bool bone_drag = false;
	Callable setup_pose_callback;
	Array setup_pose_after;
	void preview_setup_pose(const Array &p_entities);
	bool runtime_preview = false;
	Vector3 bone_drag_value;
	Vector2 bone_drag_origin;
	int resize_corner = 2;
	int move_axis = -1;
	int transform_handle = -1;
	Vector2 selected_pivot();
	Vector2 selected_axis(int p_axis) const;
	Vector2 drag_axis_direction;
	Vector2 drag_start;
	Rect2 drag_rect;
	float zoom = 1;
	Vector2 pan;
	bool panning = false;
	bool grid_visible = true;
	bool skeleton_authoring=false, keyframe_edit_mode=false, center_origin_pending=false;
	bool mesh_edit_mode=false;
	int mesh_vertex=-1;
	Dictionary mesh_before,mesh_work;
	Transform2D mesh_vertex_transform(int p_vertex) const;
	void finish_mesh(bool p_commit);
	void edit_mesh_topology(const Vector2 &p_position,bool p_remove);
	int weight_bone=-1;
	float weight_radius=60,weight_strength=.15;
	bool painting=false;
	Vector2 brush_cursor;
	Dictionary stroke_before,stroke_data;
	void paint_at(const Vector2 &p_position,bool p_subtract);
	void finish_paint(bool p_commit);
	float display_scale() const;
	void finish_drag(bool p_commit);
	void draw_rulers();
	void draw_origin_guides();
	void capture_visual_test();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
    void set_authoring_item_state(int p_index,bool p_hidden,bool p_locked);
    bool is_authoring_hidden(int p_index) const { return authoring_hidden.has(p_index); }
    bool is_authoring_locked(int p_index) const { return authoring_locked.has(p_index); }
	void set_setup_pose_callback(const Callable &p_callback) { setup_pose_callback=p_callback; }
	void configure_tilemap_brush(int p_source,Vector2i p_atlas,int p_alternative=0) { finish_tilemap(false); tilemap_source=p_source; tilemap_atlas=p_atlas; tilemap_alternative=p_alternative; }
	Vector2 get_canvas_mouse_position() const { return (get_local_mouse_position()-pan)/display_scale(); }
	void set_mesh_edit_mode(bool p_enabled) { finish_mesh(false); mesh_edit_mode=p_enabled; queue_redraw(); }
	void configure_weight_brush(int p_bone,float p_radius,float p_strength) { finish_paint(false); weight_bone=p_bone; weight_radius=p_radius; weight_strength=p_strength; queue_redraw(); }
	static Dictionary paint_weights(const Dictionary &p_polygon,const Transform3D &p_transform,const Vector2 &p_center,float p_radius,int p_bone,float p_strength);
	void preview_authoring_vector(int p_index,const String &p_field,const Vector3 &p_value) { if(preview.is_valid() && p_index>=0 && p_index<ids.size()) { preview->set_vector(ids[p_index],p_field,p_value); sync_authoring_preview(); queue_redraw(); } }
	Vector3 get_authoring_vector(int p_index,const String &p_field) const { return preview.is_valid() && p_index>=0 && p_index<ids.size()?preview->get_vector(ids[p_index],p_field):Vector3(); }
	void set_skeleton_authoring(bool p_enabled);
	void set_authoring_tool(int p_tool) { set_tool(p_tool); }
	void set_axis_space(int p_space) { finish_drag(false); axis_space=p_space; queue_redraw(); }
	void set_authoring_option(bool p_enabled,int p_option);
	void set_keyframe_edit_mode(bool p_enabled) { if(skeleton_authoring && keyframe_edit_mode!=p_enabled) { center_origin_pending=true; } keyframe_edit_mode=p_enabled; }
	void update_runtime_preview(const Array &p_entities);
	void update_runtime_definition(int p_index,const Dictionary &p_definition);
	Ref<Image> capture_canvas() const { return viewport->get_texture()->get_image(); }
	bool preview_animation(int p_owner, double p_time, bool p_playing, const String &p_state = String());
	void pause_animation_preview(int p_owner) { if (preview.is_valid() && p_owner >= 0 && p_owner < ids.size()) { Dictionary patch; patch["playing"] = false; preview->set_animation(ids[p_owner], patch); } }
	void stop_animation_preview();
	bool transition_animation_preview(int p_owner,const String &p_state,double p_duration);
	bool is_grid_visible() const { return grid_visible; }
	void set_grid_visible(bool p_visible) { grid_visible = p_visible; queue_redraw(); }
	void edit_scene(const Ref<ECSScene> &p_scene, int p_selected);
	bool can_drop_data(const Point2 &p_point,const Variant &p_data) const override;
	void drop_data(const Point2 &p_point,const Variant &p_data) override;
	void gui_input(const Ref<InputEvent> &p_event) override;
	Rect2 get_selected_rect();
	bool run_self_test();
	bool run_tilemap_self_test(const Ref<ECSScene> &p_scene);
	bool run_mesh_edit_self_test(const Ref<ECSScene> &p_scene);
	bool run_compensated_drag_self_test(const Ref<ECSScene> &p_scene);
	bool run_weight_brush_self_test(const Ref<ECSScene> &p_scene);
	void start_visual_test();
	ECSUICanvasEditor();
	~ECSUICanvasEditor();
};
#endif
