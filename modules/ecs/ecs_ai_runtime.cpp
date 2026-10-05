#ifdef TOOLS_ENABLED
#include "ecs_ai_runtime.h"
#include "ecs_ai_value.h"
#include "main/performance.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/input/input_map.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "servers/rendering/rendering_server.h"

void ECSAIRuntime::append(const String &level, const String &message) {
	MutexLock lock(mutex);
	Dictionary item;
	item["sequence"] = int64_t(++sequence); item["level"] = level; item["message"] = message;
	logs.append(item);
	if (logs.size() > 1000) { logs.remove_at(0); }
}
void ECSAIRuntime::printed(void *self, const String &text, bool error, bool) { static_cast<ECSAIRuntime *>(self)->append(error ? "error" : "info", text); }
void ECSAIRuntime::failed(void *self, const char *, const char *file, int line, const char *error, const char *message, bool, ErrorHandlerType type) {
	static_cast<ECSAIRuntime *>(self)->append(type == ERR_HANDLER_WARNING ? "warning" : "error", String::utf8(file) + ":" + itos(line) + " " + String::utf8(message && *message ? message : error));
}
void ECSAIRuntime::start() {
	if (!bool(ProjectSettings::get_singleton()->get_setting("ecs/editor/ai_tools", false)) && !OS::get_singleton()->get_cmdline_user_args().find("--ecs-ai-runtime")) { return; }
	directory = "res://.godot/agechaos-ai-runtime/" + itos(OS::get_singleton()->get_process_id());
	if (DirAccess::make_dir_recursive_absolute(directory) != OK) { directory = String(); return; }
	Dictionary session; session["protocol"] = 1; session["pid"] = OS::get_singleton()->get_process_id(); session["runtime"] = true;
	Ref<FileAccess> file = FileAccess::open(directory.path_join("session.json"), FileAccess::WRITE);
	if (file.is_null()) { directory = String(); return; }
	file->store_string(JSON::stringify(session));
	print_handler.printfunc = printed; print_handler.userdata = this; add_print_handler(&print_handler);
	error_handler.errfunc = failed; error_handler.userdata = this; add_error_handler(&error_handler);
}
ECSAIRuntime::~ECSAIRuntime() {
	if (!directory.is_empty()) { remove_print_handler(&print_handler); remove_error_handler(&error_handler); DirAccess::remove_absolute(directory.path_join("session.json")); }
}
void ECSAIRuntime::poll(const Ref<ECSWorld> &world, RID viewport, uint64_t frame) {
	if (directory.is_empty()) { return; }
	Ref<DirAccess> dir = DirAccess::open(directory);
	if (dir.is_null()) { return; }
	Vector<String> names;
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty() && names.size() < 8; name = dir->get_next()) { if (name.ends_with(".request.json")) { names.push_back(name); } }
	dir->list_dir_end();
	for (const String &name : names) {
		String path = directory.path_join(name);
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
		if (file.is_null()) { continue; }
		Dictionary reply; reply["ok"] = false; reply["error"] = "Invalid runtime request"; reply["frame"] = int64_t(frame);
		JSON json;
		if (file->get_length() <= 1024 * 1024 && json.parse(file->get_as_text()) == OK && json.get_data().get_type() == Variant::DICTIONARY) {
			file.unref();
			if (DirAccess::rename_absolute(path, path + ".processing") != OK) { continue; }
			Dictionary request = json.get_data(); String op = request.get("operation", "status");
			if (op == "status") { reply["statistics"] = world->get_statistics(); reply["ok"] = true; }
			else if (op == "snapshot") { reply["world"] = ECSAIValue::encode(world->serialize()); reply["ok"] = true; }
			else if (op == "performance") {
				Dictionary values; for (int i = 0; i < Performance::MONITOR_MAX; i++) { values[Performance::get_singleton()->get_monitor_name(Performance::Monitor(i))] = Performance::get_singleton()->get_monitor(Performance::Monitor(i)); }
				reply["monitors"] = values; reply["ok"] = true;
			}
			else if (op == "entities") {
				PackedInt64Array ids = world->query(PackedStringArray(), true); Array entities;
				int offset = MAX(0, int(request.get("offset", 0))), limit = CLAMP(int(request.get("limit", 100)), 1, 500);
				for (int i = offset; i < ids.size() && entities.size() < limit; i++) {
					Dictionary entity; entity["id"] = itos(ids[i]); Vector3 pos = world->get_vector(ids[i], "position"); Array position; position.append(pos.x); position.append(pos.y); position.append(pos.z); entity["position"] = position; entities.append(entity);
				}
				reply["entities"] = entities; reply["total"] = ids.size(); reply["ok"] = true;
			} else if (op == "logs") {
				MutexLock lock(mutex); Array result; int64_t since = request.get("since", 0);
				for (const Variant &item : logs) { if (int64_t(Dictionary(item)["sequence"]) > since) { result.append(item); } }
				reply["logs"] = result; reply["cursor"] = int64_t(sequence); reply["dropped"] = !logs.is_empty() && since < int64_t(Dictionary(logs[0])["sequence"]) - 1; reply["ok"] = true;
			} else if (op == "effect_control") {
				uint64_t entity=request.get("entity",0); String action=request.get("action","restart");
				reply["ok"]=world->control_particles(entity,action,request.get("time",0.0)); reply["statistics"]=world->get_particles_statistics();
			} else if(op=="effect_trigger") {
				reply["ok"]=world->trigger_effect(request.get("entity",0),request.get("event","play"));
			} else if(op=="effect_statistics") {
				reply["statistics"]=world->get_particles_statistics(); reply["ok"]=true;
			} else if (op == "screenshot" && viewport.is_valid()) {
				Ref<Image> image = RenderingServer::get_singleton()->texture_2d_get(RenderingServer::get_singleton()->viewport_get_texture(viewport));
				String output = directory.path_join("game.png");
				if (image.is_valid() && !image->is_empty() && image->save_png(output) == OK) { reply["path"] = ProjectSettings::get_singleton()->globalize_path(output); reply["ok"] = true; }
			} else if (op == "input") {
				String kind = request.get("kind", ""); Ref<InputEvent> event;
				if (kind == "action") {
					String action = request.get("action", "");
					if (InputMap::get_singleton()->has_action(action)) { Ref<InputEventAction> e; e.instantiate(); e->set_action(action); e->set_pressed(bool(request.get("pressed", true))); event = e; }
				} else if (kind == "key") {
					int key = request.get("keycode", 0);
					if (key > 0) { Ref<InputEventKey> e; e.instantiate(); e->set_keycode(Key(key)); e->set_physical_keycode(Key(key)); e->set_pressed(bool(request.get("pressed", true))); event = e; }
				} else if (kind == "mouse_button") {
					int button = request.get("button", 1); Vector2 pos(request.get("x", 0.0), request.get("y", 0.0));
					if (button > 0 && button <= 9 && pos.is_finite()) { Ref<InputEventMouseButton> e; e.instantiate(); e->set_button_index(MouseButton(button)); e->set_position(pos); e->set_global_position(pos); e->set_pressed(bool(request.get("pressed", true))); event = e; }
				} else if (kind == "touch") {
					Ref<InputEventScreenTouch> e; e.instantiate(); e->set_index(request.get("index", 0)); e->set_position(Vector2(request.get("x", 0.0), request.get("y", 0.0))); e->set_pressed(request.get("pressed", false)); event = e;
				} else if (kind == "drag") {
					Ref<InputEventScreenDrag> e; e.instantiate(); e->set_index(request.get("index", 0)); e->set_position(Vector2(request.get("x", 0.0), request.get("y", 0.0))); e->set_relative(Vector2(request.get("dx", 0.0), request.get("dy", 0.0))); event = e;
				} else if (kind == "joy_axis") {
					int axis = request.get("axis", -1); double value = request.get("value", 0.0);
					if (axis >= 0 && axis < int(JoyAxis::MAX) && Math::is_finite(value) && value >= -1 && value <= 1) { Ref<InputEventJoypadMotion> e; e.instantiate(); e->set_device(request.get("device", 0)); e->set_axis(JoyAxis(axis)); e->set_axis_value(value); event = e; }
				} else if (kind == "joy_button") {
					int button = request.get("button", -1);
					if (button >= 0 && button < int(JoyButton::MAX)) { Ref<InputEventJoypadButton> e; e.instantiate(); e->set_device(request.get("device", 0)); e->set_button_index(JoyButton(button)); e->set_pressed(request.get("pressed", false)); event = e; }
				} else if (kind == "mouse_motion") {
					Vector2 pos(request.get("x", 0.0), request.get("y", 0.0)), relative(request.get("dx", 0.0), request.get("dy", 0.0));
					if (pos.is_finite() && relative.is_finite()) { Ref<InputEventMouseMotion> e; e.instantiate(); e->set_position(pos); e->set_global_position(pos); e->set_relative(relative); event = e; }
				}
				if (event.is_valid()) { Input::get_singleton()->parse_input_event(event); reply["ok"] = true; reply["accepted"] = true; }
			}
			DirAccess::remove_absolute(path + ".processing");
		}
		file.unref();
		if (FileAccess::exists(path)) { DirAccess::remove_absolute(path); }
		if (bool(reply["ok"])) { reply.erase("error"); }
		String output = directory.path_join(name.trim_suffix(".request.json") + ".response.json");
		file = FileAccess::open(output + ".tmp", FileAccess::WRITE);
		if (file.is_valid()) { file->store_string(JSON::stringify(reply)); file.unref(); DirAccess::rename_absolute(output + ".tmp", output); }
	}
}
#endif
