#ifdef TOOLS_ENABLED
#include "ecs_component_tool.h"
#include "ecs_tool_adapters.h"
#include "ecs_replication.h"
#include "ecs_animation_graph.h"
#include "core/object/callable_mp.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/gui/button.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/line_edit.h"
#include "scene/resources/animation.h"
#include "scene/resources/sprite_frames.h"

void ECSComponentTool::_bind_methods() { ADD_SIGNAL(MethodInfo("entity_selected", PropertyInfo(Variant::INT, "index"))); }
void ECSComponentTool::select_entity(int p_index) { emit_signal("entity_selected", p_index); }
void ECSComponentTool::setup(const String &p_component) {
	component = p_component;
	proxy.instantiate();
	status = memnew(Label);
	status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(status);
	auto *bar = memnew(HBoxContainer);
	add_child(bar);
	if (component == "polygon_2d") {
		mode = memnew(OptionButton);
		mode->add_item(String(U"选择 / 变换"));
		mode->add_item(String(U"编辑顶点"));
		mode->add_item(String(U"绘制权重"));
		mode->add_item(String(U"UV 编辑"));
		bar->add_child(mode);
		mode->connect("item_selected", callable_mp(this, &ECSComponentTool::select_mode));
		auto spin = [&](const String &p_label, double p_max, double p_step, double p_default) {
			auto *label = memnew(Label); label->set_text(p_label); bar->add_child(label);
			auto *value = memnew(SpinBox); value->set_max(p_max); value->set_step(p_step); value->set_value(p_default); bar->add_child(value);
			value->connect("value_changed", callable_mp(this, &ECSComponentTool::brush_changed));
			return value;
		};
		bone = spin(String(U"骨骼索引"), 65535, 1, 0);
		radius = spin(String(U"半径"), 1024, 1, 40);
		strength = spin(String(U"强度"), 1, .01, .25);
	} else if (component == "animation") {
		for (bool playing : {true, false}) {
			auto *button = memnew(Button); button->set_text(playing ? String(U"播放预览") : String(U"暂停预览")); bar->add_child(button);
			button->connect("pressed", callable_mp(this, &ECSComponentTool::preview).bind(playing));
		}
		time = memnew(SpinBox); time->set_max(3600); time->set_step(.01); bar->add_child(time);
		time->connect("value_changed", callable_mp(this, &ECSComponentTool::seek));
		states = memnew(OptionButton); bar->add_child(states); states->connect("item_selected", callable_mp(this, &ECSComponentTool::select_state));
		state_name = memnew(LineEdit); state_name->set_placeholder(String(U"新状态名称")); bar->add_child(state_name);
		auto *add = memnew(Button); add->set_text(String(U"添加状态")); bar->add_child(add); add->connect("pressed", callable_mp(this, &ECSComponentTool::add_state));
		auto *keys = memnew(HBoxContainer); add_child(keys);
		channel = memnew(OptionButton); for (const String &field : {String("position"), String("rotation"), String("scale")}) { channel->add_item(field); } keys->add_child(channel);
		for (int i = 0; i < 3; i++) { values[i] = memnew(SpinBox); values[i]->set_min(-100000); values[i]->set_max(100000); values[i]->set_step(.01); keys->add_child(values[i]); }
		auto *key = memnew(Button); key->set_text(String(U"在当前时间插入关键帧")); keys->add_child(key); key->connect("pressed", callable_mp(this, &ECSComponentTool::insert_key));
	}
	if(component=="sprite_frames") {
		frame_texture=memnew(EditorResourcePicker); frame_texture->set_base_type("Texture2D"); frame_texture->set_custom_minimum_size(Size2(180,0)); bar->add_child(frame_texture);
		frame_index=memnew(SpinBox); frame_index->set_max(65535); frame_index->set_tooltip_text(String(U"删除帧的索引，从 0 开始")); bar->add_child(frame_index);
		for(bool remove:{false,true}) { auto *button=memnew(Button); button->set_text(remove?String(U"删除帧"):String(U"追加帧")); bar->add_child(button); button->connect("pressed",callable_mp(this,&ECSComponentTool::sprite_frame).bind(remove)); }
	}
	if (component=="gridmap_3d") {
		for (int i=0;i<3;i++) { auto *label=memnew(Label); label->set_text(i==0?"X":i==1?"Y":"Z"); bar->add_child(label); values[i]=memnew(SpinBox); values[i]->set_min(-100000); values[i]->set_max(100000); bar->add_child(values[i]); }
		auto spin=[&](const String &label,int maximum) { auto *text=memnew(Label); text->set_text(label); bar->add_child(text); auto *value=memnew(SpinBox); value->set_max(maximum); bar->add_child(value); return value; };
		grid_item=spin(String(U"网格编号"),100000); grid_orientation=spin(String(U"朝向"),23);
		for (bool remove:{false,true}) { auto *button=memnew(Button); button->set_text(remove?String(U"擦除单元格"):String(U"放置单元格")); bar->add_child(button); button->connect("pressed",callable_mp(this,&ECSComponentTool::grid_cell).bind(remove)); }
	}
	auto *split = memnew(HSplitContainer); split->set_v_size_flags(SIZE_EXPAND_FILL); add_child(split);
	auto *preview_tabs=memnew(TabContainer); preview_tabs->set_h_size_flags(SIZE_EXPAND_FILL); split->add_child(preview_tabs);
	auto *preview_host=memnew(VBoxContainer); preview_host->set_name(String(U"预览")); preview_tabs->add_child(preview_host);
	view = memnew(ECSUICanvasEditor); view->set_v_size_flags(SIZE_EXPAND_FILL); view->set_custom_minimum_size(Size2(240, 180)); view->set_h_size_flags(SIZE_EXPAND_FILL); preview_host->add_child(view);
	view->connect("mesh_edited", callable_mp(this, &ECSComponentTool::mesh_changed));
	view->connect("entity_selected", callable_mp(this, &ECSComponentTool::select_entity));
	if (component == "polygon_2d") {
		uv_view=memnew(ECSUVEditor); uv_view->set_focus_mode(FOCUS_ALL); uv_view->set_clip_contents(true); uv_view->set_custom_minimum_size(Size2(240,180)); uv_view->set_h_size_flags(SIZE_EXPAND_FILL); preview_host->add_child(uv_view); uv_view->set_v_size_flags(SIZE_EXPAND_FILL); uv_view->hide();
		uv_view->connect("uv_edited",callable_mp(this,&ECSComponentTool::uv_changed));
	}
	if (component == "animation" || component == "gridmap_3d") { spatial_view = memnew(ECSSpatialEditor); spatial_view->set_h_size_flags(SIZE_EXPAND_FILL); preview_host->add_child(spatial_view); spatial_view->set_v_size_flags(SIZE_EXPAND_FILL); spatial_view->connect("entity_selected", callable_mp(this, &ECSComponentTool::select_entity)); }
	if(component=="animation") { graph_view=memnew(ECSAnimationGraphEditor); graph_view->set_h_size_flags(SIZE_EXPAND_FILL); graph_view->set_name(String(U"混合图")); preview_tabs->add_child(graph_view); graph_view->connect("graph_applied",callable_mp(this,&ECSComponentTool::graph_changed)); }
	fields = memnew(EditorInspector); fields->set_custom_minimum_size(Size2(280, 140)); fields->set_h_size_flags(SIZE_EXPAND_FILL); split->add_child(fields);
}

void ECSComponentTool::edit(const Ref<ECSScene> &p_scene, int p_entity, bool p_running) {
	const bool valid = !p_running && p_scene.is_valid() && p_entity >= 0 && p_entity < p_scene->get_entities().size() && Dictionary(p_scene->get_entities()[p_entity]).has(component);
	const bool changed = scene != p_scene || entity != (valid ? p_entity : -1);
	scene = p_scene; entity = valid ? p_entity : -1;
	Ref<Resource> resource;
	if (valid && (component=="sprite_frames" || component=="theme" || component=="gridmap_3d")) { Dictionary settings=Dictionary(scene->get_entities()[entity])[component]; resource=settings.get(component=="sprite_frames"?"frames":component=="theme"?"resource":"library",Variant()); }
	if(resource!=watched_resource) { if(watched_resource.is_valid()) { watched_resource->disconnect_changed(callable_mp(this,&ECSComponentTool::resource_changed)); } watched_resource=resource; if(resource.is_valid()) { resource->connect_changed(callable_mp(this,&ECSComponentTool::resource_changed)); } }
	if (changed) { view->stop_animation_preview(); view->set_mesh_edit_mode(false); view->configure_weight_brush(-1, 40, .25); if (mode) { mode->select(0); } }
	proxy->target(scene, entity, component);
	fields->edit(valid ? proxy.ptr() : nullptr);
	if (states) {
		states->clear();
		if (valid) {
			Dictionary animation = Dictionary(scene->get_entities()[entity])[component];
			Dictionary clips = animation.get("states", Dictionary());
			for (const Variant &key : clips.keys()) { states->add_item(String(key)); if (String(key) == String(animation.get("state", String()))) { states->select(states->get_item_count() - 1); } }
		}
	}
	if(graph_view) { Dictionary data=valid?Dictionary(Dictionary(scene->get_entities()[entity])[component]):Dictionary(); graph_view->edit(data.get("graph",Dictionary()),data.get("clip",Variant())); }
	view->edit_scene(valid ? scene : Ref<ECSScene>(), entity);
	view->set_visible(valid);
	if (uv_view) { uv_view->edit(valid ? Dictionary(Dictionary(scene->get_entities()[entity])[component]) : Dictionary()); uv_view->set_visible(valid && mode->get_selected()==3); view->set_visible(valid && mode->get_selected()!=3); }
	if (spatial_view) {
		bool is_3d = component == "gridmap_3d";
		if (valid) { for (const Variant &raw : scene->get_entities()) { if (Dictionary(raw).has("mesh")) { is_3d = true; break; } } }
		spatial_view->edit_scene(valid ? scene : Ref<ECSScene>(), entity);
		spatial_view->set_visible(valid && is_3d); view->set_visible(valid && !is_3d);
	}
	status->set_text(p_running ? String(U"请停止游戏后编辑；运行参数可在检查器调整。") : valid ? String(Dictionary(scene->get_entities()[entity]).get("name", "")) + " / " + component : String(U"请在层级中选择带此组件的实体：") + component);
}

void ECSComponentTool::graph_changed(const Dictionary &graph) {
	if(entity<0 || scene.is_null()) { return; }
	Dictionary data=Dictionary(Dictionary(scene->get_entities()[entity])[component]).duplicate(true);
	if(!ECSAnimationGraph::validate(graph,data.get("clip",Variant()))) { status->set_text(String(U"混合图无效：请检查输出、连线、循环及动画轨道是否匹配。")); return; }
	data["graph"]=graph; data.erase("secondary"); data.erase("blend_space"); data.erase("states"); data.erase("state"); data.erase("root_motion_track");
	apply_component(data,String(U"编辑 ECS 动画混合图"));
}
void ECSComponentTool::select_mode(int p_mode) {
	if (uv_view) { uv_view->set_visible(entity>=0 && p_mode==3); view->set_visible(entity>=0 && p_mode!=3); }
	view->set_mesh_edit_mode(entity >= 0 && p_mode == 1);
	view->configure_weight_brush(entity >= 0 && p_mode == 2 ? int(bone->get_value()) : -1, MAX(1.0, radius->get_value()), strength->get_value());
}
void ECSComponentTool::uv_changed(const Dictionary &p_polygon) { mesh_changed(entity,p_polygon); }
void ECSComponentTool::resource_changed() { if(scene.is_valid()) { scene->emit_changed(); } }
ECSComponentTool::~ECSComponentTool() { if(watched_resource.is_valid()) { watched_resource->disconnect_changed(callable_mp(this,&ECSComponentTool::resource_changed)); } }
void ECSComponentTool::sprite_frame(bool remove) {
	if(entity<0 || scene.is_null()) { return; }
	Dictionary data=Dictionary(Dictionary(scene->get_entities()[entity])[component]).duplicate(true);
	Ref<SpriteFrames> frames=data.get("frames",Variant()); StringName name=data.get("animation",StringName("default"));
	if(frames.is_null() || !frames->has_animation(name)) { return; } frames=frames->duplicate(true);
	if(remove) { int index=frame_index->get_value(); if(frames->get_frame_count(name)<=1 || index>=frames->get_frame_count(name)) { status->set_text(String(U"需保留至少一帧，且索引必须有效。")); return; } frames->remove_frame(name,index); }
	else { Ref<Texture2D> texture=frame_texture->get_edited_resource(); if(texture.is_null()) { status->set_text(String(U"请先选择帧纹理。")); return; } frames->add_frame(name,texture); }
	data["frames"]=frames; apply_component(data,String(U"编辑 ECS 精灵帧"));
}
void ECSComponentTool::grid_cell(bool p_remove) {
	if(entity<0 || scene.is_null()) { return; }
	Dictionary settings=Dictionary(Dictionary(scene->get_entities()[entity])[component]).duplicate(true); Array cells=settings.get("cells",Array()); Vector3i position(values[0]->get_value(),values[1]->get_value(),values[2]->get_value());
	for(int i=cells.size()-1;i>=0;i--) { if(Vector3i(Dictionary(cells[i]).get("position",Vector3i()))==position) { cells.remove_at(i); } }
	if(!p_remove) { Dictionary cell; cell["position"]=position; cell["item"]=int(grid_item->get_value()); cell["orientation"]=int(grid_orientation->get_value()); cells.push_back(cell); }
	settings["cells"]=cells; apply_component(settings,String(U"编辑 ECS 网格地图"));
}
void ECSComponentTool::brush_changed(double) { if (view && mode) { select_mode(mode->get_selected()); } }
void ECSComponentTool::preview(bool p_playing) {
	if (entity < 0) { return; }
	if (spatial_view && spatial_view->is_visible()) {
		if (p_playing) { spatial_view->preview_animation(entity, time->get_value(), true); } else { spatial_view->pause_animation_preview(entity); }
	} else {
		if (p_playing) { view->preview_animation(entity, time->get_value(), true); } else { view->pause_animation_preview(entity); }
	}
}
void ECSComponentTool::seek(double p_time) { if (entity >= 0) { if (spatial_view && spatial_view->is_visible()) { spatial_view->preview_animation(entity, p_time, false); } else { view->preview_animation(entity, p_time, false); } } }

void ECSComponentTool::mesh_changed(int p_index, const Dictionary &p_polygon) {
	if (component != "polygon_2d" || entity < 0 || p_index != entity || scene.is_null() || entity >= scene->get_entities().size()) { return; }
	apply_component(p_polygon, String(U"编辑 ECS 多边形与权重"));
}
void ECSComponentTool::apply_component(const Dictionary &p_data, const String &p_action) {
	if (scene.is_null() || entity < 0 || entity >= scene->get_entities().size()) { return; }
	Array before = scene->get_entities(), after = before.duplicate(true);
	Dictionary definition = after[entity]; definition[component] = p_data; after[entity] = definition;
	Ref<ECSScene> check; check.instantiate(); check->set_entities(after);
	if (check->instantiate().is_null()) { status->set_text(String(U"组件数据无效，修改未保存。")); return; }
	auto *undo = EditorUndoRedoManager::get_singleton();
	undo->create_action(p_action, UndoRedo::MERGE_DISABLE, scene.ptr());
	undo->add_do_method(scene.ptr(), "set_entities", after);
	undo->add_undo_method(scene.ptr(), "set_entities", before);
	undo->commit_action();
}

void ECSComponentTool::insert_key() {
	if (entity < 0 || scene.is_null()) { return; }
	Dictionary data = Dictionary(Dictionary(scene->get_entities()[entity])[component]).duplicate(true);
	Ref<Animation> clip = data.get("clip", Variant()); if (clip.is_null()) { return; }
	clip = clip->duplicate(true);
	String field = channel->get_item_text(channel->get_selected());
	PackedInt64Array targets = data.get("targets", PackedInt64Array());
	if (targets.is_empty()) { for (int i = 0; i < clip->get_track_count(); i++) { targets.push_back(entity); } }
	int track = -1;
	for (int i = 0; i < clip->get_track_count(); i++) {
		if (clip->track_get_type(i) == Animation::TYPE_VALUE && clip->track_get_path(i) == NodePath(":" + field) && i < targets.size() && targets[i] == entity) { track = i; break; }
	}
	if (track < 0) {
		// All state clips must have the same track layout; adding a track to only one is invalid.
		if (!Dictionary(data.get("states", Dictionary())).is_empty()) { status->set_text(String(U"请先在基础动画建立轨道，再创建状态；已有状态只可修改现有轨道。")); return; }
		track = clip->add_track(Animation::TYPE_VALUE); clip->track_set_path(track, NodePath(":" + field)); targets.push_back(entity);
	}
	clip->set_length(MAX(clip->get_length(), time->get_value()));
	clip->track_insert_key(track, time->get_value(), Vector3(values[0]->get_value(), values[1]->get_value(), values[2]->get_value()));
	data["clip"] = clip; data["targets"] = targets;
	String current = data.get("state", String());
	if (!current.is_empty()) { Dictionary clips = data.get("states", Dictionary()); clips[current] = clip; data["states"] = clips; }
	apply_component(data, String(U"插入 ECS 动画关键帧"));
}
void ECSComponentTool::add_state() {
	if (entity < 0 || scene.is_null()) { return; }
	String name = state_name->get_text().strip_edges(); if (name.is_empty()) { return; }
	Dictionary data = Dictionary(Dictionary(scene->get_entities()[entity])[component]).duplicate(true);
	if (data.has("blend_space")) { status->set_text(String(U"混合空间与状态机不能同时配置。")); return; }
	Dictionary clips = data.get("states", Dictionary());
	if (clips.has(name)) { status->set_text(String(U"状态名称已存在。")); return; }
	Ref<Animation> clip = data.get("clip", Variant()); if (clip.is_null()) { return; }
	clips[name] = clip->duplicate(true); data["states"] = clips;
	apply_component(data, String(U"添加 ECS 动画状态"));
}
void ECSComponentTool::select_state(int p_index) {
	if (entity < 0 || scene.is_null() || p_index < 0) { return; }
	Dictionary data = Dictionary(Dictionary(scene->get_entities()[entity])[component]).duplicate(true);
	Dictionary clips = data.get("states", Dictionary()); String name = states->get_item_text(p_index);
	if (!clips.has(name)) { return; }
	data["state"] = name; data["clip"] = clips[name]; data["time"] = 0.0; data.erase("secondary"); data.erase("transition");
	apply_component(data, String(U"设置 ECS 动画状态"));
}

bool ECSComponentTool::run_self_test() {
	if (component == "replication") { return ECSReplication::test(); }
	if (component != "animation" && component != "polygon_2d") { return ECSToolAdapters::test(); }
	Ref<ECSScene> original = scene; int original_entity = entity;
	Ref<ECSScene> test; test.instantiate();
	if (component == "animation") {
		Ref<Animation> clip; clip.instantiate(); Dictionary animation; animation["clip"] = clip;
		Dictionary definition; definition["animation"] = animation; test->set_entities(Array({definition}));
		edit(test, 0, false); time->set_value(.5); values[0]->set_value(42); insert_key();
		Dictionary current = Dictionary(test->get_entities()[0])["animation"]; Ref<Animation> result = current["clip"];
		bool ok = ECSAnimationGraph::test() && result->get_track_count() == 1 && Vector3(result->track_get_key_value(0, 0)).x == 42 && clip->get_track_count() == 0 && test->instantiate().is_valid();
		state_name->set_text("walk"); add_state(); edit(test, 0, false); select_state(0);
		current = Dictionary(test->get_entities()[0])["animation"]; ok &= String(current.get("state", String())) == "walk" && test->instantiate().is_valid();
		Dictionary graph,node; node["type"]="clip"; node["clip"]=current["clip"]; graph["nodes"]=Array({node}); graph["output"]=0; graph_changed(graph);
		current=Dictionary(test->get_entities()[0])["animation"]; ok &= current.has("graph") && test->instantiate().is_valid();
		const String path = "user://ecs_component_tool_validation.tres";
		ok &= ResourceSaver::save(test, path) == OK;
		Ref<ECSScene> loaded = ResourceLoader::load(path, "ECSScene", ResourceFormatLoader::CACHE_MODE_IGNORE);
		ok &= loaded.is_valid() && loaded->instantiate().is_valid();
		edit(original, original_entity, false);
		return ok;
	}
	Dictionary polygon; polygon["polygon"] = PackedVector2Array({Vector2(), Vector2(80, 0), Vector2(0, 80)}); polygon["uv"] = PackedVector2Array({Vector2(), Vector2(1, 0), Vector2(0, 1)}); polygon["skeleton"] = -1;
	Dictionary definition; definition["polygon_2d"] = polygon; test->set_entities(Array({definition}));
	edit(test, 0, false);
	Dictionary changed = polygon.duplicate(true); changed["polygon"] = PackedVector2Array({Vector2(), Vector2(120, 0), Vector2(0, 80)});
	mesh_changed(0, changed);
	bool ok = PackedVector2Array(Dictionary(Dictionary(test->get_entities()[0])["polygon_2d"])["polygon"])[1].x == 120 && test->instantiate().is_valid();
	auto *undo = EditorUndoRedoManager::get_singleton()->get_history_undo_redo(EditorUndoRedoManager::get_singleton()->get_history_id_for_object(test.ptr()));
	ok &= undo->undo();
	ok &= PackedVector2Array(Dictionary(Dictionary(test->get_entities()[0])["polygon_2d"])["polygon"])[1].x == 80;
	ok &= undo->redo();
	Dictionary uv_update=Dictionary(Dictionary(test->get_entities()[0])["polygon_2d"]).duplicate(true);
	uv_update["uv"]=PackedVector2Array({Vector2(),Vector2(.75,.25),Vector2(0,1)}); uv_changed(uv_update);
	ok &= PackedVector2Array(Dictionary(Dictionary(test->get_entities()[0])["polygon_2d"])["uv"])[1]==Vector2(.75,.25);
	ok &= undo->undo(); ok &= PackedVector2Array(Dictionary(Dictionary(test->get_entities()[0])["polygon_2d"])["uv"])[1]==Vector2(1,0);
	ok &= undo->redo();
	edit(test, -1, false); select_mode(1); mesh_changed(0, polygon);
	ok &= PackedVector2Array(Dictionary(Dictionary(test->get_entities()[0])["polygon_2d"])["polygon"])[1].x == 120;
	edit(original, original_entity, false);
	return ok;
}
#endif

