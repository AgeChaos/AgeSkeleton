#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene_editor.h"
#include "ecs_uv_editor.h"
#include "ecs_animation_graph_editor.h"
#include "editor/inspector/editor_resource_picker.h"

// Authoring tools operate on ECSScene data. No adapter gameplay nodes are created.
class ECSComponentTool : public VBoxContainer {
	GDCLASS(ECSComponentTool, VBoxContainer);
	Ref<ECSScene> scene;
	Ref<ECSSceneEntityEditor> proxy;
	String component;
	int entity = -1;
	EditorInspector *fields = nullptr;
	ECSUICanvasEditor *view = nullptr;
	ECSSpatialEditor *spatial_view = nullptr;
	ECSUVEditor *uv_view = nullptr;
	ECSAnimationGraphEditor *graph_view=nullptr;
	void graph_changed(const Dictionary &p_graph);
	void uv_changed(const Dictionary &p_polygon);
	Label *status = nullptr;
	OptionButton *mode = nullptr;
	LineEdit *state_name = nullptr;
	OptionButton *states = nullptr, *channel = nullptr;
	SpinBox *values[3] = {};
	SpinBox *grid_item=nullptr, *grid_orientation=nullptr;
	void grid_cell(bool p_remove);
	EditorResourcePicker *frame_texture=nullptr;
	SpinBox *frame_index=nullptr;
	void sprite_frame(bool p_remove);
	Ref<Resource> watched_resource;
	void resource_changed();
	SpinBox *bone = nullptr, *radius = nullptr, *strength = nullptr, *time = nullptr;
	void mesh_changed(int p_index, const Dictionary &p_polygon);
	void select_mode(int p_mode);
	void brush_changed(double p_value);
	void preview(bool p_playing);
	void seek(double p_time);
	void apply_component(const Dictionary &p_data, const String &p_action);
	void insert_key();
	void add_state();
	void select_state(int p_index);
	void select_entity(int p_index);

protected:
	static void _bind_methods();

public:
	void setup(const String &p_component);
	void edit(const Ref<ECSScene> &p_scene, int p_entity, bool p_running);
	bool run_self_test();
	~ECSComponentTool();
};
#endif
