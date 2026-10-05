/**************************************************************************/
/*  export_template_manager.h                                             */
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

#pragma once

#include "scene/gui/dialogs.h"

class EditorExportPreset;
class EditorFileDialog;
class Tree;
class TreeItem;

class ExportTemplateManager : public AcceptDialog {
	GDCLASS(ExportTemplateManager, AcceptDialog);

	Tree *templates_tree = nullptr;
	EditorFileDialog *import_dialog = nullptr;
	ConfirmationDialog *confirm_import = nullptr;
	String import_target;
	String import_source;

	void _refresh();
	void _check_layout();
	void _report_layout();
	void _report_closed();
	void _add_platform(const String &p_name, const PackedStringArray &p_files);
	void _open_directory();
	void _select_import();
	void _file_selected(const String &p_file);
	void _import_confirmed();
	bool _valid_file(const String &p_path, const String &p_target) const;

public:
	static String get_android_build_directory(const Ref<EditorExportPreset> &p_preset);
	static String get_android_source_zip(const Ref<EditorExportPreset> &p_preset);
	static String get_android_template_identifier(const Ref<EditorExportPreset> &p_preset);
	bool is_android_template_installed(const Ref<EditorExportPreset> &p_preset);
	bool can_install_android_template(const Ref<EditorExportPreset> &p_preset);
	Error install_android_template(const Ref<EditorExportPreset> &p_preset);
	Error install_android_template_from_file(const String &p_file, const Ref<EditorExportPreset> &p_preset);

	void popup_manager();
	bool is_downloading() const { return false; }
	void stop_download() {}
	ExportTemplateManager();
};
