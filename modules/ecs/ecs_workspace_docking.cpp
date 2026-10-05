#ifdef TOOLS_ENABLED
#include "ecs_workspace_docking.h"

#include "ecs_scene_editor.h"
#include "editor/editor_node.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "scene/gui/tab_bar.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

void ECSWorkspaceDocking::setup(ECSSceneEditorPanel *p_owner) {
	owner = p_owner;
	panel_menu=memnew(PopupMenu);
	panel_menu->connect("about_to_popup",callable_mp(this,&ECSWorkspaceDocking::populate_panel_menu));
	panel_menu->connect("id_pressed",callable_mp(this,&ECSWorkspaceDocking::panel_menu_selected));
	EditorNode::get_singleton()->add_tool_submenu_item(String(U"工作区面板"),panel_menu);
	tab_menu=memnew(PopupMenu); add_child(tab_menu); tab_menu->add_item(String(U"关闭"),0);
	tab_menu->connect("id_pressed",callable_mp(this,&ECSWorkspaceDocking::tab_menu_selected));
	for (TabContainer *tabs : owner->workspace_tabs) {
		register_tabs(tabs);
	}
	set_process(true);
}

void ECSWorkspaceDocking::register_tabs(TabContainer *p_tabs) {
	// One drag controller handles both native windows and split targets.
	p_tabs->set_drag_to_rearrange_enabled(false);
	p_tabs->get_tab_bar()->set_tab_close_display_policy(TabBar::CLOSE_BUTTON_SHOW_ALWAYS);
	p_tabs->get_tab_bar()->connect("tab_close_pressed",callable_mp(this,&ECSWorkspaceDocking::close_tab).bind(p_tabs),CONNECT_DEFERRED);
	p_tabs->get_tab_bar()->connect("gui_input", callable_mp(this, &ECSWorkspaceDocking::tab_input).bind(p_tabs));
	Callable changed = callable_mp(owner, &ECSSceneEditorPanel::update_workspace_menus);
	if (!p_tabs->is_connected("child_order_changed", changed)) {
		p_tabs->connect("child_order_changed", changed, CONNECT_DEFERRED);
		p_tabs->connect("tab_changed", changed.unbind(1), CONNECT_DEFERRED);
	}
}

TabContainer *ECSWorkspaceDocking::make_tabs(Node *p_parent) {
	auto *tabs = memnew(TabContainer);
	tabs->set_custom_minimum_size(Size2(160, 100));
	tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	TabContainer *reference = owner->workspace_tabs[0];
	for (const StringName &name : { StringName("panel"), StringName("tabbar_background"), StringName("tab_selected"), StringName("tab_unselected") }) {
		tabs->add_theme_style_override(name, reference->get_theme_stylebox(name));
	}
	p_parent->add_child(tabs);
	owner->workspace_tabs.push_back(tabs);
	register_tabs(tabs);
	return tabs;
}

Control *ECSWorkspaceDocking::find_panel(const String &p_name) const {
	for (TabContainer *tabs : owner->workspace_tabs) {
		for (int i = 0; i < tabs->get_tab_count(); i++) {
			Control *panel = tabs->get_tab_control(i);
			if (panel->get_name() == p_name) {
				return panel;
			}
		}
	}
	return nullptr;
}

void ECSWorkspaceDocking::tab_input(const Ref<InputEvent> &p_event, TabContainer *p_tabs) {
	Ref<InputEventMouseButton> button = p_event;
	if(button.is_valid() && button->is_pressed() && button->get_button_index()==MouseButton::RIGHT) {
		int index=p_tabs->get_tab_bar()->get_tab_idx_at_point(button->get_position());
		if(index>=0 && index<p_tabs->get_tab_count()) { finish_drag(true); context_panel=p_tabs->get_tab_control(index)->get_name(); tab_menu->set_position(DisplayServer::get_singleton()->mouse_get_position()); tab_menu->popup(); p_tabs->get_tab_bar()->accept_event(); }
		return;
	}
	if (button.is_valid() && button->get_button_index() == MouseButton::LEFT) {
		if (button->is_pressed()) {
			int index = p_tabs->get_tab_bar()->get_tab_idx_at_point(button->get_position());
			if (index >= 0 && index < p_tabs->get_tab_count()) {
				candidate = p_tabs->get_tab_control(index);
				drag_start = p_tabs->get_tab_bar()->get_screen_position() + button->get_position();
			}
		} else if (candidate) {
			Vector2 position = p_tabs->get_tab_bar()->get_screen_position() + button->get_position();
			if (position.distance_to(drag_start) >= 8) {
				dragging = true;
				update_target(position);
			}
			finish_drag(false);
		}
	}
	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid() && candidate && motion->get_button_mask().has_flag(MouseButtonMask::LEFT)) {
		Vector2 position = p_tabs->get_tab_bar()->get_screen_position() + motion->get_position();
		if (position.distance_to(drag_start) >= 8) {
			dragging = true;
			update_target(position);
		}
	}
}

void ECSWorkspaceDocking::clear_hint() {
	if (hint) {
		hint->get_parent()->remove_child(hint);
		hint->queue_free();
		hint = nullptr;
	}
}

void ECSWorkspaceDocking::update_target(const Vector2 &p_screen) {
	drop_target = nullptr;
	drop_side = 0;
	drop_index = -1;
	// Floating leaves are appended, so they win over the editor behind them.
	for (int i = owner->workspace_tabs.size() - 1; i >= 0; i--) {
		TabContainer *tabs = owner->workspace_tabs[i];
		if (!tabs->is_visible_in_tree() || !tabs->get_window()->is_visible()) {
			continue;
		}
		Rect2 rect(tabs->get_screen_position(), tabs->get_size());
		if (!rect.grow(8).has_point(p_screen)) {
			continue;
		}
		drop_target = tabs;
		Vector2 local = p_screen - rect.position;
		float header = tabs->get_tab_bar()->get_size().y;
		float edge_x = MIN(80.0f, rect.size.x * .22f), edge_y = MIN(80.0f, rect.size.y * .22f);
		if (local.y >= 7 && local.y <= header + 3) {
			drop_index = tabs->get_tab_bar()->get_tab_idx_at_point(p_screen - tabs->get_tab_bar()->get_screen_position());
		} else if (local.x < edge_x) {
			drop_side = 1;
		} else if (local.x > rect.size.x - edge_x) {
			drop_side = 2;
		} else if (local.y < edge_y + header) {
			drop_side = 3;
		} else if (local.y > rect.size.y - edge_y) {
			drop_side = 4;
		}
		break;
	}
	clear_hint();
	if (!drop_target) {
		return;
	}
	Rect2 highlight(Vector2(), drop_target->get_size());
	if (drop_side == 1 || drop_side == 2) {
		highlight.size.x *= .5;
		if (drop_side == 2) {
			highlight.position.x = highlight.size.x;
		}
	} else if (drop_side == 3 || drop_side == 4) {
		highlight.size.y *= .5;
		if (drop_side == 4) {
			highlight.position.y = highlight.size.y;
		}
	}
	hint = memnew(Panel);
	hint->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	hint->set_z_index(100);
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(Color(.18, .48, .85, .28));
	style->set_border_width_all(2);
	style->set_border_color(Color(.35, .65, 1, .95));
	hint->add_theme_style_override("panel", style);
	drop_target->add_child(hint, false, Node::INTERNAL_MODE_BACK);
	hint->set_position(highlight.position);
	hint->set_size(highlight.size);
}

void ECSWorkspaceDocking::finish_drag(bool p_cancel) {
	Control *panel = candidate;
	bool apply = dragging && !p_cancel && panel;
	candidate = nullptr;
	dragging = false;
	clear_hint();
	if (apply) {
		dock(panel, drop_target, drop_target ? drop_side : -1, DisplayServer::get_singleton()->mouse_get_position(), drop_index);
	}
	drop_target = nullptr;
}

void ECSWorkspaceDocking::_notification(int p_what) {
	if (p_what != NOTIFICATION_PROCESS || !candidate) {
		return;
	}
	if (Input::get_singleton()->is_key_pressed(Key::ESCAPE)) {
		finish_drag(true);
		return;
	}
	Vector2 mouse = DisplayServer::get_singleton()->mouse_get_position();
	if (!dragging && mouse.distance_to(drag_start) >= 8) {
		dragging = true;
	}
	if (dragging) {
		update_target(mouse);
	}
	if (!DisplayServer::get_singleton()->mouse_get_button_state().has_flag(MouseButtonMask::LEFT)) {
		finish_drag(false);
	}
}

void ECSWorkspaceDocking::dock(Control *p_panel, TabContainer *p_target, int p_side, const Vector2 &p_screen, int p_index) {
	ERR_FAIL_NULL(p_panel);
	auto *source = Object::cast_to<TabContainer>(p_panel->get_parent());
	ERR_FAIL_NULL(source);
	if (source == p_target && source->get_tab_count() == 1) {
		return;
	}
	Dictionary operation;
	operation["panel"] = String(p_panel->get_name());
	operation["target"] = p_target && p_target->get_tab_count() ? String(p_target->get_tab_control(0)->get_name()) : String();
	operation["side"] = p_side;
	operation["position"] = p_screen;
	operation["index"] = p_index;
	TabContainer *destination = p_target;
	if (p_side < 0) {
		auto *window = memnew(Window);
		window->hide();
		window->set_title(String(U"AgeChaos · ") + String(p_panel->get_name()));
		window->set_force_native(true);
		window->set_transient(true);
		window->set_min_size(Size2i(400, 260));
		window->set_size(Size2i(900, 600));
		window->set_position(p_screen - Vector2(100, 20));
		add_child(window);
		destination = make_tabs(window);
		destination->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		window->connect("close_requested", callable_mp(this, &ECSWorkspaceDocking::close_floating).bind(window));
		floating.push_back(window);
		if(!replaying) { window->show(); }
	} else if (p_side > 0 && destination) {
		Node *parent = destination->get_parent();
		int index = destination->get_index();
		auto *split = memnew(SplitContainer);
		split->set_vertical(p_side >= 3);
		split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		split->add_theme_constant_override("separation", 6);
		parent->add_child(split);
		parent->move_child(split, index);
		if (Object::cast_to<Window>(parent)) {
			split->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		}
		destination->reparent(split);
		splits.push_back({ split, destination });
		owner->workspace_splits.push_back(split);
		destination = make_tabs(split);
		if (p_side == 1 || p_side == 3) {
			split->move_child(destination, 0);
		}
	} else if (!destination) {
		destination = owner->workspace_tabs[0];
	}
	if (p_panel->get_parent() != destination) {
		p_panel->reparent(destination);
	}
	if (p_index >= 0) {
		destination->move_child(p_panel, MIN(p_index, destination->get_tab_count() - 1));
	}
	p_panel->remove_meta("ecs_panel_closed"); destination->set_tab_hidden(destination->get_tab_idx_from_control(p_panel),false);
	destination->set_current_tab(destination->get_tab_idx_from_control(p_panel));
	if (!replaying) {
		operations.push_back(operation);
	}
	owner->update_workspace_menus();
	refresh_windows();
}

void ECSWorkspaceDocking::close_floating(Window *p_window) {
	Vector<Control *> panels;
	for (TabContainer *tabs : owner->workspace_tabs) {
		if (tabs->get_window() == p_window) {
			for (int i = 0; i < tabs->get_tab_count(); i++) {
				panels.push_back(tabs->get_tab_control(i));
			}
		}
	}
	for (Control *panel : panels) {
		close_panel(panel);
	}
	p_window->hide();
}

void ECSWorkspaceDocking::refresh_windows() {
	if(replaying) { return; }
	for (Window *window : floating) {
		bool populated = false;
		for (TabContainer *tabs : owner->workspace_tabs) {
			if (tabs->get_window() == window && has_open_tabs(tabs)) {
				populated = true;
				break;
			}
		}
		window->set_visible(populated);
	}
}

Array ECSWorkspaceDocking::get_windows() const {
	Array result;
	for (Window *window : floating) {
		result.push_back(Rect2i(window->get_position(), window->get_size()));
	}
	return result;
}

void ECSWorkspaceDocking::reset_extensions() {
	for(TabContainer *tabs:owner->workspace_tabs) { for(int i=0;i<tabs->get_tab_count();i++) { tabs->get_tab_control(i)->remove_meta("ecs_panel_closed"); tabs->set_tab_hidden(i,false); } }
	finish_drag(true);
	Vector<Control *> panels;
	for (TabContainer *tabs : owner->workspace_tabs) {
		for (int i = 0; i < tabs->get_tab_count(); i++) {
			panels.push_back(tabs->get_tab_control(i));
		}
	}
	for (Control *panel : panels) {
		if (panel->get_parent() != owner->workspace_tabs[0]) {
			panel->reparent(owner->workspace_tabs[0]);
		}
	}
	for (int i = splits.size() - 1; i >= 0; i--) {
		SplitContainer *split = splits[i].split;
		Node *parent = split->get_parent();
		int index = split->get_index();
		splits[i].original->reparent(parent);
		parent->move_child(splits[i].original, index);
		parent->remove_child(split);
		split->queue_free();
	}
	for (Window *window : floating) {
		window->hide();
		remove_child(window);
		window->queue_free();
	}
	splits.clear();
	floating.clear();
	operations.clear();
	owner->workspace_tabs.resize(6);
	owner->workspace_splits.resize(5);
	for (Control *panel : panels) {
		String name = panel->get_name();
		int index = (name == "Game" || name == "Audio" || name == "Animation" || name == "Shader Editor" || name == "TileSet") ? 1 : name == "Hierarchy" ? 2
				: name == "Project"																														  ? 3
				: name == "Inspector"																													  ? 4
				: (name == "Console" || name == "Debugger")																														  ? 5
																																						  : 0;
		if (panel->get_parent() != owner->workspace_tabs[index]) {
			panel->reparent(owner->workspace_tabs[index]);
		}
	}
}

void ECSWorkspaceDocking::restore(const Array &p_operations, const Array &p_windows) {
	replaying = true;
	for (int i = 0; i < MIN(p_operations.size(), 512); i++) {
		if (p_operations[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary op = p_operations[i];
		Control *panel = find_panel(op.get("panel", ""));
		Control *target_panel = find_panel(op.get("target", ""));
		int side = op.get("side", 0);
		if (!panel || side < -1 || side > 4) {
			continue;
		}
		auto *target = target_panel ? Object::cast_to<TabContainer>(target_panel->get_parent()) : owner->workspace_tabs[0];
		dock(panel, target, side, op.get("position", Vector2(100, 100)), op.get("index", -1));
	}
	for (int i = 0; i < MIN(floating.size(), p_windows.size()); i++) {
		if (p_windows[i].get_type() != Variant::RECT2I) {
			continue;
		}
		Rect2i rect = p_windows[i];
		floating[i]->set_position(rect.position);
		floating[i]->set_size(rect.size.maxi(260));
	}
	replaying = false;
	operations = p_operations.duplicate(true);
}

bool ECSWorkspaceDocking::run_self_test() {
	Control *project = owner->project_host;
	Control *console = owner->console_host;
	TabContainer *target = Object::cast_to<TabContainer>(console->get_parent());
	dock(project, target, 1);
	bool ok = owner->workspace_tabs.size() == 7 && project->get_parent() != console->get_parent();
	dock(project, nullptr, -1, Vector2(160, 160));
	ok &= floating.size() == 1 && project->get_window() == floating[0] && floating[0]->is_visible();
	close_floating(floating[0]);
	ok &= project->get_window() == floating[0] && !floating[0]->is_visible();
	show_panel(project); ok &= floating[0]->is_visible();
	dock(project,owner->workspace_tabs[0],0);
	owner->save_workspace_layout();
	owner->restore_workspace_layout(false);
	ok &= project->get_window() == owner->get_window() && owner->workspace_tabs.size() == 8 && splits.size() == 1 && floating.size() == 1 && operations.size() == 3;
	owner->restore_workspace_layout(true);
	ok &= owner->workspace_tabs.size() == 6 && owner->workspace_splits.size() == 5 && project->get_parent() == owner->workspace_tabs[3];
	return ok;
}

ECSWorkspaceDocking::~ECSWorkspaceDocking() {
	if(panel_menu && EditorNode::get_singleton()) { EditorNode::get_singleton()->remove_tool_menu_item(String(U"工作区面板")); }
}
bool ECSWorkspaceDocking::has_open_tabs(TabContainer *tabs) {
	for(int i=0;i<tabs->get_tab_count();i++) { if(!tabs->is_tab_hidden(i)) { return true; } } return false;
}
void ECSWorkspaceDocking::close_tab(int index,TabContainer *tabs) {
	if(index>=0 && index<tabs->get_tab_count()) { close_panel(tabs->get_tab_control(index)); }
}
void ECSWorkspaceDocking::close_panel(Control *panel) {
	if(!panel) { return; } auto *tabs=Object::cast_to<TabContainer>(panel->get_parent()); if(!tabs) { return; }
	finish_drag(true); panel->set_meta("ecs_panel_closed",true); tabs->set_tab_hidden(tabs->get_tab_idx_from_control(panel),true);
	owner->update_workspace_menus(); refresh_windows(); owner->save_workspace_layout();
}
void ECSWorkspaceDocking::show_panel(Control *panel) {
	if(!panel) { return; } auto *tabs=Object::cast_to<TabContainer>(panel->get_parent()); if(!tabs) { return; }
	panel->remove_meta("ecs_panel_closed"); int index=tabs->get_tab_idx_from_control(panel); tabs->set_tab_hidden(index,false); tabs->set_current_tab(index);
	owner->update_workspace_menus(); refresh_windows(); owner->save_workspace_layout();
}
PackedStringArray ECSWorkspaceDocking::get_closed_panels() const {
	PackedStringArray names; for(TabContainer *tabs:owner->workspace_tabs) { for(int i=0;i<tabs->get_tab_count();i++) { if(tabs->is_tab_hidden(i)) { names.push_back(tabs->get_tab_control(i)->get_name()); } } } return names;
}
void ECSWorkspaceDocking::restore_closed_panels(const PackedStringArray &names) {
	for(TabContainer *tabs:owner->workspace_tabs) { for(int i=0;i<tabs->get_tab_count();i++) { Control *panel=tabs->get_tab_control(i); bool closed=names.has(panel->get_name()); panel->set_meta("ecs_panel_closed",closed); tabs->set_tab_hidden(i,closed); } }
	owner->update_workspace_menus(); refresh_windows();
}
void ECSWorkspaceDocking::tab_menu_selected(int) { close_panel(find_panel(context_panel)); }
void ECSWorkspaceDocking::populate_panel_menu() {
	panel_menu->clear(); menu_panels.clear();
	for(TabContainer *tabs:owner->workspace_tabs) { for(int i=0;i<tabs->get_tab_count();i++) { String name=tabs->get_tab_control(i)->get_name(); int index=menu_panels.size(); menu_panels.push_back(name); panel_menu->add_check_item(name == "Inspector" ? String(U"检查器") : tabs->get_tab_title(i),index); panel_menu->set_item_checked(index,!tabs->is_tab_hidden(i)); } }
}
void ECSWorkspaceDocking::panel_menu_selected(int id) { if(id>=0 && id<menu_panels.size()) { show_panel(find_panel(menu_panels[id])); } }
bool ECSWorkspaceDocking::run_visibility_test() {
	Control *panel=owner->project_host;
	close_panel(panel); bool ok=get_closed_panels().has("Project") && !panel->is_visible_in_tree();
	owner->restore_workspace_layout(false); ok &= get_closed_panels().has("Project") && !panel->is_visible_in_tree();
	populate_panel_menu(); int index=menu_panels.find("Project"); panel_menu_selected(index);
	ok &= !get_closed_panels().has("Project") && panel->is_visible_in_tree();
	context_panel="Project"; tab_menu_selected(0); ok &= get_closed_panels().has("Project");
	show_panel(panel); owner->update_workspace_menus(); return ok && panel->is_visible_in_tree();
}
#endif
