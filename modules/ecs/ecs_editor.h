#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene_editor.h"

#include "editor/debugger/editor_debugger_plugin.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/tree.h"
class ECSDebuggerPanel : public VBoxContainer {
	GDCLASS(ECSDebuggerPanel, VBoxContainer);
	Ref<EditorDebuggerSession> session;
	Tree *tree = nullptr;
	Label *status = nullptr;
	Label *ui_status = nullptr;
	Label *selected_label = nullptr;
	OptionButton *component = nullptr;
	SpinBox *axes[3];
	uint64_t selected_id = 0;
	int offset = 0;
	Dictionary selected_components;
	ECSSceneEditorPanel *scene_panel = nullptr;
	bool preview_waiting = false;
	double preview_elapsed = 0;
	double inspect_elapsed = 0;
	void selected();
	void component_selected(int p_index);
	void apply();
	void save_snapshot();
	void reload_game();
	void page(int p_direction);
	void stopped();

protected:
	static void _bind_methods() {}
	void _notification(int p_what);

public:
	void attach(const Ref<EditorDebuggerSession> &p_session, ECSSceneEditorPanel *p_scene_panel);
	void scene_preview_received(const Array &p_data);
	void update_snapshot(const Array &p_data);
	void snapshot_saved(const Array &p_data);
	ECSDebuggerPanel();
};
class ECSDebuggerPlugin : public EditorDebuggerPlugin {
	GDCLASS(ECSDebuggerPlugin, EditorDebuggerPlugin);
	HashMap<int, ObjectID> panels;
	ECSSceneEditorPanel *scene_panel = nullptr;

protected:
	static void _bind_methods() {}

public:
	void set_scene_panel(ECSSceneEditorPanel *p_panel) { scene_panel=p_panel; }
	void setup_session(int p_index) override;
	bool has_capture(const String &p_capture) const override { return p_capture == "ecs"; }
	bool capture(const String &p_message, const Array &p_data, int p_session) override;
};
class ECSEditorPlugin : public EditorPlugin {
	GDCLASS(ECSEditorPlugin, EditorPlugin);
	Ref<ECSDebuggerPlugin> debugger;
	ECSSceneEditorPanel *scene_panel = nullptr;
	void visual_test();
	void open_workspace();
	void reveal_workspace();
	Control *startup_cover = nullptr;
	bool workspace_opened = false;
	bool pure_project = false;

protected:
	static void _bind_methods() {}
	void _notification(int p_what);

public:
	ECSEditorPlugin() { debugger.instantiate(); }
	String get_plugin_name() const override { return "ECS"; }
	bool has_main_screen() const override { return true; }
	void make_visible(bool p_visible) override;
	bool handles(Object *p_object) const override { return Object::cast_to<ECSScene>(p_object) != nullptr; }
	void edit(Object *p_object) override;
};
void initialize_ecs_editor();
void uninitialize_ecs_editor();
#endif
