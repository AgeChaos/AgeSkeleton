#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_sprite_atlas.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/export/editor_export_plugin.h"
#include "scene/gui/box_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/check_box.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/label.h"
#include "scene/main/timer.h"
#include "editor/gui/editor_file_dialog.h"

class SpriteAtlasExportPlugin : public EditorExportPlugin {
	GDCLASS(SpriteAtlasExportPlugin, EditorExportPlugin);
	Dictionary sprites, sprite_pages;
	HashSet<String> emitted;
protected:
	void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;
	void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override;
	void _export_end() override;
public:
	String get_name() const override { return "AgeChaosSpriteAtlas"; }
	static String platform_name(const HashSet<String> &p_features);
};

class SpriteAtlasPanel : public VBoxContainer {
	GDCLASS(SpriteAtlasPanel, VBoxContainer);
	Ref<SpriteAtlas> atlas;
	SpriteAtlasBuild result;
	ItemList *sources = nullptr;
	OptionButton *platform = nullptr, *compression = nullptr, *page = nullptr;
	SpinBox *max_size = nullptr, *padding = nullptr, *quality = nullptr;
	CheckBox *trim = nullptr, *enabled = nullptr, *override_platform = nullptr;
	TextureRect *preview = nullptr;
	Label *status = nullptr, *title = nullptr;
	Timer *rebuild_timer = nullptr;
	EditorFileDialog *dialog = nullptr;
	int dialog_action = 0;
	bool updating = false;
	bool dirty = false;
	void refresh();
	void settings_changed();
	void changed();
	void resource_changed();
	void rebuild();
	void select_page(int p_index);
	void select_platform(int p_index);
	void choose(int p_action);
	void chosen(const String &p_path);
	void files_chosen(const Vector<String> &p_paths);
	void remove_source();
	void save();
	void files_changed(const PackedStringArray &p_files);
	void add_sources(const PackedStringArray &p_paths);
	void undo_sources(const PackedStringArray &p_paths);
protected:
	void _notification(int p_what);
public:
	String unsaved_status() const { return dirty ? String(U"精灵图集有未保存的修改。") : String(); }
	void save_existing() { if (dirty && atlas.is_valid() && !atlas->get_path().is_empty()) { save(); } }
	void edit(const Ref<SpriteAtlas> &p_atlas);
	bool can_drop_data(const Point2 &p_point, const Variant &p_data) const override;
	void drop_data(const Point2 &p_point, const Variant &p_data) override;
	SpriteAtlasPanel();
};

class SpriteAtlasEditorPlugin : public EditorPlugin {
	GDCLASS(SpriteAtlasEditorPlugin, EditorPlugin);
	SpriteAtlasPanel *panel = nullptr;
	Window *window = nullptr;
	Ref<SpriteAtlasExportPlugin> exporter;
	void open();
protected:
	void _notification(int p_what);
public:
	bool handles(Object *p_object) const override { return Object::cast_to<SpriteAtlas>(p_object) != nullptr; }
	void edit(Object *p_object) override;
	String get_plugin_name() const override { return "SpriteAtlas"; }
	String get_unsaved_status(const String &p_for_scene = "") const override { return panel ? panel->unsaved_status() : String(); }
	void save_external_data() override { if (panel) { panel->save_existing(); } }
};
#endif
