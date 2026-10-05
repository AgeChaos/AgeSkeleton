#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene.h"
#include "ecs_spatial_editor.h"
#include "ecs_ui_editor.h"

#include "editor/inspector/editor_inspector.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/tree.h"

class EditorDock;
class TabContainer;
class SplitContainer;
class PopupMenu;
class PopupPanel;
class LineEdit;
class CheckBox;
class ItemList;
class ECSWorkspaceDocking;
class ECSTilemapEditor;
class ECSComponentTool;
class TextEdit;
class EditorDebuggerSession;

class ECSSceneEntityEditor : public RefCounted {
	GDCLASS(ECSSceneEntityEditor, RefCounted);
	Ref<ECSScene> scene;
	int index = -1;
	String property_filter;
	Callable runtime_writer;
	bool _dont_undo_redo() const { return true; }

protected:
	static void _bind_methods();
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_value) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	void target(const Ref<ECSScene> &p_scene, int p_index, const String &p_filter = String());
	void set_runtime_writer(const Callable &p_writer) { runtime_writer=p_writer; }
};
class ECSSceneEditorPanel : public VBoxContainer {
	GDCLASS(ECSSceneEditorPanel, VBoxContainer);
	friend class ECSWorkspaceDocking;
	ECSWorkspaceDocking *docking = nullptr;
	Ref<ECSScene> scene;
	Ref<ECSScene> runtime_scene;
	Ref<EditorDebuggerSession> runtime_session;
	Dictionary runtime_last_result;
	Dictionary runtime_results;
	bool runtime_updating = false;
	int64_t runtime_request_id = 0;
	HBoxContainer *runtime_actions = nullptr;
	Dictionary runtime_copied;
	int runtime_copied_index = -1;
	Ref<ECSScene> inspector_scene() const { return runtime_scene.is_valid()?runtime_scene:scene; }
	void setup_runtime_proxy(const Ref<ECSSceneEntityEditor> &p_proxy);
	bool runtime_set_property(int p_index,const StringName &p_property,const Variant &p_value);
	void runtime_transform_changed(int p_index,const Transform3D &p_transform);
	void runtime_copy();
	void runtime_apply();
	void update_runtime_inspector(int p_index,const Variant &p_definition);
	struct RuntimeResourceWatch { Ref<Resource> resource; Callable changed; };
	Vector<RuntimeResourceWatch> runtime_resources;
	void clear_runtime_resources();
	void runtime_resource_changed(int p_index,const String &p_path);
	Ref<ECSSceneEntityEditor> proxy;
	Tree *tree = nullptr;
	EditorInspector *inspector = nullptr;
	HBoxContainer *entity_header = nullptr;
	CheckBox *entity_active = nullptr;
	LineEdit *entity_name = nullptr;
	void update_entity_header();
	void entity_active_changed(bool p_active);
	void entity_name_submitted(const String &p_name);
	void entity_name_focus_exited();
	VBoxContainer *scene_host = nullptr;
	Button *scene_2d = nullptr, *scene_grid = nullptr;
	void set_scene_2d(bool p_enabled);
	void set_scene_grid(bool p_enabled);
	void preview_particle_action(const String &p_action);
	void preview_particle_seek(double p_time);
	ECSUICanvasEditor *canvas = nullptr;
	ECSTilemapEditor *tilemap_editor=nullptr;
	void tilemap_edited(int p_entity,const Dictionary &p_definition);
	ECSSpatialEditor *spatial = nullptr;
	void canvas_selected(int p_index);
	Label *status = nullptr;
	VBoxContainer *debugger_console = nullptr;
	VBoxContainer *project_host = nullptr, *console_host = nullptr, *game_host = nullptr;
	Vector<TabContainer *> workspace_tabs;
	Vector<SplitContainer *> workspace_splits;
	bool workspace_mounted = false;
	Vector<EditorDock *> workspace_tool_docks;
	Vector<ECSComponentTool *> entity_tools;
	Ref<Resource> tool_tiles;
	void tool_resource_changed();
	void update_entity_tools();
	PopupMenu *hierarchy_menu = nullptr;
	PopupMenu *component_actions = nullptr;
	void update_component_actions();
	VBoxContainer *component_sections = nullptr;
	Vector<EditorInspector *> component_views;
	Vector<Ref<ECSSceneEntityEditor>> component_proxies;
	HashMap<String, bool> component_folded;
	String component_signature;
	String component_clipboard_type;
	Dictionary component_clipboard;
	bool sections_queued = false;
	void queue_component_sections();
	void rebuild_component_sections();
	void style_component_inputs();
	void open_anchor_presets(Control *p_button);
	void apply_anchor_preset(int p_x, int p_y);
	PopupPanel *anchor_presets = nullptr;
	void component_fold_changed(bool p_folded, const String &p_key);
	void component_section_action(int p_action, const String &p_key);
	Button *component_add_button = nullptr;
	Label *component_picker_status = nullptr;
	PopupPanel *component_picker = nullptr;
	LineEdit *component_search = nullptr;
	ItemList *component_results = nullptr;
	int context_entity = -1;
	void hierarchy_context(const Vector2 &p_position, int p_button, bool p_empty);
	void hierarchy_action(int p_action);
	void create_preset_action(int p_action);
	Variant hierarchy_drag(const Vector2 &p_position);
	bool hierarchy_can_drop(const Vector2 &p_position, const Variant &p_data) const;
	bool model_can_drop(const Vector2 &p_position, const Variant &p_data) const;
	void model_drop(const Vector2 &p_position, const Variant &p_data);
	void import_models(const Variant &p_data, int p_parent);
	void hierarchy_drop(const Vector2 &p_position, const Variant &p_data);
	bool can_reparent_entity(int p_entity, int p_parent) const;
	void reparent_entity(int p_entity, int p_parent);
	void component_filter(const String &p_filter);
	void component_pick(int p_index);
	void component_click(int p_index, const Vector2 &p_position, int p_button);
	void open_component_picker();
	void component_search_input(const Ref<InputEvent> &p_event);
	void component_picker_test(int p_stage);
	void inspector_action(int p_action);
	void restore_workspace_layout(bool p_reset);
	void save_workspace_layout();
	void show_game_tab();
	void update_workspace_menus();
	void show_rect_tool();
	void workspace_test_step(int p_step);
	int workspace_test_retries = 0;
	OptionButton *component = nullptr;
	int selected = -1;
	bool refreshing = false;
	void refresh();
	void changed();
	void select();
	void add_entity(bool p_ui);
	void delete_entity();
	void duplicate_entity();
	void add_component();
	void remove_component();
	void save();
	void bake_navigation();
	void begin_navigation_bake();
	void cancel_navigation_bake();
	Ref<ECSScene> baking_scene;
	int64_t baking_request = 0;
	int baking_entity = -1;
	uint64_t scene_revision = 0, baking_revision = 0;
	void commit(const Array &p_entities, const String &p_action);
	String ai_directory;
	bool ai_executing = false;
	Dictionary ai_jobs;
	int ai_next_job = 1;
	void ai_compile_worker();
	void ai_start();
	void ai_stop();
	void ai_poll();
	Dictionary ai_request(const Dictionary &p_request);
	Tree *assembly_tree = nullptr;
	TextEdit *assembly_details = nullptr;
	OptionButton *assembly_platform = nullptr;
	void assembly_action(int p_action);
	bool assembly_create_assets();
	void assembly_selected();
	void assembly_refresh_view();
	void assembly_visual_test(int p_stage);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void shortcut_input(const Ref<InputEvent> &p_event) override;
	int get_runtime_selection() const { return selected; }
	bool accepts_runtime_scene(const Array &p_data) const { return p_data.size()==6 && scene.is_valid() && String(p_data[0])==scene->get_path(); }
	void set_runtime_session(const Ref<EditorDebuggerSession> &p_session);
	void runtime_edit_result(const Array &p_result);
	void update_runtime_preview(const Array &p_data);
	void stop_runtime_preview();
	bool wants_runtime_preview() const;
	void route_workspace_dock(EditorDock *p_dock, bool p_added);
	void mount_workspace_tools();
	void unmount_workspace_tools();
	void run_self_test();
	void run_visual_test();
	void edit_scene(const Ref<ECSScene> &p_scene);
	ECSSceneEditorPanel();
	~ECSSceneEditorPanel();
};
#endif
