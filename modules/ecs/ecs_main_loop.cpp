#include "modules/modules_enabled.gen.h"
#ifdef MODULE_LEANCLR_ENABLED
#include "modules/leanclr/leanclr_language.h"
#endif
#if defined(MODULE_OBJECTDB_PROFILER_ENABLED) && defined(DEBUG_ENABLED)
#include "modules/objectdb_profiler/snapshot_collector.h"
#endif
#include "ecs_profile_scope.h"
#include "core/object/class_db.h"
// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_main_loop.h"
#if defined(TOOLS_ENABLED) && defined(MODULE_OBJECTDB_PROFILER_ENABLED)
#include "modules/objectdb_profiler/editor/data_viewers/managed_heap_view.h"
#endif
#include "ecs_scene.h"
#ifdef TOOLS_ENABLED
#include "ecs_live_edit.h"
#include "core/io/marshalls.h"
#endif

#include "core/config/project_settings.h"
#include "core/debugger/engine_debugger.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "scene/resources/material.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"
#include "servers/physics_2d/physics_server_2d.h"
#include "servers/physics_3d/physics_server_3d.h"

void ECSMainLoop::_bind_methods() {
	ClassDB::bind_method(D_METHOD("request_game_reload"), &ECSMainLoop::request_game_reload);
	ClassDB::bind_method(D_METHOD("quit", "exit_code"), &ECSMainLoop::quit, DEFVAL(0));
	ADD_SIGNAL(MethodInfo("input_event", PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_RESOURCE_TYPE, "InputEvent"), PropertyInfo(Variant::BOOL, "ui_handled")));
	ClassDB::bind_method(D_METHOD("get_world"), &ECSMainLoop::get_world);
	ClassDB::bind_method(D_METHOD("set_camera_transform", "transform"), &ECSMainLoop::set_camera_transform);
	ClassDB::bind_method(D_METHOD("save_scene", "path"), &ECSMainLoop::save_scene);
}
void ECSMainLoop::window_event(int event) {
	if (event == int(DisplayServerEnums::WINDOW_EVENT_FOCUS_OUT)) {
		ui_system.cancel_input();
	}
	if (event == int(DisplayServerEnums::WINDOW_EVENT_CLOSE_REQUEST)) {
		quitting = true;
	}
}
void ECSMainLoop::initialize() {
#if defined(DEBUG_ENABLED) && defined(MODULE_LEANCLR_ENABLED)
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-profiler-test")) {
		bool ok = LeanCLRLanguage::profiler_self_test();
		print_line(ok ? "LEANCLR_TIMING_UNIT_PASS" : "LEANCLR_TIMING_UNIT_FAILED");
		if (!ok) { quitting = true; OS::get_singleton()->set_exit_code(1); return; }
		if (EngineDebugger::get_singleton()) { Array opts = { 512, true }; EngineDebugger::get_singleton()->profiler_enable("servers", true, opts); }
		LeanCLRLanguage::get_singleton()->profiling_start();
		LeanCLRLanguage::get_singleton()->profiling_set_save_native_calls(true);
	}
#endif
#ifdef TOOLS_ENABLED
	ai_runtime.instantiate();
	ai_runtime->start();
#endif
	benchmark = GLOBAL_GET("ecs/run/benchmark");
	benchmark_started = OS::get_singleton()->get_ticks_usec();
	world.instantiate();
	bool test = GLOBAL_GET("ecs/run/self_test");
	if (test && !world->self_test()) {
		ERR_PRINT("ECS_SELF_TEST_FAILED");
		OS::get_singleton()->set_exit_code(1);
		quitting = true;
		return;
	}
	if (test) {
		print_line("ECS_SELF_TEST_OK entities=20000 lifecycle=pass sparse_join=pass movement=pass swap_remove=pass");
	}
	String path = GLOBAL_GET("ecs/run/scene");
	Dictionary config;
	bool resource_scene = !path.is_empty() && path.get_extension().to_lower() != "json";
	if (resource_scene) {
		scene_resource = ResourceLoader::load(path);
		if (scene_resource.is_valid()) {
			world = scene_resource->instantiate();
#ifdef TOOLS_ENABLED
			if(world.is_valid()) { editor_scene_entities=world->query(PackedStringArray(),true); }
#endif
		} else {
			world.unref();
		}
		if (world.is_null()) {
			ERR_PRINT("Cannot instantiate ECS scene: " + path);
			OS::get_singleton()->set_exit_code(1);
			quitting = true;
			return;
		}
	}
	if (!path.is_empty() && !resource_scene) {
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
		if (file.is_null()) {
			ERR_PRINT("Cannot open ECS scene: " + path);
			OS::get_singleton()->set_exit_code(1);
			quitting = true;
			return;
		}
		JSON parser;
		Error err = parser.parse(file->get_as_text());
		if (err != OK || parser.get_data().get_type() != Variant::DICTIONARY) {
			ERR_PRINT("Invalid ECS scene JSON: " + path);
			OS::get_singleton()->set_exit_code(1);
			quitting = true;
			return;
		}
		config = parser.get_data();
	}
	Variant requested = config.get("count", 0);
	if ((requested.get_type() != Variant::INT && requested.get_type() != Variant::FLOAT) || !Math::is_finite(double(requested)) || double(requested) != Math::floor(double(requested)) || double(requested) < 0 || double(requested) > 1000000) {
		ERR_PRINT("ECS count must be an integer in 0..1000000");
		OS::get_singleton()->set_exit_code(1);
		quitting = true;
		return;
	}
	count = int(requested);
	if (count < 0 || count > 1000000) {
		ERR_PRINT("ECS scene count outside 0..1000000");
		OS::get_singleton()->set_exit_code(1);
		quitting = true;
		return;
	}
	if (resource_scene) {
		count = int(world->get_statistics()["entities"]);
	} else if (config.has("entities")) {
		if (config["entities"].get_type() != Variant::ARRAY) {
			ERR_PRINT("ECS entities must be an array");
			OS::get_singleton()->set_exit_code(1);
			quitting = true;
			return;
		}
		Array entities = config["entities"];
		count = entities.size();
		PackedInt64Array entity_ids;
		if (count > 1000000) {
			ERR_PRINT("Too many ECS entities");
			OS::get_singleton()->set_exit_code(1);
			quitting = true;
			return;
		}
		for (int i = 0; i < count; i++) {
			if (entities[i].get_type() != Variant::DICTIONARY) {
				quitting = true;
				break;
			}
			Dictionary entity = entities[i];
			uint64_t id = world->create_entity();
			entity_ids.push_back(id);
			for (const String &name : { String("position"), String("velocity"), String("rotation"), String("scale") }) {
				if (!entity.has(name)) {
					continue;
				}
				if (entity[name].get_type() != Variant::ARRAY) {
					quitting = true;
					break;
				}
				Array values = entity[name];
				if (values.size() != 3) {
					quitting = true;
					break;
				}
				for (int j = 0; j < 3; j++) {
					if (values[j].get_type() != Variant::INT && values[j].get_type() != Variant::FLOAT) {
						quitting = true;
					}
				}
				if (quitting) {
					break;
				}
				if (!world->set_vector(id, name, Vector3(double(values[0]), double(values[1]), double(values[2])))) {
					quitting = true;
					break;
				}
			}
			if (quitting) {
				break;
			}
		}
		for (int i = 0; !quitting && i < entities.size(); i++) {
			Dictionary entity = entities[i];
			Variant parent = entity.get("parent", -1);
			if ((parent.get_type() != Variant::INT && parent.get_type() != Variant::FLOAT) || !Math::is_finite(double(parent)) || double(parent) != Math::floor(double(parent)) || double(parent) < -1 || double(parent) >= entities.size()) {
				quitting = true;
			} else if (int(parent) >= 0 && !world->set_parent(entity_ids[i], entity_ids[int(parent)])) {
				quitting = true;
			}
		}
		if (quitting) {
			ERR_PRINT("Invalid ECS entity component or parent: expected three finite numbers");
			OS::get_singleton()->set_exit_code(1);
			return;
		}
	} else {
		int width = MAX(1, int(Math::ceil(Math::sqrt(double(count)))));
		for (int i = 0; i < count; i++) {
			uint64_t id = world->create_entity();
			world->set_vector(id, "position", Vector3((i % width - width / 2) * 1.5, 0, (i / width - width / 2) * 1.5));
			world->set_vector(id, "velocity", Vector3(0, .15 * ((i % 3) - 1), 0));
		}
	}
	int node_count = 0;
	ObjectDB::debug_objects([](Object *object, void *data) {if(object->is_class("Node")){(*static_cast<int *>(data))++;
} }, &node_count);
	bool scene_tree = OS::get_singleton()->get_main_loop()->is_class("SceneTree");
	if (node_count != 0 || scene_tree) {
		ERR_PRINT("ECS runtime unexpectedly created scene nodes");
		OS::get_singleton()->set_exit_code(1);
		quitting = true;
		return;
	}
	print_line("ECS_STARTED scene_tree=false entity_nodes=" + itos(node_count) + " " + JSON::stringify(world->get_statistics()));
	if (EngineDebugger::is_active()) {
		EngineDebugger::register_message_capture("ecs", EngineDebugger::Capture(this, &ECSMainLoop::debug_capture));
	}
	DisplayServer *ds = DisplayServer::get_singleton();
	ds->window_set_window_event_callback(callable_mp(this, &ECSMainLoop::window_event));
	ds->window_set_input_event_callback(callable_mp(this, &ECSMainLoop::input_event));
	ds->window_set_input_text_callback(callable_mp(this, &ECSMainLoop::input_text));
	if (ds->get_name() == "headless") {
		if (scene_resource.is_valid()) {
			active_camera = scene_resource->get_camera_transform();
		}
		world->set_audio_listener(active_camera);
		ui_system.attach(world, RID());
		start_game();
		return;
	}
	auto *rs = RenderingServer::get_singleton();
	scenario = rs->scenario_create();
	viewport = rs->viewport_create();
	camera = rs->camera_create();
	rs->camera_set_perspective(camera, 60, .1, 2000);
	float distance = MAX(12.0f, float(Math::sqrt(double(count))) * 1.8f);
	Transform3D transform;
	transform.origin = Vector3(0, distance, distance * .75);
	transform = transform.looking_at(Vector3(), Vector3(0, 1, 0));
	if (scene_resource.is_valid()) {
		transform = scene_resource->get_camera_transform();
		rs->camera_set_perspective(camera, scene_resource->get_camera_fov(), .1, 2000);
		if (scene_resource->get_environment().is_valid()) {
			rs->scenario_set_environment(scenario, scene_resource->get_environment()->get_rid());
		}
	}
	active_camera = transform;
	if (world.is_valid()) {
		world->set_audio_listener(transform);
	}
	rs->camera_set_transform(camera, transform);
	rs->viewport_set_scenario(viewport, scenario);
	rs->viewport_attach_camera(viewport, camera);
	rs->viewport_set_update_mode(viewport, RSE::VIEWPORT_UPDATE_ALWAYS);
	rs->viewport_set_active(viewport, true);
	if (benchmark) {
		rs->viewport_set_measure_render_time(viewport, true);
		ds->window_set_vsync_mode(DisplayServerEnums::VSYNC_DISABLED);
	}
	rs->viewport_attach_to_screen(viewport);
	// JSON compatibility demos use a default box; resource scenes render explicit Mesh components only.
	if (!resource_scene) {
		Ref<StandardMaterial3D> material;
		material.instantiate();
		material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		material->set_albedo(Color(.15, .65, 1));
		mesh.instantiate();
		mesh->set_size(Vector3(.8, .8, .8));
		mesh->set_material(material);
	}
	update_rendering();
	ui_system.attach(world, viewport);
	start_game();
}
bool ECSMainLoop::physics_process(double delta) {
	if(editor_paused) { return quitting; }
	if (!quitting) {
		{ ECSProfileScope profile("Physics readback"); world->sync_physics(); }
		uint64_t start = OS::get_singleton()->get_ticks_usec();
		Array arguments;
		arguments.push_back(delta);
		if (!game_callback("PhysicsUpdate", arguments)) {
			return true;
		}
		world->step(delta);
		{ ECSProfileScope profile("Physics submit"); world->submit_physics(); }
		if (benchmark && elapsed >= 5) {
			physics_samples.push_back((OS::get_singleton()->get_ticks_usec() - start) / 1000.0);
		}
	}
	return quitting;
}
bool ECSMainLoop::process(double delta) {
#ifdef TOOLS_ENABLED
	if (ai_runtime.is_valid() && world.is_valid() && !quitting) { ai_runtime->poll(world, viewport, frames); }
#endif
	if (game_reload_pending && !quitting) {
		reload_game();
	}
	if (quitting) {
		return true;
	}
	uint64_t now = OS::get_singleton()->get_ticks_usec();
	if (benchmark && elapsed >= 5 && previous_frame_ticks) {
		frame_samples.push_back((now - previous_frame_ticks) / 1000.0);
	}
	previous_frame_ticks = now;
	Array arguments;
	arguments.push_back(delta);
	if (!editor_paused && !game_callback("Update", arguments)) {
		return true;
	}
	if(!editor_paused) { elapsed += delta; }
	debug_elapsed += delta;
	frames++;
#if defined(DEBUG_ENABLED) && defined(MODULE_LEANCLR_ENABLED)
    if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-heap-test") && (frames == 2 || frames == 4 || frames == 8)) {
        Dictionary snapshot = LeanCLRLanguage::heap_snapshot();
        Ref<FileAccess> output = FileAccess::open("res://heap-" + itos(frames) + ".json", FileAccess::WRITE);
        if (output.is_valid()) output->store_string(JSON::stringify(snapshot, "\t"));
        else OS::get_singleton()->set_exit_code(1);
#if defined(TOOLS_ENABLED) && defined(MODULE_OBJECTDB_PROFILER_ENABLED)
        if (frames == 8) {
            auto *view = memnew(SnapshotManagedHeapView);
            Dictionary first = JSON::parse_string(FileAccess::get_file_as_string("res://heap-2.json"));
            Dictionary second = JSON::parse_string(FileAccess::get_file_as_string("res://heap-4.json"));
            bool ok = view->validate_fixture(first, second);
            memdelete(view);
            print_line(ok ? "MANAGED_HEAP_EDITOR_MODEL_PASS" : "MANAGED_HEAP_EDITOR_MODEL_FAILED");
            if (!ok) OS::get_singleton()->set_exit_code(1);
        }
#endif
    }
#endif
	if (viewport.is_valid()) {
		auto *rs = RenderingServer::get_singleton();
		Size2i size = get_ui_viewport_size();
		rs->viewport_set_size(viewport, MAX(1, size.x), MAX(1, size.y));
		rs->viewport_attach_to_screen(viewport, Rect2(Vector2(), DisplayServer::get_singleton()->window_get_size()));
		uint64_t extraction_start = OS::get_singleton()->get_ticks_usec();
		{ ECSProfileScope profile("Render submission"); update_rendering(); }
		{ ECSProfileScope profile("UI draw"); ui_system.draw(size); }
		if (benchmark && elapsed >= 5) {
			extraction_samples.push_back((OS::get_singleton()->get_ticks_usec() - extraction_start) / 1000.0);
			gpu_samples.push_back(rs->viewport_get_measured_render_time_gpu(viewport));
		}
	}
	String screenshot = GLOBAL_GET("ecs/run/screenshot");
#ifdef TOOLS_ENABLED
	if(scene_preview_requested) { send_scene_preview(); }
#endif
	if (viewport.is_valid() && elapsed > 2 && !screenshot.is_empty()) {
		Ref<Image> image = RenderingServer::get_singleton()->texture_2d_get(RenderingServer::get_singleton()->viewport_get_texture(viewport));
		if (image.is_valid() && !image->is_empty()) {
			Error error = image->save_png(screenshot);
			if (error != OK) {
				ERR_PRINT("ECS screenshot failed");
				OS::get_singleton()->set_exit_code(1);
				quitting = true;
			}
			ProjectSettings::get_singleton()->set_setting("ecs/run/screenshot", "");
		}
	}
	if (debug_elapsed >= .25) {
		debug_elapsed = 0;
		send_snapshot();
	}
	double quit_after = GLOBAL_GET("ecs/run/quit_after_seconds");
	if (quit_after > 0 && elapsed >= quit_after) {
		if (benchmark) {
			report_benchmark();
		}
		print_line("ECS_RUN_OK frames=" + itos(frames) + " " + JSON::stringify(world->get_statistics()));
		return true;
	}
	return false;
}
void ECSMainLoop::report_benchmark() {
	Dictionary report;
	auto statistics = [](Vector<double> values) {
		Dictionary result;
		result["samples"] = values.size();
		if (values.is_empty()) {
			return result;
		}
		double sum = 0;
		for (double v : values) {
			sum += v;
		}
		values.sort();
		result["mean_ms"] = sum / values.size();
		result["p95_ms"] = values[MIN(values.size() - 1, int(values.size() * .95))];
		result["max_ms"] = values[values.size() - 1];
		return result;
	};
	report["frames"] = statistics(frame_samples);
	report["physics_steps"] = statistics(physics_samples);
	report["render_extraction"] = statistics(extraction_samples);
	report["gpu"] = statistics(gpu_samples);
	report["world"] = world->get_statistics();
	report["wall_seconds"] = (OS::get_singleton()->get_ticks_usec() - benchmark_started) / 1000000.0;
	report["warmup_seconds"] = 5;
	if (viewport.is_valid()) {
		auto *rs = RenderingServer::get_singleton();
		report["draw_calls"] = rs->viewport_get_render_info(viewport, RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE, RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME);
		report["rendering_method"] = rs->get_current_rendering_method();
	}
	print_line("ECS_BENCHMARK " + JSON::stringify(report));
}
#ifdef TOOLS_ENABLED
void ECSMainLoop::send_scene_preview() {
	scene_preview_requested=false;
	if(!EngineDebugger::is_active() || world.is_null() || scene_resource.is_null()) { return; }
	Array entities;
	int64_t bytes=0;
	String error;
	if(editor_scene_entities.size()>16384) { error="Scene preview exceeds 16384 authored entities."; }
	if(error.is_empty()) {
		for(uint64_t id:editor_scene_entities) {
			Dictionary entity;
			bool alive=world->is_alive(id);
			entity["active"]=alive && world->is_active_in_hierarchy(id);
			entity["transform"]=alive?world->get_global_transform(id):Transform3D();
			Dictionary particles=world->particle_preview(id);
			PackedFloat32Array buffer=particles.get("buffer",PackedFloat32Array());
			bytes+=buffer.size()*sizeof(float)+128;
			if(bytes>4*1024*1024) { error="Scene preview exceeds the 4 MiB transfer budget."; entities.clear(); break; }
			entity["particles"]=particles;
			entities.push_back(entity);
		}
	}
	Array reply; reply.push_back(scene_resource->get_path()); reply.push_back(entities); reply.push_back(frames); reply.push_back(error);
	reply.push_back(editor_inspect_entity);
	reply.push_back(ECSLiveEdit::encode(live_definition(editor_inspect_entity)));
	EngineDebugger::get_singleton()->send_message("ecs:scene_preview",reply);
}
#endif
void ECSMainLoop::send_snapshot() {
	if (!EngineDebugger::is_active()) {
		return;
	}
	Array data;
	data.push_back(world->get_statistics());
	data.push_back(world->inspect_entities(inspect_offset, 128));
	data.push_back(inspect_offset);
	EngineDebugger::get_singleton()->send_message("ecs:snapshot", data);
}
Error ECSMainLoop::debug_capture(void *user, const String &message, const Array &args, bool &captured) {
	auto *loop = static_cast<ECSMainLoop *>(user);
	captured = true;
#ifdef TOOLS_ENABLED
	if(message=="set_paused" && args.size()==1 && args[0].get_type()==Variant::BOOL) {
		loop->editor_paused=args[0];
		loop->ui_system.cancel_input();
		PhysicsServer2D::get_singleton()->set_active(!loop->editor_paused);
		PhysicsServer3D::get_singleton()->set_active(!loop->editor_paused);
		return OK;
	}
	if(message=="live_set" && args.size()==5 && args[0].get_type()==Variant::INT && args[1].get_type()==Variant::INT && args[2].get_type()==Variant::STRING && args[4].get_type()==Variant::BOOL) {
		bool ok=true;
		Variant value=ECSLiveEdit::decode(args[3],ok);
		ok=ok && loop->live_set(args[1],args[2],value,args[4]);
		if(ok) { loop->world->submit_physics(); if(loop->viewport.is_valid()) { loop->update_rendering(); } }
		Array result; result.push_back(args[0]); result.push_back(ok); result.push_back(ok?String():String("Runtime rejected property value or unsupported field."));
		EngineDebugger::get_singleton()->send_message("ecs:live_result",result);
		loop->editor_inspect_entity=args[1];
		loop->send_scene_preview();
		return OK;
	}
	if(message=="scene_preview" && args.size()<=1) {
		if(args.size()==1 && args[0].get_type()==Variant::INT) { loop->editor_inspect_entity=args[0]; }
		loop->scene_preview_requested=true;
		// Reply immediately: the debugger also processes requests while paused.
		loop->send_scene_preview();
		return OK;
	}
#endif
	if (message == "preview_size") {
		if (!Engine::get_singleton()->is_embedded_in_editor() || args.size() != 1 || args[0].get_type() != Variant::VECTOR2I) {
			return ERR_INVALID_PARAMETER;
		}
		Size2i size = args[0];
		if (size != Size2i() && (size.x < 128 || size.y < 128 || size.x > 4096 || size.y > 4096)) {
			return ERR_INVALID_PARAMETER;
		}
		loop->preview_resolution = size;
		return OK;
	}
	if (message == "reload_game" && args.is_empty()) {
		return loop->request_game_reload() ? OK : ERR_UNAVAILABLE;
	}
	if (message == "save_scene" && args.is_empty()) {
		String path = "user://ecs-snapshot-" + itos(OS::get_singleton()->get_ticks_usec()) + ".tres";
		Error error = loop->save_scene(path);
		Array result;
		result.push_back(int(error));
		result.push_back(ProjectSettings::get_singleton()->globalize_path(path));
		EngineDebugger::get_singleton()->send_message("ecs:saved", result);
		return OK;
	} else if (message == "destroy" && args.size() == 1 && args[0].get_type() == Variant::INT) {
		if (!loop->world->destroy_entity(uint64_t(int64_t(args[0])))) {
			return ERR_INVALID_PARAMETER;
		}
	} else if (message == "page" && args.size() == 1 && args[0].get_type() == Variant::INT) {
		loop->inspect_offset = MAX(0, int(args[0]));
	} else if (message == "set_vector" && args.size() == 3 && args[0].get_type() == Variant::INT && args[1].get_type() == Variant::STRING && args[2].get_type() == Variant::VECTOR3) {
		// Debug commands are dispatched on the main thread between simulation steps.
		if (!loop->world->set_vector(uint64_t(int64_t(args[0])), args[1], args[2])) {
			return ERR_INVALID_PARAMETER;
		}
	} else {
		return ERR_INVALID_PARAMETER;
	}
	loop->send_snapshot();
	return OK;
}
void ECSMainLoop::finalize() {
	if(editor_paused) {
		PhysicsServer2D::get_singleton()->set_active(true);
		PhysicsServer3D::get_singleton()->set_active(true);
	}
#ifdef TOOLS_ENABLED
	ai_runtime.unref();
#endif
	if (game_initialized && game_callback("Shutdown", Array(), true)) {
		print_line("ECS_GAME_STOPPED " + JSON::stringify(world->get_statistics()));
	}
#if defined(DEBUG_ENABLED) && defined(MODULE_LEANCLR_ENABLED)
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-profiler-test")) {
		auto *language = LeanCLRLanguage::get_singleton();
		language->profiling_stop();
		Vector<ScriptLanguage::ProfilingInfo> rows; rows.resize(32768);
		int count = language->profiling_get_accumulated_data(rows.ptrw(), rows.size());
		Array methods; bool parent = false, child = false, throwing = false;
		for (int i = 0; i < count; i++) {
			const auto &row = rows[i]; String name = row.signature;
			Dictionary item; item["method"] = name; item["calls"] = row.call_count; item["total_us"] = row.total_time; item["self_us"] = row.self_time; methods.push_back(item);
			parent |= name.contains("ProbeParent") && row.call_count > 0 && row.total_time >= row.self_time;
			child |= name.contains("ProbeChild") && row.call_count > 0;
			throwing |= name.contains("ProbeThrow") && row.call_count > 0;
		}
		Dictionary memory, context;
		if (world.is_valid()) memory = world->get_statistics();
#if defined(MODULE_OBJECTDB_PROFILER_ENABLED)
		Array objects; SnapshotCollector::snapshot_objects(&objects, context);
#endif
		bool ok = parent && child && throwing && ECSProfileScope::samples > 0 && int64_t(memory.get("component_bytes", 0)) > 0 && context.has("ecs_memory") && context.has("leanclr_memory");
		Dictionary report; report["methods"] = methods; report["memory_snapshot"] = context; report["ecs_scope_samples"] = ECSProfileScope::samples; report["passed"] = ok;
		Ref<FileAccess> output = FileAccess::open("res://profiler-result.json", FileAccess::WRITE);
		if (output.is_valid()) output->store_string(JSON::stringify(report, "\t")); else ok = false;
		print_line(ok ? "ECS_MANAGED_PROFILER_PASS" : "ECS_MANAGED_PROFILER_FAILED");
		if (!ok) OS::get_singleton()->set_exit_code(1);
	}
#endif
	game_initialized = false;
#ifdef MODULE_LEANCLR_ENABLED
	game_host.reset();
#endif
	if (EngineDebugger::has_capture("ecs")) {
		EngineDebugger::unregister_message_capture("ecs");
	}
	DisplayServer::get_singleton()->window_set_window_event_callback(Callable());
	DisplayServer::get_singleton()->window_set_input_event_callback(Callable());
	DisplayServer::get_singleton()->window_set_input_text_callback(Callable());
	ui_system.cancel_input();
	ui_system.clear();
	auto *rs = RenderingServer::get_singleton();
	for (const RenderGroup &group : render_groups) {
		rs->free_rid(group.instance);
		if (group.multimesh.is_valid()) {
			rs->free_rid(group.multimesh);
		}
	}
	render_groups.clear();
	if (world.is_valid()) {
		world->clear_lights();
	}
	for (RID rid : { camera, viewport, scenario }) {
		if (rid.is_valid()) {
			rs->free_rid(rid);
		}
	}
	camera = viewport = scenario = RID();
	mesh.unref();
	if (world.is_valid()) {
		world->clear_physics();
		world->clear_audio();
		world->clear_navigation();
		world->clear_skeletons();
	}
	world.unref();
	scene_resource.unref();
}

void ECSMainLoop::update_rendering() {
	auto *rs = RenderingServer::get_singleton();
	world->sync_lights(scenario);
	world->sync_particles(scenario);
	world->sync_skeletal_2d(viewport);
	Dictionary entity_camera = world->get_active_camera();
	if (entity_camera.is_empty()) {
		rs->camera_set_transform(camera, active_camera);
		rs->camera_set_perspective(camera, scene_resource.is_valid() ? scene_resource->get_camera_fov() : 60, .1, 2000);
	} else {
		rs->camera_set_transform(camera, entity_camera["transform"]);
		if (String(entity_camera["projection"]) == "orthogonal") {
			rs->camera_set_orthogonal(camera, entity_camera["size"], entity_camera["near"], entity_camera["far"]);
		} else {
			rs->camera_set_perspective(camera, entity_camera["fov"], entity_camera["near"], entity_camera["far"]);
		}
	}
	Vector<ECSWorld::RenderBatch> batches = world->get_render_batches(mesh);
	// Reconcile groups by resource identity; reorder/removal never associates an old material with a new mesh.
	Vector<RenderGroup> next;
	HashMap<ObjectID, HashMap<ObjectID, int>> existing;
	HashMap<uint64_t, int> existing_skeletons;
	Vector<uint8_t> retained;
	retained.resize(render_groups.size());
	retained.fill(0);
	for (int i = 0; i < render_groups.size(); i++) {
		const RenderGroup &group = render_groups[i];
		if (group.entity) {
			existing_skeletons[group.entity] = i;
			continue;
		}
		existing[group.mesh->get_instance_id()][group.material.is_valid() ? group.material->get_instance_id() : ObjectID()] = i;
	}
	for (const ECSWorld::RenderBatch &batch : batches) {
		RenderGroup group;
		if (batch.entity) {
			if (const int *index = existing_skeletons.getptr(batch.entity)) {
				group = render_groups[*index];
				retained.write[*index] = 1;
			}
			if (!group.instance.is_valid()) {
				group.instance = rs->instance_create();
				rs->instance_set_scenario(group.instance, scenario);
			}
			group.entity = batch.entity;
			group.mesh = batch.mesh;
			group.material = batch.material;
			rs->instance_set_base(group.instance, batch.mesh->get_rid());
			rs->instance_attach_skeleton(group.instance, batch.skeleton);
			rs->instance_set_transform(group.instance, batch.transform);
			rs->instance_geometry_set_material_override(group.instance, batch.material.is_valid() ? batch.material->get_rid() : RID());
			next.push_back(group);
			continue;
		}
		const auto *materials = existing.getptr(batch.mesh->get_instance_id());
		const int *index = materials ? materials->getptr(batch.material.is_valid() ? batch.material->get_instance_id() : ObjectID()) : nullptr;
		if (index) {
			group = render_groups[*index];
			retained.write[*index] = 1;
		}
		if (!group.multimesh.is_valid()) {
			group.mesh = batch.mesh;
			group.material = batch.material;
			group.multimesh = rs->multimesh_create();
			rs->multimesh_set_mesh(group.multimesh, batch.mesh->get_rid());
			group.instance = rs->instance_create();
			rs->instance_set_base(group.instance, group.multimesh);
			rs->instance_set_scenario(group.instance, scenario);
			rs->instance_geometry_set_material_override(group.instance, batch.material.is_valid() ? batch.material->get_rid() : RID());
		}
		int instances = batch.transforms.size() / 12;
		if (group.count != instances) {
			group.count = instances;
			rs->multimesh_allocate_data(group.multimesh, instances, RSE::MULTIMESH_TRANSFORM_3D);
		}
		if (instances) {
			rs->multimesh_set_buffer(group.multimesh, batch.transforms);
		}
		next.push_back(group);
	}
	for (int i = 0; i < render_groups.size(); i++) {
		if (retained[i]) {
			continue;
		}
		rs->free_rid(render_groups[i].instance);
		if (render_groups[i].multimesh.is_valid()) {
			rs->free_rid(render_groups[i].multimesh);
		}
	}
	render_groups = next;
}

void ECSMainLoop::set_camera_transform(const Transform3D &transform) {
	ERR_FAIL_COND(!Thread::is_main_thread() || !transform.is_finite());
	active_camera = transform;
	if (world.is_valid()) {
		world->set_audio_listener(transform);
	}
	if (scene_resource.is_valid()) {
		scene_resource->set_camera_transform(transform);
	}
	if (camera.is_valid()) {
		RenderingServer::get_singleton()->camera_set_transform(camera, transform);
	}
}
Error ECSMainLoop::save_scene(const String &path) {
	ERR_FAIL_COND_V(!Thread::is_main_thread() || world.is_null(), ERR_UNCONFIGURED);
	ERR_FAIL_COND_V(path.get_extension().to_lower() != "tres" && path.get_extension().to_lower() != "res", ERR_INVALID_PARAMETER);
	Ref<ECSScene> snapshot;
	snapshot.instantiate();
	if (!snapshot->capture(world)) {
		return ERR_INVALID_DATA;
	}
	snapshot->set_camera_transform(active_camera);
	if (mesh.is_valid()) {
		Array entities = snapshot->get_entities();
		for (int i = 0; i < entities.size(); i++) {
			Dictionary entity = entities[i];
			if (entity.has("position") && !entity.has("mesh")) {
				entity["mesh"] = mesh;
			}
		}
		snapshot->set_entities(entities);
	}
	if (scene_resource.is_valid()) {
		snapshot->set_camera_transform(scene_resource->get_camera_transform());
		snapshot->set_camera_fov(scene_resource->get_camera_fov());
		snapshot->set_environment(scene_resource->get_environment());
	}
	return ResourceSaver::save(snapshot, path);
}

void ECSMainLoop::input_event(const Ref<InputEvent> &event) {
	if(editor_paused) { return; }
	if (quitting || world.is_null()) {
		return;
	}
	Ref<InputEvent> mapped_event = event;
	if (preview_resolution != Size2i()) {
		Size2i window_size = DisplayServer::get_singleton()->window_get_size().maxi(1);
		Transform2D transform;
		transform.scale(Vector2(preview_resolution) / Vector2(window_size));
		mapped_event = event->xformed_by(transform);
	}
	bool handled = ui_system.input(mapped_event, get_ui_viewport_size());
	Array arguments;
	arguments.push_back(mapped_event);
	arguments.push_back(handled);
	game_callback("HandleInput", arguments);
	emit_signal("input_event", mapped_event, handled);
}

void ECSMainLoop::quit(int exit_code) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	OS::get_singleton()->set_exit_code(exit_code);
	quitting = true;
}
void ECSMainLoop::start_game() {
	String assembly = GLOBAL_GET("ecs/game/assembly");
	String entry = GLOBAL_GET("ecs/game/class");
	if (assembly.is_empty() && entry.is_empty()) {
		return;
	}
	if (assembly.is_empty() || entry.is_empty() || assembly.contains("/") || assembly.contains("\\") || assembly.contains(":")) {
		ERR_PRINT("ECS requires ecs/game/assembly (simple assembly name) and ecs/game/class together.");
		quit(1);
		return;
	}
	if (assembly.ends_with(".dll")) {
		assembly = assembly.get_basename();
	}
#ifdef MODULE_LEANCLR_ENABLED
	game_host = std::make_unique<LeanCLRHost>();
	if (!game_host->load(ObjectID(), assembly, entry) || !game_host->validate_ecs_entry()) {
		ERR_PRINT("ECS game startup failed [" + assembly + " / " + entry + "]: " + game_host->get_last_error());
		quit(1);
		return;
	}
	// A partially completed Initialize still receives Shutdown for cleanup.
	game_initialized = true;
	Array arguments;
	arguments.push_back(world);
	arguments.push_back(this);
	if (game_callback("Initialize", arguments)) {
		print_line("ECS_GAME_READY " + assembly + " / " + entry + " " + JSON::stringify(world->get_statistics()));
	}
#else
	ERR_PRINT("ECS game entry requires a build with LeanCLR enabled.");
	quit(1);
#endif
}
bool ECSMainLoop::game_callback(const String &method, const Array &arguments, bool cleanup) {
	CharString profile_name;
	if (ECSProfileScope::is_active()) profile_name = ("C# entry: " + method).utf8();
	ECSProfileScope profile(profile_name.get_data());
	if (!game_initialized) {
		return true;
	}
#ifdef MODULE_LEANCLR_ENABLED
	ERR_FAIL_COND_V(arguments.size() > 2, false);
	const Variant *args[2]{};
	for (int i = 0; i < arguments.size(); i++) {
		args[i] = &arguments[i];
	}
	Variant result;
	if (!game_host->invoke_callback(method, args, arguments.size(), result, cleanup)) {
		ERR_PRINT("ECS game callback failed [" + method + "]: " + game_host->get_last_error());
		quit(1);
		return false;
	}
#endif
	return true;
}

void ECSMainLoop::input_text(const String &text) {
	if(editor_paused) { return; }
	if (!quitting) {
		ui_system.input_text(text);
	}
}
void ECSMainLoop::_notification(int what) {
	if (what == NOTIFICATION_OS_IME_UPDATE) {
		ui_system.update_ime();
	}
}

bool ECSMainLoop::request_game_reload() {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), false);
	if (!game_initialized || quitting || game_reload_pending) {
		return false;
	}
	game_reload_pending = true;
	return true;
}
void ECSMainLoop::reload_game() {
	game_reload_pending = false;
#ifdef MODULE_LEANCLR_ENABLED
	Variant state;
	if (!game_host->invoke_callback("PrepareReload", nullptr, 0, state) || state.get_type() != Variant::STRING) {
		ERR_PRINT("ECS reload state capture failed: " + game_host->get_last_error());
		quit(1);
		return;
	}
	if (String(state).utf8().length() > 4 * 1024 * 1024) {
		ERR_PRINT("ECS reload state exceeds 4 MiB.");
		quit(1);
		return;
	}
	game_initialized = false;
	game_host.reset();
	String assembly = GLOBAL_GET("ecs/game/assembly"), entry = GLOBAL_GET("ecs/game/class"), error;
	if (assembly.ends_with(".dll")) {
		assembly = assembly.get_basename();
	}
	if (!LeanCLRHost::reload_ecs_assembly(assembly, error)) {
		ERR_PRINT(error);
		quit(1);
		return;
	}
	game_host = std::make_unique<LeanCLRHost>();
	if (!game_host->load(ObjectID(), assembly, entry) || !game_host->validate_ecs_entry()) {
		ERR_PRINT("ECS reload entry failed: " + game_host->get_last_error());
		quit(1);
		return;
	}
	game_initialized = true;
	Variant world_arg = world, loop_arg = this, result;
	const Variant *args[] = { &world_arg, &loop_arg, &state };
	if (!game_host->invoke_callback("ResumeAfterReload", args, 3, result)) {
		ERR_PRINT("ECS state restore failed: " + game_host->get_last_error());
		quit(1);
		return;
	}
	print_line("ECS_GAME_RELOADED " + assembly + " " + JSON::stringify(world->get_statistics()));
#endif
}

Size2i ECSMainLoop::get_ui_viewport_size() const {
	if (preview_resolution != Size2i()) {
		return preview_resolution;
	}
	Size2i size = DisplayServer::get_singleton()->window_get_size();
	if (size.x <= 0 || size.y <= 0) {
		size = Size2i(int(GLOBAL_GET("display/window/size/viewport_width")), int(GLOBAL_GET("display/window/size/viewport_height")));
		size.x = MAX(1, size.x);
		size.y = MAX(1, size.y);
	}
	return size;
}
