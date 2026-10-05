#ifdef TOOLS_ENABLED
#include "ecs_scene_editor.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"

void ECSSceneEditorPanel::assembly_visual_test(int stage) {
	if (stage == 0) {
		assembly_action(0);
		Control *panel = Object::cast_to<Control>(assembly_tree->get_parent());
		auto *tabs = Object::cast_to<TabContainer>(panel->get_parent());
		if (!tabs || !assembly_tree->get_root() || !assembly_tree->get_root()->get_first_child()) {
			get_tree()->quit(1);
			return;
		}
		tabs->set_current_tab(tabs->get_tab_idx_from_control(panel));
		assembly_tree->get_root()->get_first_child()->select(0);
		assembly_selected();
		get_tree()->create_timer(1.0)->connect("timeout", callable_mp(this, &ECSSceneEditorPanel::assembly_visual_test).bind(1));
	} else {
		Ref<Image> screenshot = get_viewport()->get_texture()->get_image();
		bool ok = screenshot.is_valid() && screenshot->save_png("D:/GODOTS/TempSDK/ecs-assemblies-panel.png") == OK;
		if (ok) {
			print_line("ECS_ASSEMBLIES_PANEL_PASS");
		}
		get_tree()->quit(ok ? 0 : 1);
	}
}



void ECSSceneEditorPanel::assembly_selected() {
	TreeItem *item = assembly_tree->get_selected();
	if (!item) {
		return;
	}
	assembly_details->set_text(JSON::stringify(item->get_metadata(0), "  "));
}

void ECSSceneEditorPanel::assembly_refresh_view() {
	String path = "res://.godot/agechaos-assemblies.json";
	if (!FileAccess::exists(path)) {
		return;
	}
	Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(path));
	if (parsed.get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary report = parsed;
	assembly_tree->clear();
	TreeItem *root = assembly_tree->create_item();
	Array assemblies = report.get("assemblies", Array());
	for (const Variant &value : assemblies) {
		Dictionary entry = value;
		TreeItem *item = assembly_tree->create_item(root);
		item->set_text(0, entry.get("name", ""));
		item->set_text(1, bool(entry.get("editor_only", false)) ? String(U"仅编辑器") : String(U"运行时"));
		item->set_text(2, entry.get("framework", ""));
		item->set_text(3, itos(Array(entry.get("sources", Array())).size()));
		item->set_metadata(0, entry);
	}
	Dictionary summary = report.duplicate();
	summary.erase("assemblies");
	assembly_details->set_text(JSON::stringify(summary, "  "));
}

void ECSSceneEditorPanel::assembly_action(int action) {
	if (action == 0) {
		bool ok = false; // This standalone tool has no C# assembly builder.
		assembly_refresh_view();
		status->set_text(ok ? String(U"程序集依赖已刷新。") : String(U"程序集依赖校验失败，查看报告和控制台。"));
	} else if (action == 1) {
		bool ok = false; // C# is not available in the standalone skeleton tool.
		status->set_text(ok ? String(U"项目构建完成。") : String(U"项目构建失败，查看编译诊断。"));
	} else if (action == 2) {
		Ref<DirAccess> dir = DirAccess::open("res://");
		PackedStringArray solutions;
		if (dir.is_valid()) {
			dir->list_dir_begin();
			for (String file = dir->get_next(); !file.is_empty(); file = dir->get_next()) {
				if (file.ends_with(".sln") || file.ends_with(".slnx")) {
					solutions.append("res://" + file);
				}
			}
			dir->list_dir_end();
		}
		if (solutions.size() > 1) {
			status->set_text(String(U"项目有多个解决方案，请从项目目录选择。"));
			OS::get_singleton()->shell_open(ProjectSettings::get_singleton()->globalize_path("res://"));
			return;
		}
		String path = solutions.size() == 1 ? solutions[0] : String("res://.godot/agechaos-ide/AgeChaos.Generated.sln");
		if (!FileAccess::exists(path)) {
			status->set_text(String(U"还没有解决方案，请先构建 C# 项目。"));
			return;
		}
		OS::get_singleton()->shell_open(ProjectSettings::get_singleton()->globalize_path(path));
	} else if (action == 3) {
		assembly_create_assets();
	}
}
bool ECSSceneEditorPanel::assembly_create_assets() {
	// Add directories only; existing project files and business code are untouched.
	for (const char *path : { "Bundles", "Config", "Editor", "Fonts", "Plugins", "Res", "Resources", "Scenes", "Scripts", "Settings", "Scripts/Core", "Scripts/Loader", "Scripts/Bundles", "Scripts/HotUpdate/Model", "Scripts/HotUpdate/Hotfix", "Scripts/ThirdParty", "Scripts/LeanCLR" }) {
		String directory = String("res://Assets/") + path;
		if (DirAccess::make_dir_recursive_absolute(directory) != OK || !DirAccess::dir_exists_absolute(directory)) {
			status->set_text(String(U"创建 Assets 目录失败：") + path);
			return false;
		}
	}
	EditorFileSystem::get_singleton()->scan();
	status->set_text(String(U"Assets 目录结构已创建；已有文件保持不变。"));
	return true;
}
#endif
