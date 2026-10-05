#pragma once
#ifdef TOOLS_ENABLED
#include "scene/gui/panel.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/main/node.h"
#include "scene/main/window.h"

class MenuButton;
// Native editor docking. Game entities remain on the ECS runtime path.
class SkeletonWorkspaceDocking : public Node {
	GDCLASS(SkeletonWorkspaceDocking, Node);
	Control *owner = nullptr;
	Vector<TabContainer *> workspace_tabs;
	Vector<SplitContainer *> workspace_splits;
	HashMap<String, int> default_slots;
	int base_tabs=0, base_splits=0;
	bool initialized=false, saving=false;
	bool side_by_side_base=true;
	void configure_base_layout(bool p_side_by_side);
	double save_elapsed=0;
	void update_workspace();
	void save_layout();
	void load_layout();
	struct SplitRecord {
		SplitContainer *split;
		Control *original;
	};
	Vector<SplitRecord> splits;
	Vector<Window *> floating;
	Array operations;
	Control *candidate = nullptr;
	Vector2 drag_start;
	bool dragging = false, replaying = false;
	TabContainer *drop_target = nullptr;
	int drop_side = 0, drop_index = -1;
	Panel *hint = nullptr;
	PopupMenu *panel_menu = nullptr, *tab_menu = nullptr;
	PackedStringArray menu_panels;
	String context_panel;
	Vector<Control *> maximized_hidden;
	TabContainer *maximized_tabs = nullptr;
	void prepare_tab_menu(TabContainer *p_tabs);
	void restore_maximized();
	void close_tab(int p_index, TabContainer *p_tabs);
	void tab_menu_selected(int p_id);
	void populate_panel_menu();
	void panel_menu_selected(int p_id);
	void tab_input(const Ref<InputEvent> &p_event, TabContainer *p_tabs);
	void finish_drag(bool p_cancel);
	void update_target(const Vector2 &p_screen);
	void close_floating(Window *p_window);
	TabContainer *make_tabs(Node *p_parent);
	Control *find_panel(const String &p_name) const;
	void clear_hint();

protected:
	void _notification(int p_what);

public:
	static bool has_open_tabs(TabContainer *p_tabs);
	void close_panel(Control *p_panel);
	void show_panel(Control *p_panel);
	PackedStringArray get_closed_panels() const;
	void restore_closed_panels(const PackedStringArray &p_names);
	~SkeletonWorkspaceDocking();
	void setup(Control *p_owner, MenuButton *p_menu, const Vector<TabContainer *> &p_tabs, const Vector<SplitContainer *> &p_splits);
	void reset_layout();
	void register_tabs(TabContainer *p_tabs);
	void update_tab_menu(TabContainer *p_tabs);
	void apply_maximized() { for (Control *control : maximized_hidden) { control->hide(); } }
	void dock(Control *p_panel, TabContainer *p_target, int p_side, const Vector2 &p_screen = Vector2(), int p_index = -1);
	void reset_extensions();
	void restore(const Array &p_operations, const Array &p_windows);
	Array get_operations() const { return operations.duplicate(true); }
	Array get_windows() const;
	void refresh_windows();
	bool run_self_test();
};
#endif
