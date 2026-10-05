#pragma once
#ifdef TOOLS_ENABLED
#include "scene/gui/panel.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/main/node.h"
#include "scene/main/window.h"

class ECSSceneEditorPanel;
// Native editor docking. Game entities remain on the ECS runtime path.
class ECSWorkspaceDocking : public Node {
	GDCLASS(ECSWorkspaceDocking, Node);
	ECSSceneEditorPanel *owner = nullptr;
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
	bool run_visibility_test();
	~ECSWorkspaceDocking();
	void setup(ECSSceneEditorPanel *p_owner);
	void register_tabs(TabContainer *p_tabs);
	void dock(Control *p_panel, TabContainer *p_target, int p_side, const Vector2 &p_screen = Vector2(), int p_index = -1);
	void reset_extensions();
	void restore(const Array &p_operations, const Array &p_windows);
	Array get_operations() const { return operations.duplicate(true); }
	Array get_windows() const;
	void refresh_windows();
	bool run_self_test();
};
#endif
