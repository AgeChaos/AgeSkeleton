/**************************************************************************/
/*  export_template_manager.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "export_template_manager.h"
#include "portable_export_paths.h"

#include "core/io/dir_access.h"
#include "core/io/zip_io.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/progress_dialog.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/tree.h"
#include "scene/main/scene_tree.h"

bool ExportTemplateManager::_valid_file(const String &p_path, const String &p_target) const {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null() || file->get_length() == 0) {
		return false;
	}
	Vector<uint8_t> prefix = file->get_buffer(MIN(uint64_t(40), file->get_length()));
	if (prefix.size() == 40 && memcmp(prefix.ptr(), "version https://git-lfs.github.com/spec/v1", 40) == 0) {
		return false;
	}
	file->seek(0);
	String extension = p_target.get_extension();
	if (extension == "zip" || extension == "apk" || extension == "aar") {
		Ref<FileAccess> zip_file;
		zlib_filefunc_def io = zipio_create_io(&zip_file);
		unzFile archive = unzOpen2(p_path.utf8().get_data(), &io);
		if (!archive) {
			return false;
		}
		bool valid = unzGoToFirstFile(archive) == UNZ_OK;
		unzClose(archive);
		return valid;
	}
	if (extension == "wasm") {
		return file->get_32() == 0x6d736100 && file->get_32() == 1;
	}
	if (extension == "exe") {
		return file->get_16() == 0x5a4d;
	}
	if (p_target.begins_with("Linux/")) {
		return file->get_32() == 0x464c457f;
	}
	return true;
}

void ExportTemplateManager::_add_platform(const String &p_name, const PackedStringArray &p_files) {
	TreeItem *group = templates_tree->create_item(templates_tree->get_root());
	group->set_text(0, p_name);
	int installed = 0;
	for (const String &relative : p_files) {
		String path = PortableExportPaths::templates().path_join(relative);
		bool exists = FileAccess::exists(path);
		bool valid = exists && _valid_file(path, relative);
		installed += valid ? 1 : 0;
		TreeItem *item = templates_tree->create_item(group);
		item->set_text(0, relative);
		item->set_text(1, valid ? TTR("Installed") : exists ? TTR("Invalid file") : TTR("Missing"));
		item->set_text(2, path);
		item->set_tooltip_text(2, path);
		item->set_metadata(0, relative);
	}
	group->set_text(1, vformat("%d / %d", installed, p_files.size()));
	group->set_collapsed(true);
}

void ExportTemplateManager::_refresh() {
	templates_tree->clear();
	templates_tree->create_item();
	_add_platform("Windows", { "Windows/windows_debug_x86_64.exe", "Windows/windows_debug_x86_64.console.exe", "Windows/windows_release_x86_64.exe", "Windows/windows_release_x86_64.console.exe" });
	_add_platform("Android", { "Android/android_debug.apk", "Android/android_release.apk", "Android/android_source.zip", "Android/godot-lib.template_debug.aar", "Android/godot-lib.template_release.aar" });
	_add_platform("macOS", { "macOS/macos.zip" });
	_add_platform("iOS", { "iOS/ios.zip" });
	_add_platform("Linux", { "Linux/linux_debug.x86_64", "Linux/linux_release.x86_64", "Linux/linux_debug.arm64", "Linux/linux_release.arm64" });
	_add_platform("OpenHarmony", { "OpenHarmony/openharmony_debug_arm64-v8a_leanclr.zip", "OpenHarmony/openharmony_release_arm64-v8a_leanclr.zip", "OpenHarmony/openharmony_debug_x86_64_leanclr.zip", "OpenHarmony/openharmony_release_x86_64_leanclr.zip" });
	_add_platform("visionOS", { "visionOS/visionos.zip" });
	_add_platform("Web", { "Web/web_debug.zip", "Web/web_release.zip", "Web/debug/template.js", "Web/debug/template.wasm", "Web/release/template.js", "Web/release/template.wasm" });
	_add_platform(TTR("WeChat / Douyin (shared Web templates)"), { "Web/debug/template.js", "Web/debug/template.wasm", "Web/release/template.js", "Web/release/template.wasm" });
}

void ExportTemplateManager::_open_directory() {
	String path = PortableExportPaths::templates();
	TreeItem *item = templates_tree->get_selected();
	if (item && item->get_metadata(0).get_type() == Variant::STRING) {
		path = path.path_join(String(item->get_metadata(0))).get_base_dir();
	}
	if (!DirAccess::exists(path)) {
		path = PortableExportPaths::templates();
	}
	OS::get_singleton()->shell_open(path);
}

void ExportTemplateManager::_select_import() {
	TreeItem *item = templates_tree->get_selected();
	if (!item || item->get_metadata(0).get_type() != Variant::STRING) {
		EditorNode::get_singleton()->show_warning(TTR("Expand a platform and select the template file to import or update."));
		return;
	}
	import_target = item->get_metadata(0);
	import_dialog->clear_filters();
	String extension = import_target.get_extension();
	if (extension == "zip" || extension == "apk" || extension == "aar" || extension == "js" || extension == "wasm" || extension == "exe") {
		import_dialog->add_filter("*." + extension);
	}
	import_dialog->set_title(vformat(TTR("Import template: %s"), import_target));
	import_dialog->popup_file_dialog();
}

void ExportTemplateManager::_file_selected(const String &p_file) {
	if (!_valid_file(p_file, import_target)) {
		EditorNode::get_singleton()->show_warning(TTR("The selected file is empty, an LFS pointer, or not a valid template file format."));
		return;
	}
	import_source = p_file;
	confirm_import->set_text(vformat(TTR("Install %s\nTo: %s\nUse templates built for this LeanCLR editor and the selected platform. Existing files are backed up before replacement."), p_file, PortableExportPaths::templates().path_join(import_target)));
	confirm_import->popup_centered();
}

void ExportTemplateManager::_import_confirmed() {
	String destination = PortableExportPaths::templates().path_join(import_target);
	if (import_source.simplify_path() == destination.simplify_path()) {
		_refresh();
		return;
	}
	if (!_valid_file(import_source, import_target)) {
		EditorNode::get_singleton()->show_warning(TTR("The selected template file is no longer valid."));
		return;
	}
	Ref<DirAccess> directory = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	Error error = directory->make_dir_recursive(destination.get_base_dir());
	String temporary = destination + ".import-" + itos(OS::get_singleton()->get_ticks_usec());
	if (error == OK) {
		error = directory->copy(import_source, temporary);
	}
	if (error == OK && FileAccess::get_sha256(import_source) != FileAccess::get_sha256(temporary)) {
		error = ERR_FILE_CORRUPT;
	}
	String backup;
	if (error == OK && FileAccess::exists(destination)) {
		backup = PortableExportPaths::templates().path_join(".backups").path_join(itos(OS::get_singleton()->get_ticks_usec())).path_join(import_target);
		error = directory->make_dir_recursive(backup.get_base_dir());
		if (error == OK) {
			error = DirAccess::rename_absolute(destination, backup);
		}
	}
	if (error == OK) {
#ifndef WINDOWS_ENABLED
		if (import_target.begins_with("Linux/")) {
			FileAccess::set_unix_permissions(temporary, 0755);
		}
#endif
		error = DirAccess::rename_absolute(temporary, destination);
		if (error != OK && !backup.is_empty()) {
			Error restore_error = DirAccess::rename_absolute(backup, destination);
			if (restore_error != OK) {
				EditorNode::get_singleton()->show_warning(vformat(TTR("Could not restore the old template. Its backup is at: %s"), backup));
			}
		}
	}
	if (FileAccess::exists(temporary)) {
		DirAccess::remove_absolute(temporary);
	}
	if (error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not install template (error %d). Check directory permissions and available disk space."), error));
	}
	_refresh();
}

void ExportTemplateManager::popup_manager() {
	_refresh();
	popup_centered_clamped(Size2(780, 480) * EDSCALE, 0.8);
}

void ExportTemplateManager::_check_layout() {
	popup_manager();
	Ref<SceneTreeTimer> timer = SceneTree::get_singleton()->create_timer(1.0);
	timer->connect("timeout", callable_mp(this, &ExportTemplateManager::_report_layout));
}

void ExportTemplateManager::_report_layout() {
	print_line(vformat("TEMPLATE_LAYOUT size=%s minimum=%s parent=%s position=%s", get_size(), get_contents_minimum_size(), get_usable_parent_rect(), get_position()));
	bool valid = get_size().y <= 600 * EDSCALE && get_contents_minimum_size().y <= 480 * EDSCALE;
	valid = valid && get_usable_parent_rect().encloses(Rect2i(get_position(), get_size()));
	if (!valid) {
		SceneTree::get_singleton()->quit(1);
		return;
	}
	notification(NOTIFICATION_WM_CLOSE_REQUEST);
	Ref<SceneTreeTimer> timer = SceneTree::get_singleton()->create_timer(0.1);
	timer->connect("timeout", callable_mp(this, &ExportTemplateManager::_report_closed));
}

void ExportTemplateManager::_report_closed() {
	bool valid = !is_visible() && get_close_on_escape();
	print_line(valid ? "TEMPLATE_LAYOUT_CLOSE_OK" : "TEMPLATE_LAYOUT_CLOSE_FAILED");
	SceneTree::get_singleton()->quit(valid ? 0 : 1);
}

ExportTemplateManager::ExportTemplateManager() {
	set_title(TTR("Export Template Manager"));
	set_ok_button_text(TTR("Close"));
	set_flag(Window::FLAG_RESIZE_DISABLED, false);
	VBoxContainer *layout = memnew(VBoxContainer);
	add_child(layout);
	Label *location = memnew(Label);
	location->set_text(PortableExportPaths::templates());
	location->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	location->set_tooltip_text(PortableExportPaths::templates());
	layout->add_child(location);
	Label *help = memnew(Label);
	// A wrapping label can report a very tall minimum size before the dialog
	// receives its initial width. Keep this hint to one line instead.
	help->set_text(TTR("Expand a platform to select a template file."));
	help->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	help->set_tooltip_text(TTR("Select a file to import or update. Installed means the file is present, not that the target platform has been tested."));
	layout->add_child(help);
	HBoxContainer *actions = memnew(HBoxContainer);
	layout->add_child(actions);
	Button *refresh = memnew(Button);
	refresh->set_text(TTR("Refresh"));
	refresh->connect("pressed", callable_mp(this, &ExportTemplateManager::_refresh));
	actions->add_child(refresh);
	Button *open = memnew(Button);
	open->set_text(TTR("Open Folder"));
	open->connect("pressed", callable_mp(this, &ExportTemplateManager::_open_directory));
	actions->add_child(open);
	Button *import = memnew(Button);
	import->set_text(TTR("Import / Update File"));
	import->connect("pressed", callable_mp(this, &ExportTemplateManager::_select_import));
	actions->add_child(import);
	templates_tree = memnew(Tree);
	templates_tree->set_columns(3);
	templates_tree->set_column_titles_visible(true);
	templates_tree->set_column_title(0, TTR("Platform / File"));
	templates_tree->set_column_title(1, TTR("Status"));
	templates_tree->set_column_title(2, TTR("Path"));
	templates_tree->set_column_expand_ratio(0, 3);
	templates_tree->set_column_expand_ratio(1, 1);
	templates_tree->set_column_expand_ratio(2, 5);
	templates_tree->set_hide_root(true);
	templates_tree->set_select_mode(Tree::SELECT_ROW);
	templates_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	templates_tree->set_custom_minimum_size(Size2(560, 280) * EDSCALE);
	layout->add_child(templates_tree);
	import_dialog = memnew(EditorFileDialog);
	import_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	import_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	import_dialog->connect("file_selected", callable_mp(this, &ExportTemplateManager::_file_selected));
	add_child(import_dialog);
	confirm_import = memnew(ConfirmationDialog);
	confirm_import->set_title(TTR("Import / Update Template"));
	confirm_import->connect("confirmed", callable_mp(this, &ExportTemplateManager::_import_confirmed));
	add_child(confirm_import);
	if (OS::get_singleton()->get_cmdline_user_args().find("--test-template-manager-layout")) {
		callable_mp(this, &ExportTemplateManager::_check_layout).call_deferred();
	}
}

String ExportTemplateManager::get_android_build_directory(const Ref<EditorExportPreset> &p_preset) {
	if (p_preset.is_valid()) {
		String gradle_build_dir = p_preset->get("gradle_build/gradle_build_directory");
		if (!gradle_build_dir.is_empty()) {
			return gradle_build_dir.path_join("build");
		}
	}
	return "res://android/build";
}

String ExportTemplateManager::get_android_source_zip(const Ref<EditorExportPreset> &p_preset) {
	if (p_preset.is_valid()) {
		String android_source_zip = p_preset->get("gradle_build/android_source_template");
		if (!android_source_zip.is_empty()) {
			return android_source_zip;
		}
	}

	String portable = PortableExportPaths::templates().path_join("Android/android_source.zip");
	if (FileAccess::exists(portable)) {
		return portable;
	}
	return portable;
}

String ExportTemplateManager::get_android_template_identifier(const Ref<EditorExportPreset> &p_preset) {
	// The template identifier is the Godot version for the default template, and the full path plus md5 hash for custom templates.
	if (p_preset.is_valid()) {
		String android_source_zip = p_preset->get("gradle_build/android_source_template");
		if (!android_source_zip.is_empty()) {
			return android_source_zip + String(" [") + FileAccess::get_md5(android_source_zip) + String("]");
		}
	}
	return GODOT_VERSION_FULL_CONFIG;
}

bool ExportTemplateManager::is_android_template_installed(const Ref<EditorExportPreset> &p_preset) {
	return DirAccess::exists(get_android_build_directory(p_preset));
}

bool ExportTemplateManager::can_install_android_template(const Ref<EditorExportPreset> &p_preset) {
	return FileAccess::exists(get_android_source_zip(p_preset));
}

Error ExportTemplateManager::install_android_template(const Ref<EditorExportPreset> &p_preset) {
	const String source_zip = get_android_source_zip(p_preset);
	ERR_FAIL_COND_V(!FileAccess::exists(source_zip), ERR_CANT_OPEN);
	return install_android_template_from_file(source_zip, p_preset);
}

Error ExportTemplateManager::install_android_template_from_file(const String &p_file, const Ref<EditorExportPreset> &p_preset) {
	// To support custom Android builds, we install the Java source code and buildsystem
	// from android_source.zip to the project's res://android folder.

	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	ERR_FAIL_COND_V(da.is_null(), ERR_CANT_CREATE);

	String build_dir = get_android_build_directory(p_preset);
	String parent_dir = build_dir.get_base_dir();

	// Make parent of the build dir (if it does not exist).
	da->make_dir_recursive(parent_dir);
	{
		// Add identifier, to ensure building won't work if the current template doesn't match.
		Ref<FileAccess> f = FileAccess::open(parent_dir.path_join(".build_version"), FileAccess::WRITE);
		ERR_FAIL_COND_V(f.is_null(), ERR_CANT_CREATE);
		f->store_line(get_android_template_identifier(p_preset));
	}

	// Create the android build directory.
	Error err = da->make_dir_recursive(build_dir);
	ERR_FAIL_COND_V(err != OK, err);
	{
		// Add an empty .gdignore file to avoid scan.
		Ref<FileAccess> f = FileAccess::open(build_dir.path_join(".gdignore"), FileAccess::WRITE);
		ERR_FAIL_COND_V(f.is_null(), ERR_CANT_CREATE);
		f->store_line("");
	}

	// Uncompress source template.

	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);

	unzFile pkg = unzOpen2(p_file.utf8().get_data(), &io);
	ERR_FAIL_NULL_V_MSG(pkg, ERR_CANT_OPEN, "Android sources not in ZIP format.");

	int ret = unzGoToFirstFile(pkg);
	int total_files = 0;
	// Count files to unzip.
	while (ret == UNZ_OK) {
		total_files++;
		ret = unzGoToNextFile(pkg);
	}
	ret = unzGoToFirstFile(pkg);

	ProgressDialog::get_singleton()->add_task("uncompress_src", TTR("Uncompressing Android Build Sources"), total_files);

	HashSet<String> dirs_tested;
	int idx = 0;
	while (ret == UNZ_OK) {
		// Get file path.
		unz_file_info info;
		char fpath[16384];
		ret = unzGetCurrentFileInfo(pkg, &info, fpath, 16384, nullptr, 0, nullptr, 0);
		if (ret != UNZ_OK) {
			break;
		}

		String path = String::utf8(fpath);
		String base_dir = path.get_base_dir();

		if (!path.ends_with("/")) {
			Vector<uint8_t> uncomp_data;
			uncomp_data.resize(info.uncompressed_size);

			// Read.
			unzOpenCurrentFile(pkg);
			unzReadCurrentFile(pkg, uncomp_data.ptrw(), uncomp_data.size());
			unzCloseCurrentFile(pkg);

			if (!dirs_tested.has(base_dir)) {
				da->make_dir_recursive(build_dir.path_join(base_dir));
				dirs_tested.insert(base_dir);
			}

			String to_write = build_dir.path_join(path);
			Ref<FileAccess> f = FileAccess::open(to_write, FileAccess::WRITE);
			if (f.is_valid()) {
				f->store_buffer(uncomp_data.ptr(), uncomp_data.size());
				f.unref(); // close file.
#ifndef WINDOWS_ENABLED
				FileAccess::set_unix_permissions(to_write, (info.external_fa >> 16) & 0x01FF);
#endif
			} else {
				ERR_PRINT("Can't uncompress file: " + to_write);
			}
		}

		ProgressDialog::get_singleton()->task_step("uncompress_src", path, idx);

		idx++;
		ret = unzGoToNextFile(pkg);
	}

	ProgressDialog::get_singleton()->end_task("uncompress_src");
	unzClose(pkg);
	EditorFileSystem::get_singleton()->scan_changes();
	return OK;
}
