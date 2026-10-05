// Local, opt-in editor automation. No network listener or arbitrary code execution.
#ifdef TOOLS_ENABLED
#include "ecs_custom_components.h"
#include "ecs_ai_value.h"
#include "ecs_scene_editor.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "editor/editor_interface.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/run/editor_run_bar.h"
#include "scene/gui/button.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/main/scene_tree.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_preset.h"
#include "editor/export/editor_export_platform.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "core/io/config_file.h"
#include "main/performance.h"
#include "scene/resources/animation.h"

static Mutex ai_error_mutex;
static Array ai_errors;
static ErrorHandlerList ai_error_handler;
static PrintHandlerList ai_print_handler;
static Array ai_logs;
static uint64_t ai_log_sequence = 0;
static void ai_capture_print(void *, const String &text, bool error, bool) {
	MutexLock lock(ai_error_mutex);
	Dictionary entry; entry["sequence"] = int64_t(++ai_log_sequence); entry["level"] = error ? "error" : "info"; entry["message"] = text;
	ai_logs.append(entry);
	if (ai_logs.size() > 1000) { ai_logs.remove_at(0); }
}
static void ai_capture_error(void *, const char *function, const char *file, int line, const char *error, const char *message, bool, ErrorHandlerType type) {
	MutexLock lock(ai_error_mutex);
	Dictionary entry;
	entry["file"] = String::utf8(file);
	entry["line"] = line;
	entry["function"] = String::utf8(function);
	entry["message"] = String::utf8(message && *message ? message : error);
	entry["severity"] = type == ERR_HANDLER_WARNING ? "warning" : "error";
	if (ai_errors.size() >= 200) {
		ai_errors.remove_at(0);
	}
	ai_errors.append(entry);
}

void ECSSceneEditorPanel::ai_stop() {
	if (ai_directory.is_empty()) {
		return;
	}
	remove_error_handler(&ai_error_handler);
	remove_print_handler(&ai_print_handler);
	DirAccess::remove_absolute(ai_directory.path_join("session.json"));
	ai_directory = String();
}



static bool ai_project_path(const String &path) {
	return path.begins_with("res://") && !path.contains("..") && !path.contains("::") && !path.contains("\\");
}

static Variant ai_encode(const Variant &value, int depth = 0) {
	if (depth > 16) {
		return "<depth limit>";
	}
	if (value.get_type() == Variant::DICTIONARY) {
		Dictionary out, data = value;
		for (const Variant &key : data.keys()) {
			out[String(key)] = ai_encode(data[key], depth + 1);
		}
		return out;
	}
	if (value.get_type() == Variant::ARRAY) {
		Array out, data = value;
		for (const Variant &item : data) {
			out.push_back(ai_encode(item, depth + 1));
		}
		return out;
	}
	if (value.get_type() == Variant::OBJECT) {
		Dictionary out;
		Ref<Resource> resource = value;
		if (resource.is_valid()) {
			out["resource"] = resource->get_path();
			out["class"] = resource->get_class();
		}
		return out;
	}
	if (value.get_type() <= Variant::STRING) {
		return value;
	}
	Dictionary out;
	out["type"] = Variant::get_type_name(value.get_type());
	Array items;
	if (value.get_type() == Variant::VECTOR2) {
		Vector2 v = value;
		items.append(v.x);
		items.append(v.y);
	} else if (value.get_type() == Variant::VECTOR3) {
		Vector3 v = value;
		items.append(v.x);
		items.append(v.y);
		items.append(v.z);
	} else if (value.get_type() == Variant::VECTOR4) {
		Vector4 v = value;
		for (int i = 0; i < 4; i++) {
			items.append(v[i]);
		}
	} else if (value.get_type() == Variant::COLOR) {
		Color v = value;
		for (int i = 0; i < 4; i++) {
			items.append(v[i]);
		}
	} else if (value.get_type() == Variant::QUATERNION) {
		Quaternion v = value;
		for (int i = 0; i < 4; i++) {
			items.append(v[i]);
		}
	} else if (value.get_type() == Variant::RECT2) {
		Rect2 v = value; items.append(v.position.x); items.append(v.position.y); items.append(v.size.x); items.append(v.size.y);
	} else {
		out["text"] = String(value);
		return out;
	}
	out["values"] = items;
	return out;
}

static bool ai_decode(const Variant &input, Variant::Type type, Variant &out) {
	if (type == Variant::ARRAY || type == Variant::DICTIONARY || type == Variant::TRANSFORM2D || type == Variant::TRANSFORM3D || type == Variant::BASIS || type == Variant::PROJECTION || type == Variant::AABB || type == Variant::PLANE || type == Variant::STRING_NAME || type == Variant::NODE_PATH || type >= Variant::PACKED_BYTE_ARRAY || type == Variant::VECTOR2I || type == Variant::VECTOR3I || type == Variant::VECTOR4I || type == Variant::RECT2I) {
		bool ok = true;
		out = ECSAIValue::decode(input, ok);
		return ok && out.get_type() == type;
	}
	if (type == Variant::OBJECT) {
		if (input.get_type() == Variant::NIL) {
			out = Variant();
			return true;
		}
		if (input.get_type() != Variant::DICTIONARY) {
			return false;
		}
		String path = Dictionary(input).get("resource", "");
		if (!ai_project_path(path)) {
			return false;
		}
		Ref<Resource> resource = ResourceLoader::load(path);
		out = resource;
		return resource.is_valid();
	}
	if (type == Variant::INT || type == Variant::FLOAT) {
		if (input.get_type() != Variant::INT && input.get_type() != Variant::FLOAT) {
			return false;
		}
		double value = input;
		if (!Math::is_finite(value)) {
			return false;
		}
		// JSON numeric integers must fit exactly; large runtime handles are not editor indices.
		if (type == Variant::INT && (Math::abs(value) > 9007199254740991.0 || Math::floor(value) != value)) {
			return false;
		}
		out = type == Variant::INT ? Variant(int64_t(value)) : Variant(value);
		return true;
	}
	if (type == Variant::BOOL || type == Variant::STRING) {
		out = input;
		return input.get_type() == type;
	}
	if (input.get_type() != Variant::DICTIONARY) {
		return false;
	}
	Dictionary data = input;
	if (String(data.get("type", "")) != Variant::get_type_name(type) || data.get("values", Variant()).get_type() != Variant::ARRAY) {
		return false;
	}
	Array values = data["values"];
	int count = type == Variant::VECTOR2 ? 2 : type == Variant::VECTOR3							  ? 3
			: (type == Variant::VECTOR4 || type == Variant::COLOR || type == Variant::QUATERNION || type == Variant::RECT2) ? 4
																								  : 0;
	if (!count || values.size() != count) {
		return false;
	}
	double v[4] = {};
	for (int i = 0; i < count; i++) {
		if ((values[i].get_type() != Variant::FLOAT && values[i].get_type() != Variant::INT) || !Math::is_finite(double(values[i])) || Math::abs(double(values[i])) > 1e30) {
			return false;
		}
		v[i] = values[i];
	}
	if (type == Variant::RECT2) {
		out = Rect2(v[0], v[1], v[2], v[3]);
	} else if (type == Variant::VECTOR2) {
		out = Vector2(v[0], v[1]);
	} else if (type == Variant::VECTOR3) {
		out = Vector3(v[0], v[1], v[2]);
	} else if (type == Variant::VECTOR4) {
		out = Vector4(v[0], v[1], v[2], v[3]);
	} else if (type == Variant::COLOR) {
		out = Color(v[0], v[1], v[2], v[3]);
	} else {
		out = Quaternion(v[0], v[1], v[2], v[3]);
	}
	return true;
}

void ECSSceneEditorPanel::ai_start() {
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-ai-compile-worker")) {
		callable_mp(this, &ECSSceneEditorPanel::ai_compile_worker).call_deferred();
		return;
	}
	if (!OS::get_singleton()->get_cmdline_user_args().find("--ecs-ai-editor") && !bool(ProjectSettings::get_singleton()->get_setting("ecs/editor/ai_tools", false))) {
		return;
	}
	ai_directory = "res://.godot/agechaos-ai/" + itos(OS::get_singleton()->get_process_id());
	if (DirAccess::make_dir_recursive_absolute(ai_directory) != OK) {
		ai_directory = String();
		return;
	}
	Dictionary session;
	session["pid"] = OS::get_singleton()->get_process_id();
	session["project"] = ProjectSettings::get_singleton()->globalize_path("res://");
	session["protocol"] = 1;
	Ref<FileAccess> file = FileAccess::open(ai_directory.path_join("session.json"), FileAccess::WRITE);
	if (file.is_null()) {
		ai_directory = String();
		return;
	}
	file->store_string(JSON::stringify(session));
	file.unref();
	ai_error_handler.errfunc = ai_capture_error;
	add_error_handler(&ai_error_handler);
	ai_print_handler.printfunc = ai_capture_print;
	add_print_handler(&ai_print_handler);
	Timer *timer = memnew(Timer);
	timer->set_wait_time(0.1);
	timer->connect("timeout", callable_mp(this, &ECSSceneEditorPanel::ai_poll));
	add_child(timer);
	timer->start();
	print_line("ECS_AI_EDITOR_READY " + ProjectSettings::get_singleton()->globalize_path(ai_directory));
}

void ECSSceneEditorPanel::ai_compile_worker() {
	bool ok = false; // C# is not available in the standalone skeleton tool.
	print_line(ok ? "ECS_AI_COMPILE_OK" : "ECS_AI_COMPILE_FAILED");
	get_tree()->quit(ok ? 0 : 1);
}

void ECSSceneEditorPanel::ai_poll() {
	if (ai_directory.is_empty() || ai_executing) {
		return;
	}
	ai_executing = true;
	struct ExecutionGuard { bool &flag; ~ExecutionGuard() { flag = false; } } guard{ ai_executing };
	Ref<DirAccess> dir = DirAccess::open(ai_directory);
	if (dir.is_null()) {
		return;
	}
	Vector<String> requests;
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty() && requests.size() < 8; name = dir->get_next()) {
		if (!dir->current_is_dir() && name.ends_with(".request.json")) {
			requests.push_back(name);
		}
	}
	dir->list_dir_end();
	for (const String &name : requests) {
		String path = ai_directory.path_join(name);
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
		if (file.is_null()) {
			continue;
		}
		Dictionary response;
		response["ok"] = false;
		response["error"] = "Invalid request or size exceeds 1 MiB";
		if (file->get_length() <= 1024 * 1024) {
			JSON json;
			if (json.parse(file->get_as_text()) == OK && json.get_data().get_type() == Variant::DICTIONARY) {
				// Claim before execution: a crash must never replay an edit.
				file.unref();
				if (DirAccess::rename_absolute(path, path + ".processing") != OK) {
					continue;
				}
				response = ai_request(json.get_data());
				DirAccess::remove_absolute(path + ".processing");
			}
		}
		file.unref();
		if (FileAccess::exists(path)) {
			DirAccess::remove_absolute(path);
		}
		String output = ai_directory.path_join(name.trim_suffix(".request.json") + ".response.json");
		file = FileAccess::open(output + ".tmp", FileAccess::WRITE);
		if (file.is_valid()) {
			file->store_string(JSON::stringify(response));
			file.unref();
			DirAccess::rename_absolute(output + ".tmp", output);
		}
	}
}

Dictionary ECSSceneEditorPanel::ai_request(const Dictionary &request) {
	String op = request.get("operation", "status");
	Dictionary reply;
	auto finish = [&](bool ok, const String &error = String()) {
		reply["ok"] = ok;
		reply["operation"] = op;
		reply["revision"] = int64_t(scene_revision);
		reply["scene"] = scene.is_valid() ? scene->get_path() : String();
		reply["selected"] = selected;
		if (!error.is_empty()) {
			reply["error"] = error;
		}
		return reply;
	};
	for (const char *key : { "revision", "entity", "parent", "offset", "limit" }) {
		if (!request.has(key)) {
			continue;
		}
		Variant v = request[key];
		if ((v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT) || !Math::is_finite(double(v)) || Math::floor(double(v)) != double(v) || Math::abs(double(v)) > 2147483647.0) {
			return finish(false, String("Invalid integer argument: ") + key);
		}
	}
	if (op == "variant_validate") {
		bool ok = true;
		Variant value = ECSAIValue::decode(request.get("value", Variant()), ok);
		if (ok) { reply["value"] = ECSAIValue::encode(value); }
		return finish(ok, ok ? String() : "Unsupported or invalid value");
	}
	if (op == "performance") {
		Dictionary values;
		for (int i = 0; i < Performance::MONITOR_MAX; i++) { values[Performance::get_singleton()->get_monitor_name(Performance::Monitor(i))] = Performance::get_singleton()->get_monitor(Performance::Monitor(i)); }
		reply["monitors"] = values;
		return finish(true);
	}
	if (op == "import_status") {
		auto *fs = EditorFileSystem::get_singleton();
		reply["scanning"] = fs->is_scanning(); reply["importing"] = fs->is_importing(); reply["progress"] = fs->get_scanning_progress();
		return finish(true);
	}
	if (op == "export_presets") {
		Array presets;
		auto *exports = EditorExport::get_singleton();
		for (int i = 0; i < exports->get_export_preset_count(); i++) {
			auto preset = exports->get_export_preset(i); Dictionary item;
			item["index"] = i; item["name"] = preset->get_name(); item["platform"] = preset->get_platform()->get_name(); item["path"] = preset->get_export_path();
			presets.append(item);
		}
		reply["presets"] = presets;
		return finish(true);
	}
	if (op == "job_status" || op == "job_cancel") {
		int id = request.get("id", 0);
		if (!ai_jobs.has(id)) { return finish(false, "Unknown job ID"); }
		Dictionary job = ai_jobs[id];
		ProcessID pid = int64_t(job["pid"]);
		if (String(job["state"]) == "running") {
			bool running = OS::get_singleton()->is_process_running(pid);
			if (op == "job_cancel" && running) {
				if (OS::get_singleton()->kill(pid) != OK) { return finish(false, "Cannot cancel job"); }
				job["state"] = "cancelled";
			} else if (!running) {
				int code = OS::get_singleton()->get_process_exit_code(pid); job["exit_code"] = code;
				job["state"] = code == 0 ? "completed" : "failed";
			}
		}
		Ref<FileAccess> log = FileAccess::open(job["log"], FileAccess::READ);
		if (log.is_valid()) { job["output"] = log->get_as_text().right(32768); }
		if (String(job["state"]) == "completed") {
			bool valid = bool(job.get("compile", false)) ? String(job.get("output", "")).contains("ECS_AI_COMPILE_OK") : FileAccess::exists(job["path"]);
			if (!valid) { job["state"] = "failed"; }
		}
		ai_jobs[id] = job; reply["job"] = job;
		return finish(true);
	}
	if (op == "errors") {
		MutexLock lock(ai_error_mutex);
		reply["errors"] = ai_errors.duplicate(true);
		return finish(true);
	}
	if (op == "logs") {
		MutexLock lock(ai_error_mutex);
		Array result; int64_t since = request.get("since", 0);
		for (const Variant &item : ai_logs) { if (int64_t(Dictionary(item)["sequence"]) > since) { result.append(item); } }
		reply["logs"] = result; reply["cursor"] = int64_t(ai_log_sequence);
		reply["dropped"] = !ai_logs.is_empty() && since < int64_t(Dictionary(ai_logs[0])["sequence"]) - 1;
		return finish(true);
	}
	if (op == "assemblies") {
		String path = "res://.godot/agechaos-assemblies.json";
		reply["report"] = FileAccess::exists(path) ? JSON::parse_string(FileAccess::get_file_as_string(path)) : Variant();
		return finish(true);
	}
	if (op == "status") {
		reply["scene_preview"]=ECSAIValue::encode(spatial->runtime_preview_status());
		reply["runtime_edit"]=runtime_last_result;
		reply["runtime_results"]=runtime_results;
		reply["playing"] = EditorInterface::get_singleton()->is_playing_scene();
		reply["paused"] = EditorRunBar::get_singleton()->get_pause_button()->is_pressed();
		reply["entity_count"] = scene.is_valid() ? scene->get_entities().size() : 0;
		reply["message"] = status->get_text();
		return finish(true);
	}
	if(op=="runtime_copy" || op=="runtime_apply") {
		if(op=="runtime_copy") { runtime_copy(); } else { runtime_apply(); }
		return finish(!runtime_copied.is_empty(),runtime_copied.is_empty()?String("No runtime values selected"):String());
	}
	if(op=="runtime_drag_test") { return finish(spatial->test_runtime_drag()); }
	if(op=="runtime_resource_set") {
		int index=request.get("entity",-1);
		String path=request.get("property","");
		if(runtime_scene.is_null() || index<0 || index>=runtime_scene->get_entities().size()) { return finish(false,"No live entity"); }
		Dictionary entity=runtime_scene->get_entities()[index];
		Variant value=entity.get(path.get_slice("/",0),Variant());
		if(path.contains("/") && value.get_type()==Variant::DICTIONARY) { value=Dictionary(value).get(path.get_slice("/",1),Variant()); }
		Ref<Resource> resource=value;
		if(resource.is_null()) { return finish(false,"No live resource"); }
		String field=request.get("field",""); bool valid=false;
		Variant before=resource->get(field,&valid),decoded;
		if(!valid || !ai_decode(request.get("value",Variant()),before.get_type(),decoded)) { return finish(false,"Invalid live resource field"); }
		resource->set(field,decoded,&valid);
		reply["runtime_request"]=runtime_request_id;
		return finish(valid);
	}
	if(op=="runtime_transform") {
		bool ok=true;
		Variant value=ECSAIValue::decode(request.get("value",Variant()),ok);
		if(!ok || value.get_type()!=Variant::TRANSFORM3D || runtime_scene.is_null()) { return finish(false,"Expected a Transform3D in a live session"); }
		runtime_transform_changed(request.get("entity",-1),value);
		return finish(true);
	}
	if ((op == "screenshot" || op == "effect_capture")) {
		Ref<Image> image = get_viewport()->get_texture()->get_image();
		String path = ai_directory.path_join("editor.png");
		if (image.is_null() || image->is_empty() || image->save_png(path) != OK) {
			return finish(false, "Screenshot requires a graphical editor");
		}
		reply["path"] = ProjectSettings::get_singleton()->globalize_path(path);
		return finish(true);
	}
	if (op == "script_read") {
		String path = request.get("path", "");
		if (!ai_project_path(path) || path.begins_with("res://.godot") || path.get_extension() != "cs" || !FileAccess::exists(path)) {
			return finish(false, "Expected an existing project C# file");
		}
		reply["content"] = FileAccess::get_file_as_string(path);
		reply["sha256"] = FileAccess::get_sha256(path);
		return finish(true);
	}
	if (op == "diagnostics") {
		String path = "res://.godot/leanclr-diagnostics.json";
		reply["diagnostics"] = FileAccess::exists(path) ? JSON::parse_string(FileAccess::get_file_as_string(path)) : Variant(Array());
		reply["available"] = FileAccess::exists(path);
		return finish(true);
	}
	if (op == "resources") {
		String path = request.get("path", "res://");
		if (!ai_project_path(path) || path.begins_with("res://.godot")) {
			return finish(false, "Expected project resource directory");
		}
		Ref<DirAccess> dir = DirAccess::open(path);
		if (dir.is_null()) {
			return finish(false, "Directory unavailable");
		}
		Array entries;
		dir->list_dir_begin();
		for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
			if (name.begins_with(".")) {
				continue;
			}
			if (entries.size() == 2048) {
				reply["truncated"] = true;
				break;
			}
			Dictionary entry;
			entry["path"] = path.path_join(name);
			entry["directory"] = dir->current_is_dir();
			entries.append(entry);
		}
		dir->list_dir_end();
		reply["entries"] = entries;
		return finish(true);
	}
	if (op == "components") {
		Array names;
		for (int i = 0; i < component->get_item_count(); i++) {
			names.append(component->get_item_text(i));
		}
		reply["components"] = names;
		reply["custom_schemas"] = ai_encode(ECSCustomComponents::registry());
		return finish(true);
	}
	if (op == "entities") {
		if (scene.is_null()) {
			return finish(false, "No ECS scene open");
		}
		Array all = scene->get_entities(), page;
		int offset = MAX(0, int(request.get("offset", 0))), limit = CLAMP(int(request.get("limit", 100)), 1, 500);
		for (int i = offset; i < all.size() && page.size() < limit; i++) {
			Dictionary entry;
			entry["index"] = i;
			entry["components"] = ai_encode(all[i]);
			page.append(entry);
		}
		reply["entities"] = page;
		reply["total"] = all.size();
		return finish(true);
	}
	// All actions require a fresh scene revision, including selection and run controls.
	if (op != "inspect" && (!request.has("revision") || int64_t(request["revision"]) != int64_t(scene_revision))) {
		return finish(false, "Stale or missing revision; query status/entities again");
	}
	if (op == "script_write") {
		String path = request.get("path", ""), expected = request.get("sha256", "");
		if (!ai_project_path(path) || path.begins_with("res://.godot") || path.get_extension() != "cs" || !request.has("content")) {
			return finish(false, "Expected project C# path and content");
		}
		bool exists = FileAccess::exists(path);
		if ((exists && (expected.is_empty() || expected != FileAccess::get_sha256(path))) || (!exists && !expected.is_empty())) {
			return finish(false, "Source changed; read the current hash before writing");
		}
		DirAccess::make_dir_recursive_absolute(path.get_base_dir());
		if (exists && DirAccess::copy_absolute(path, path + ".ai-backup") != OK) {
			return finish(false, "Cannot back up source");
		}
		Ref<FileAccess> output = FileAccess::open(path + ".ai-tmp", FileAccess::WRITE);
		if (output.is_null()) { return finish(false, "Cannot write source"); }
		output->store_string(request["content"]);
		output.unref();
		if (DirAccess::rename_absolute(path + ".ai-tmp", path) != OK) { return finish(false, "Cannot replace source"); }
		reply["sha256"] = FileAccess::get_sha256(path);
		EditorFileSystem::get_singleton()->update_file(path);
		return finish(true);
	}
	if (op == "compile_start" || op == "export_start") {
		for (const Variant &key : ai_jobs.keys()) { Dictionary job = ai_jobs[key]; if (String(job["state"]) == "running" && OS::get_singleton()->is_process_running(int64_t(job["pid"]))) { return finish(false, "Another build/export job is active"); } }
		int id = ai_next_job++;
		String log = ai_directory.path_join("job-" + itos(id) + ".log");
		String path = request.get("path", ""); List<String> args;
		args.push_back("--headless"); args.push_back("--path"); args.push_back(ProjectSettings::get_singleton()->globalize_path("res://"));
		args.push_back("--log-file"); args.push_back(ProjectSettings::get_singleton()->globalize_path(log));
		if (op == "compile_start") { args.push_back("--editor"); args.push_back("--"); args.push_back("--ecs-ai-compile-worker"); }
		else {
			String name = request.get("preset", ""); bool found = false;
			for (int i = 0; i < EditorExport::get_singleton()->get_export_preset_count(); i++) { found |= EditorExport::get_singleton()->get_export_preset(i)->get_name() == name; }
			if (!found || !ai_project_path(path) || FileAccess::exists(path)) { return finish(false, "Expected known preset and unused project output path"); }
			args.push_back(bool(request.get("debug", false)) ? "--export-debug" : "--export-release"); args.push_back(name); args.push_back(ProjectSettings::get_singleton()->globalize_path(path));
		}
		ProcessID pid = 0;
		if (OS::get_singleton()->create_process(OS::get_singleton()->get_executable_path(), args, &pid) != OK) { return finish(false, "Cannot start job"); }
		Dictionary job; job["pid"] = int64_t(pid); job["state"] = "running"; job["log"] = log; job["path"] = path; job["compile"] = op == "compile_start";
		ai_jobs[id] = job; reply["id"] = id; return finish(true);
	}
	if (op == "import_scan") { EditorFileSystem::get_singleton()->scan(); return finish(true); }
	if (op == "animation_edit") {
		String path = request.get("path", "");
		if (!ai_project_path(path)) { return finish(false, "Expected project animation resource"); }
		Ref<Animation> original = ResourceLoader::load(path);
		if (original.is_null()) { return finish(false, "Not an Animation"); }
		Ref<Animation> clip = original->duplicate(true);
		String action = request.get("action", ""); int track = request.get("track", -1);
		if (action == "add_track") {
			int type = request.get("type", -1);
			if (type < Animation::TYPE_VALUE || type > Animation::TYPE_ANIMATION) { return finish(false, "Invalid track type"); }
			track = clip->add_track(Animation::TrackType(type)); clip->track_set_path(track, NodePath(String(request.get("target", "")))); reply["track"] = track;
		} else if (track < 0 || track >= clip->get_track_count()) { return finish(false, "Invalid track index"); }
		else if (action == "insert_key") {
			bool ok = true; Variant value = ECSAIValue::decode(request.get("value", Variant()), ok); double time = request.get("time", -1.0);
			if (!ok || !Math::is_finite(time) || time < 0) { return finish(false, "Invalid keyframe"); }
			int key = clip->track_insert_key(track, time, value); if (key < 0) { return finish(false, "Keyframe rejected"); } reply["key"] = key;
		} else if (action == "remove_track") { clip->remove_track(track); }
		else { return finish(false, "Unknown animation operation"); }
		if (DirAccess::copy_absolute(path, path + ".ai-backup") != OK || ResourceSaver::save(clip, path) != OK) { return finish(false, "Cannot save animation"); }
		ResourceLoader::load(path, "", ResourceLoader::CACHE_MODE_REPLACE); return finish(true);
	}
	if (op == "resource_method") {
		String path = request.get("path", ""), method = request.get("method", "");
		if (!ai_project_path(path)) { return finish(false, "Expected resource path"); }
		Ref<Resource> original = ResourceLoader::load(path);
		bool allowed = original.is_valid() && ((original->is_class("AnimationNodeStateMachine") && (method == "add_node" || method == "remove_node" || method == "add_transition" || method == "remove_transition")) || (original->is_class("AnimationLibrary") && (method == "add_animation" || method == "remove_animation")));
		if (!allowed || request.get("arguments", Variant()).get_type() != Variant::ARRAY) { return finish(false, "Resource method is not supported"); }
		Array input = request["arguments"]; if (input.size() > 16) { return finish(false, "Too many arguments"); }
		Variant values[16]; const Variant *pointers[16]; bool ok = true;
		for (int i = 0; i < input.size(); i++) { values[i] = ECSAIValue::decode(input[i], ok); pointers[i] = &values[i]; }
		if (!ok) { return finish(false, "Invalid resource arguments"); }
		Ref<Resource> copy = original->duplicate(true); Callable::CallError error;
		reply["result"] = ECSAIValue::encode(copy->callp(method, pointers, input.size(), error));
		if (error.error != Callable::CallError::CALL_OK) { return finish(false, "Resource method failed"); }
		if (DirAccess::copy_absolute(path, path + ".ai-backup") != OK || ResourceSaver::save(copy, path) != OK) { return finish(false, "Cannot save resource"); }
		ResourceLoader::load(path, "", ResourceLoader::CACHE_MODE_REPLACE); return finish(true);
	}
	if (op == "import_configure") {
		String path = request.get("path", ""); Ref<ConfigFile> config; config.instantiate();
		if (!ai_project_path(path) || config->load(path + ".import") != OK || request.get("properties", Variant()).get_type() != Variant::DICTIONARY) { return finish(false, "Invalid import file or properties"); }
		Dictionary properties = request["properties"];
		for (const Variant &key : properties.keys()) {
			if (!config->has_section_key("params", key)) { return finish(false, "Unknown import option"); }
			Variant value; Variant old = config->get_value("params", key);
			if (!ai_decode(properties[key], old.get_type(), value)) { return finish(false, "Invalid import option type"); }
			config->set_value("params", key, value);
		}
		if (DirAccess::copy_absolute(path + ".import", path + ".import.ai-backup") != OK || config->save(path + ".import") != OK) { return finish(false, "Cannot save import configuration"); }
		Vector<String> paths; paths.push_back(path); EditorFileSystem::get_singleton()->reimport_files(paths); return finish(true);
	}
	if (op == "debug_control" || op == "debug_inspect" || op == "debug_breakpoint") {
		auto *debug = EditorDebuggerNode::get_singleton();
		if (op == "debug_breakpoint") {
			String path = request.get("path", ""); int line = request.get("line", 0);
			if (!ai_project_path(path) || line <= 0) { return finish(false, "Expected source path and one-based line"); }
			debug->set_breakpoint(path, line, bool(request.get("enabled", true))); return finish(true);
		}
		auto *session = debug->get_current_debugger();
		if (!session || !session->is_session_active()) { return finish(false, "No active debugger session"); }
		if (op == "debug_inspect") {
			reply["paused"] = session->is_breaked(); reply["file"] = session->get_stack_script_file(); reply["line"] = session->get_stack_script_line(); reply["frame"] = session->get_stack_script_frame();
			if (request.has("variable")) { reply["variable"] = session->get_var_value(request["variable"]); }
			return finish(true);
		}
		String action = request.get("action", "");
		if (action == "break") { debug->debug_break(); } else if (action == "continue") { debug->debug_continue(); } else if (action == "step") { debug->debug_step(); } else if (action == "next") { debug->debug_next(); } else { return finish(false, "Unknown debugger control"); }
		return finish(true);
	}
	if (op == "new_scene") {
		String path = request.get("path", "");
		if (!ai_project_path(path) || path.begins_with("res://.godot") || path.get_extension() != "tres" || FileAccess::exists(path)) {
			return finish(false, "Expected unused project .tres path");
		}
		DirAccess::make_dir_recursive_absolute(path.get_base_dir());
		Ref<ECSScene> created;
		created.instantiate();
		if (ResourceSaver::save(created, path) != OK) { return finish(false, "Cannot save new scene"); }
		created->set_path(path);
		edit_scene(created);
		EditorFileSystem::get_singleton()->update_file(path);
		return finish(true);
	}
	if (op == "resource_create" || op == "resource_inspect" || op == "resource_set") {
		String path = request.get("path", "");
		if (!ai_project_path(path) || path.begins_with("res://.godot") || path.get_extension() != "tres") { return finish(false, "Expected project .tres path"); }
		Ref<Resource> resource;
		if (op == "resource_create") {
			String type = request.get("class", "");
			if (FileAccess::exists(path) || !ClassDB::is_parent_class(type, "Resource") || ClassDB::is_parent_class(type, "Script") || !ClassDB::can_instantiate(type)) { return finish(false, "Resource type unavailable or path already exists"); }
			resource = Object::cast_to<Resource>(ClassDB::instantiate(type));
		} else { resource = ResourceLoader::load(path); }
		if (resource.is_null()) { return finish(false, "Resource unavailable"); }
		List<PropertyInfo> properties;
		resource->get_property_list(&properties);
		if (op == "resource_inspect") {
			Array fields;
			for (const PropertyInfo &info : properties) {
				if (!(info.usage & PROPERTY_USAGE_EDITOR)) { continue; }
				Dictionary field;
				field["name"] = info.name; field["type"] = Variant::get_type_name(info.type); field["hint"] = info.hint_string; field["value"] = ai_encode(resource->get(info.name)); fields.append(field);
			}
			reply["properties"] = fields;
			reply["sha256"] = FileAccess::get_sha256(path);
			return finish(true);
		}
		if (op == "resource_set") {
			// Edit a detached copy, so failed validation/save cannot mutate a live resource.
			resource = resource->duplicate();
			String name = request.get("property", "");
			bool found = false;
			for (const PropertyInfo &info : properties) {
				if (info.name != name || name == "script" || !(info.usage & PROPERTY_USAGE_EDITOR) || (info.usage & PROPERTY_USAGE_READ_ONLY)) { continue; }
				Variant value;
				if (!ai_decode(request.get("value", Variant()), info.type, value)) { return finish(false, "Invalid resource property type"); }
				if (info.type == Variant::OBJECT && value.get_type() == Variant::OBJECT) {
					Ref<Resource> ref = value;
					bool matches = ref.is_null() || info.hint_string.is_empty();
					for (const String &type : info.hint_string.split(",")) { matches |= ref.is_valid() && ref->is_class(type); }
					if (!matches) { return finish(false, "Resource class mismatch"); }
				}
				resource->set(name, value, &found);
				break;
			}
			if (!found) { return finish(false, "Resource property unavailable"); }
			String expected = request.get("sha256", "");
			if (expected.is_empty() || expected != FileAccess::get_sha256(path)) { return finish(false, "Resource hash required; read resource_inspect first"); }
			if (DirAccess::copy_absolute(path, path + ".ai-backup") != OK) { return finish(false, "Cannot back up resource"); }
		}
		DirAccess::make_dir_recursive_absolute(path.get_base_dir());
		if (ResourceSaver::save(resource, path) != OK) { return finish(false, "Resource save failed"); }
		ResourceLoader::load(path, "", ResourceLoader::CACHE_MODE_REPLACE);
		reply["sha256"] = FileAccess::get_sha256(path);
		EditorFileSystem::get_singleton()->update_file(path);
		return finish(true);
	}
	if (op == "refresh_assemblies") {
		String platform = request.get("platform", "windows");
		if (platform != "windows" && platform != "linuxbsd" && platform != "macos" && platform != "android" && platform != "ios" && platform != "web" && platform != "ohos") {
			return finish(false, "Unsupported platform");
		}
		bool ok = false; // This standalone tool has no C# assembly builder.
		assembly_refresh_view();
		return finish(ok, ok ? String() : "Assembly graph validation failed; query assemblies");
	}
	if (op == "create_assets") {
		bool ok = assembly_create_assets();
		reply["message"] = status->get_text();
		return finish(ok, ok ? String() : status->get_text());
	}
	if (op == "build") {
		bool ok = false; // C# is not available in the standalone skeleton tool.
		return finish(ok, ok ? String() : "C# build failed; query diagnostics and editor log");
	}
	if (op == "play") {
		EditorInterface::get_singleton()->play_main_scene();
		reply["accepted"] = true;
		return finish(true);
	}
	if (op == "stop") {
		EditorInterface::get_singleton()->stop_playing_scene();
		return finish(true);
	}
	if (op == "pause") {
		Button *button = EditorRunBar::get_singleton()->get_pause_button();
		if (!EditorInterface::get_singleton()->is_playing_scene() || button->is_disabled()) {
			return finish(false, "Game is not running or pause is unavailable");
		}
		button->set_pressed(bool(request.get("paused", true)));
		button->emit_signal("pressed");
		return finish(true);
	}
	if (op == "open_scene") {
		String path = request.get("path", "");
		if (!ai_project_path(path)) {
			return finish(false, "Expected res:// ECSScene path");
		}
		Ref<ECSScene> resource = ResourceLoader::load(path);
		if (resource.is_null()) {
			return finish(false, "Not an ECSScene resource");
		}
		edit_scene(resource);
		return finish(true);
	}
	if (scene.is_null()) {
		return finish(false, "No ECS scene open");
	}
	if (op == "save") {
		if (!ai_project_path(scene->get_path())) {
			return finish(false, "Scene needs a project resource path");
		}
		return finish(ResourceSaver::save(scene, scene->get_path()) == OK, "");
	}
	if (op == "undo" || op == "redo") {
		auto *manager = EditorUndoRedoManager::get_singleton();
		UndoRedo *history = manager->get_history_undo_redo(manager->get_history_id_for_object(scene.ptr()));
		bool ok = history && (op == "undo" ? history->undo() : history->redo());
		return finish(ok, ok ? String() : "No scene action available");
	}
	if (op == "create_entity") {
		add_entity(false);
		return finish(true);
	}
	if (op == "batch_set") {
		if (request.get("changes", Variant()).get_type() != Variant::ARRAY) { return finish(false, "Expected changes array"); }
		Array changes = request["changes"], after = scene->get_entities();
		if (changes.is_empty() || changes.size() > 1000) { return finish(false, "Batch requires 1–1000 edits"); }
		for (const Variant &item : changes) {
			if (item.get_type() != Variant::DICTIONARY) { return finish(false, "Invalid batch entry"); }
			Dictionary change = item; int index = change.get("entity", -1); String name = change.get("property", "");
			if (index < 0 || index >= after.size() || name == "parent" || name == "definition" || name == "script") { return finish(false, "Invalid batch target"); }
			Ref<ECSSceneEntityEditor> target; target.instantiate(); target->target(scene, index, (name == "position" || name == "rotation" || name == "scale") ? "@transform" : "");
			List<PropertyInfo> properties; target->get_property_list(&properties); bool found = false; Variant value;
			for (const PropertyInfo &info : properties) { if (info.name == name && (info.usage & PROPERTY_USAGE_EDITOR) && !(info.usage & PROPERTY_USAGE_READ_ONLY)) { found = ai_decode(change.get("value", Variant()), info.type, value); break; } }
			if (!found) { return finish(false, "Invalid batch property/type"); }
			Dictionary definition = after[index];
			if (name.contains("/")) {
				String group = name.get_slice("/", 0), field = name.get_slice("/", 1);
				if (!definition.has(group) || definition[group].get_type() != Variant::DICTIONARY) { return finish(false, "Missing component"); }
				Dictionary data = definition[group]; data[field] = value; definition[group] = data;
			} else { definition[name] = value; }
			after[index] = definition;
		}
		Ref<ECSScene> validation = scene->duplicate(); validation->set_entities(after);
		if (validation->instantiate().is_null()) { return finish(false, "Batch violates ECS component constraints"); }
		commit(after, "AI: Batch properties"); return finish(true);
	}
	if (op == "navigation_start" || op == "navigation_status" || op == "navigation_cancel") {
		if (op == "navigation_start") {
			int index = request.get("entity", -1); if (index < 0 || index >= scene->get_entities().size()) { return finish(false, "Invalid navigation region"); }
			int64_t id = scene->start_navigation_bake(index); reply["id"] = id; return finish(id > 0);
		}
		int64_t id = request.get("id", 0);
		if (op == "navigation_cancel") { return finish(scene->cancel_navigation_bake(id)); }
		Dictionary state = scene->get_navigation_bake(id); reply["bake"] = ECSAIValue::encode(state); return finish(!state.is_empty());
	}
	int entity = int(request.get("entity", -1));
	if (entity < 0 || entity >= scene->get_entities().size()) {
		return finish(false, "Invalid entity index");
	}
	if (op == "animation_preview") {
		double time=request.get("time",0.0); String action=request.get("action","seek");
		if(!Math::is_finite(time) || time<0) { return finish(false,"Invalid animation time"); }
		set_scene_2d(true);
		if(action=="stop") { canvas->stop_animation_preview(); return finish(true); }
		if(action!="seek" && action!="play") { return finish(false,"Expected seek, play or stop"); }
		return finish(canvas->preview_animation(entity,time,action=="play",request.get("state",String())),"Animation preview rejected");
	}
	if(op=="skeletal_2d_capture") {
		Ref<Image> image=canvas->capture_canvas(); String path=ai_directory.path_join("skeletal-2d.png");
		if(image.is_null() || image->is_empty() || image->save_png(path)!=OK) { return finish(false,"Requires graphical 2D preview"); }
		reply["path"]=ProjectSettings::get_singleton()->globalize_path(path); return finish(true);
	}
	if (op == "effect_preview" || op == "effect_statistics") {
		Variant raw=request.get("time",0.0);
		if ((raw.get_type()!=Variant::INT && raw.get_type()!=Variant::FLOAT) || !Math::is_finite(double(raw))) { return finish(false,"Invalid preview time"); }
		set_scene_2d(false);
		Dictionary result=spatial->effect_preview(entity,op=="effect_statistics"?String("statistics"):String(request.get("action","restart")),double(raw));
		reply["preview"]=ECSAIValue::encode(result); return finish(result.get("ok",false),"Invalid emitter or action");
	}
	if (op == "effect_set" || op == "effect_load" || op == "effect_save") {
		Array after=scene->get_entities().duplicate(true); Dictionary definition=after[entity];
		String path=request.get("path","");
		if(op=="effect_save" || op=="effect_load") {
			if(!ai_project_path(path) || path.begins_with("res://.godot") || path.get_extension()!="tres") { return finish(false,"Expected project .tres path"); }
		}
		if(op=="effect_save") {
			if(!definition.has("particles") || FileAccess::exists(path)) { return finish(false,"Missing particles or destination already exists"); }
			Ref<Resource> preset; preset.instantiate(); preset->set_meta("particles",definition["particles"]);
			return finish(ResourceSaver::save(preset,path)==OK,"Cannot save particle preset");
		}
		Dictionary parameters;
		if(op=="effect_load") {
			Ref<Resource> preset=ResourceLoader::load(path);
			if(preset.is_null() || preset->get_meta("particles",Variant()).get_type()!=Variant::DICTIONARY) { return finish(false,"Not a particle preset"); }
			parameters=preset->get_meta("particles");
		} else {
			bool valid=true; Variant decoded=ECSAIValue::decode(request.get("parameters",Dictionary()),valid);
			if(!valid || decoded.get_type()!=Variant::DICTIONARY) { return finish(false,"Invalid effect parameters"); }
			parameters=Dictionary(definition.get("particles",Dictionary())).duplicate(true); parameters.merge(decoded,true);
		}
		for(const String &key:{String("amount"),String("seed"),String("budget")}) {
			Variant value=parameters.get(key,Variant());
			if(value.get_type()==Variant::FLOAT && Math::is_finite(double(value)) && double(value)>=0 && double(value)<=UINT32_MAX && Math::floor(double(value))==double(value)) { parameters[key]=int64_t(double(value)); }
		}
		Ref<ECSWorld> validation; validation.instantiate(); uint64_t id=validation->create_entity();
		if(!validation->set_particles(id,parameters)) { return finish(false,"Particle parameter validation failed"); }
		definition["particles"]=validation->get_particles(id); after[entity]=definition;
		commit(after,"AI: Edit particle effect"); return finish(true);
	}
	if (op == "inspect") {
		Ref<ECSSceneEntityEditor> target;
		target.instantiate();
		target->target(inspector_scene(), entity);
		List<PropertyInfo> properties;
		target->get_property_list(&properties);
		Array result;
		for (const PropertyInfo &info : properties) {
			if (!(info.usage & PROPERTY_USAGE_EDITOR) || info.type == Variant::NIL || info.name == "script") {
				continue;
			}
			Dictionary field;
			field["name"] = info.name;
			field["type"] = Variant::get_type_name(info.type);
			field["hint"] = info.hint_string;
			field["value"] = ai_encode(target->get(info.name));
			result.append(field);
		}
		reply["properties"] = result;
		return finish(true);
	}
	if (op == "select") {
		canvas_selected(entity);
		return finish(true);
	}
	uint64_t before = scene_revision;
	if (op == "delete_entity") {
		selected = entity;
		delete_entity();
	} else if (op == "duplicate_entity") {
		selected = entity;
		duplicate_entity();
	} else if (op == "reparent") {
		int parent = request.get("parent", -1);
		if (!can_reparent_entity(entity, parent)) {
			return finish(false, "Invalid parent, cycle, unchanged parent or dynamic physics restriction");
		}
		reparent_entity(entity, parent);
	} else if (op == "add_component" || op == "remove_component") {
		String name = request.get("component", "");
		if (name == "Transform") {
			name = "position";
		}
		if (name == "rotation" || name == "scale") {
			return finish(false, "Use Transform for grouped transform operations");
		}
		int option = -1;
		for (int i = 0; i < component->get_item_count(); i++) {
			if (component->get_item_text(i) == name) {
				option = i;
				break;
			}
		}
		if (option < 0) {
			return finish(false, "Unknown component; query components");
		}
		selected = entity;
		component->select(option);
		if (op == "add_component") {
			add_component();
		} else {
			remove_component();
		}
	} else if (op == "set_property") {
		String name = request.get("property", "");
		if (name == "parent") {
			return finish(false, "Use reparent for hierarchy validation");
		}
		Ref<ECSSceneEntityEditor> target;
		target.instantiate();
		target->target(inspector_scene(), entity, (name == "position" || name == "rotation" || name == "scale") ? "@transform" : "");
		setup_runtime_proxy(target);
		List<PropertyInfo> properties;
		target->get_property_list(&properties);
		bool found = false;
		for (const PropertyInfo &info : properties) {
			if (String(info.name) != name || !(info.usage & PROPERTY_USAGE_EDITOR) || info.type == Variant::NIL || name == "script") {
				continue;
			}
			Variant value;
			if (!request.has("value") || !ai_decode(request["value"], info.type, value)) {
				return finish(false, "Invalid or unsupported property value/type");
			}
			if (value.get_type() == Variant::OBJECT) {
				Ref<Resource> resource = value;
				if (resource.is_valid() && !info.hint_string.is_empty()) {
					bool matches = false;
					for (const String &type : info.hint_string.split(",")) {
						matches |= resource->is_class(type);
					}
					if (!matches) {
						return finish(false, "Resource class does not match property");
					}
				}
			}
			bool valid = false;
			target->set(info.name, value, &valid);
			if(runtime_session.is_valid()) { reply["runtime_request"]=runtime_request_id; }
			found = valid;
			break;
		}
		if (!found) {
			return finish(false, "Property unavailable or rejected by component validation");
		}
	} else {
		return finish(false, "Unknown operation");
	}
	if(op=="set_property" && runtime_session.is_valid()) { reply["accepted"]=true; return finish(true); }
	return finish(scene_revision != before, scene_revision == before ? String("No change; check component dependencies and current values") : String());
}
#endif
