#ifdef TOOLS_ENABLED
#include "ecs_editor.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "scene/gui/button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/line_edit.h"
#include "scene/main/scene_tree.h"
#include "servers/rendering/rendering_server.h"

ECSDebuggerPanel::ECSDebuggerPanel() {
	set_name(String(U"ECS 实体"));
	status = memnew(Label);
	status->set_text(String(U"运行 ECSMainLoop 项目后显示实时实体数据。"));
	add_child(status);
	ui_status = memnew(Label);
	add_child(ui_status);
	auto *toolbar = memnew(HBoxContainer);
	add_child(toolbar);
	auto *previous = memnew(Button);
	previous->set_text(String(U"上一页"));
	previous->connect("pressed", callable_mp(this, &ECSDebuggerPanel::page).bind(-1));
	toolbar->add_child(previous);
	auto *next = memnew(Button);
	next->set_text(String(U"下一页"));
	next->connect("pressed", callable_mp(this, &ECSDebuggerPanel::page).bind(1));
	toolbar->add_child(next);
	auto *save = memnew(Button);
	save->set_text(String(U"保存运行快照"));
	save->set_tooltip_text(String(U"保存为游戏用户目录下的 ECSScene 资源；不会覆盖原场景。系统回调不包含在快照中。"));
	save->connect("pressed", callable_mp(this, &ECSDebuggerPanel::save_snapshot));
	toolbar->add_child(save);
	auto *reload = memnew(Button);
	reload->set_text(String(U"加载新游戏 DLL"));
	reload->set_tooltip_text(String(U"先构建游戏程序集，再在帧边界替换。游戏须实现状态捕获与恢复；不能替换 Godot.LeanCLR 或 BCL。"));
	reload->connect("pressed", callable_mp(this, &ECSDebuggerPanel::reload_game));
	toolbar->add_child(reload);
	tree = memnew(Tree);
	tree->set_columns(3);
	tree->set_column_titles_visible(true);
	tree->set_column_title(0, String(U"实体 ID"));
	tree->set_column_title(1, String(U"位置"));
	tree->set_column_title(2, String(U"速度 / 组件"));
	tree->set_hide_root(true);
	tree->set_v_size_flags(SIZE_EXPAND_FILL);
	tree->set_custom_minimum_size(Vector2(0, 160));
	add_child(tree);
	tree->connect("item_selected", callable_mp(this, &ECSDebuggerPanel::selected));
	selected_label = memnew(Label);
	selected_label->set_text(String(U"选择实体后修改 position / velocity / rotation / scale；运行时修改不保存到场景。"));
	add_child(selected_label);
	auto *edit = memnew(HBoxContainer);
	add_child(edit);
	component = memnew(OptionButton);
	component->add_item("position");
	component->add_item("velocity");
	component->add_item("rotation");
	component->add_item("scale");
	edit->add_child(component);
	component->connect("item_selected", callable_mp(this, &ECSDebuggerPanel::component_selected));
	for (int i = 0; i < 3; i++) {
		axes[i] = memnew(SpinBox);
		axes[i]->set_min(-1e9);
		axes[i]->set_max(1e9);
		axes[i]->set_step(.01);
		axes[i]->set_h_size_flags(SIZE_EXPAND_FILL);
		edit->add_child(axes[i]);
	}
	auto *button = memnew(Button);
	button->set_text(String(U"提交修改"));
	button->connect("pressed", callable_mp(this, &ECSDebuggerPanel::apply));
	edit->add_child(button);
}
void ECSDebuggerPanel::attach(const Ref<EditorDebuggerSession> &value,ECSSceneEditorPanel *p_scene_panel) {
	session = value;
	scene_panel=p_scene_panel;
	set_process(true);
	session->connect("stopped", callable_mp(this, &ECSDebuggerPanel::stopped));
}
void ECSDebuggerPanel::stopped() {
	preview_waiting=false;
	if(scene_panel) { scene_panel->stop_runtime_preview(); }
	tree->clear();
	selected_id = 0;
	selected_components.clear();
	status->set_text(String(U"ECS 会话已停止。"));
	ui_status->set_text("");
}
void ECSDebuggerPanel::_notification(int what) {
	if(what!=NOTIFICATION_PROCESS || !scene_panel || session.is_null() || !session->is_active()) { return; }
	preview_elapsed+=get_process_delta_time();
	inspect_elapsed+=get_process_delta_time();
	if(preview_waiting || preview_elapsed<1.0/30.0 || !scene_panel->wants_runtime_preview()) { return; }
	preview_elapsed=0;
	preview_waiting=true;
	Array args; args.push_back(inspect_elapsed>=.2?scene_panel->get_runtime_selection():-1);
	if(inspect_elapsed>=.2) { inspect_elapsed=0; }
	session->send_message("ecs:scene_preview",args);
}
void ECSDebuggerPanel::scene_preview_received(const Array &data) {
	preview_waiting=false;
	if(scene_panel && session.is_valid() && session->is_active() && scene_panel->accepts_runtime_scene(data)) { scene_panel->set_runtime_session(session); scene_panel->update_runtime_preview(data); }
}
void ECSDebuggerPanel::page(int direction) {
	if (session.is_valid() && session->is_active()) {
		offset = MAX(0, offset + direction * 128);
		Array args;
		args.push_back(offset);
		session->send_message("ecs:page", args);
	}
}
void ECSDebuggerPanel::selected() {
	TreeItem *item = tree->get_selected();
	if (!item) {
		return;
	}
	Dictionary entity_data = item->get_metadata(0);
	selected_id = int64_t(entity_data["id"]);
	selected_components = entity_data["components"];
	selected_label->set_text(String(U"实体 ") + itos(selected_id) + String(U" · ") + String(Variant(selected_components)));
	component_selected(component->get_selected());
}
void ECSDebuggerPanel::component_selected(int index) {
	String name = component->get_item_text(index);
	Variant value = selected_components.get(name, name == "scale" ? Vector3(1, 1, 1) : Vector3());
	if (value.get_type() != Variant::VECTOR3) {
		return;
	}
	Vector3 v = value;
	for (int i = 0; i < 3; i++) {
		axes[i]->set_value(v[i]);
	}
}
void ECSDebuggerPanel::apply() {
	if (!selected_id || session.is_null() || !session->is_active()) {
		return;
	}
	Array args;
	args.push_back(selected_id);
	args.push_back(component->get_item_text(component->get_selected()));
	args.push_back(Vector3(axes[0]->get_value(), axes[1]->get_value(), axes[2]->get_value()));
	session->send_message("ecs:set_vector", args);
}
void ECSDebuggerPanel::save_snapshot() {
	if (session.is_valid() && session->is_active()) {
		session->send_message("ecs:save_scene", Array());
	}
}
void ECSDebuggerPanel::reload_game() {
	if (session.is_valid() && session->is_active()) {
		session->send_message("ecs:reload_game", Array());
		selected_label->set_text(String(U"已请求加载新 DLL；完成或拒绝原因见游戏输出。"));
	}
}
void ECSDebuggerPanel::snapshot_saved(const Array &result) {
	if (result.size() != 2) {
		return;
	}
	selected_label->set_text(int(result[0]) == OK ? String(U"快照已保存：") + String(result[1]) : String(U"快照保存失败，错误码：") + itos(int(result[0])));
}
void ECSDebuggerPanel::update_snapshot(const Array &snapshot) {
	if (snapshot.size() != 3 || snapshot[0].get_type() != Variant::DICTIONARY || snapshot[1].get_type() != Variant::ARRAY) {
		return;
	}
	Dictionary stats = snapshot[0];
	Array entities = snapshot[1];
	ui_status->set_text(vformat(String(U"UI 实体 %s · UI 事件 %s · 最近事件 %s"), stats.get("ui_entities", 0), stats.get("ui_events_dispatched", 0), stats.get("last_ui_event", Dictionary())));
	offset = snapshot[2];
	status->set_text(vformat(String(U"原生 ECS · 实体 %s · 组件数据 %s 字节 · 第 %d 页 · 每页最多 128 条"), stats.get("entities", 0), stats.get("component_bytes", 0), offset / 128 + 1));
	tree->set_block_signals(true);
	tree->clear();
	TreeItem *root = tree->create_item();
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		Dictionary components = entity["components"];
		TreeItem *item = tree->create_item(root);
		item->set_text(0, itos(int64_t(entity["id"])));
		item->set_text(1, String(components.get("position", String(U"—"))));
		item->set_text(2, String(components.get("velocity", String(U"—"))));
		item->set_tooltip_text(2, String(Variant(components)));
		item->set_metadata(0, entity);
		item->set_tooltip_text(0, String(U"父实体: ") + itos(int64_t(entity.get("parent", 0))));
		if (uint64_t(int64_t(entity["id"])) == selected_id) {
			item->select(0);
			selected_components = components;
		}
	}
	tree->set_block_signals(false);
}
void ECSDebuggerPlugin::setup_session(int index) {
	auto *panel = memnew(ECSDebuggerPanel);
	panel->attach(get_session(index),scene_panel);
	get_session(index)->add_session_tab(panel);
	panels[index] = panel->get_instance_id();
}
bool ECSDebuggerPlugin::capture(const String &message, const Array &data, int session) {
	if(message=="ecs:live_result") { if(scene_panel) { scene_panel->runtime_edit_result(data); } return true; }
	if (message != "ecs:snapshot" && message != "ecs:saved" && message != "ecs:scene_preview") {
		return false;
	}
	const ObjectID *id = panels.getptr(session);
	if (id) {
		auto *panel = Object::cast_to<ECSDebuggerPanel>(ObjectDB::get_instance(*id));
		if (panel) {
			if (message == "ecs:snapshot") {
				panel->update_snapshot(data);
			} else if(message=="ecs:scene_preview") {
				panel->scene_preview_received(data);
			} else {
				panel->snapshot_saved(data);
			}
		}
	}
	return true;
}
void ECSEditorPlugin::make_visible(bool visible) {
	if (scene_panel) {
		scene_panel->set_visible(visible);
	}
}
void ECSEditorPlugin::open_workspace() {
	if (!pure_project || !scene_panel || workspace_opened) {
		return;
	}
	workspace_opened = true;
	String path = GLOBAL_GET("ecs/run/scene");
	if (path.get_extension() == "tres" || path.get_extension() == "res") {
		Ref<ECSScene> resource = ResourceLoader::load(path, "ECSScene");
		if (resource.is_valid()) {
			scene_panel->edit_scene(resource);
		}
	}
	EditorNode::get_editor_main_screen()->select_by_name("ECS");
	EditorNode::get_editor_main_screen()->set_button_enabled(EditorMainScreen::EDITOR_2D, false);
	EditorNode::get_editor_main_screen()->set_button_enabled(EditorMainScreen::EDITOR_3D, false);
	auto *dock = SceneTreeDock::get_singleton();
	EditorDockManager::get_singleton()->remove_dock(dock);
	add_child(dock);
	dock->hide();
	dock->set_process_shortcut_input(false);
	dock->set_process_input(false);
	scene_panel->mount_workspace_tools();
	// Containers finish their deferred sorting before this frame is drawn.
	// Reveal the completed layout on the next frame, not the intermediate docks.
	RenderingServer::get_singleton()->connect(SNAME("frame_post_draw"), callable_mp(this, &ECSEditorPlugin::reveal_workspace), CONNECT_ONE_SHOT);
}
void ECSEditorPlugin::reveal_workspace() {
	if (startup_cover) {
		startup_cover->queue_free();
		startup_cover = nullptr;
	}
}
void ECSEditorPlugin::visual_test() {
	EditorNode::get_editor_main_screen()->select_by_name("ECS");
	scene_panel->run_visual_test();
}
void ECSEditorPlugin::_notification(int what) {
	if (what == NOTIFICATION_ENTER_TREE) {
		scene_panel = memnew(ECSSceneEditorPanel);
		debugger->set_scene_panel(scene_panel);
		add_debugger_plugin(debugger);
		EditorNode::get_editor_main_screen()->get_control()->add_child(scene_panel);
		scene_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		scene_panel->hide();
		pure_project = String(GLOBAL_GET("application/run/main_loop_type")) == "ECSMainLoop";
		if (pure_project) {
			auto *editor = EditorNode::get_singleton();
			auto *cover = memnew(ColorRect);
			startup_cover = cover;
			cover->set_color(Color(.12, .12, .12));
			cover->set_z_index(RSE::CANVAS_ITEM_Z_MAX);
			editor->get_gui_base()->add_child(cover);
			cover->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
			auto *label = memnew(Label);
			label->set_text(TTR("Loading editor layout..."));
			label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
			cover->add_child(label);
			label->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
			if (editor->is_editor_layout_loaded()) {
				callable_mp(this, &ECSEditorPlugin::open_workspace).call_deferred();
			} else {
				editor->connect(SNAME("editor_layout_loaded"), callable_mp(this, &ECSEditorPlugin::open_workspace), CONNECT_ONE_SHOT);
			}
		}
		if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-ui-authoring-visual") || OS::get_singleton()->get_cmdline_user_args().find("--ecs-3d-authoring-visual")) {
			get_tree()->create_timer(3.0)->connect("timeout", callable_mp(this, &ECSEditorPlugin::visual_test));
		}
		if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-authoring-self-test")) {
			get_tree()->create_timer(2.0)->connect("timeout", callable_mp(scene_panel, &ECSSceneEditorPanel::run_self_test));
		}
	}
	if (what == NOTIFICATION_EXIT_TREE) {
		if (startup_cover) {
			memdelete(startup_cover);
			startup_cover = nullptr;
		}
		remove_debugger_plugin(debugger);
		if (scene_panel) {
			scene_panel->unmount_workspace_tools();
			scene_panel->get_parent()->remove_child(scene_panel);
			memdelete(scene_panel);
			scene_panel = nullptr;
		}
	}
}
void ECSEditorPlugin::edit(Object *object) {
	if (scene_panel) {
		Ref<ECSScene> scene = Object::cast_to<ECSScene>(object);
		if (scene.is_valid()) {
			scene_panel->edit_scene(scene);
			EditorNode::get_editor_main_screen()->select_by_name("ECS");
		}
	}
}
void initialize_ecs_editor() {
#ifndef AGE_SKELETON_EDITOR
    EditorPlugins::add_by_type<ECSEditorPlugin>();
#endif
}
void uninitialize_ecs_editor() {}
#endif
