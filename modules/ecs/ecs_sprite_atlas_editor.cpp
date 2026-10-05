#ifdef TOOLS_ENABLED
#include "ecs_sprite_atlas_editor.h"
#include "core/object/callable_mp.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/gui/split_container.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/image_texture.h"

bool sprite_atlas_self_test();

static void find_atlases(EditorFileSystemDirectory *p_dir, Vector<String> &r_paths) {
	if (!p_dir) { return; }
	for (int i = 0; i < p_dir->get_file_count(); ++i) {
		if (p_dir->get_file_type(i) == "SpriteAtlas") { r_paths.push_back(p_dir->get_file_path(i)); }
	}
	for (int i = 0; i < p_dir->get_subdir_count(); ++i) { find_atlases(p_dir->get_subdir(i), r_paths); }
}
String SpriteAtlasExportPlugin::platform_name(const HashSet<String> &p_features) {
	for (const char *name : { "wechat", "douyin", "web", "android", "ios", "openharmony", "windows", "macos", "linux" }) {
		if (p_features.has(name)) { return name; }
	}
	return "default";
}
void SpriteAtlasExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	sprites.clear(); sprite_pages.clear(); emitted.clear();
	Vector<String> paths;
	find_atlases(EditorFileSystem::get_singleton()->get_filesystem(), paths);
	paths.sort();
	for (const String &path : paths) {
		Ref<SpriteAtlas> atlas = ResourceLoader::load(path);
		if (atlas.is_null() || !atlas->is_enabled()) { continue; }
		Dictionary settings = atlas->resolve_settings(platform_name(p_features));
		if (!bool(settings.get("enabled", true))) { continue; }
		SpriteAtlasBuild result;
		if (SpriteAtlasBuilder::build(atlas, platform_name(p_features), result) != OK) {
			get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Sprite Atlas", path + ": " + result.error);
			return;
		}
		for (const Variant *key = result.sprites.next(nullptr); key; key = result.sprites.next(key)) {
			if (sprites.has(*key)) {
				get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Sprite Atlas", "Image belongs to multiple enabled atlases: " + String(*key));
				return;
			}
			sprites[*key] = result.sprites[*key];
			sprite_pages[*key] = result.sprite_pages[*key];
		}
	}
}
void SpriteAtlasExportPlugin::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	if (p_type == "SpriteAtlas") { skip(); return; }
	if (!sprites.has(p_path)) { return; }
	String page_path = sprite_pages[p_path];
	if (!emitted.has(page_path)) { add_file(page_path, FileAccess::get_file_as_bytes(page_path), false); emitted.insert(page_path); }
	String sprite_path = sprites[p_path];
	add_file(sprite_path, FileAccess::get_file_as_bytes(sprite_path), true);
}
void SpriteAtlasExportPlugin::_export_end() { sprites.clear(); sprite_pages.clear(); emitted.clear(); }

SpriteAtlasPanel::SpriteAtlasPanel() {
	auto *bar = memnew(HBoxContainer); add_child(bar);
	for (int i = 0; i < 5; ++i) {
		auto *button = memnew(Button);
		button->set_text(i == 0 ? String(U"新建") : i == 1 ? String(U"打开") : i == 2 ? String(U"添加图片") : i == 3 ? String(U"添加文件夹") : String(U"保存"));
		bar->add_child(button);
		if (i == 4) { button->connect("pressed", callable_mp(this, &SpriteAtlasPanel::save)); }
		else { button->connect("pressed", callable_mp(this, &SpriteAtlasPanel::choose).bind(i)); }
	}
	title = memnew(Label); add_child(title);
	auto *split = memnew(HSplitContainer); split->set_v_size_flags(SIZE_EXPAND_FILL); add_child(split);
	auto *left = memnew(VBoxContainer); left->set_custom_minimum_size(Vector2(270, 0)); split->add_child(left);
	sources = memnew(ItemList); sources->set_v_size_flags(SIZE_EXPAND_FILL); sources->set_custom_minimum_size(Vector2(270, 150)); left->add_child(sources);
	auto *remove = memnew(Button); remove->set_text(String(U"移除选中来源")); left->add_child(remove); remove->connect("pressed", callable_mp(this, &SpriteAtlasPanel::remove_source));
	enabled = memnew(CheckBox); enabled->set_text(String(U"导出时启用图集")); left->add_child(enabled);
	enabled->connect("toggled", callable_mp(this, &SpriteAtlasPanel::settings_changed).unbind(1));
	platform = memnew(OptionButton); left->add_child(platform);
	for (const char *name : { "default", "windows", "macos", "linux", "android", "ios", "web", "wechat", "douyin", "openharmony" }) { platform->add_item(name); }
	platform->connect("item_selected", callable_mp(this, &SpriteAtlasPanel::select_platform));
	override_platform = memnew(CheckBox); override_platform->set_text(String(U"覆盖此平台设置")); left->add_child(override_platform);
	override_platform->connect("toggled", callable_mp(this, &SpriteAtlasPanel::settings_changed).unbind(1));
	auto spin = [&](const String &text, double min, double max, double step) {
		auto *row = memnew(HBoxContainer); left->add_child(row);
		auto *label = memnew(Label); label->set_text(text); label->set_h_size_flags(SIZE_EXPAND_FILL); row->add_child(label);
		auto *value = memnew(SpinBox); value->set_min(min); value->set_max(max); value->set_step(step); row->add_child(value);
		value->connect("value_changed", callable_mp(this, &SpriteAtlasPanel::settings_changed).unbind(1)); return value;
	};
	max_size = spin(String(U"最大页尺寸（2 的幂）"), 32, 8192, 32);
	padding = spin(String(U"边缘扩展 / 间距"), 1, 32, 1);
	quality = spin(String(U"压缩质量"), 0, 1, .05);
	trim = memnew(CheckBox); trim->set_text(String(U"裁剪透明边缘（保留原尺寸）")); left->add_child(trim);
	trim->connect("toggled", callable_mp(this, &SpriteAtlasPanel::settings_changed).unbind(1));
	compression = memnew(OptionButton); compression->add_item(String(U"无损")); compression->add_item(String(U"有损")); compression->add_item(String(U"Basis Universal（GPU 压缩）")); left->add_child(compression);
	compression->connect("item_selected", callable_mp(this, &SpriteAtlasPanel::settings_changed).unbind(1));
	auto *note = memnew(Label); note->set_text(String(U"不旋转图片，不生成 mipmap。\n不同图集资源独立分组，按需加载页。\n可将图片或文件夹拖到此窗口。")); left->add_child(note);
	auto *right = memnew(VBoxContainer); right->set_h_size_flags(SIZE_EXPAND_FILL); split->add_child(right);
	page = memnew(OptionButton); right->add_child(page); page->connect("item_selected", callable_mp(this, &SpriteAtlasPanel::select_page));
	preview = memnew(TextureRect); preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE); preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED); preview->set_v_size_flags(SIZE_EXPAND_FILL); preview->set_custom_minimum_size(Vector2(300, 250)); right->add_child(preview);
	status = memnew(Label); status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART); add_child(status);
	rebuild_timer = memnew(Timer); rebuild_timer->set_wait_time(.6); rebuild_timer->set_one_shot(true); add_child(rebuild_timer); rebuild_timer->connect("timeout", callable_mp(this, &SpriteAtlasPanel::rebuild));
	dialog = memnew(EditorFileDialog); add_child(dialog); dialog->connect("file_selected", callable_mp(this, &SpriteAtlasPanel::chosen)); dialog->connect("dir_selected", callable_mp(this, &SpriteAtlasPanel::chosen)); dialog->connect("files_selected", callable_mp(this, &SpriteAtlasPanel::files_chosen));
	Ref<SpriteAtlas> initial; initial.instantiate(); edit(initial);
}
void SpriteAtlasPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		EditorFileSystem::get_singleton()->connect("resources_reimported", callable_mp(this, &SpriteAtlasPanel::files_changed));
		EditorFileSystem::get_singleton()->connect("filesystem_changed", callable_mp(this, &SpriteAtlasPanel::changed));
		changed();
	}
}
void SpriteAtlasPanel::edit(const Ref<SpriteAtlas> &p_atlas) {
	if (atlas.is_valid() && atlas->is_connected("changed", callable_mp(this, &SpriteAtlasPanel::resource_changed))) { atlas->disconnect("changed", callable_mp(this, &SpriteAtlasPanel::resource_changed)); }
	atlas = p_atlas;
	dirty = false;
	atlas->connect("changed", callable_mp(this, &SpriteAtlasPanel::resource_changed)); refresh(); changed();
}
void SpriteAtlasPanel::refresh() {
	updating = true;
	title->set_text((dirty ? "* " : "") + (atlas->get_path().is_empty() ? String(U"未保存的精灵图集") : atlas->get_path()));
	sources->clear(); for (const String &path : atlas->get_sources()) { sources->add_item(path); }
	String key = platform->get_item_text(platform->get_selected());
	Dictionary settings = atlas->resolve_settings(key);
	bool custom = key != "default" && atlas->get_platform_overrides().has(key);
	override_platform->set_disabled(key == "default"); override_platform->set_pressed(custom);
	enabled->set_pressed(key == "default" ? atlas->is_enabled() : bool(settings.get("enabled", true)));
	max_size->set_value(settings.get("max_size", 2048)); padding->set_value(settings.get("padding", 2)); quality->set_value(settings.get("quality", .9)); trim->set_pressed(settings.get("trim", true)); compression->select(settings.get("compression", 0));
	bool editable = key == "default" || custom;
	max_size->set_editable(editable); padding->set_editable(editable); quality->set_editable(editable); trim->set_disabled(!editable); compression->set_disabled(!editable); enabled->set_disabled(!editable);
	updating = false;
}
void SpriteAtlasPanel::settings_changed() {
	if (updating || atlas.is_null()) { return; }
	String key = platform->get_item_text(platform->get_selected());
	Dictionary settings; settings["max_size"] = int(max_size->get_value()); settings["padding"] = int(padding->get_value()); settings["quality"] = quality->get_value(); settings["trim"] = trim->is_pressed(); settings["compression"] = compression->get_selected();
	auto *undo = EditorUndoRedoManager::get_singleton();
	undo->create_action(String(U"修改精灵图集设置 ") + key, UndoRedo::MERGE_ENDS, atlas.ptr());
	if (key == "default") {
		undo->add_do_property(atlas.ptr(), "settings", settings); undo->add_undo_property(atlas.ptr(), "settings", atlas->get_settings());
		undo->add_do_property(atlas.ptr(), "enabled", enabled->is_pressed()); undo->add_undo_property(atlas.ptr(), "enabled", atlas->is_enabled());
	} else {
		Dictionary overrides = atlas->get_platform_overrides();
		settings["enabled"] = enabled->is_pressed();
		if (override_platform->is_pressed()) { overrides[key] = settings; } else { overrides.erase(key); }
		undo->add_do_property(atlas.ptr(), "platform_overrides", overrides); undo->add_undo_property(atlas.ptr(), "platform_overrides", atlas->get_platform_overrides());
	}
	undo->commit_action(); refresh();
}
void SpriteAtlasPanel::changed() { if (is_inside_tree()) { rebuild_timer->start(); } }
void SpriteAtlasPanel::resource_changed() { dirty = true; changed(); }
void SpriteAtlasPanel::files_changed(const PackedStringArray &p_files) { changed(); }
void SpriteAtlasPanel::rebuild() {
	refresh();
	Error err = SpriteAtlasBuilder::build(atlas, platform->get_item_text(platform->get_selected()), result, true);
	page->clear(); preview->set_texture(Ref<Texture2D>());
	if (err != OK) { status->set_text(String(U"打包失败：") + result.error); return; }
	for (int i = 0; i < result.pages.size(); ++i) { page->add_item(vformat(String(U"第 %d 页 · %d × %d"), i + 1, result.previews[i]->get_width(), result.previews[i]->get_height())); }
	if (!result.pages.is_empty()) { page->select(0); select_page(0); }
	status->set_text(vformat(String(U"%d 张图片 / %d 页 · 未压缩像素内存 %.2f MiB · 导出时自动替换原图片引用"), result.sprite_count, result.pages.size(), result.pixel_bytes / 1048576.0));
}
void SpriteAtlasPanel::select_page(int p_index) { if (p_index >= 0 && p_index < result.previews.size()) { preview->set_texture(ImageTexture::create_from_image(result.previews[p_index])); } }
void SpriteAtlasPanel::select_platform(int p_index) { refresh(); changed(); }
void SpriteAtlasPanel::choose(int p_action) {
	if ((p_action == 0 || p_action == 1) && dirty && !atlas->get_path().is_empty()) { save_existing(); }
	if (p_action == 1 && dirty && atlas->get_path().is_empty()) { status->set_text(String(U"请先保存当前新图集。")); return; }
	dialog_action = p_action; dialog->clear_filters();
	if (p_action == 0 || p_action == 1) { dialog->add_filter("*.tres", "Sprite Atlas"); dialog->set_file_mode(p_action == 0 ? EditorFileDialog::FILE_MODE_SAVE_FILE : EditorFileDialog::FILE_MODE_OPEN_FILE); }
	else if (p_action == 2) { dialog->add_filter("*.png,*.jpg,*.jpeg,*.webp,*.svg,*.tga,*.bmp", "Images"); dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILES); }
	else { dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR); }
	dialog->popup_file_dialog();
}
void SpriteAtlasPanel::chosen(const String &p_path) {
	if (dialog_action == 0) {
		if (!atlas->get_path().is_empty()) { Ref<SpriteAtlas> fresh; fresh.instantiate(); edit(fresh); }
		Error err = ResourceSaver::save(atlas, p_path);
		if (err != OK) { status->set_text(String(U"保存失败：") + itos(err)); return; }
		atlas->set_path(p_path); dirty = false; EditorFileSystem::get_singleton()->update_file(p_path); refresh(); changed();
	} else if (dialog_action == 1) {
		Ref<SpriteAtlas> loaded = ResourceLoader::load(p_path);
		if (loaded.is_valid()) { edit(loaded); } else { status->set_text(String(U"请选择 SpriteAtlas 资源。")); }
	} else { PackedStringArray paths; paths.push_back(p_path); add_sources(paths); }
}
void SpriteAtlasPanel::files_chosen(const Vector<String> &p_paths) { add_sources(p_paths); }
void SpriteAtlasPanel::undo_sources(const PackedStringArray &p_paths) {
	auto *undo = EditorUndoRedoManager::get_singleton(); undo->create_action(String(U"修改图集来源"), UndoRedo::MERGE_DISABLE, atlas.ptr());
	undo->add_do_property(atlas.ptr(), "sources", p_paths); undo->add_undo_property(atlas.ptr(), "sources", atlas->get_sources()); undo->commit_action(); refresh();
}
void SpriteAtlasPanel::add_sources(const PackedStringArray &p_paths) {
	PackedStringArray paths = atlas->get_sources(); for (const String &path : p_paths) { if (!paths.has(path)) { paths.push_back(path); } } undo_sources(paths);
}
void SpriteAtlasPanel::remove_source() { PackedInt32Array selected = sources->get_selected_items(); if (selected.is_empty()) { return; } PackedStringArray paths = atlas->get_sources(); paths.remove_at(selected[0]); undo_sources(paths); }
void SpriteAtlasPanel::save() {
	if (atlas->get_path().is_empty()) { choose(0); return; }
	Error err = ResourceSaver::save(atlas); if (err != OK) { status->set_text(String(U"保存失败：") + itos(err)); } else { dirty = false; EditorFileSystem::get_singleton()->update_file(atlas->get_path()); changed(); }
}
bool SpriteAtlasPanel::can_drop_data(const Point2 &p_point, const Variant &p_data) const { if (p_data.get_type() != Variant::DICTIONARY) { return false; } Dictionary data = p_data; return data.get("type", "") == "files"; }
void SpriteAtlasPanel::drop_data(const Point2 &p_point, const Variant &p_data) { Dictionary data = p_data; add_sources(data.get("files", PackedStringArray())); }
void SpriteAtlasEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY && OS::get_singleton()->get_cmdline_user_args().find("--sprite-atlas-self-test")) {
		bool passed = sprite_atlas_self_test();
		print_line(passed ? "SPRITE_ATLAS_SELF_TEST_PASS" : "SPRITE_ATLAS_SELF_TEST_FAIL");
		get_tree()->quit(passed ? 0 : 1);
	}
	if (p_what == NOTIFICATION_ENTER_TREE) {
		exporter.instantiate(); add_export_plugin(exporter);
		window = memnew(Window); window->set_title(String(U"AgeChaos 精灵图集")); window->set_size(Vector2i(980, 680)); window->set_min_size(Vector2i(760, 520)); window->set_visible(false); add_child(window);
		panel = memnew(SpriteAtlasPanel); window->add_child(panel); panel->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT, Control::PRESET_MODE_MINSIZE, 12);
		window->connect("close_requested", callable_mp(window, &Window::hide));
		add_tool_menu_item(String(U"精灵图集…"), callable_mp(this, &SpriteAtlasEditorPlugin::open));
	} else if (p_what == NOTIFICATION_EXIT_TREE) { remove_tool_menu_item(String(U"精灵图集…")); if (EditorExport::get_singleton()) { remove_export_plugin(exporter); } }
}
void SpriteAtlasEditorPlugin::open() { window->popup_centered(); }
void SpriteAtlasEditorPlugin::edit(Object *p_object) { Ref<SpriteAtlas> resource = Object::cast_to<SpriteAtlas>(p_object); if (resource.is_valid()) { panel->edit(resource); open(); } }
#endif
