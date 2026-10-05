#ifdef TOOLS_ENABLED
#include "skeleton_workspace_docking.h"

#include "core/io/config_file.h"
#include "core/config/project_settings.h"
#include "editor/themes/editor_scale.h"
#include "editor/editor_node.h"


#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/menu_button.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

// Adapted from AgeChaos ECSWorkspaceDocking. The standalone workspace owns its
// panel registry and layout; it does not depend on the engine scene editor.
void SkeletonWorkspaceDocking::setup(Control *p_owner, MenuButton *p_menu, const Vector<TabContainer *> &p_tabs, const Vector<SplitContainer *> &p_splits) {
	owner=p_owner; workspace_tabs=p_tabs; workspace_splits=p_splits;
	base_tabs=p_tabs.size(); base_splits=p_splits.size(); panel_menu=p_menu->get_popup();
	panel_menu->connect("about_to_popup",callable_mp(this,&SkeletonWorkspaceDocking::populate_panel_menu));
	panel_menu->connect("id_pressed",callable_mp(this,&SkeletonWorkspaceDocking::panel_menu_selected));
	tab_menu=memnew(PopupMenu); add_child(tab_menu); tab_menu->add_item(TTR("Close Tab"),0);
	tab_menu->add_item(TTR("Floating Window"),2);
	tab_menu->connect("id_pressed",callable_mp(this,&SkeletonWorkspaceDocking::tab_menu_selected));
	for(int i=0;i<base_tabs;i++) { auto *tabs=workspace_tabs[i];
		for(int j=0;j<tabs->get_tab_count();j++) { default_slots[String(tabs->get_tab_control(j)->get_name())]=i; }
		register_tabs(tabs);
	}
	for(auto *split:workspace_splits) { split->connect("dragged",callable_mp(this,&SkeletonWorkspaceDocking::save_layout).unbind(1)); }
	configure_base_layout(true);
	set_process(true); callable_mp(this,&SkeletonWorkspaceDocking::load_layout).call_deferred();
}

void SkeletonWorkspaceDocking::configure_base_layout(bool p_side_by_side) {
	side_by_side_base=p_side_by_side;
	// Keep the original base profile available when replaying older saved layouts:
	// their split offsets and docking operations were recorded against that profile.
	SplitContainer *left=workspace_splits[1],*sheet=workspace_splits[2],*sidebar=workspace_splits[3];
	sidebar->set_vertical(!p_side_by_side);
	left->set_stretch_ratio(p_side_by_side?3.0:1.0);
	sidebar->set_h_size_flags(p_side_by_side?Control::SIZE_EXPAND_FILL:Control::SIZE_FILL);
	sidebar->set_stretch_ratio(p_side_by_side?2.0:1.0);
	workspace_tabs[0]->set_stretch_ratio(p_side_by_side?.43:3.0);
	sheet->set_stretch_ratio(p_side_by_side?.57:1.3);
	workspace_tabs[3]->set_stretch_ratio(1.05);
	workspace_tabs[4]->set_stretch_ratio(1.0);
	workspace_tabs[4]->set_custom_minimum_size(Size2(p_side_by_side?360:160,120)*EDSCALE);
}

void SkeletonWorkspaceDocking::register_tabs(TabContainer *p_tabs) {
	auto *menu = memnew(PopupMenu); p_tabs->add_child(menu);
	p_tabs->set_meta("ecs_workspace_menu", menu);
	menu->connect("about_to_popup", callable_mp(this, &SkeletonWorkspaceDocking::prepare_tab_menu).bind(p_tabs));
	menu->connect("id_pressed", callable_mp(this, &SkeletonWorkspaceDocking::tab_menu_selected));
	update_tab_menu(p_tabs);
	// One drag controller handles both native windows and split targets.
	p_tabs->set_drag_to_rearrange_enabled(false);
	p_tabs->get_tab_bar()->set_tab_close_display_policy(TabBar::CLOSE_BUTTON_SHOW_NEVER);
	p_tabs->get_tab_bar()->connect("tab_close_pressed",callable_mp(this,&SkeletonWorkspaceDocking::close_tab).bind(p_tabs),CONNECT_DEFERRED);
	p_tabs->get_tab_bar()->connect("gui_input", callable_mp(this, &SkeletonWorkspaceDocking::tab_input).bind(p_tabs));
	Callable changed = callable_mp(this, &SkeletonWorkspaceDocking::update_workspace);
	if (!p_tabs->is_connected("child_order_changed", changed)) {
		p_tabs->connect("child_order_changed", changed, CONNECT_DEFERRED);
		p_tabs->connect("tab_changed", changed.unbind(1), CONNECT_DEFERRED);
	}
}

TabContainer *SkeletonWorkspaceDocking::make_tabs(Node *p_parent) {
	auto *tabs = memnew(TabContainer);
	tabs->set_custom_minimum_size(Size2(160, 100));
	tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	TabContainer *reference = workspace_tabs[0];
	for (const StringName &name : { StringName("panel"), StringName("tabbar_background"), StringName("tab_selected"), StringName("tab_unselected") }) {
		tabs->add_theme_style_override(name, reference->get_theme_stylebox(name));
	}
	tabs->set_theme(owner->get_theme());
	p_parent->add_child(tabs);
	workspace_tabs.push_back(tabs);
	register_tabs(tabs);
	return tabs;
}

Control *SkeletonWorkspaceDocking::find_panel(const String &p_name) const {
	for (TabContainer *tabs : workspace_tabs) {
		for (int i = 0; i < tabs->get_tab_count(); i++) {
			Control *panel = tabs->get_tab_control(i);
			if (panel->get_name() == p_name) {
				return panel;
			}
		}
	}
	return nullptr;
}

void SkeletonWorkspaceDocking::tab_input(const Ref<InputEvent> &p_event, TabContainer *p_tabs) {
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

void SkeletonWorkspaceDocking::clear_hint() {
	if (hint) {
		hint->get_parent()->remove_child(hint);
		hint->queue_free();
		hint = nullptr;
	}
}

void SkeletonWorkspaceDocking::update_target(const Vector2 &p_screen) {
	drop_target = nullptr;
	drop_side = 0;
	drop_index = -1;
	// Floating leaves are appended, so they win over the editor behind them.
	for (int i = workspace_tabs.size() - 1; i >= 0; i--) {
		TabContainer *tabs = workspace_tabs[i];
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

void SkeletonWorkspaceDocking::finish_drag(bool p_cancel) {
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

void SkeletonWorkspaceDocking::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS && initialized && !candidate) { save_elapsed+=get_process_delta_time(); if(save_elapsed>=1) { save_elapsed=0; save_layout(); } }
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

void SkeletonWorkspaceDocking::dock(Control *p_panel, TabContainer *p_target, int p_side, const Vector2 &p_screen, int p_index) {
	restore_maximized();
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
		window->set_title(String(U"AgeSkeleton · ") + TTR(String(p_panel->get_name())));
		window->set_force_native(true);
		window->set_transient(true);
		window->set_min_size(Size2i(400, 260));
		window->set_size(Size2i(900, 600));
		window->set_position(p_screen - Vector2(100, 20));
		add_child(window);
		destination = make_tabs(window);
		destination->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		window->connect("close_requested", callable_mp(this, &SkeletonWorkspaceDocking::close_floating).bind(window));
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
		workspace_splits.push_back(split);
		split->connect("dragged",callable_mp(this,&SkeletonWorkspaceDocking::save_layout).unbind(1));
		destination = make_tabs(split);
		if (p_side == 1 || p_side == 3) {
			split->move_child(destination, 0);
		}
	} else if (!destination) {
		destination = workspace_tabs[0];
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
	update_workspace();
	refresh_windows();
}

void SkeletonWorkspaceDocking::close_floating(Window *p_window) {
	Vector<Control *> panels;
	for (TabContainer *tabs : workspace_tabs) {
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

void SkeletonWorkspaceDocking::refresh_windows() {
	if(replaying) { return; }
	for (Window *window : floating) {
		bool populated = false;
		for (TabContainer *tabs : workspace_tabs) {
			if (tabs->get_window() == window && has_open_tabs(tabs)) {
				populated = true;
				break;
			}
		}
		window->set_visible(populated);
	}
}

Array SkeletonWorkspaceDocking::get_windows() const {
	Array result;
	for (Window *window : floating) {
		result.push_back(Rect2i(window->get_position(), window->get_size()));
	}
	return result;
}

void SkeletonWorkspaceDocking::reset_extensions() {
	restore_maximized();
	for(TabContainer *tabs:workspace_tabs) { for(int i=0;i<tabs->get_tab_count();i++) { tabs->get_tab_control(i)->remove_meta("ecs_panel_closed"); tabs->set_tab_hidden(i,false); } }
	finish_drag(true);
	Vector<Control *> panels;
	for (TabContainer *tabs : workspace_tabs) {
		for (int i = 0; i < tabs->get_tab_count(); i++) {
			panels.push_back(tabs->get_tab_control(i));
		}
	}
	for (Control *panel : panels) {
		if (panel->get_parent() != workspace_tabs[0]) {
			panel->reparent(workspace_tabs[0]);
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
	workspace_tabs.resize(base_tabs);
	workspace_splits.resize(base_splits);
	for (Control *panel : panels) {
		String name = panel->get_name();
		int index = default_slots.has(name) ? default_slots[name] : 0;
		if (panel->get_parent() != workspace_tabs[index]) {
			panel->reparent(workspace_tabs[index]);
		}
	}
}

void SkeletonWorkspaceDocking::restore(const Array &p_operations, const Array &p_windows) {
	replaying = true;
	for (int i = 0; i < p_operations.size(); i++) {
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
		auto *target = target_panel ? Object::cast_to<TabContainer>(target_panel->get_parent()) : workspace_tabs[0];
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



SkeletonWorkspaceDocking::~SkeletonWorkspaceDocking() {}
bool SkeletonWorkspaceDocking::has_open_tabs(TabContainer *tabs) {
	for(int i=0;i<tabs->get_tab_count();i++) { if(!tabs->is_tab_hidden(i)) { return true; } } return false;
}
void SkeletonWorkspaceDocking::close_tab(int index,TabContainer *tabs) {
	if(index>=0 && index<tabs->get_tab_count()) { close_panel(tabs->get_tab_control(index)); }
}
void SkeletonWorkspaceDocking::close_panel(Control *panel) {
	if(!panel) { return; } auto *tabs=Object::cast_to<TabContainer>(panel->get_parent()); if(!tabs) { return; }
	finish_drag(true); panel->set_meta("ecs_panel_closed",true); tabs->set_tab_hidden(tabs->get_tab_idx_from_control(panel),true);
	update_workspace(); refresh_windows(); save_layout();
}
void SkeletonWorkspaceDocking::show_panel(Control *panel) {
	if(!panel) { return; } auto *tabs=Object::cast_to<TabContainer>(panel->get_parent()); if(!tabs) { return; }
	panel->remove_meta("ecs_panel_closed"); int index=tabs->get_tab_idx_from_control(panel); tabs->set_tab_hidden(index,false); tabs->set_current_tab(index);
	update_workspace(); refresh_windows(); save_layout();
}
PackedStringArray SkeletonWorkspaceDocking::get_closed_panels() const {
	PackedStringArray names; for(TabContainer *tabs:workspace_tabs) { for(int i=0;i<tabs->get_tab_count();i++) { if(tabs->is_tab_hidden(i)) { names.push_back(tabs->get_tab_control(i)->get_name()); } } } return names;
}
void SkeletonWorkspaceDocking::restore_closed_panels(const PackedStringArray &names) {
	for(TabContainer *tabs:workspace_tabs) { for(int i=0;i<tabs->get_tab_count();i++) { Control *panel=tabs->get_tab_control(i); bool closed=names.has(panel->get_name()); panel->set_meta("ecs_panel_closed",closed); tabs->set_tab_hidden(i,closed); } }
	update_workspace(); refresh_windows();
}
void SkeletonWorkspaceDocking::update_tab_menu(TabContainer *tabs) {
	if (tabs->has_meta("ecs_workspace_menu")) { tabs->set_popup(Object::cast_to<PopupMenu>(tabs->get_meta("ecs_workspace_menu"))); }
}
void SkeletonWorkspaceDocking::prepare_tab_menu(TabContainer *tabs) {
	Control *active = tabs->get_current_tab_control(); if (!active) { return; }
	context_panel = active->get_name();
	auto *menu = Object::cast_to<PopupMenu>(tabs->get_meta("ecs_workspace_menu"));
	menu->clear(); menu->add_item(maximized_tabs ? TTR("Restore Panel") : TTR("Maximize Panel"), 1);
	menu->add_item(TTR("Close Tab"), 0); menu->add_item(TTR("Floating Window"), 2);
	if (active->has_meta("skeleton_panel_options")) { menu->add_item(TTR("Panel Options"),3); }

}
void SkeletonWorkspaceDocking::restore_maximized() {
	for (Control *control : maximized_hidden) { control->show(); }
	maximized_hidden.clear(); maximized_tabs = nullptr;
}
void SkeletonWorkspaceDocking::tab_menu_selected(int id) {
	Control *panel = find_panel(context_panel); if (!panel) { return; }
	auto *tabs = Object::cast_to<TabContainer>(panel->get_parent());
	if (id == 1) {
		bool restore = maximized_tabs != nullptr; restore_maximized(); update_workspace();
		if (!restore) {
			maximized_tabs = tabs; Node *branch = tabs;
			while (auto *split = Object::cast_to<SplitContainer>(branch->get_parent())) {
				for (int i=0; i<split->get_child_count(false); ++i) { auto *sibling=Object::cast_to<Control>(split->get_child(i,false)); if (sibling && sibling!=branch && sibling->is_visible()) { maximized_hidden.push_back(sibling); sibling->hide(); } }
				branch=split;
			}
		}
	} else if (id == 0) { restore_maximized(); close_panel(panel); }
	else if (id == 2) { dock(panel, nullptr, -1, panel->get_screen_position()); }
	else if (id == 3 && panel->has_meta("skeleton_panel_options")) {
		auto *menu=Object::cast_to<PopupMenu>(panel->get_meta("skeleton_panel_options"));
		if(menu) { menu->set_position(DisplayServer::get_singleton()->mouse_get_position()); menu->popup(); }
	}
}
void SkeletonWorkspaceDocking::populate_panel_menu() {
	panel_menu->clear(); menu_panels.clear();
	for(TabContainer *tabs:workspace_tabs) { for(int i=0;i<tabs->get_tab_count();i++) { String name=tabs->get_tab_control(i)->get_name(); int index=menu_panels.size(); menu_panels.push_back(name); panel_menu->add_check_item(TTR(name),index); panel_menu->set_item_checked(index,!tabs->is_tab_hidden(i)); } }
	panel_menu->add_separator(); panel_menu->add_item(TTR("Reset Layout"),1000);
}
void SkeletonWorkspaceDocking::panel_menu_selected(int id) { if(id==1000) { reset_layout(); return; } if(id>=0 && id<menu_panels.size()) { show_panel(find_panel(menu_panels[id])); } }


void SkeletonWorkspaceDocking::update_workspace() {
	if (!owner || !owner->is_inside_tree()) { return; }
	for (TabContainer *tabs:workspace_tabs) {
		for(int i=0;i<tabs->get_tab_count();i++) { tabs->set_tab_title(i,TTR(String(tabs->get_tab_control(i)->get_name()))); if(tabs->get_tab_control(i)->has_meta("skeleton_dock_icon")) { tabs->set_tab_icon(i,tabs->get_tab_control(i)->get_meta("skeleton_dock_icon")); tabs->set_tab_icon_max_width(i,16*EDSCALE); } }
		tabs->set_visible(has_open_tabs(tabs));
	}
	// Visit the real tree bottom-up: a user-created split can be above an older split.
	for(int pass=0;pass<workspace_splits.size();pass++) { for(auto *split:workspace_splits) {
		bool visible=false;
		for(int i=0;i<split->get_child_count();i++) { auto *c=Object::cast_to<Control>(split->get_child(i)); if(c && c->is_visible()) { visible=true; } }
		split->set_visible(visible);
	} }
	apply_maximized(); refresh_windows(); save_layout();
}
void SkeletonWorkspaceDocking::save_layout() {
	if(!initialized || replaying || saving || !owner->is_inside_tree() || maximized_tabs) { return; }
	Dictionary state; state["operations"]=get_operations(); state["windows"]=get_windows(); state["closed"]=get_closed_panels();
	Array offsets,selected;
	for(auto *split:workspace_splits) { offsets.push_back(split->get_split_offset()); }
	for(auto *tabs:workspace_tabs) { selected.push_back(tabs->get_current_tab()); }
	state["offsets"]=offsets; state["selected"]=selected; state["side_by_side_base"]=side_by_side_base;
	if(has_meta("saved_layout") && Dictionary(get_meta("saved_layout"))==state) { return; }
	Ref<ConfigFile> config; config.instantiate(); config->set_value("workspace","layout",state);
	if(config->save(ProjectSettings::get_singleton()->get_project_data_path().path_join("age_skeleton_layout.cfg"))==OK) { set_meta("saved_layout",state); }
}
void SkeletonWorkspaceDocking::load_layout() {
	Ref<ConfigFile> config; config.instantiate();
	if(config->load(ProjectSettings::get_singleton()->get_project_data_path().path_join("age_skeleton_layout.cfg"))==OK) {
		Dictionary state=config->get_value("workspace","layout",Dictionary());
		configure_base_layout(state.get("side_by_side_base",false));
		restore(state.get("operations",Array()),state.get("windows",Array()));
		restore_closed_panels(state.get("closed",PackedStringArray()));
		Array offsets=state.get("offsets",Array()),selected=state.get("selected",Array());
		for(int i=0;i<MIN(offsets.size(),workspace_splits.size());i++) { workspace_splits[i]->set_split_offset(offsets[i]); }
		for(int i=0;i<MIN(selected.size(),workspace_tabs.size());i++) { int n=selected[i]; auto *tabs=workspace_tabs[i]; if(n>=0 && n<tabs->get_tab_count() && !tabs->is_tab_hidden(n)) { tabs->set_current_tab(n); } }
	}
	initialized=true; update_workspace();
}
void SkeletonWorkspaceDocking::reset_layout() {
	initialized=false; reset_extensions();
	configure_base_layout(true);
	for(auto *split:workspace_splits) { split->set_split_offset(0); }
	initialized=true; update_workspace(); save_layout();
}
bool SkeletonWorkspaceDocking::run_self_test() {
	bool was_initialized=initialized; initialized=false;
	Control *tree=find_panel("Hierarchy"),*properties=find_panel("Properties");
	if(!tree || !properties || tree->get_parent()==properties->get_parent()) { return false; }
	const int original=workspace_tabs.size();
	dock(properties,Object::cast_to<TabContainer>(tree->get_parent()),1);
	bool ok=workspace_tabs.size()==original+1 && properties->get_parent()!=tree->get_parent();
	dock(properties,Object::cast_to<TabContainer>(tree->get_parent()),0);
	ok &= properties->get_parent()==tree->get_parent();
	dock(properties,nullptr,-1,Vector2(100,100));
	ok &= floating.size()==1 && properties->get_window()==floating[0];
	close_panel(properties); ok &= get_closed_panels().has("Properties"); show_panel(properties);
	Array ops=get_operations(),windows=get_windows(); reset_extensions(); restore(ops,windows);
	ok &= properties->get_window()!=owner->get_window() && properties->get_parent()!=tree->get_parent();
	// The regression runner uses a temporary project. Exercise the on-disk format,
	// including closed floating panels, instead of only replaying in memory.
	close_panel(properties); initialized=true; save_layout(); initialized=false;
	reset_extensions(); load_layout(); initialized=false;
	ok &= get_closed_panels().has("Properties") && !properties->is_visible_in_tree();
	show_panel(properties); ok &= properties->get_window()!=owner->get_window() && properties->is_visible_in_tree();
	reset_extensions(); update_workspace();
	ok &= workspace_tabs.size()==base_tabs && properties->get_parent()==workspace_tabs[default_slots["Properties"]];
	for(auto *tabs:workspace_tabs) { ok &= tabs->get_popup()!=nullptr && tabs->get_tab_bar()->get_tab_close_display_policy()==TabBar::CLOSE_BUTTON_SHOW_NEVER; }
	initialized=was_initialized;
	if(ok) { print_line("SKELETON_DOCKING_PASS split merge float close reopen replay disk_save_load reset independent_properties"); }
	return ok;
}
#endif
