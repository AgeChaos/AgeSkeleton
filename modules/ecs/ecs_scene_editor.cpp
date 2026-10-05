#include "scene/gui/check_box.h"
#include "ecs_custom_components.h"
#include "ecs_ui_components.h"

#ifdef TOOLS_ENABLED
#include "ecs_scene_editor.h"
#include "ecs_model_importer.h"
#include "ecs_tilemap_editor.h"
#include "ecs_component_tool.h"
#include "scene/resources/sprite_frames.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/3d/mesh_library.h"
#include "editor/scene/2d/tiles/tile_set_editor.h"
#include "scene/resources/2d/tile_set.h"
#include "ecs_workspace_docking.h"
#include "ecs_live_edit.h"
#include "editor/debugger/editor_debugger_plugin.h"

#include "core/input/input.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_log.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_bottom_panel.h"
#include "editor/gui/editor_spin_slider.h"
#include "editor/gui/window_wrapper.h"
#include "editor/run/editor_run_bar.h"
#include "editor/run/embedded_process.h"
#include "editor/run/game_view_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/foldable_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup.h"
#include "scene/gui/separator.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/audio_stream_wav.h"
#include "scene/resources/canvas_item_material.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

static void ecs_repair_skeletal_references(Array &entities) {
	for(int i=0;i<entities.size();i++) {
		Dictionary entity=entities[i]; if(!entity.has("skeleton_2d")) { continue; }
		Dictionary rig=entity["skeleton_2d"]; bool valid=true;
		for(int64_t bone:PackedInt64Array(rig.get("bones",PackedInt64Array()))) { if(bone<0 || bone>=entities.size() || !Dictionary(entities[bone]).has("bone_2d")) { valid=false; break; } }
		if(!valid) { entity.erase("skeleton_2d"); }
	}
	for(int i=0;i<entities.size();i++) {
		Dictionary entity=entities[i]; if(!entity.has("polygon_2d")) { continue; }
		Dictionary polygon=entity["polygon_2d"]; int rig=polygon.get("skeleton",-1);
		if(rig>=0 && (rig>=entities.size() || !Dictionary(entities[rig]).has("skeleton_2d"))) { polygon["skeleton"]=-1; polygon["bones"]=PackedInt32Array(); polygon["weights"]=PackedFloat32Array(); }
	}
}
static bool ecs_transform_field(const String &name) {
	return name == "position" || name == "rotation" || name == "scale";
}
static bool ecs_has_transform(const Dictionary &entity) {
	return entity.has("position") || entity.has("rotation") || entity.has("scale");
}
static String ecs_component_icon_name(const String &name) {
	return ECSCustomComponents::is_component(name) ? "CSharpScript" : name == "ui_layout" ? "Control"
			: name == "ui_text"															  ? "Label"
			: name == "ui_image"														  ? "TextureRect"
			: name == "ui_button"														  ? "Button"
			: name == "ui_toggle"														  ? "CheckBox"
			: name == "ui_input_field"													  ? "LineEdit"
			: name == "ui_slider"														  ? "HSlider"
			: name == "ui_progress"														  ? "ProgressBar"
			: name == "ui_scroll"														  ? "ScrollContainer"
			: name == "mesh"															  ? "MeshInstance3D"
			: name == "physics"															  ? "RigidBody3D"
			: name == "physics_2d"														  ? "RigidBody2D"
			: name == "camera"															  ? "Camera3D"
			: name == "bone_2d" ? "Bone2D"
			: name == "skeleton_2d" ? "Skeleton2D"
			: name == "tilemap_2d" ? "TileMapLayer"
			: name == "polygon_2d" ? "Polygon2D"
			: name == "particles" ? "GPUParticles3D"
			: name == "light"															  ? "DirectionalLight3D"
			: name == "animation"														  ? "AnimationPlayer"
			: name == "audio"															  ? "AudioStreamPlayer"
																						  : "Node3D";
}
void ECSSceneEntityEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_dont_undo_redo"), &ECSSceneEntityEditor::_dont_undo_redo);
}
void ECSSceneEntityEditor::target(const Ref<ECSScene> &value, int entity, const String &filter) {
	runtime_writer=Callable();
	property_filter = filter;
	scene = value;
	index = entity;
	notify_property_list_changed();
}
bool ECSSceneEntityEditor::_get(const StringName &p_name, Variant &value) const {
	const StringName name = (ECSUIComponents::is_component(property_filter) || ECSCustomComponents::is_component(property_filter)) && !String(p_name).contains("/") ? StringName(property_filter + "/" + String(p_name)) : p_name;
	if (scene.is_null()) {
		return false;
	}
	Array entities = scene->get_entities();
	if (index < 0 || index >= entities.size()) {
		return false;
	}
	Dictionary definition = ECSCustomComponents::with_defaults(entities[index], scene->get_custom_schemas());
	if (property_filter != "ui" && !String(name).begins_with("ui/")) {
		definition = ECSUIComponents::migrate(definition);
	}
	if (property_filter == "@transform" && ecs_transform_field(name)) {
		value = definition.get(name, name == StringName("scale") ? Vector3(1, 1, 1) : Vector3());
		return true;
	}
	if (name == StringName("active")) { value = definition.get("active", true); return true; }
	if (name == StringName("name")) {
		value = definition.get("name", String(U"实体 ") + itos(index));
		return true;
	}
	if (name == StringName("material") && definition.has("mesh") && !definition.has("material")) {
		value = Ref<Material>();
		return true;
	}
	if (name == StringName("ui/material") && definition.has("ui")) {
		value = Dictionary(definition["ui"]).get("material", Ref<Material>());
		return true;
	}
	if (name == StringName("definition")) {
		value = definition;
		return true;
	}
	if ((name == StringName("animation/clip") || name == StringName("animation/playing") || name == StringName("animation/time") || name == StringName("animation/speed")) && definition.has("animation")) {
		Dictionary animation = definition["animation"];
		String field = String(name).trim_prefix("animation/");
		if (field == "clip") {
			value = animation.get(field, Variant());
		} else if (field == "playing") {
			value = animation.get(field, true);
		} else if (field == "speed") {
			value = animation.get(field, 1.0);
		} else if (field == "time") {
			value = animation.get(field, 0.0);
		} else {
			return false;
		}
		return true;
	}
	if (String(name).contains("/")) {
		String group = String(name).get_slice("/", 0), field = String(name).get_slice("/", 1);
		if (definition.has(group) && definition[group].get_type() == Variant::DICTIONARY) {
			Dictionary data = definition[group];
			if (data.has(field)) {
				value = data[field];
				return true;
			}
		}
	}
	if (!definition.has(name)) {
		return false;
	}
	value = definition[name];
	return true;
}
void ECSSceneEntityEditor::_get_property_list(List<PropertyInfo> *list) const {
	if (scene.is_null()) {
		return;
	}
	Array entities = scene->get_entities();
	if (index < 0 || index >= entities.size()) {
		return;
	}
	Dictionary definition = ECSCustomComponents::with_defaults(entities[index], scene->get_custom_schemas());
	if (property_filter != "ui") {
		definition = ECSUIComponents::migrate(definition);
	}
	if (property_filter == "@transform") {
		for (const String &field : { String("position"), String("rotation"), String("scale") }) {
			list->push_back(PropertyInfo(Variant::VECTOR3, field));
		}
		return;
	}
	if (!definition.has("active") && property_filter.is_empty()) {
		list->push_back(PropertyInfo(Variant::BOOL, "active"));
	}
	if (!definition.has("name") && property_filter.is_empty()) {
		list->push_back(PropertyInfo(Variant::STRING, "name"));
	}
	for (const Variant &key : definition.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			continue;
		}
		String name = key;
		if (property_filter == "@metadata" && name != "parent") {
			continue;
		}
		if (!property_filter.is_empty() && property_filter != "@metadata" && name != property_filter && !(property_filter == "@renderer" && (name == "mesh" || name == "material"))) {
			continue;
		}
		Variant value = definition[key];
		if (value.get_type() == Variant::DICTIONARY) {
			Dictionary data = value;
			if (property_filter != name) {
				list->push_back(PropertyInfo(Variant::NIL, name, PROPERTY_HINT_NONE, name + "/", PROPERTY_USAGE_GROUP));
			}
			for (const Variant &field : data.keys()) {
				String field_name = field;
				if (name == "ui_layout" && field_name == "alignment") {
					continue;
				}
				if (name == "animation" && (field_name == "clip" || field_name == "playing" || field_name == "time" || field_name == "speed")) {
					continue;
				}
				Variant entry = data[field];
				PropertyInfo property(entry.get_type(), (ECSUIComponents::is_component(name) || ECSCustomComponents::is_component(name)) && property_filter == name ? field_name : name + "/" + field_name);
				if (ECSUIComponents::is_component(name)) {
					if (field_name == "texture" || field_name == "font" || field_name == "material") {
						property.type = Variant::OBJECT;
						property.hint = PROPERTY_HINT_RESOURCE_TYPE;
						property.hint_string = field_name == "texture" ? "Texture2D" : field_name == "font" ? "Font"
																											: "CanvasItemMaterial,ShaderMaterial";
					}
					if (field_name == "alignment") {
						property.hint = PROPERTY_HINT_ENUM;
						property.hint_string = String(U"自定义锚点,左上,上中,右上,左中,居中,右中,左下,下中,右下,拉伸填满");
					}
					if (field_name == "arrangement") {
						property.hint = PROPERTY_HINT_ENUM;
						property.hint_string = String(U"自由布局,横向布局,纵向布局,网格布局");
					}
					if (field_name == "image_stretch") {
						property.hint = PROPERTY_HINT_ENUM;
						property.hint_string = String(U"拉伸,平铺,原始大小,原始大小居中,等比适应,等比居中,等比裁切");
					}
				}
				if (name == "tilemap_2d" && field_name == "tile_set") { property.type=Variant::OBJECT; property.hint=PROPERTY_HINT_RESOURCE_TYPE; property.hint_string="TileSet"; }
				if(name=="sprite_frames" && field_name=="space") { property.hint=PROPERTY_HINT_ENUM; property.hint_string="ui,world"; }
				if (name == "polygon_2d" && field_name == "texture") { property.type = Variant::OBJECT; property.hint = PROPERTY_HINT_RESOURCE_TYPE; property.hint_string = "Texture2D"; }
				if ((name == "sprite_frames" && field_name == "frames") || (name == "theme" && field_name == "resource") || (name == "gridmap_3d" && field_name == "library")) { property.type=Variant::OBJECT; property.hint=PROPERTY_HINT_RESOURCE_TYPE; property.hint_string=name=="sprite_frames"?"SpriteFrames":name=="theme"?"Theme":"MeshLibrary"; }
				if (name == "particles" && (field_name == "size_curve" || field_name == "color_ramp" || field_name == "material" || field_name == "texture")) {
					property.type=Variant::OBJECT; property.hint=PROPERTY_HINT_RESOURCE_TYPE;
					property.hint_string=field_name=="size_curve"?"Curve":field_name=="color_ramp"?"Gradient":field_name=="texture"?"Texture2D":"Material";
				}
				if (name == "ui" && field_name == "material") {
					property.type = Variant::OBJECT;
					property.hint = PROPERTY_HINT_RESOURCE_TYPE;
					property.hint_string = "CanvasItemMaterial,ShaderMaterial";
				}
				if (entry.get_type() == Variant::OBJECT && !(name == "ui" && field_name == "material")) {
					Ref<Resource> resource = entry;
					if (resource.is_valid()) {
						property.hint = PROPERTY_HINT_RESOURCE_TYPE;
						property.hint_string = resource->get_class();
					}
				}
				list->push_back(property);
			}
			if (property_filter != name) {
				list->push_back(PropertyInfo(Variant::NIL, "", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_GROUP));
			}
			continue;
		}
		PropertyInfo info(value.get_type(), name);
		if (name == "parent") {
			info.hint = PROPERTY_HINT_RANGE;
			info.hint_string = "-1," + itos(entities.size() - 1) + ",1";
		}
		if (name == "mesh" || name == "material") {
			info.type = Variant::OBJECT;
			info.hint = PROPERTY_HINT_RESOURCE_TYPE;
			info.hint_string = name == "mesh" ? "Mesh" : "Material";
		}
		list->push_back(info);
	}
	if (definition.has("animation") && (property_filter.is_empty() || property_filter == "animation")) {
		list->push_back(PropertyInfo(Variant::NIL, String(U"动画播放"), PROPERTY_HINT_NONE, "animation/", PROPERTY_USAGE_GROUP));
		list->push_back(PropertyInfo(Variant::OBJECT, "animation/clip", PROPERTY_HINT_RESOURCE_TYPE, "Animation"));
		list->push_back(PropertyInfo(Variant::BOOL, "animation/playing"));
		list->push_back(PropertyInfo(Variant::FLOAT, "animation/time", PROPERTY_HINT_RANGE, "0,3600,0.01,or_greater"));
		list->push_back(PropertyInfo(Variant::FLOAT, "animation/speed", PROPERTY_HINT_RANGE, "-10,10,0.01,or_greater,or_less"));
	}
	if ((property_filter.is_empty() || property_filter == "@renderer") && definition.has("mesh") && !definition.has("material")) {
		list->push_back(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "Material"));
	}
	if ((property_filter.is_empty() || property_filter == "ui") && definition.has("ui") && !Dictionary(definition["ui"]).has("material")) {
		list->push_back(PropertyInfo(Variant::OBJECT, "ui/material", PROPERTY_HINT_RESOURCE_TYPE, "CanvasItemMaterial,ShaderMaterial"));
	}
	// Raw definition remains available through the resource, not the normal authoring Inspector.
}
bool ECSSceneEntityEditor::_set(const StringName &p_name, const Variant &value) {
	const StringName name = (ECSUIComponents::is_component(property_filter) || ECSCustomComponents::is_component(property_filter)) && !String(p_name).contains("/") ? StringName(property_filter + "/" + String(p_name)) : p_name;
	if(runtime_writer.is_valid()) { return runtime_writer.call(index,name,value); }
	if (scene.is_null()) {
		return false;
	}
	Array before = scene->get_entities();
	if (index < 0 || index >= before.size()) {
		return false;
	}
	Array after = before.duplicate(true);
	Dictionary definition = ECSCustomComponents::with_defaults(after[index], scene->get_custom_schemas());
	after[index] = definition;
	if (ECSUIComponents::is_component(String(name).get_slice("/", 0))) {
		definition = ECSUIComponents::migrate(definition);
		after[index] = definition;
	}
	if ((name == StringName("material") && definition.has("mesh")) || (name == StringName("ui/material") && definition.has("ui"))) {
		if (value.get_type() != Variant::NIL && value.get_type() != Variant::OBJECT) {
			return false;
		}
		Ref<Material> material = value;
		bool clear = value.get_type() == Variant::NIL || (value.get_type() == Variant::OBJECT && value.is_null());
		if (!clear && (value.get_type() != Variant::OBJECT || material.is_null())) {
			return false;
		}
		bool ui = name == StringName("ui/material");
		if (material.is_valid() && material->get_shader_mode() != (ui ? Shader::MODE_CANVAS_ITEM : Shader::MODE_SPATIAL)) {
			return false;
		}
		if (ui) {
			Dictionary ui_data = definition["ui"];
			if (clear) {
				ui_data.erase("material");
			} else {
				ui_data["material"] = material;
			}
		} else if (clear) {
			definition.erase("material");
		} else {
			definition["material"] = material;
		}
	} else if ((name == StringName("animation/clip") || name == StringName("animation/playing") || name == StringName("animation/time") || name == StringName("animation/speed")) && definition.has("animation")) {
		String field = String(name).trim_prefix("animation/");
		if (field == "clip") {
			Ref<Animation> clip = value;
			if (clip.is_null()) {
				return false;
			}
		} else if (field == "playing") {
			if (value.get_type() != Variant::BOOL) {
				return false;
			}
		} else if (field == "time" || field == "speed") {
			if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value)) || (field == "time" && double(value) < 0)) {
				return false;
			}
		} else {
			return false;
		}
		Dictionary animation = Dictionary(definition["animation"]).duplicate(true);
		animation[field] = value;
		definition["animation"] = animation;
	} else if (String(name).contains("/")) {
		String group = String(name).get_slice("/", 0), field = String(name).get_slice("/", 1);
		if (!definition.has(group) || definition[group].get_type() != Variant::DICTIONARY) {
			return false;
		}
		Dictionary data = definition[group];
		if (!data.has(field)) {
			return false;
		}
		data[field] = value;
		definition[group] = data;
	} else if (name == StringName("definition")) {
		if (value.get_type() != Variant::DICTIONARY) {
			return false;
		}
		after[index] = Dictionary(value).duplicate(true);
	} else {
		if (!definition.has(name) && name != StringName("name") && name != StringName("active") && !(property_filter == "@transform" && ecs_transform_field(name))) {
			return false;
		}
		if (property_filter == "@transform" && ecs_transform_field(name) && (value.get_type() != Variant::VECTOR3 || !Vector3(value).is_finite())) {
			return false;
		}
		if (name == StringName("active") && value.get_type() != Variant::BOOL) { return false; }
		definition[name] = value;
	}
	if (ECSUIComponents::is_component(String(name).get_slice("/", 0))) {
		Dictionary compiled;
		Ref<ECSWorld> validation;
		validation.instantiate();
		if (!ECSUIComponents::compose(after[index], compiled) || (!compiled.is_empty() && !validation->set_ui(validation->create_entity(), compiled))) {
			return false;
		}
	}
	String custom_name = String(name).get_slice("/", 0);
	if (ECSCustomComponents::is_component(custom_name)) {
		Dictionary schemas = scene->get_custom_schemas();
		schemas.merge(ECSCustomComponents::registry(), true);
		PackedByteArray bytes;
		if (!schemas.has(custom_name) || !ECSCustomComponents::pack(schemas[custom_name], definition[custom_name], bytes)) {
			return false;
		}
	}
	auto *undo = EditorUndoRedoManager::get_singleton();
	undo->create_action(String(U"修改 ECS 实体 ") + itos(index) + " / " + String(name), UndoRedo::MERGE_ENDS, scene.ptr());
	undo->add_do_method(scene.ptr(), "set_entities", after);
	undo->add_undo_method(scene.ptr(), "set_entities", before);
	undo->commit_action();
	return true;
}
void ECSSceneEditorPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_refresh"), &ECSSceneEditorPanel::refresh);
	ClassDB::bind_method(D_METHOD("_self_test"), &ECSSceneEditorPanel::run_self_test);
	ClassDB::bind_method(D_METHOD("_visual_test"), &ECSSceneEditorPanel::run_visual_test);
}
ECSSceneEditorPanel::ECSSceneEditorPanel() {
	callable_mp(this, &ECSSceneEditorPanel::ai_start).call_deferred();
	set_process_shortcut_input(true);
	set_custom_minimum_size(Vector2(0, 340));
	status = memnew(Label);
	status->set_text(String(U"在文件系统中选择 ECSScene 资源，编辑实体及组件。运行时不创建 Node。"));
	add_child(status);
	component = memnew(OptionButton);
	for (const char *name : { "position", "velocity", "rotation", "scale", "mesh", "material", "ui_layout", "ui_image", "ui_text", "ui_button", "ui_toggle", "ui_input_field", "ui_slider", "ui_progress", "ui_scroll", "physics", "physics_2d", "area_2d", "area", "pin_joint", "joint_2d", "joint_3d", "bone_2d", "skeleton_2d", "polygon_2d", "tilemap_2d", "sprite_frames", "gridmap_3d", "theme", "replication", "animation", "audio", "navigation", "camera", "light", "particles" }) {
		component->add_item(name);
	}
	add_child(component);
	component->hide();
	auto *columns = memnew(HSplitContainer);
	columns->set_v_size_flags(SIZE_EXPAND_FILL);
	add_child(columns);
	auto *left = memnew(VSplitContainer);
	left->set_h_size_flags(SIZE_EXPAND_FILL);
	left->set_stretch_ratio(2.4);
	columns->add_child(left);
	auto *right = memnew(HSplitContainer);
	right->set_h_size_flags(SIZE_EXPAND_FILL);
	right->set_stretch_ratio(2);
	columns->add_child(right);
	auto *middle = memnew(VSplitContainer);
	middle->set_h_size_flags(SIZE_EXPAND_FILL);
	middle->set_stretch_ratio(1.7);
	right->add_child(middle);
	auto *side = memnew(VSplitContainer);
	side->set_h_size_flags(SIZE_EXPAND_FILL);
	right->add_child(side);
	workspace_splits = { columns, left, right, middle, side };
	for (SplitContainer *split : workspace_splits) {
		split->add_theme_constant_override("separation", 6);
	}
	auto tabs = [this](Node *parent) {
		auto *t = memnew(TabContainer);
		t->set_custom_minimum_size(Size2(120, 100));
		t->set_drag_to_rearrange_enabled(true);
		t->set_tabs_rearrange_group(7301);
		t->set_h_size_flags(SIZE_EXPAND_FILL);
		t->set_v_size_flags(SIZE_EXPAND_FILL);
		Ref<StyleBoxFlat> panel;
		panel.instantiate();
		panel->set_bg_color(Color(.165, .173, .184));
		panel->set_border_width_all(1);
		panel->set_border_color(Color(.09, .098, .11));
		panel->set_content_margin_all(5);
		panel->set_border_width(SIDE_TOP,0);
		panel->set_content_margin(SIDE_TOP,0);
		t->add_theme_style_override("panel", panel);
		Ref<StyleBoxFlat> header;
		header.instantiate();
		header->set_bg_color(Color(.125, .133, .145));
		header->set_border_width_all(1);
		header->set_border_width(SIDE_BOTTOM,0);
		header->set_border_color(Color(.09, .098, .11));
		t->add_theme_style_override("tabbar_background", header);
		Ref<StyleBoxFlat> active;
		active.instantiate();
		active->set_bg_color(Color(.205, .22, .24));
		active->set_border_width_all(0);
		active->set_border_color(Color(.09, .098, .11));
		active->set_content_margin_all(7);
		active->set_corner_radius(CORNER_TOP_RIGHT,8);
		active->set_corner_radius(CORNER_TOP_LEFT,4);
		active->set_expand_margin(SIDE_BOTTOM,1);
		t->add_theme_style_override("tab_selected", active);
		Ref<StyleBoxFlat> inactive;
		inactive.instantiate();
		inactive->set_bg_color(Color(.125, .133, .145));
		inactive->set_content_margin_all(7);
		t->add_theme_style_override("tab_unselected", inactive);
		parent->add_child(t);
		workspace_tabs.push_back(t);
		return t;
	};
	auto host = [](TabContainer *tabs, const String &name) {auto *v=memnew(VBoxContainer);v->set_name(name);v->set_v_size_flags(SIZE_EXPAND_FILL);tabs->add_child(v);return v; };
	auto *scene_tabs = tabs(left);
	scene_tabs->set_stretch_ratio(0.5);
	scene_host = host(scene_tabs, "Scene");
	auto *scene_toolbar = memnew(HBoxContainer);
	scene_host->add_child(scene_toolbar);
	scene_2d = memnew(Button);
	scene_2d->set_text("2D");
	scene_2d->set_flat(true);
	scene_2d->set_focus_mode(FOCUS_NONE);
	scene_2d->set_toggle_mode(true);
	scene_2d->set_tooltip_text(String(U"切换 2D / UI 编辑视图"));
	scene_2d->connect("toggled", callable_mp(this, &ECSSceneEditorPanel::set_scene_2d));
	scene_toolbar->add_child(scene_2d);
	auto *scene_toolbar_spacer = memnew(Control);
	scene_toolbar_spacer->set_h_size_flags(SIZE_EXPAND_FILL);
	scene_toolbar->add_child(scene_toolbar_spacer);
	scene_toolbar->move_child(scene_toolbar_spacer, 0);
	scene_grid = memnew(Button);
	scene_grid->set_flat(true);
	scene_grid->set_focus_mode(FOCUS_NONE);
	scene_grid->set_tooltip_text(TTR("Show Grid"));
	scene_grid->set_toggle_mode(true);
	scene_grid->set_pressed_no_signal(true);
	scene_grid->connect("toggled", callable_mp(this, &ECSSceneEditorPanel::set_scene_grid));
	scene_toolbar->add_child(scene_grid);
	spatial = memnew(ECSSpatialEditor);
	spatial->set_drag_forwarding(Callable(), callable_mp(this, &ECSSceneEditorPanel::model_can_drop), callable_mp(this, &ECSSceneEditorPanel::model_drop));
	spatial->set_name("Scene");
	scene_host->add_child(spatial);
	spatial->connect("rect_tool_requested", callable_mp(this, &ECSSceneEditorPanel::show_rect_tool));
	spatial->connect("entity_selected", callable_mp(this, &ECSSceneEditorPanel::canvas_selected));
	spatial->connect("runtime_transform_changed",callable_mp(this,&ECSSceneEditorPanel::runtime_transform_changed));
	canvas = memnew(ECSUICanvasEditor);
	canvas->set_name("UI");
	scene_host->add_child(canvas);
	canvas->hide();
	canvas->connect("runtime_transform_changed",callable_mp(this,&ECSSceneEditorPanel::runtime_transform_changed));
	canvas->connect("entity_selected", callable_mp(this, &ECSSceneEditorPanel::canvas_selected));
	auto *assemblies = host(scene_tabs, "Assemblies");
	auto *assembly_bar = memnew(HBoxContainer);
	assemblies->add_child(assembly_bar);
	assembly_platform = memnew(OptionButton);
	for (const char *platform : { "windows", "linuxbsd", "macos", "android", "ios", "web", "ohos" }) {
		assembly_platform->add_item(platform);
	}
	assembly_bar->add_child(assembly_platform);
	const char32_t *assembly_actions[] = { U"刷新依赖", U"构建项目", U"打开解决方案", U"创建 Assets 目录" };
	for (int i = 0; i < 4; i++) {
		auto *button = memnew(Button);
		button->set_text(String(assembly_actions[i]));
		button->connect("pressed", callable_mp(this, &ECSSceneEditorPanel::assembly_action).bind(i));
		assembly_bar->add_child(button);
	}
	assembly_tree = memnew(Tree);
	assembly_tree->set_columns(4);
	assembly_tree->set_column_titles_visible(true);
	assembly_tree->set_hide_root(true);
	assembly_tree->set_column_title(0, String(U"程序集"));
	assembly_tree->set_column_title(1, String(U"用途"));
	assembly_tree->set_column_title(2, String(U"框架"));
	assembly_tree->set_column_title(3, String(U"源码数量"));
	assembly_tree->set_v_size_flags(SIZE_EXPAND_FILL);
	assemblies->add_child(assembly_tree);
	assembly_tree->connect("item_selected", callable_mp(this, &ECSSceneEditorPanel::assembly_selected));
	assembly_details = memnew(TextEdit);
	assembly_details->set_editable(false);
	assembly_details->set_v_size_flags(SIZE_EXPAND_FILL);
	assembly_details->set_text(String(U"使用 .sln / .csproj 和 ProjectReference 管理程序集。\n刷新依赖会按所选平台评估 MSBuild 条件；构建项目沿用项目配置的构建器。\n平台评估不代表该平台已编译或通过运行验收。"));
	assemblies->add_child(assembly_details);
	callable_mp(this, &ECSSceneEditorPanel::assembly_refresh_view).call_deferred();
	game_host = host(tabs(left), "Game");
    tilemap_editor=memnew(ECSTilemapEditor); host(scene_tabs,"TileMap")->add_child(tilemap_editor);
    tilemap_editor->connect("tilemap_edited",callable_mp(this,&ECSSceneEditorPanel::tilemap_edited));
    canvas->connect("tilemap_edited",callable_mp(this,&ECSSceneEditorPanel::tilemap_edited));
	auto *hierarchy_tabs = tabs(middle);
	hierarchy_tabs->set_stretch_ratio(0.5);
	auto *hierarchy = host(hierarchy_tabs, "Hierarchy");
	hierarchy_menu = memnew(PopupMenu);
	add_child(hierarchy_menu);
	hierarchy_menu->add_item(String(U"创建空实体"), 0);
	auto *ui_presets = memnew(PopupMenu);
	ui_presets->set_name("UICreatePresets");
	hierarchy_menu->add_child(ui_presets);
	const char32_t *preset_labels[] = { U"面板", U"文字", U"图片", U"按钮", U"开关", U"输入框", U"多行输入框", U"滑条", U"进度条", U"滚动区域", U"横向布局", U"纵向布局", U"网格布局" };
	for (int i = 0; i < 13; i++) {
		ui_presets->add_item(String(preset_labels[i]), 20 + i);
	}
	ui_presets->connect("id_pressed", callable_mp(this, &ECSSceneEditorPanel::hierarchy_action));
	hierarchy_menu->add_submenu_item(String(U"UI 预设"), "UICreatePresets");
	hierarchy_menu->add_item(String(U"创建子实体"), 2);
	hierarchy_menu->add_separator();
	hierarchy_menu->add_item(String(U"复制     Ctrl+D"), 3);
	hierarchy_menu->add_item(String(U"删除     Delete"), 4);
	hierarchy_menu->connect("id_pressed", callable_mp(this, &ECSSceneEditorPanel::hierarchy_action));
	tree = memnew(Tree);
	tree->set_custom_minimum_size(Vector2(190, 180));
	tree->set_hide_root(true);
	tree->set_allow_rmb_select(true);
	tree->set_drop_mode_flags(Tree::DROP_MODE_ON_ITEM);
	tree->set_drag_forwarding(callable_mp(this, &ECSSceneEditorPanel::hierarchy_drag), callable_mp(this, &ECSSceneEditorPanel::hierarchy_can_drop), callable_mp(this, &ECSSceneEditorPanel::hierarchy_drop));
	tree->connect("item_mouse_selected", callable_mp(this, &ECSSceneEditorPanel::hierarchy_context).bind(false));
	tree->connect("empty_clicked", callable_mp(this, &ECSSceneEditorPanel::hierarchy_context).bind(true));
	tree->set_v_size_flags(SIZE_EXPAND_FILL);
	tree->connect("item_selected", callable_mp(this, &ECSSceneEditorPanel::select));
	hierarchy->add_child(tree);
	project_host = host(tabs(middle), "Project");
	auto *inspector_tabs = tabs(side);
	inspector_tabs->set_stretch_ratio(0.57);
	auto *properties = host(inspector_tabs, "Inspector");
	inspector = memnew(EditorInspector);
	inspector->set_custom_minimum_size(Vector2(230, 0));
	inspector->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	inspector->set_v_size_flags(SIZE_EXPAND_FILL);
	auto *inspector_scroll = memnew(ScrollContainer);
	inspector_scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	inspector_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	properties->add_child(inspector_scroll);
	auto *inspector_body = memnew(VBoxContainer);
	inspector_body->set_h_size_flags(SIZE_EXPAND_FILL);
	inspector_scroll->add_child(inspector_body);
	entity_header = memnew(HBoxContainer);
	inspector_body->add_child(entity_header);
	runtime_actions=memnew(HBoxContainer);
	inspector_body->add_child(runtime_actions);
	auto *copy_runtime=memnew(Button); copy_runtime->set_text(String(U"复制运行值")); runtime_actions->add_child(copy_runtime);
	copy_runtime->connect("pressed",callable_mp(this,&ECSSceneEditorPanel::runtime_copy));
	auto *apply_runtime=memnew(Button); apply_runtime->set_text(String(U"应用到编辑场景")); runtime_actions->add_child(apply_runtime);
	apply_runtime->connect("pressed",callable_mp(this,&ECSSceneEditorPanel::runtime_apply));
	runtime_actions->hide();
	entity_active = memnew(CheckBox);
	entity_active->set_tooltip_text(TTR("Active"));
	entity_header->add_child(entity_active);
	entity_active->connect("toggled", callable_mp(this, &ECSSceneEditorPanel::entity_active_changed));
	entity_name = memnew(LineEdit);
	entity_name->set_h_size_flags(SIZE_EXPAND_FILL);
	entity_name->set_placeholder(TTR("Name"));
	entity_header->add_child(entity_name);
	entity_name->connect("text_submitted", callable_mp(this, &ECSSceneEditorPanel::entity_name_submitted));
	entity_name->connect("focus_exited", callable_mp(this, &ECSSceneEditorPanel::entity_name_focus_exited));
	entity_header->hide();
	inspector_body->add_child(inspector);
	component_sections = memnew(VBoxContainer);
	component_sections->set_h_size_flags(SIZE_EXPAND_FILL);
	inspector_body->add_child(component_sections);
	auto *add = memnew(Button);
	component_add_button = add;
	add->set_text(TTR("Add Component"));
	add->set_tooltip_text(String(U"搜索并添加组件"));
	add->connect("pressed", callable_mp(this, &ECSSceneEditorPanel::open_component_picker));
	properties->add_child(add);
	auto *options = memnew(MenuButton);
	options->set_text(String(U"组件操作…"));
	properties->add_child(options);
	properties->move_child(add, properties->get_child_count() - 1);
	component_actions = options->get_popup();
	component_actions->connect("about_to_popup", callable_mp(this, &ECSSceneEditorPanel::update_component_actions));
	component_actions->connect("id_pressed", callable_mp(this, &ECSSceneEditorPanel::inspector_action));
	component_picker = memnew(PopupPanel);
	component_picker->hide();
	component_picker->set_force_native(true);
	component_picker->set_transient_to_focused(true);
	add_child(component_picker);
	auto *picker_box = memnew(VBoxContainer);
	component_picker->add_child(picker_box);
	component_search = memnew(LineEdit);
	component_search->set_placeholder(TTR("Search Components"));
	component_search->set_clear_button_enabled(true);
	component_search->connect("gui_input", callable_mp(this, &ECSSceneEditorPanel::component_search_input));
	picker_box->add_child(component_search);
	component_search->connect("text_changed", callable_mp(this, &ECSSceneEditorPanel::component_filter));
	component_picker_status = memnew(Label);
	component_picker_status->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	picker_box->add_child(component_picker_status);
	component_results = memnew(ItemList);
	component_results->set_custom_minimum_size(Size2(280, 280) * EDSCALE);
	component_results->set_fixed_icon_size(Size2(16, 16) * EDSCALE);
	component_results->set_v_size_flags(SIZE_EXPAND_FILL);
	picker_box->add_child(component_results);
	component_results->connect("item_clicked", callable_mp(this, &ECSSceneEditorPanel::component_click));
	component_results->connect("item_activated", callable_mp(this, &ECSSceneEditorPanel::component_pick));
	console_host = host(tabs(side), "Debugger");
	proxy.instantiate();
	move_child(status, get_child_count() - 1);
}
static void ecs_light_popup(PopupMenu *p_menu) {
	Ref<StyleBoxFlat> panel;
	panel.instantiate();
	panel->set_bg_color(Color(.96, .96, .96));
	panel->set_border_width_all(1);
	panel->set_border_color(Color(.65, .65, .65));
	panel->set_content_margin_all(6);
	Ref<StyleBoxFlat> hover;
	hover.instantiate();
	hover->set_bg_color(Color(.76, .85, .97));
	p_menu->add_theme_style_override("panel", panel);
	p_menu->add_theme_style_override("hover", hover);
	p_menu->add_theme_color_override("font_color", Color(.08, .08, .08));
	p_menu->add_theme_color_override("font_hover_color", Color(.02, .02, .02));
	p_menu->add_theme_color_override("font_disabled_color", Color(.5, .5, .5));
	p_menu->add_theme_color_override("font_separator_color", Color(.3, .3, .3));
}
void ECSSceneEditorPanel::queue_component_sections() {
	if (sections_queued) {
		return;
	}
	sections_queued = true;
	callable_mp(this, &ECSSceneEditorPanel::rebuild_component_sections).call_deferred();
}
void ECSSceneEditorPanel::component_fold_changed(bool p_folded, const String &p_key) {
	component_folded[p_key] = p_folded;
}
void ECSSceneEditorPanel::component_section_action(int p_action, const String &p_key) {
	if (scene.is_null() || selected < 0 || selected >= scene->get_entities().size()) {
		return;
	}
	Array entities = scene->get_entities();
	Dictionary entity = ECSUIComponents::migrate(entities[selected]);
	if (p_key == "position" && ecs_has_transform(entity)) {
		if (p_action == 2) {
			component_clipboard_type = "@transform";
			component_clipboard.clear();
			for (const String &key : { String("position"), String("rotation"), String("scale") }) {
				component_clipboard[key] = entity.get(key, key == "scale" ? Vector3(1, 1, 1) : Vector3());
			}
			return;
		}
		if (p_action < 0 || p_action > 3 || (p_action == 3 && component_clipboard_type != "@transform")) {
			return;
		}
		for (const String &key : { String("position"), String("rotation"), String("scale") }) {
			if (p_action == 0) {
				entity.erase(key);
			} else {
				entity[key] = p_action == 3 ? component_clipboard[key] : Variant(key == "scale" ? Vector3(1, 1, 1) : Vector3());
			}
		}
		entities[selected] = entity;
		commit(entities, TTR("Transform"));
		return;
	}
	if (!entity.has(p_key)) {
		return;
	}
	if (p_action != 0) {
		if (!ECSCustomComponents::is_component(p_key)) {
			return;
		}
		if (p_action == 2) {
			component_clipboard_type = p_key;
			component_clipboard = Dictionary(entity[p_key]).duplicate(true);
			return;
		}
		Dictionary schemas = scene->get_custom_schemas();
		schemas.merge(ECSCustomComponents::registry(), true);
		if (!schemas.has(p_key)) {
			return;
		}
		Dictionary values;
		if (p_action == 1) {
			values = ECSCustomComponents::defaults(schemas[p_key]);
		} else if (p_action == 3 && component_clipboard_type == p_key) {
			values = component_clipboard.duplicate(true);
		} else {
			return;
		}
		PackedByteArray bytes;
		if (!ECSCustomComponents::pack(schemas[p_key], values, bytes)) {
			return;
		}
		entity[p_key] = values;
		entities[selected] = entity;
		commit(entities, p_action == 1 ? TTR("Reset") : TTR("Paste"));
		return;
	}
	if (ECSUIComponents::is_component(p_key)) {
		ECSUIComponents::remove(entity, p_key);
	} else {
		entity.erase(p_key);
	}
	entities[selected] = entity;
	if (p_key == "mesh") {
		entity.erase("material");
	}
	ecs_repair_skeletal_references(entities);
	commit(entities, String(U"移除 ECS 组件 ") + p_key);
}
namespace {
Ref<Texture2D> ecs_anchor_icon(int x, int y) {
	Ref<Image> icon = Image::create_empty(40, 40, false, Image::FORMAT_RGBA8);
	icon->fill(Color(0, 0, 0, 0));
	auto line = [&](int ax, int ay, int bx, int by, Color c) {
		int steps = MAX(Math::abs(bx - ax), Math::abs(by - ay));
		for (int i = 0; i <= steps; i++) {
			icon->set_pixel(ax + (bx - ax) * i / MAX(1, steps), ay + (by - ay) * i / MAX(1, steps), c);
		}
	};
	Color frame(.55, .55, .55), cyan(.25, .75, 1), gold(1, .68, .2);
	line(5, 5, 34, 5, frame);
	line(34, 5, 34, 34, frame);
	line(34, 34, 5, 34, frame);
	line(5, 34, 5, 5, frame);
	int left = x == 3 ? 8 : 8 + x * 10, right = x == 3 ? 31 : left;
	int top = y == 3 ? 8 : 8 + y * 10, bottom = y == 3 ? 31 : top;
	line(left, top, right, bottom, cyan);
	for (int xx : { left, right }) {
		for (int yy : { top, bottom }) {
			line(xx - 2, yy, xx + 2, yy, gold);
			line(xx, yy - 2, xx, yy + 2, gold);
		}
	}
	if (x == 3) {
		line(9, 20, 30, 20, cyan);
		line(9, 20, 13, 17, cyan);
		line(9, 20, 13, 23, cyan);
		line(30, 20, 26, 17, cyan);
		line(30, 20, 26, 23, cyan);
	}
	if (y == 3) {
		line(20, 9, 20, 30, cyan);
		line(20, 9, 17, 13, cyan);
		line(20, 9, 23, 13, cyan);
		line(20, 30, 17, 26, cyan);
		line(20, 30, 23, 26, cyan);
	}
	return ImageTexture::create_from_image(icon);
}
} //namespace
void ECSSceneEditorPanel::style_component_inputs() {
	// Reuse the editor palette without changing dock or control metrics.
	const Color base = get_theme_color("base_color", "Editor");
	const Color recessed = get_theme_color("dark_color_1", "Editor");
	const Color border = get_theme_color("dark_color_3", "Editor");
	const Color accent = get_theme_color("accent_color", "Editor");
	for (TabContainer *tabs : workspace_tabs) {
		for (const char *name : { "panel", "tabbar_background", "tab_selected", "tab_unselected" }) {
			Ref<StyleBoxFlat> style = tabs->get_theme_stylebox(name)->duplicate();
			if (style.is_valid()) {
				const String part(name);
				style->set_bg_color(part == "panel" || part == "tab_selected" ? base : recessed);
				style->set_border_color(border);
				if(part=="tab_selected") {
					style->set_border_width_all(0);
					style->set_corner_radius(CORNER_TOP_RIGHT,8*EDSCALE);
					style->set_corner_radius(CORNER_TOP_LEFT,4*EDSCALE);
					style->set_expand_margin(SIDE_BOTTOM,1*EDSCALE);
				} else if(part=="panel") {
					style->set_border_width(SIDE_TOP,0);
					style->set_content_margin(SIDE_TOP,0);
				} else if(part=="tabbar_background") { style->set_border_width(SIDE_BOTTOM,0); }
				tabs->add_theme_style_override(name, style);
			}
		}
	}
	Ref<StyleBoxFlat> box;
	box.instantiate();
	box->set_bg_color(recessed);
	box->set_border_color(get_theme_color("contrast_color_1", "Editor"));
	box->set_border_width_all(1);
	box->set_corner_radius_all(3);
	box->set_content_margin_all(4);
	Ref<Theme> input_theme;
	input_theme.instantiate();
	input_theme->set_stylebox("normal", "LineEdit", box);
	Ref<StyleBoxFlat> hover = box->duplicate();
	hover->set_bg_color(recessed.lerp(base, .5));
	hover->set_border_color(get_theme_color("contrast_color_2", "Editor"));
	Ref<StyleBoxFlat> focus = box->duplicate();
	focus->set_bg_color(Color(0, 0, 0, 0));
	focus->set_border_color(accent);
	focus->set_border_width_all(2);
	Ref<StyleBoxFlat> disabled = box->duplicate();
	disabled->set_bg_color(base);
	disabled->set_border_color(border);
	for (const char *type : { "LineEdit", "TextEdit", "OptionButton" }) {
		input_theme->set_stylebox("normal", type, box);
		input_theme->set_stylebox("hover", type, hover);
		input_theme->set_stylebox("focus", type, focus);
		input_theme->set_stylebox("read_only", type, disabled);
		input_theme->set_stylebox("disabled", type, disabled);
	}
	input_theme->set_constant("boxed_fields", "EditorSpinSlider", 1);
	component_sections->set_theme(input_theme);
	inspector->set_theme(input_theme);
	entity_header->set_theme(input_theme);
}
void ECSSceneEditorPanel::open_anchor_presets(Control *p_button) {
	ERR_FAIL_NULL(p_button);
	if (!anchor_presets) {
		anchor_presets = memnew(PopupPanel);
		anchor_presets->hide();
		anchor_presets->set_force_native(true);
		add_child(anchor_presets);
		auto *body = memnew(VBoxContainer);
		anchor_presets->add_child(body);
		auto *title = memnew(Label);
		title->set_text(String(U"锚点预设"));
		body->add_child(title);
		auto *hint = memnew(Label);
		hint->set_text(String(U"点击：设置锚点    Alt：同时重置偏移和边距"));
		body->add_child(hint);
		auto *grid = memnew(GridContainer);
		grid->set_columns(5);
		body->add_child(grid);
		const char32_t *horizontal[] = { U"左", U"中", U"右", U"拉伸" };
		const char32_t *vertical_labels[] = { U"上", U"中", U"下", U"拉伸" };
		grid->add_child(memnew(Label));
		for (int x = 0; x < 4; x++) {
			auto *label = memnew(Label);
			label->set_text(String(horizontal[x]));
			label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			grid->add_child(label);
		}
		for (int y = 0; y < 4; y++) {
			auto *label = memnew(Label);
			label->set_text(String(vertical_labels[y]));
			grid->add_child(label);
			for (int x = 0; x < 4; x++) {
				auto *button = memnew(Button);
				button->set_button_icon(ecs_anchor_icon(x, y));
				button->set_custom_minimum_size(Size2(58, 50));
				button->set_tooltip_text(String(horizontal[x]) + String(U" / ") + String(vertical_labels[y]));
				button->connect("pressed", callable_mp(this, &ECSSceneEditorPanel::apply_anchor_preset).bind(x, y));
				grid->add_child(button);
			}
		}
	}
	const Size2i popup_size = anchor_presets->get_contents_minimum_size();
	const Vector2i button_position = p_button->get_screen_position();
	Vector2i position = button_position + Vector2i(0, int(p_button->get_size().y));
	auto *display = DisplayServer::get_singleton();
	Rect2i bounds = display->screen_get_usable_rect(display->window_get_current_screen(p_button->get_window()->get_window_id()));
	if (position.y + popup_size.y > bounds.get_end().y) {
		position.y = button_position.y - popup_size.y;
	}
	position.x = CLAMP(position.x, bounds.position.x, MAX(bounds.position.x, bounds.get_end().x - popup_size.x));
	position.y = CLAMP(position.y, bounds.position.y, MAX(bounds.position.y, bounds.get_end().y - popup_size.y));
	anchor_presets->popup(Rect2i(position, popup_size));
}
void ECSSceneEditorPanel::apply_anchor_preset(int x, int y) {
	if (scene.is_null() || selected < 0 || selected >= scene->get_entities().size()) {
		return;
	}
	Array entities = scene->get_entities();
	Dictionary entity = ECSUIComponents::migrate(entities[selected]);
	if (!entity.has("ui_layout") || x < 0 || x > 3 || y < 0 || y > 3) {
		return;
	}
	Dictionary layout = entity["ui_layout"];
	layout["alignment"] = 0;
	layout["anchors"] = Vector4(x == 3 ? 0 : x * .5f, y == 3 ? 0 : y * .5f, x == 3 ? 1 : x * .5f, y == 3 ? 1 : y * .5f);
	if (Input::get_singleton()->is_key_pressed(Key::ALT)) {
		Rect2 rect = layout.get("rect", Rect2(0, 0, 180, 40));
		rect.position = Vector2();
		if (x == 3) {
			rect.size.x = 0;
		}
		if (y == 3) {
			rect.size.y = 0;
		}
		layout["rect"] = rect;
		layout["margins"] = Vector4();
	}
	entities[selected] = entity;
	commit(entities, String(U"修改 UI 锚点预设"));
	if (anchor_presets) {
		anchor_presets->hide();
	}
}
void ECSSceneEditorPanel::rebuild_component_sections() {
	sections_queued = false;
	Dictionary entity;
	if (scene.is_valid() && selected >= 0 && selected < scene->get_entities().size()) {
		entity = ECSCustomComponents::with_defaults(ECSUIComponents::migrate(scene->get_entities()[selected]), scene->get_custom_schemas());
	}
	String signature = scene.is_valid() ? itos(scene->get_instance_id()) + ":" + itos(selected) : "none";
	for (const Variant &key : entity.keys()) {
		signature += String(key) + ":" + itos(entity[key].get_type());
		if (entity[key].get_type() == Variant::DICTIONARY) {
			Dictionary fields = entity[key];
			for (const Variant &field : fields.keys()) {
				signature += String(field) + itos(fields[field].get_type());
			}
		}
	}
	Dictionary composed_ui;
	if (entity.has("ui_layout") && ECSUIComponents::compose(entity, composed_ui)) {
		signature += ":anchors:" + String(composed_ui.get("anchors", Vector4()));
	}
	if (signature == component_signature) {
		for (int i = 0; i < component_views.size(); i++) {
			List<PropertyInfo> properties;
			component_proxies[i]->get_property_list(&properties);
			for (const PropertyInfo &property : properties) {
				component_views[i]->update_property(property.name);
			}
		}
		return;
	}
	component_signature = signature;
	for (EditorInspector *view : component_views) {
		view->edit(nullptr);
	}
	component_views.clear();
	component_proxies.clear();
	for (int i = component_sections->get_child_count() - 1; i >= 0; i--) {
		Node *child = component_sections->get_child(i);
		component_sections->remove_child(child);
		child->queue_free();
	}
	Array card_keys;
	// Resource text keys can be StringName while runtime keys are String.
	// Normalize before merging transform fields and discard storage metadata.
	for(const Variant &raw:entity.keys()) {
		String key=raw;
		if(key=="components" || key=="name" || key=="parent" || key=="active" || key=="material" || ecs_transform_field(key)) { continue; }
		if(!card_keys.has(key)) { card_keys.push_back(key); }
	}
	// Serialized dictionaries may reorder keys. Inspector order must not depend
	// on insertion order or on which property was last edited.
	card_keys.sort();
	const char *preferred[] = { "ui_layout", "mesh", "ui", "ui_image", "ui_text", "ui_button", "ui_toggle", "ui_input_field", "ui_slider", "ui_progress", "ui_scroll" };
	for (int i = int(sizeof(preferred) / sizeof(preferred[0])) - 1; i >= 0; i--) {
		const String key = preferred[i];
		if (card_keys.has(key)) {
			card_keys.erase(key);
			card_keys.push_front(key);
		}
	}
	if (ecs_has_transform(entity)) { card_keys.push_front("position"); }
	for (const Variant &key : card_keys) {
		String name = key;
		if (name == "material") {
			continue;
		}
		if (name == "name" || name == "parent" || name == "active") {
			continue;
		}
		component_sections->add_child(memnew(HSeparator));
		auto *section = memnew(FoldableContainer(name == "position" ? TTR("Transform") : name == "mesh" ? "Mesh Renderer"
						: name == "ui"																	? "UI Renderer"
						: ECSCustomComponents::is_component(name)										? ECSCustomComponents::title(name)
																										: ECSUIComponents::title(name)));
		String icon_name = ecs_component_icon_name(name);
		if (has_theme_icon(icon_name, "EditorIcons")) {
			section->set_title_icon(get_editor_theme_icon(icon_name));
		}
		component_sections->add_child(section);
		auto *section_body = memnew(VBoxContainer);
		section->add_child(section_body);
		if (name == "particles") {
			auto *bar=memnew(HBoxContainer); section_body->add_child(bar);
			const char *actions[]={"play","pause","restart","stop"};
			const char32_t *labels[]={U"播放",U"暂停",U"重播",U"停止发射"};
			for(int i=0;i<4;i++) { auto *button=memnew(Button); button->set_text(String(labels[i])); button->set_flat(true); button->connect("pressed",callable_mp(this,&ECSSceneEditorPanel::preview_particle_action).bind(String(actions[i]))); bar->add_child(button); }
			auto *seek=memnew(SpinBox); seek->set_min(0); seek->set_max(120); seek->set_step(.05); seek->set_suffix("s"); seek->set_tooltip_text(String(U"指定时间预览：固定随机种子，可重复比较")); seek->connect("value_changed",callable_mp(this,&ECSSceneEditorPanel::preview_particle_seek)); section_body->add_child(seek);
		}
		if (name == "ui_layout") {
			auto *preset = memnew(Button);
			preset->set_name("ECSAnchorPreset");
			Vector4 anchors = composed_ui.get("anchors", Vector4());
			auto axis_preset = [](real_t start, real_t end) {
				if (Math::is_equal_approx(start, (real_t)0) && Math::is_equal_approx(end, (real_t)1)) {
					return 3;
				}
				for (int i = 0; i < 3; i++) {
					if (Math::is_equal_approx(start, (real_t)(i * .5)) && Math::is_equal_approx(end, start)) {
						return i;
					}
				}
				return -1;
			};
			int x = axis_preset(anchors.x, anchors.z), y = axis_preset(anchors.y, anchors.w);
			const String horizontal[] = { String(U"左"), String(U"居中"), String(U"右"), String(U"水平拉伸") };
			const String vertical_labels[] = { String(U"顶部"), String(U"居中"), String(U"底部"), String(U"垂直拉伸") };
			preset->set_text(x < 0 || y < 0 ? String(U"自定义锚点…") : horizontal[x] + " / " + vertical_labels[y] + String(U"…"));
			if (x >= 0 && y >= 0) {
				preset->set_button_icon(ecs_anchor_icon(x, y));
			}
			preset->set_meta("anchors", anchors);
			preset->set_tooltip_text(String(U"选择定位、水平拉伸、垂直拉伸或双向拉伸"));
			preset->connect("pressed", callable_mp(this, &ECSSceneEditorPanel::open_anchor_presets).bind(preset));
			section_body->add_child(preset);
		}
		Ref<StyleBoxFlat> section_title;
		section_title.instantiate();
		section_title->set_bg_color(get_theme_color("base_color", "Editor"));
		section_title->set_border_width_all(1);
		section_title->set_border_color(get_theme_color("dark_color_3", "Editor"));
		section_title->set_content_margin_all(5);
		section->add_theme_style_override("title_panel", section_title);
		section->add_theme_style_override("title_collapsed_panel", section_title);
		section->set_folded(component_folded.has(name) && component_folded[name]);
		section->connect("folding_changed", callable_mp(this, &ECSSceneEditorPanel::component_fold_changed).bind(name));
		auto *menu = memnew(MenuButton);
		menu->set_text(String(U"⋮"));
		menu->set_tooltip_text(String(U"组件菜单"));
		section->add_title_bar_control(menu);
		menu->get_popup()->add_item(name == "ui_layout" ? String(U"移除布局及所有 UI 组件") : String(U"移除组件"), 0);
		if (name == "position" || ECSCustomComponents::is_component(name)) {
			menu->get_popup()->add_separator();
			menu->get_popup()->add_item(TTR("Reset"), 1);
			menu->get_popup()->add_item(TTR("Copy"), 2);
			menu->get_popup()->add_item(TTR("Paste"), 3);
		}
		ecs_light_popup(menu->get_popup());
		menu->get_popup()->connect("id_pressed", callable_mp(this, &ECSSceneEditorPanel::component_section_action).bind(name));
		Ref<ECSSceneEntityEditor> component_proxy;
		component_proxy.instantiate();
		component_proxy->target(inspector_scene(), selected, name == "position" ? "@transform" : name == "mesh" ? "@renderer"
																									: name);
		setup_runtime_proxy(component_proxy);
		auto *view = memnew(EditorInspector);
		view->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
		view->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
		view->set_use_folding(false);
		section_body->add_child(view);
		view->edit(component_proxy.ptr());
		section->set_meta("_component_proxy", component_proxy);
		component_views.push_back(view);
		component_proxies.push_back(component_proxy);
	}
	callable_mp(this, &ECSSceneEditorPanel::style_component_inputs).call_deferred();
}
void ECSSceneEditorPanel::hierarchy_context(const Vector2 &p_position, int p_button, bool p_empty) {
	if (p_button != int(MouseButton::RIGHT)) {
		return;
	}
	context_entity = p_empty || !tree->get_selected() ? -1 : int(tree->get_selected()->get_metadata(0));
	for (int id : { 2, 3, 4 }) {
		hierarchy_menu->set_item_disabled(hierarchy_menu->get_item_index(id), context_entity < 0);
	}
	hierarchy_menu->set_position(tree->get_screen_position() + p_position);
	hierarchy_menu->popup();
}
void ECSSceneEditorPanel::create_preset_action(int p_action) {
	context_entity = selected;
	hierarchy_action(p_action);
}
void ECSSceneEditorPanel::hierarchy_action(int p_action) {
	if (p_action >= 20 && p_action < 33 && scene.is_valid()) {
		const char *kinds[] = { "panel", "label", "image", "button", "toggle", "text_field", "text_area", "slider", "progress", "scroll", "hbox", "vbox", "grid" };
		Array entities = scene->get_entities();
		Dictionary entity = ECSUIComponents::preset(kinds[p_action - 20]);
		entity["parent"] = context_entity >= 0 && context_entity < entities.size() ? context_entity : -1;
		selected = entities.size();
		entities.push_back(entity);
		commit(entities, String(U"创建 UI 组件预设"));
		return;
	}
	if (p_action == 0 || p_action == 1) {
		add_entity(p_action == 1);
		return;
	}
	if (scene.is_null() || context_entity < 0 || context_entity >= scene->get_entities().size()) {
		return;
	}
	selected = context_entity;
	if (p_action == 3) {
		duplicate_entity();
	} else if (p_action == 4) {
		delete_entity();
	} else if (p_action == 2) {
		Array entities = scene->get_entities();
		Dictionary entity;
		entity["position"] = Vector3();
		entity["parent"] = context_entity;
		selected = entities.size();
		entities.push_back(entity);
		commit(entities, String(U"创建 ECS 子实体"));
	}
}
void ECSSceneEditorPanel::open_component_picker() {
	if (scene.is_null() || selected < 0) {
		return;
	}
	for (int i = component->get_item_count() - 1; i >= 0; i--) {
		if (ECSCustomComponents::is_component(component->get_item_text(i))) {
			component->remove_item(i);
		}
	}
	Dictionary schemas = ECSCustomComponents::registry();
	for (const Variant &key : schemas.keys()) {
		if (ECSCustomComponents::is_component(String(key))) {
			component->add_item(key);
		}
	}
	component_search->set_text("");
	component_filter("");
	const Rect2 button_rect = component_add_button->get_screen_rect();
	const Size2i popup_size = component_picker->get_contents_minimum_size().max(Size2i(320, 360) * EDSCALE);
	auto *display = DisplayServer::get_singleton();
	const Rect2i bounds = display->screen_get_usable_rect(component_add_button->get_window()->get_current_screen());
	Vector2i position(button_rect.position.x, button_rect.get_end().y);
	if (position.y + popup_size.y > bounds.get_end().y) {
		position.y = int(button_rect.position.y) - popup_size.y;
	}
	position.x = CLAMP(position.x, bounds.position.x, MAX(bounds.position.x, bounds.get_end().x - popup_size.x));
	position.y = CLAMP(position.y, bounds.position.y, MAX(bounds.position.y, bounds.get_end().y - popup_size.y));
	component_picker->popup(Rect2i(position, popup_size));
	component_search->grab_focus();
}
void ECSSceneEditorPanel::component_picker_test(int stage) {
	if (stage == 0) {
		canvas_selected(0);
		get_tree()->create_timer(.5)->connect("timeout", callable_mp(this, &ECSSceneEditorPanel::component_picker_test).bind(1));
		return;
	}
	if (stage == 1) {
		open_component_picker();
		get_tree()->create_timer(.5)->connect("timeout", callable_mp(this, &ECSSceneEditorPanel::component_picker_test).bind(2));
		return;
	}
	Rect2 button_rect = component_add_button->get_screen_rect();
	Rect2i popup_rect(component_picker->get_position(), component_picker->get_size());
	Rect2i bounds = DisplayServer::get_singleton()->screen_get_usable_rect(component_add_button->get_window()->get_current_screen());
	int expected_x = CLAMP(int(button_rect.position.x), bounds.position.x, MAX(bounds.position.x, bounds.get_end().x - popup_rect.size.x));
	bool ok = component_results->get_item_count() > 0 && Math::abs(popup_rect.position.x - expected_x) < 4 &&
			(Math::abs(popup_rect.position.y - int(button_rect.get_end().y)) < 4 || Math::abs(popup_rect.get_end().y - int(button_rect.position.y)) < 4);
	Ref<Image> preview = DisplayServer::get_singleton()->screen_get_image_rect(popup_rect.merge(Rect2i(button_rect)).grow(4));
	if (preview.is_valid()) {
		preview->save_png("D:/GODOTS/TempSDK/ecs-component-picker.png");
	}
	if (ok) {
		print_line("ECS_COMPONENT_PICKER_PASS default_list icons button_position");
	} else {
		ERR_PRINT("ECS_COMPONENT_PICKER_FAILED default list or button placement");
	}
	component_picker->hide();
	get_tree()->quit(ok ? 0 : 1);
}
void ECSSceneEditorPanel::component_search_input(const Ref<InputEvent> &event) {
	Ref<InputEventKey> key = event;
	if (key.is_null() || !key->is_pressed()) {
		return;
	}
	int count = component_results->get_item_count();
	Vector<int> selected_items = component_results->get_selected_items();
	int current = selected_items.is_empty() ? -1 : selected_items[0];
	if (key->get_keycode() == Key::ENTER || key->get_keycode() == Key::KP_ENTER) {
		if (current >= 0) {
			component_pick(current);
		}
	} else if (key->get_keycode() == Key::DOWN || key->get_keycode() == Key::UP) {
		if (count > 0) {
			component_results->select(CLAMP(current + (key->get_keycode() == Key::DOWN ? 1 : -1), 0, count - 1));
			component_results->ensure_current_is_visible();
		}
	} else {
		return;
	}
	component_search->accept_event();
}
void ECSSceneEditorPanel::component_filter(const String &p_filter) {
	const String filter = p_filter.strip_edges();
	component_results->clear();
	if (scene.is_null() || selected < 0 || selected >= scene->get_entities().size()) {
		return;
	}
	Dictionary entity = ECSUIComponents::migrate(scene->get_entities()[selected]);
	for (int i = 0; i < component->get_item_count(); i++) {
		String name = component->get_item_text(i);
		if (name == "rotation" || name == "scale" || (name == "position" && ecs_has_transform(entity))) {
			continue;
		}
		String display_name = name == "position" ? TTR("Transform") : ECSCustomComponents::is_component(name) ? ECSCustomComponents::title(name)
																											  : ECSUIComponents::title(name);
		if ((!filter.is_empty() && name.findn(filter) < 0 && display_name.findn(filter) < 0) || entity.has(name)) {
			continue;
		}
		if (ECSUIComponents::is_component(name)) {
			Dictionary probe = entity.duplicate(true);
			if (!ECSUIComponents::add(probe, name)) {
				continue;
			}
		}
		int index = component_results->add_item(name == "position" ? display_name : ECSCustomComponents::is_component(name) ? display_name
						: ECSUIComponents::is_component(name)																? ECSUIComponents::title(name)
																															: name);
		String icon_name = ecs_component_icon_name(name);
		if (has_theme_icon(icon_name, "EditorIcons")) {
			component_results->set_item_icon(index, get_editor_theme_icon(icon_name));
		}
		component_results->set_item_metadata(index, i);
	}
	component_picker_status->set_text(component_results->get_item_count() == 0 ? TTR("No matching components") : filter.is_empty() ? TTR("Components")
																																   : TTR("Search Results"));
	if (component_results->get_item_count() > 0) {
		component_results->select(0);
	}
}
void ECSSceneEditorPanel::component_pick(int p_index) {
	if (p_index < 0 || p_index >= component_results->get_item_count()) {
		return;
	}
	component->select(int(component_results->get_item_metadata(p_index)));
	add_component();
	component_picker->hide();
}
void ECSSceneEditorPanel::component_click(int p_index, const Vector2 &p_position, int p_button) {
	if (p_button == int(MouseButton::LEFT)) {
		component_pick(p_index);
	}
}
void ECSSceneEditorPanel::update_component_actions() {
	component_actions->clear();
	component_actions->add_item(String(U"保存场景     Ctrl+S"), 2);
	if (scene.is_null() || selected < 0 || selected >= scene->get_entities().size()) {
		return;
	}
	Dictionary entity = ECSUIComponents::migrate(scene->get_entities()[selected]);
	if (entity.has("navigation") || entity.has("mesh")) {
		component_actions->add_item(String(U"烘焙导航"), 0);
		component_actions->add_item(String(U"取消烘焙"), 1);
	}
	component_actions->add_separator(String(U"移除组件"));
	for (int i = 0; i < component->get_item_count(); i++) {
		String key = component->get_item_text(i);
		if (key == "rotation" || key == "scale") {
			continue;
		}
		if (key == "position") {
			if (ecs_has_transform(entity)) {
				component_actions->add_item(TTR("Remove") + " " + TTR("Transform"), 100 + i);
			}
			continue;
		}
		if (entity.has(component->get_item_text(i))) {
			component_actions->add_item(String(U"移除 ") + component->get_item_text(i), 100 + i);
		}
	}
}
void ECSSceneEditorPanel::inspector_action(int p_action) {
	if (p_action == 0) {
		begin_navigation_bake();
	} else if (p_action == 1) {
		cancel_navigation_bake();
	} else if (p_action == 2) {
		save();
	} else if (p_action >= 100 && p_action < 100 + component->get_item_count()) {
		component->select(p_action - 100);
		remove_component();
	}
}
void ECSSceneEditorPanel::shortcut_input(const Ref<InputEvent> &p_event) {
	if (!is_visible_in_tree()) {
		return;
	}
	Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed() || key->is_echo()) {
		return;
	}
	if (key->is_command_or_control_pressed() && key->get_keycode() == Key::P) {
		auto *editor = EditorInterface::get_singleton();
		if (editor->is_playing_scene()) {
			editor->stop_playing_scene();
		} else {
			editor->play_main_scene();
		}
		get_viewport()->set_input_as_handled();
		return;
	}
	Control *focus = get_viewport()->gui_get_focus_owner();
	if (focus != tree && focus != canvas && focus != spatial) {
		return;
	}
	if (key->is_command_or_control_pressed() && key->get_keycode() == Key::D) {
		duplicate_entity();
	} else if (!key->is_command_or_control_pressed() && key->get_keycode() == Key::KEY_DELETE) {
		delete_entity();
	} else if (key->is_command_or_control_pressed() && key->get_keycode() == Key::S) {
		save();
	} else if (focus == tree && key->get_keycode() == Key::F) {
		spatial->gui_input(p_event);
	} else {
		return;
	}
	get_viewport()->set_input_as_handled();
}
void ECSSceneEditorPanel::save_workspace_layout() {
	Array groups, offsets, active;
	for (TabContainer *tabs : workspace_tabs) {
		PackedStringArray names;
		for (int i = 0; i < tabs->get_tab_count(); i++) {
			names.push_back(tabs->get_tab_control(i)->get_name());
		}
		groups.push_back(names);
		active.push_back(tabs->get_current_tab());
	}
	for (SplitContainer *split : workspace_splits) {
		offsets.push_back(split->get_split_offset());
	}
	Dictionary layout;
	layout["debugger_console_version"] = 1;
	layout["groups"] = groups;
	layout["offsets"] = offsets;
	layout["active"] = active;
	if (docking) {
		layout["docking"] = docking->get_operations();
		layout["floating"] = docking->get_windows();
		layout["closed_panels"] = docking->get_closed_panels();
	}
	EditorSettings::get_singleton()->set_project_metadata("ecs_workspace", "layout", layout);
}
void ECSSceneEditorPanel::restore_workspace_layout(bool p_reset) {
	Dictionary layout = p_reset ? Dictionary() : Dictionary(EditorSettings::get_singleton()->get_project_metadata("ecs_workspace", "layout", Dictionary()));
	const bool use_default = layout.is_empty();
	if (int(layout.get("debugger_console_version", 0)) < 1) {
		Array groups = layout.get("groups", Array());
		for (int i = 0; i < groups.size(); i++) {
			PackedStringArray names = groups[i];
			PackedStringArray updated;
			for (const String &name : names) {
				if (name == "Debugger") { continue; }
				updated.push_back(name == "Console" ? "Debugger" : name);
			}
			groups[i] = updated;
		}
		layout["groups"] = groups;
		layout.erase("docking");
		layout.erase("floating");
	}
	if (docking) {
		docking->reset_extensions();
		if (!p_reset) {
			docking->restore(layout.get("docking", Array()), layout.get("floating", Array()));
		}
	}
	Array groups = layout.get("groups", Array());
	if (groups.size() != workspace_tabs.size()) {
		groups.clear();
		groups.push_back(PackedStringArray({ "Scene", "Assemblies" }));
		groups.push_back(PackedStringArray({ "Game", "Audio", "Animation", "Shader Editor", "TileSet" }));
		groups.push_back(PackedStringArray({ "Hierarchy" }));
		groups.push_back(PackedStringArray({ "Project" }));
		groups.push_back(PackedStringArray({ "Inspector" }));
		groups.push_back(PackedStringArray({ "Debugger" }));
	}
	HashMap<String, Control *> controls;
	for (TabContainer *tabs : workspace_tabs) {
		for (int i = 0; i < tabs->get_tab_count(); i++) {
			Control *c = tabs->get_tab_control(i);
			controls[String(c->get_name())] = c;
		}
	}
	for (int i = 0; i < groups.size(); i++) {
		PackedStringArray names = groups[i];
		for (int j = 0; j < names.size(); j++) {
			Control **entry = controls.getptr(names[j]);
			if (!entry) {
				continue;
			}
			Control *c = *entry;
			if (c->get_parent() != workspace_tabs[i]) {
				c->reparent(workspace_tabs[i]);
			}
			workspace_tabs[i]->move_child(c, MIN(j, workspace_tabs[i]->get_child_count() - 1));
			controls.erase(names[j]);
		}
	}
	Array offsets = layout.get("offsets", Array()), active = layout.get("active", Array());
	for (int i = 0; i < workspace_splits.size(); i++) {
		workspace_splits[i]->set_split_offset(i < offsets.size() ? int(offsets[i]) : 0);
	}
	for (int i = 0; i < workspace_tabs.size(); i++) {
		if (workspace_tabs[i]->get_tab_count()) {
			workspace_tabs[i]->set_current_tab(CLAMP(i < active.size() ? int(active[i]) : 0, 0, workspace_tabs[i]->get_tab_count() - 1));
		}
	}
	if (docking) {
		PackedStringArray closed = layout.get("closed_panels", PackedStringArray());
		if (use_default) {
			PackedStringArray visible({ "Scene", "Game", "Hierarchy", "Project", "Inspector", "Debugger" });
			for (TabContainer *tabs : workspace_tabs) {
				for (int i = 0; i < tabs->get_tab_count(); i++) {
					String name = tabs->get_tab_control(i)->get_name();
					if (!visible.has(name)) closed.push_back(name);
				}
			}
		}
		docking->restore_closed_panels(closed);
	}
}
void ECSSceneEditorPanel::set_scene_2d(bool enabled) {
	spatial->set_visible(!enabled);
	canvas->set_visible(enabled);
	scene_2d->set_pressed_no_signal(enabled);
	// Flat buttons skip the pressed StyleBox as well as the normal background.
	scene_2d->set_flat(!enabled);
	scene_grid->set_pressed_no_signal(enabled ? canvas->is_grid_visible() : spatial->is_grid_visible());
}
void ECSSceneEditorPanel::preview_particle_action(const String &action) {
 set_scene_2d(false); spatial->effect_preview(selected,action,0);
}
void ECSSceneEditorPanel::preview_particle_seek(double time) {
 set_scene_2d(false); spatial->effect_preview(selected,"seek",time);
}
void ECSSceneEditorPanel::set_scene_grid(bool enabled) {
	if (scene_2d->is_pressed()) { canvas->set_grid_visible(enabled); }
	else { spatial->set_grid_visible(enabled); }
}
void ECSSceneEditorPanel::show_rect_tool() {
	if(docking) { docking->show_panel(scene_host); }
	set_scene_2d(true);
	auto *tabs = Object::cast_to<TabContainer>(scene_host->get_parent());
	if (tabs) { tabs->set_current_tab(tabs->get_tab_idx_from_control(scene_host)); }
	canvas->grab_focus();
}
void ECSSceneEditorPanel::show_game_tab() {
	if(docking) { docking->show_panel(game_host); }
	auto *tabs = Object::cast_to<TabContainer>(game_host->get_parent());
	if (tabs) {
		tabs->set_current_tab(tabs->get_tab_idx_from_control(game_host));
	}
}

void ECSSceneEditorPanel::update_workspace_menus() {
	if (!workspace_mounted) {
		return;
	}
	auto *project_menu = Object::cast_to<MenuButton>(project_host->find_child("ECSProjectViewMenu", true, false));
	auto *game_menu = Object::cast_to<MenuButton>(game_host->find_child("ECSGameViewMenu", true, false));
	for (TabContainer *tabs : workspace_tabs) {
		for(int i=0;i<tabs->get_tab_count();i++) {
			Control *panel=tabs->get_tab_control(i);
			if(docking) { panel->set_meta("ecs_panel_show",callable_mp(docking,&ECSWorkspaceDocking::show_panel).bind(panel)); panel->set_meta("ecs_panel_close",callable_mp(docking,&ECSWorkspaceDocking::close_panel).bind(panel)); }
			const String name=panel->get_name();
			StringName icon=name=="Hierarchy"?"FileTree":name=="Scene"?"PackedScene":name=="Game"?"Game":name=="Inspector"?"Object":name=="Project"?"Filesystem":name=="Debugger"?"Debug":name=="Animator"?"AnimationTree":name=="TileSet"?"TileSet":name=="Assemblies"?"PackedDataContainer":name=="Audio"?"AudioStreamPlayer":name=="Animation"?"AnimationPlayer":name=="Shader Editor"?"Shader":"File";
			if(has_theme_icon(icon,"EditorIcons")) { tabs->set_tab_icon(i,get_editor_theme_icon(icon)); tabs->set_tab_icon_max_width(i,int(16*EDSCALE)); }
		}
		Control *active = tabs->get_current_tab_control();
		MenuButton *menu = active == project_host ? project_menu : active == game_host ? game_menu
																					   : nullptr;
		tabs->set_popup(menu ? menu->get_popup() : nullptr);
		tabs->set_visible(ECSWorkspaceDocking::has_open_tabs(tabs));
	}
	// Follow the actual tree: docking can insert a newer split above an older one.
	// Internal drag handles are not workspace content and must not keep a branch alive.
	auto collapse_branch = [&](auto &&self, Control *control) -> bool {
		if (auto *tabs = Object::cast_to<TabContainer>(control)) {
			const bool populated = ECSWorkspaceDocking::has_open_tabs(tabs);
			tabs->set_visible(populated);
			return populated;
		}
		auto *split = Object::cast_to<SplitContainer>(control);
		if (!split) { return false; }
		bool populated = false;
		for (int i = 0; i < split->get_child_count(false); i++) {
			if (auto *child = Object::cast_to<Control>(split->get_child(i, false))) {
				populated = self(self, child) || populated;
			}
		}
		split->set_visible(populated);
		return populated;
	};
	for (SplitContainer *split : workspace_splits) {
		if (!Object::cast_to<SplitContainer>(split->get_parent())) { collapse_branch(collapse_branch, split); }
	}
	if (docking) {
		docking->refresh_windows();
	}
}
void ECSSceneEditorPanel::workspace_test_step(int p_step) {
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-model-import-test")) {
		bool ok = ECSModelImporter::test();
		Ref<ECSScene> test_scene;
		test_scene.instantiate(); edit_scene(test_scene);
		Dictionary drag; drag["type"] = "files"; drag["files"] = PackedStringArray({ "res://Assets/BlenderProbe.glb" });
		ok &= model_can_drop(Vector2(), drag);
		model_drop(Vector2(), drag);
		const int imported_count = test_scene->get_entities().size();
		ok &= imported_count > 3;
		auto *undo = EditorUndoRedoManager::get_singleton();
		ok &= undo->undo() && test_scene->get_entities().is_empty();
		ok &= undo->redo() && test_scene->get_entities().size() == imported_count;
		ok &= test_scene->instantiate().is_valid();
		restore_workspace_layout(true);
		ok &= workspace_tabs.size() == 6;
		for (TabContainer *tabs : workspace_tabs) {
			int visible = 0;
			for (int i = 0; i < tabs->get_tab_count(); i++) visible += !tabs->is_tab_hidden(i);
			ok &= visible == 1;
		}
		ok &= docking->run_visibility_test();
		print_line(ok ? "ECS_MODEL_IMPORT_PASS" : "ECS_MODEL_IMPORT_FAILED");
		get_tree()->quit(ok ? 0 : 1); return;
	}
	if(OS::get_singleton()->get_cmdline_user_args().find("--ecs-component-tools-test")) {
		bool ok=docking && docking->run_visibility_test(); print_line(ok?"ECS_PANEL_VISIBILITY_PASS":"ECS_PANEL_VISIBILITY_FAILED"); int count=0;
		for(EditorDock *dock:workspace_tool_docks) {
			if(!dock->has_meta("ecs_workspace_content")) { continue; }
			auto *tool=Object::cast_to<ECSComponentTool>(dock->get_meta("ecs_workspace_content")); if(!tool) { continue; }
			bool passed=tool->run_self_test(); ok &= passed; count++;
			print_line(vformat("ECS_COMPONENT_TOOL_%s %s",passed?"PASS":"FAILED",dock->get_name()));
		}
		ok &= count>=7; print_line(ok?"ECS_COMPONENT_TOOLS_PASS":"ECS_COMPONENT_TOOLS_FAILED"); get_tree()->quit(ok?0:1); return;
	}
#ifndef ANDROID_ENABLED
	auto *editor = EditorInterface::get_singleton();
	auto *game = Object::cast_to<GameViewPluginBase>(EditorNode::get_editor_main_screen()->get_plugin_by_name("Game"));
	bool ok = workspace_mounted && game;
	if (p_step == 0) {
		restore_workspace_layout(true);
		bool docking_ok = docking && docking->run_self_test();
		print_line(vformat("ECS_DOCK_BASE_CHECK mounted=%s game=%s docking=%s", workspace_mounted, game != nullptr, docking_ok));
		ok &= docking_ok;
		ok &= workspace_tool_docks.size() >= 5;
		for (EditorDock *dock : workspace_tool_docks) {
			auto *host = Object::cast_to<Control>(dock->get_parent());
			docking->dock(host, nullptr, -1, Vector2(240, 180));
			EditorDockManager::get_singleton()->focus_dock(dock);
			Control *content = dock;
			if (dock->has_meta("ecs_workspace_content")) { content = Object::cast_to<Control>(dock->get_meta("ecs_workspace_content")); }
			bool tool_ok = content && content->is_visible_in_tree() && host->get_window() != get_window();
			print_line(vformat("ECS_DOCK_TOOL_CHECK %s visible=%s floating=%s", dock->get_name(), content && content->is_visible_in_tree(), host->get_window() != get_window()));
			ok &= tool_ok;
			docking->dock(host, workspace_tabs[1], 0);
		}
		if (ok) {
			print_line(vformat("ECS_TOOLS_DOCK_PASS count=%d float redock focus", workspace_tool_docks.size()));
		}
		if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-tools-dock-test")) {
			for (EditorDock *dock : workspace_tool_docks) {
				if (dock->has_meta("ecs_workspace_content")) {
					auto *tool = Object::cast_to<ECSComponentTool>(dock->get_meta("ecs_workspace_content"));
					if (!tool) { continue; }
					const bool authoring_ok = tool->run_self_test();
					print_line(vformat("ECS_COMPONENT_TOOL_%s %s", authoring_ok ? "PASS" : "FAILED", dock->get_name()));
					ok &= authoring_ok;
				}
			}
			auto *manager = EditorDockManager::get_singleton();
			int count = workspace_tool_docks.size();
			auto *late = memnew(EditorDock);
			late->set_name("LatePluginDockTest");
			late->set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
			manager->add_dock(late);
			ok &= workspace_tool_docks.has(late) && workspace_tool_docks.size() == count + 1;
			manager->focus_dock(late);
			ok &= late->is_visible_in_tree();
			manager->remove_dock(late);
			ok &= !workspace_tool_docks.has(late) && workspace_tool_docks.size() == count && !late->get_parent();
			memdelete(late);
			save_workspace_layout();
			restore_workspace_layout(false);
			for (EditorDock *tool : workspace_tool_docks) {
				ok &= tool->get_parent() && Object::cast_to<TabContainer>(tool->get_parent()->get_parent());
				print_line("ECS_ROUTED_TOOL " + tool->get_name());
			}
			print_line(ok ? "ECS_ALL_DOCKS_PASS late_plugin_add_remove layout_restore" : "ECS_ALL_DOCKS_FAILED");
			get_tree()->quit(ok ? 0 : 1);
			return;
		}
		workspace_tabs[0]->set_current_tab(0);
		auto *files = FileSystemDock::get_singleton();
		auto *view_menu = Object::cast_to<MenuButton>(files->find_child("ECSProjectViewMenu", true, false));
		auto *zoom = Object::cast_to<HSlider>(files->find_child("ECSProjectZoom", true, false));
		auto *resolution = Object::cast_to<OptionButton>(game_host->find_child("ECSGameResolution", true, false));
		ok &= view_menu && zoom && resolution;
		if (view_menu && zoom && resolution) {
			view_menu->get_popup()->emit_signal("id_pressed", 1);
			ok &= files->get_display_mode() == FileSystemDock::DISPLAY_MODE_TREE_ONLY;
			view_menu->get_popup()->emit_signal("id_pressed", 2);
			ok &= files->get_display_mode() == FileSystemDock::DISPLAY_MODE_HSPLIT;
			zoom->set_value(0);
			ok &= files->get_file_list_display_mode() == FileSystemDock::FILE_LIST_DISPLAY_LIST;
			zoom->set_value(128);
			ok &= files->get_file_list_display_mode() == FileSystemDock::FILE_LIST_DISPLAY_THUMBNAILS && files->get_list_control()->get_fixed_icon_size().x > 100;
			zoom->set_value(64);
			resolution->select(4); // 720 x 1280 portrait.
			resolution->emit_signal("item_selected", 4);
		}
		canvas_selected(0);
		console_host->reparent(workspace_tabs[0]);
		workspace_splits[0]->set_split_offset(75);
		save_workspace_layout();
		console_host->reparent(workspace_tabs[5]);
		restore_workspace_layout(false);
		ok &= console_host->get_parent() == workspace_tabs[0] && workspace_splits[0]->get_split_offset() == 75;
		restore_workspace_layout(true);
		docking->dock(game_host, nullptr, -1, Vector2(180, 180));
		editor->play_main_scene();
	} else if (p_step == 1 || p_step == 3) {
		TypedArray<Node> processes = game_host->find_children("*", "EmbeddedProcessBase", true, false);
		auto *process = processes.size() == 1 ? Object::cast_to<EmbeddedProcessBase>(processes[0]) : nullptr;
		auto *wrapper = Object::cast_to<WindowWrapper>(game->get_workspace_control());
		ok &= editor->is_playing_scene() && process && process->is_embedding_completed() && wrapper && !wrapper->get_window_enabled() && wrapper->is_visible_in_tree();
		if (!ok && editor->is_playing_scene() && workspace_test_retries++ < 25) {
			get_tree()->create_timer(1.0)->connect("timeout", callable_mp(this, &ECSSceneEditorPanel::workspace_test_step).bind(p_step));
			return;
		}
		if (!ok) {
			print_line(vformat("ECS_EMBED_STATE playing=%s count=%d complete=%s floating=%s visible=%s", editor->is_playing_scene(), processes.size(), process && process->is_embedding_completed(), wrapper && wrapper->get_window_enabled(), wrapper && wrapper->is_visible_in_tree()));
		}
		workspace_test_retries = 0;
		if (p_step == 1) {
			Size2i rect_size = process->get_screen_embedded_window_rect().size;
			ok &= Math::abs(double(rect_size.x) / MAX(1, rect_size.y) - 720.0 / 1280.0) < .01;
			ok &= game_host->get_window() != get_window();
			docking->dock(game_host, workspace_tabs[0], 0);
			show_game_tab();
		}
		editor->stop_playing_scene();
	} else if (p_step == 2) {
		ok &= !editor->is_playing_scene();
		ok &= game_host->get_parent() != workspace_tabs[1];
		auto *resolution = Object::cast_to<OptionButton>(game_host->find_child("ECSGameResolution", true, false));
		auto *scale = Object::cast_to<HSlider>(game_host->find_child("ECSGameScale", true, false));
		ok &= resolution && scale;
		if (resolution && scale) {
			resolution->select(3); // 1280 x 720 landscape.
			resolution->emit_signal("item_selected", 3);
			scale->set_value(.3);
		}
		editor->play_main_scene();
	} else if (p_step == 4) {
		ok &= !editor->is_playing_scene();
		restore_workspace_layout(true);
		ok &= game_host->get_parent() == workspace_tabs[1] && console_host->get_parent() == workspace_tabs[5];
	} else {
		ok &= !editor->is_playing_scene();
		for (TabContainer *tabs : workspace_tabs) {
			ok &= tabs->is_visible() && tabs->get_tab_count() > 0;
		}
		get_viewport()->get_texture()->get_image()->save_png("D:/GODOTS/TempSDK/ecs-unity-workspace.png");
		if (ok) {
			print_line("ECS_WORKSPACE_PASS layout_restore dock_reparent embedded_play_stop_twice");
			print_line("ECS_PREVIEW_CONTROLS_PASS project_columns thumbnails portrait landscape scale");
			print_line("ECS_DOCKING_PASS split float close_redock save_restore reset");
		}
		get_tree()->quit(ok ? 0 : 1);
		return;
	}
	if (!ok) {
		ERR_PRINT(vformat("ECS_WORKSPACE_FAILED step %d", p_step));
		editor->stop_playing_scene();
		get_tree()->quit(1);
		return;
	}
	get_tree()->create_timer(4.0)->connect("timeout", callable_mp(this, &ECSSceneEditorPanel::workspace_test_step).bind(p_step + 1));
#endif
}
void ECSSceneEditorPanel::route_workspace_dock(EditorDock *dock, bool added) {
	if (!workspace_mounted || !dock) { return; }
	auto *manager = EditorDockManager::get_singleton();
	if (!added) {
		if (!workspace_tool_docks.has(dock)) { return; }
		Node *host = dock->get_parent();
		if (dock->has_meta("ecs_workspace_content")) {
			if (auto *tool = Object::cast_to<ECSComponentTool>(dock->get_meta("ecs_workspace_content"))) { entity_tools.erase(tool); }
			dock->remove_meta("ecs_workspace_content");
		}
		host->remove_child(dock);
		workspace_tool_docks.erase(dock);
		manager->set_workspace_dock(dock, false);
		if (host != console_host) { host->get_parent()->remove_child(host); host->queue_free(); }
		update_workspace_menus();
		return;
	}
	if (dock == EditorNode::get_log() || workspace_tool_docks.has(dock)) { return; }
	const auto slot = dock->get_default_slot();
	if (slot != EditorDock::DOCK_SLOT_BOTTOM && slot != EditorDock::DOCK_SLOT_BOTTOM_L && slot != EditorDock::DOCK_SLOT_BOTTOM_R) { return; }
	String name = dock->get_name();
	if (name == "TileMap") { name = "TileMapLayer"; }
	auto *host = dock == EditorDebuggerNode::get_singleton() ? console_host : memnew(VBoxContainer);
	host->set_name(name);
	host->set_v_size_flags(SIZE_EXPAND_FILL);
	if (host != console_host) { workspace_tabs[1]->add_child(host); }
	manager->set_workspace_dock(dock, true);
	host->add_child(dock);
	dock->set_v_size_flags(SIZE_EXPAND_FILL);
	dock->show();
	if (name == "Polygon" || name == "AnimationTree" || name == "Animation" || name == "SpriteFrames" || name == "Theme" || name == "GridMap" || name == "Replication") {
		auto *tool = memnew(ECSComponentTool);
		tool->setup(name == "Polygon" ? "polygon_2d" : name == "SpriteFrames" ? "sprite_frames" : name == "Theme" ? "theme" : name == "GridMap" ? "gridmap_3d" : name == "Replication" ? "replication" : "animation");
		tool->connect("entity_selected", callable_mp(this, &ECSSceneEditorPanel::canvas_selected), CONNECT_DEFERRED);
		tool->set_v_size_flags(SIZE_EXPAND_FILL);
		host->add_child(tool);
		dock->set_meta("ecs_workspace_content", tool);
		dock->hide();
		entity_tools.push_back(tool);
		tool->edit(scene, selected, runtime_session.is_valid());
	} else if (name == "TileMapLayer") {
		auto *tool = memnew(ECSTilemapEditor);
		tool->set_v_size_flags(SIZE_EXPAND_FILL);
		host->add_child(tool);
		tool->connect("tilemap_edited", callable_mp(this, &ECSSceneEditorPanel::tilemap_edited));
		dock->set_meta("ecs_workspace_content", tool);
		dock->hide();
		tool->edit(scene, selected, canvas);
	}
	workspace_tool_docks.push_back(dock);
	update_workspace_menus();
}
void ECSSceneEditorPanel::mount_workspace_tools() {
	if (workspace_mounted) {
		return;
	}
	workspace_mounted = true;
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-assemblies-visual")) {
		get_tree()->create_timer(3.0)->connect("timeout", callable_mp(this, &ECSSceneEditorPanel::assembly_visual_test).bind(0));
	}
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-component-picker-test")) {
		get_tree()->create_timer(3.0)->connect("timeout", callable_mp(this, &ECSSceneEditorPanel::component_picker_test).bind(0));
	}
	if (!docking) {
		docking = memnew(ECSWorkspaceDocking);
		add_child(docking);
		docking->setup(this);
	}

	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-model-import-test") || OS::get_singleton()->get_cmdline_user_args().find("--ecs-component-tools-test") || OS::get_singleton()->get_cmdline_user_args().find("--ecs-workspace-self-test") || OS::get_singleton()->get_cmdline_user_args().find("--ecs-tools-dock-test")) {
		get_tree()->create_timer(2.0)->connect("timeout", callable_mp(this, &ECSSceneEditorPanel::workspace_test_step).bind(0));
	}
	EditorRunBar::get_singleton()->connect("play_pressed", callable_mp(this, &ECSSceneEditorPanel::show_game_tab));
	auto *manager = EditorDockManager::get_singleton();
	manager->set_workspace_dock_handler(callable_mp(this, &ECSSceneEditorPanel::route_workspace_dock));
	for (EditorDock *dock : manager->get_registered_docks()) { route_workspace_dock(dock, true); }
	restore_workspace_layout(false);
	auto *files = FileSystemDock::get_singleton();
	manager->remove_dock(files);
	manager->set_workspace_dock(files, true);
	project_host->add_child(files);
	files->set_v_size_flags(SIZE_EXPAND_FILL);
	files->show();
	files->set_workspace_mode(true);
	auto *log = EditorNode::get_log();
	manager->remove_dock(log);
	manager->set_workspace_dock(log, true);
	debugger_console = memnew(VBoxContainer);
	debugger_console->set_name(TTR("Console"));
	EditorDebuggerNode::get_singleton()->get_default_debugger()->prepend_debugger_tab(debugger_console);
	debugger_console->add_child(log);
	log->set_v_size_flags(SIZE_EXPAND_FILL);
	log->show();
#ifndef ANDROID_ENABLED
	auto *game = Object::cast_to<GameViewPluginBase>(EditorNode::get_editor_main_screen()->get_plugin_by_name("Game"));
	if (game) {
		Control *view = game->get_workspace_control();
		view->reparent(game_host);
		view->set_v_size_flags(SIZE_EXPAND_FILL);
		game->set_workspace_embedded(true);
		view->show();
	}
#endif
	EditorInterface::get_singleton()->set_distraction_free_mode(true);
	for (TabContainer *tabs : workspace_tabs) {
		Callable changed = callable_mp(this, &ECSSceneEditorPanel::update_workspace_menus);
		if (!tabs->is_connected("child_order_changed", changed)) {
			tabs->connect("child_order_changed", changed, CONNECT_DEFERRED);
			tabs->connect("tab_changed", changed.unbind(1), CONNECT_DEFERRED);
		}
	}
	update_workspace_menus();
}
void ECSSceneEditorPanel::unmount_workspace_tools() {
	if (!workspace_mounted) {
		return;
	}
	workspace_mounted = false;
	EditorDockManager::get_singleton()->set_workspace_dock_handler(Callable());
	for (TabContainer *tabs : workspace_tabs) {
		tabs->set_popup(nullptr);
	}
	save_workspace_layout();
	for (EditorDock *dock : workspace_tool_docks) {
		if (dock->has_meta("ecs_workspace_content")) { dock->remove_meta("ecs_workspace_content"); }
		Node *host = dock->get_parent();
		host->remove_child(dock);
		EditorDockManager::get_singleton()->set_workspace_dock(dock, false);
		EditorDockManager::get_singleton()->remove_dock(dock);
		EditorDockManager::get_singleton()->add_dock(dock);
		if (host != console_host) {
			host->get_parent()->remove_child(host);
			host->queue_free();
		}
	}
	workspace_tool_docks.clear();
	entity_tools.clear();
	EditorRunBar::get_singleton()->disconnect("play_pressed", callable_mp(this, &ECSSceneEditorPanel::show_game_tab));
	auto *files = FileSystemDock::get_singleton();
	if (files && files->get_parent() == project_host) {
		files->set_workspace_mode(false);
		project_host->remove_child(files);
		EditorDockManager::get_singleton()->set_workspace_dock(files, false);
		EditorDockManager::get_singleton()->add_dock(files);
	}
	auto *log = EditorNode::get_log();
	if (log && log->get_parent() == debugger_console) {
		debugger_console->remove_child(log);
		EditorDockManager::get_singleton()->set_workspace_dock(log, false);
		EditorDockManager::get_singleton()->add_dock(log);
	}
	if (debugger_console) {
		debugger_console->get_parent()->remove_child(debugger_console);
		debugger_console->queue_free();
		debugger_console = nullptr;
	}
#ifndef ANDROID_ENABLED
	auto *game = Object::cast_to<GameViewPluginBase>(EditorNode::get_editor_main_screen()->get_plugin_by_name("Game"));
	if (game && game->get_workspace_control()->get_parent() == game_host) {
		game->set_workspace_embedded(false);
		game->get_workspace_control()->reparent(EditorNode::get_editor_main_screen()->get_control());
		game->get_workspace_control()->hide();
	}
#endif
}
ECSSceneEditorPanel::~ECSSceneEditorPanel() {
	if (tool_tiles.is_valid()) { tool_tiles->disconnect_changed(callable_mp(this, &ECSSceneEditorPanel::tool_resource_changed)); }
	EditorNode::get_singleton()->remove_tool_menu_item(String(U"2D 骨骼动画"));
	clear_runtime_resources();
	ai_stop();
	for (EditorInspector *view : component_views) {
		view->edit(nullptr);
	}
	if (baking_scene.is_valid()) {
		baking_scene->cancel_navigation_bake(baking_request);
	}
	inspector->edit(nullptr);
	if (scene.is_valid()) {
		scene->disconnect("changed", callable_mp(this, &ECSSceneEditorPanel::changed));
	}
}
void ECSSceneEditorPanel::edit_scene(const Ref<ECSScene> &value) {
	if (scene == value) {
		return;
	}
	if(runtime_session.is_valid()) { stop_runtime_preview(); }
	runtime_copied.clear(); runtime_copied_index=-1;
	if (scene.is_valid()) {
		scene->disconnect("changed", callable_mp(this, &ECSSceneEditorPanel::changed));
	}
	inspector->edit(nullptr);
	scene = value;
	scene_revision++;
	selected = -1;
	if (scene.is_valid()) {
		scene->connect("changed", callable_mp(this, &ECSSceneEditorPanel::changed));
	}
	refresh();
}
void ECSSceneEditorPanel::changed() {
	scene_revision++;
	call_deferred("_refresh");
}
void ECSSceneEditorPanel::update_entity_tools() {
	for (ECSComponentTool *tool : entity_tools) { tool->edit(scene, selected, runtime_session.is_valid()); }
	Ref<TileSet> tiles;
	if (scene.is_valid() && selected >= 0 && selected < scene->get_entities().size()) {
		Dictionary definition = scene->get_entities()[selected];
		Dictionary map = definition.get("tilemap_2d", Dictionary());
		tiles = map.get("tile_set", Variant());
	}
	if (TileSetEditor::get_singleton()) { TileSetEditor::get_singleton()->edit(tiles); }
	if (tool_tiles != tiles) {
		if (tool_tiles.is_valid()) { tool_tiles->disconnect_changed(callable_mp(this, &ECSSceneEditorPanel::tool_resource_changed)); }
		tool_tiles = tiles;
		if (tool_tiles.is_valid()) { tool_tiles->connect_changed(callable_mp(this, &ECSSceneEditorPanel::tool_resource_changed)); }
	}
	for (EditorDock *dock : workspace_tool_docks) {
		if (dock->has_meta("ecs_workspace_content")) {
			if (auto *map = Object::cast_to<ECSTilemapEditor>(dock->get_meta("ecs_workspace_content"))) { map->edit(runtime_session.is_valid() ? Ref<ECSScene>() : scene, selected, canvas); }
		}
	}
}
void ECSSceneEditorPanel::tool_resource_changed() {
	if (scene.is_valid()) { scene->emit_changed(); }
}
bool ECSSceneEditorPanel::wants_runtime_preview() const {
	return (spatial && spatial->is_visible_in_tree()) || (canvas && canvas->is_visible_in_tree());
}
void ECSSceneEditorPanel::update_runtime_preview(const Array &data) {
	if(data.size()!=6 || scene.is_null()) { return; }
	if(String(data[0])!=scene->get_path()) { spatial->stop_runtime_preview(); return; }
	String error=data[3];
	if(!error.is_empty()) { spatial->stop_runtime_preview(); spatial->set_tooltip_text(error); return; }
	spatial->set_tooltip_text(String(U"运行场景：显示游戏进程状态；停止后恢复编辑。"));
	update_runtime_inspector(data[4],data[5]);
	spatial->update_runtime_preview(data[1],data[2]);
	canvas->update_runtime_preview(data[1]);
}
void ECSSceneEditorPanel::stop_runtime_preview() {
	runtime_session.unref();
	runtime_scene.unref();
	runtime_actions->set_visible(!runtime_copied.is_empty());
	component_add_button->set_disabled(false);
	entity_name->set_editable(true);
	clear_runtime_resources();
	spatial->stop_runtime_preview();
	spatial->set_tooltip_text("");
	component_signature="";
	refresh();
}
void ECSSceneEditorPanel::refresh() {
	if (refreshing) {
		return;
	}
	refreshing = true;
	tree->clear();
	if (scene.is_valid()) {
		Array entities = scene->get_entities();
		auto *root = tree->create_item();
		HashMap<int, TreeItem *> rows;
		for (int i = 0; i < entities.size(); i++) {
			rows[i] = tree->create_item(root);
		}
		for (int i = 0; i < entities.size(); i++) {
			auto *item = rows[i];
			Dictionary definition = entities[i];
			String description = definition.get("name", String(U"实体 ") + itos(i));
			description += "  [";
			bool first = true;
			for (const Variant &key : definition.keys()) {
				if (String(key) == "name" || String(key) == "parent" || String(key) == "active") {
					continue;
				}
				if (!first) {
					description += ", ";
				}
				description += String(key);
				first = false;
			}
			item->set_text(0, definition.get("name", String(U"实体 ") + itos(i)));
			item->set_tooltip_text(0, description + "]");
			item->set_icon(0, get_editor_theme_icon(SNAME("ECSEntity")));
			item->set_metadata(0, i);
			bool active = true;
			HashSet<int> ancestors;
			for (int cursor = i; cursor >= 0 && cursor < entities.size() && !ancestors.has(cursor); cursor = Dictionary(entities[cursor]).get("parent", -1)) {
				ancestors.insert(cursor);
				if (!bool(Dictionary(entities[cursor]).get("active", true))) { active = false; break; }
			}
			if (!active) {
				item->set_custom_color(0, get_theme_color("disabled_font_color", "Editor"));
				item->set_icon_modulate(0, Color(1, 1, 1, .35));
			}

			if (i == selected) {
				item->select(0);
			}
		}
		// Reparent only valid, acyclic links. The full hierarchy is scrollable.
		for (int i = 0; i < entities.size(); i++) {
			Dictionary definition = entities[i];
			int parent = definition.get("parent", -1);
			int cursor = parent;
			HashSet<int> visited;
			bool valid = true;
			while (cursor >= 0 && cursor < entities.size()) {
				if (cursor == i || visited.has(cursor)) {
					valid = false;
					break;
				}
				visited.insert(cursor);
				cursor = Dictionary(entities[cursor]).get("parent", -1);
			}
			if (valid && rows.has(parent)) {
				TreeItem *item = rows[i];
				root->remove_child(item);
				rows[parent]->add_child(item);
			}
		}
		if (selected >= entities.size()) {
			selected = -1;
		}
		status->set_text(scene->get_path() + String(U" · ") + itos(entities.size()) + String(U" 个实体 · Ctrl+Z / Ctrl+Shift+Z 撤销重做"));
	}
	proxy->target(inspector_scene(), selected, "@metadata");
	setup_runtime_proxy(proxy);
	update_entity_header();
	queue_component_sections();
	inspector->edit(selected >= 0 ? proxy.ptr() : nullptr);
	canvas->edit_scene(scene, selected);
	if(tilemap_editor) { tilemap_editor->edit(scene,selected,canvas); }
	update_entity_tools();
	spatial->edit_scene(scene, selected);
	refreshing = false;
}
void ECSSceneEditorPanel::update_entity_header() {
	const bool valid = scene.is_valid() && selected >= 0 && selected < scene->get_entities().size();
	entity_header->set_visible(valid);
	if (!valid) { return; }
	Dictionary definition = scene->get_entities()[selected];
	entity_active->set_pressed_no_signal(bool(definition.get("active", true)));
	entity_name->set_text(definition.get("name", String()));
}
void ECSSceneEditorPanel::entity_active_changed(bool p_active) {
	if (refreshing || scene.is_null() || selected < 0) { return; }
	proxy->set("active", p_active);
}
void ECSSceneEditorPanel::entity_name_submitted(const String &p_name) {
	if (refreshing || scene.is_null() || selected < 0 || selected >= scene->get_entities().size()) { return; }
	if (String(Dictionary(scene->get_entities()[selected]).get("name", String())) != p_name) {
		proxy->set("name", p_name);
	}
}
void ECSSceneEditorPanel::entity_name_focus_exited() {
	entity_name_submitted(entity_name->get_text());
}
void ECSSceneEditorPanel::canvas_selected(int index) {
	selected = index;
	refreshing = true;
	TreeItem *item = tree->get_root();
	while (item) {
		if (item->get_metadata(0).get_type() == Variant::INT && int(item->get_metadata(0)) == index) {
			item->select(0);
			tree->ensure_cursor_is_visible();
			break;
		}
		item = item->get_next_in_tree();
	}
	refreshing = false;
	proxy->target(inspector_scene(), selected, "@metadata");
	setup_runtime_proxy(proxy);
	update_entity_header();
	queue_component_sections();
	inspector->edit(selected >= 0 ? proxy.ptr() : nullptr);
	if (tilemap_editor) { tilemap_editor->edit(scene, selected, canvas); }
	update_entity_tools();
}
void ECSSceneEditorPanel::select() {
	if (refreshing || !tree->get_selected()) {
		return;
	}
	selected = int(tree->get_selected()->get_metadata(0));
	proxy->target(inspector_scene(), selected, "@metadata");
	setup_runtime_proxy(proxy);
	update_entity_header();
	queue_component_sections();
	inspector->edit(proxy.ptr());
	canvas->edit_scene(scene, selected);
	if(tilemap_editor) { tilemap_editor->edit(scene,selected,canvas); }
	update_entity_tools();
	spatial->edit_scene(scene, selected);
}
Variant ECSSceneEditorPanel::hierarchy_drag(const Vector2 &p_position) {
	TreeItem *item = tree->get_item_at_position(p_position);
	if (scene.is_null() || !item || item->get_metadata(0).get_type() != Variant::INT) {
		return Variant();
	}
	Dictionary drag_data;
	drag_data["type"] = "ecs_hierarchy_entity";
	drag_data["scene"] = scene;
	drag_data["entity"] = item->get_metadata(0);
	drag_data["entities"] = scene->get_entities();
	auto *preview = memnew(Label);
	preview->set_text(item->get_text(0));
	tree->set_drag_preview(preview);
	return drag_data;
}

bool ECSSceneEditorPanel::can_reparent_entity(int p_entity, int p_parent) const {
	if (scene.is_null()) {
		return false;
	}
	Array entities = scene->get_entities();
	if (p_entity < 0 || p_entity >= entities.size() || p_parent < -1 || p_parent >= entities.size() || int(Dictionary(entities[p_entity]).get("parent", -1)) == p_parent) {
		return false;
	}
	int cursor = p_parent;
	if (p_parent >= 0) {
		Dictionary entity = entities[p_entity];
		for (const StringName &kind : { StringName("physics"), StringName("physics_2d") }) {
			if (entity.has(kind) && String(Dictionary(entity[kind]).get("mode", "static")) != "static") {
				return false; // Moving physics bodies must remain roots in ECSWorld.
			}
		}
	}
	for (int steps = 0; cursor >= 0; steps++) {
		if (cursor == p_entity || cursor >= entities.size() || steps >= entities.size()) {
			return false;
		}
		cursor = Dictionary(entities[cursor]).get("parent", -1);
		if (cursor < -1) {
			return false;
		}
	}
	return true;
}

bool ECSSceneEditorPanel::hierarchy_can_drop(const Vector2 &p_position, const Variant &p_data) const {
	if (model_can_drop(p_position, p_data)) return true;
	if (scene.is_null() || p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	Dictionary drag_data = p_data;
	if (String(drag_data.get("type", "")) != "ecs_hierarchy_entity" || drag_data.get("scene", Variant()) != Variant(scene) || drag_data.get("entity", Variant()).get_type() != Variant::INT || drag_data.get("entities", Variant()).get_type() != Variant::ARRAY) {
		return false;
	}
	// Reject stale indices when the scene was edited while a drag was active.
	if (!scene->get_entities().recursive_equal(drag_data["entities"], 0)) {
		return false;
	}
	TreeItem *target = tree->get_item_at_position(p_position);
	int parent = target && target->get_metadata(0).get_type() == Variant::INT ? int(target->get_metadata(0)) : -1;
	return can_reparent_entity(drag_data["entity"], parent);
}

void ECSSceneEditorPanel::hierarchy_drop(const Vector2 &p_position, const Variant &p_data) {
	if (!hierarchy_can_drop(p_position, p_data)) {
		return;
	}
	TreeItem *target = tree->get_item_at_position(p_position);
	int parent = target && target->get_metadata(0).get_type() == Variant::INT ? int(target->get_metadata(0)) : -1;
	if (ECSModelImporter::accepts(p_data)) { import_models(p_data, parent); return; }
	reparent_entity(Dictionary(p_data)["entity"], parent);
}

bool ECSSceneEditorPanel::model_can_drop(const Vector2 &, const Variant &data) const {
	return scene.is_valid() && runtime_session.is_null() && ECSModelImporter::accepts(data);
}

void ECSSceneEditorPanel::model_drop(const Vector2 &, const Variant &data) { import_models(data, -1); }

void ECSSceneEditorPanel::import_models(const Variant &data, int parent) {
	if (!model_can_drop(Vector2(), data)) return;
	Array entities = scene->get_entities();
	PackedStringArray files = Dictionary(data)["files"];
	for (const String &path : files) {
		String error;
		if (!ECSModelImporter::append(path, entities, parent, error, scene)) {
			status->set_text(String(U"模型导入失败：") + error);
			EditorNode::get_singleton()->show_warning(String(U"模型导入失败：") + path + "\n" + error);
			return;
		}
	}
	commit(entities, String(U"导入模型为 ECS 实体"));
}

void ECSSceneEditorPanel::reparent_entity(int p_entity, int p_parent) {
	if (!can_reparent_entity(p_entity, p_parent)) {
		return;
	}
	Array entities = scene->get_entities();
	Dictionary entity = entities[p_entity];
	entity["parent"] = p_parent;
	entities[p_entity] = entity;
	selected = p_entity;
	commit(entities, String(U"修改 ECS 实体父级"));
}

void ECSSceneEditorPanel::tilemap_edited(int index,const Dictionary &definition) {
    if(scene.is_null() || index<0 || index>=scene->get_entities().size()) { return; }
    Array entities=scene->get_entities().duplicate(true); Dictionary entity=entities[index]; entity["tilemap_2d"]=definition;
    Ref<ECSScene> check; check.instantiate(); check->set_entities(entities); if(check->instantiate().is_null()) { status->set_text(String(U"地图数据无效，没有保存修改。")); return; }
    commit(entities,String(U"编辑 ECS 瓦片地图"));
}
void ECSSceneEditorPanel::commit(const Array &entities, const String &action) {
	if(runtime_session.is_valid()) { status->set_text(String(U"运行时请调整已有组件参数；结构修改请停止运行后操作。")); return; }
	if (scene.is_null()) {
		return;
	}
	auto *undo = EditorUndoRedoManager::get_singleton();
	undo->create_action(action, UndoRedo::MERGE_DISABLE, scene.ptr());
	undo->add_do_method(scene.ptr(), "set_entities", entities);
	undo->add_undo_method(scene.ptr(), "set_entities", scene->get_entities());
	undo->commit_action();
}
void ECSSceneEditorPanel::add_entity(bool ui) {
	if (scene.is_null()) {
		return;
	}
	Array entities = scene->get_entities();
	Dictionary entity;
	if (ui) {
		entity = ECSUIComponents::preset("panel");
	} else {
		entity["position"] = Vector3();
	}
	entity["parent"] = -1;
	selected = entities.size();
	entities.push_back(entity);
	commit(entities, String(U"新增 ECS 实体"));
}
void ECSSceneEditorPanel::duplicate_entity() {
	if (scene.is_null()) {
		return;
	}
	Array entities = scene->get_entities();
	if (selected < 0 || selected >= entities.size()) {
		return;
	}
	Dictionary copy = Dictionary(entities[selected]).duplicate(true);
	int next = entities.size();
	for (const String &kind : { String("pin_joint"), String("joint_2d"), String("joint_3d") }) {
		if (!copy.has(kind) || copy[kind].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary joint = copy[kind];
		bool remapped = false;
		for (const String &key : { String("body_a"), String("body_b") }) {
			if (joint.has(key) && int64_t(joint[key]) == selected) {
				joint[key] = next;
				remapped = true;
			}
		}
		// A separate owner cannot duplicate the same constrained body pair.
		if (!remapped) {
			copy.erase(kind);
		}
	}
	for (const String &kind : { String("animation"), String("skeleton"), String("skeleton_2d") }) {
		if (!copy.has(kind)) {
			continue;
		}
		Dictionary definition = copy[kind];
		String field = kind == "animation" ? "targets" : "bones";
		if (!definition.has(field)) {
			continue;
		}
		PackedInt64Array refs = definition[field];
		for (int i = 0; i < refs.size(); i++) {
			if (refs[i] == selected) {
				refs.set(i, next);
			}
		}
		definition[field] = refs;
	}
	entities.push_back(copy);
	selected = next;
	commit(entities, String(U"复制 ECS 实体"));
}
void ECSSceneEditorPanel::delete_entity() {
	if (scene.is_null()) {
		return;
	}
	Array entities = scene->get_entities();
	if (selected < 0 || selected >= entities.size()) {
		return;
	}
	int removed = selected;
	entities.remove_at(removed);
	for (int i = 0; i < entities.size(); i++) {
		Dictionary definition = entities[i];
		for (const String &kind : { String("pin_joint"), String("joint_2d"), String("joint_3d") }) {
			if (!definition.has(kind) || definition[kind].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary joint = definition[kind];
			bool invalid = false;
			for (const String &key : { String("body_a"), String("body_b") }) {
				int index = joint.get(key, -1);
				invalid |= index == removed;
				if (index > removed) {
					joint[key] = index - 1;
				}
			}
			if (invalid) {
				definition.erase(kind);
			}
		}
		int parent = definition.get("parent", -1);
		if (parent == removed) {
			definition["parent"] = -1;
		} else if (parent > removed) {
			definition["parent"] = parent - 1;
		}
		for (const String &kind : { String("animation"), String("skeleton"), String("skeleton_2d") }) {
			if (!definition.has(kind)) {
				continue;
			}
			Dictionary component_data = definition[kind];
			String key = kind == "animation" ? "targets" : "bones";
			if (!component_data.has(key)) {
				continue;
			}
			PackedInt64Array refs = component_data[key];
			if (refs.has(removed)) {
				definition.erase(kind);
				continue;
			}
			for (int j = 0; j < refs.size(); j++) {
				if (refs[j] > removed) {
					refs.set(j, refs[j] - 1);
				}
			}
			component_data[key] = refs;
		}
	}
	for(int i=0;i<entities.size();i++) {
		Dictionary entity=entities[i]; if(!entity.has("polygon_2d")) { continue; }
		Dictionary polygon=entity["polygon_2d"]; int rig=polygon.get("skeleton",-1);
		if(rig>removed) { rig--; }
		else if(rig==removed) { rig=-1; }
		if(rig>=0 && !Dictionary(entities[rig]).has("skeleton_2d")) { rig=-1; }
		polygon["skeleton"]=rig;
		if(rig<0) { polygon["bones"]=PackedInt32Array(); polygon["weights"]=PackedFloat32Array(); }
	}
	selected = MIN(removed, entities.size() - 1);
	commit(entities, String(U"删除 ECS 实体并修复引用"));
}
void ECSSceneEditorPanel::add_component() {
	if (scene.is_null()) {
		return;
	}
	Array entities = scene->get_entities();
	if (selected < 0 || selected >= entities.size()) {
		return;
	}
	String name = component->get_item_text(component->get_selected());
	if (name == "position") {
		Dictionary entity = entities[selected];
		for (const String &key : { String("position"), String("rotation"), String("scale") }) {
			if (!entity.has(key)) {
				entity[key] = key == "scale" ? Vector3(1, 1, 1) : Vector3();
			}
		}
		entities[selected] = entity;
		commit(entities, TTR("Add Component") + " " + TTR("Transform"));
		return;
	}
	if (ECSCustomComponents::is_component(name)) {
		Dictionary schemas = ECSCustomComponents::registry();
		if (!schemas.has(name)) {
			status->set_text(TTR("Component definition not found. Rebuild the C# project."));
			return;
		}
		Dictionary entity = entities[selected];
		if (entity.has(name)) {
			return;
		}
		entity[name] = ECSCustomComponents::defaults(schemas[name]);
		entities[selected] = entity;
		Dictionary saved = scene->get_custom_schemas();
		saved[name] = schemas[name];
		scene->set_custom_schemas(saved);
		commit(entities, TTR("Add Component"));
		return;
	}
	if (ECSUIComponents::is_component(name)) {
		Dictionary entity = entities[selected];
		if (!ECSUIComponents::add(entity, name)) {
			status->set_text(String(U"已有此组件或存在互斥的交互/布局组件，请先移除旧组件。"));
			return;
		}
		entities[selected] = entity;
		commit(entities, String(U"添加 ") + ECSUIComponents::title(name));
		return;
	}

	Dictionary definition = entities[selected];
	if (definition.has(name)) {
		return;
	}
	if (name == "pin_joint" || name == "joint_2d" || name == "joint_3d") {
		String physics_kind = name == "joint_2d" ? "physics_2d" : "physics";
		int body_a = -1, body_b = -1;
		for (int i = 0; i < entities.size(); i++) {
			if (entities[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary candidate = entities[i];
			if (!candidate.has(physics_kind) || candidate[physics_kind].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary physics = candidate[physics_kind];
			if (String(physics.get("mode", "static")) == "rigid") {
				body_a = i;
				break;
			}
		}
		for (int i = 0; body_a >= 0 && i < entities.size(); i++) {
			if (i == body_a || entities[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary candidate = entities[i];
			if (candidate.has(physics_kind) && candidate[physics_kind].get_type() == Variant::DICTIONARY) {
				body_b = i;
				break;
			}
		}
		if (body_a < 0 || body_b < 0) {
			status->set_text(String(U"点关节需要两个物理实体，其中至少一个为 rigid；先添加物理组件。"));
			return;
		}
		Dictionary joint;
		joint["body_a"] = body_a;
		joint["body_b"] = body_b;
		if (name == "joint_3d") {
			joint["frame_a"] = Transform3D();
			joint["frame_b"] = Transform3D();
		} else {
			joint["anchor_a"] = name == "joint_2d" ? Variant(Vector2()) : Variant(Vector3());
			joint["anchor_b"] = name == "joint_2d" ? Variant(Vector2()) : Variant(Vector3());
		}
		definition[name] = joint;
		commit(entities, String(U"添加 ECS 点关节（请在 Inspector 调整两个物体及局部锚点）"));
		return;
	}
	if (name == "position" || name == "velocity" || name == "rotation" || name == "scale") {
		definition[name] = name == "scale" ? Vector3(1, 1, 1) : Vector3();
	} else if (name == "mesh") {
		Ref<BoxMesh> value;
		value.instantiate();
		definition[name] = value;
	} else if (name == "material") {
		if (!definition.has("mesh")) {
			Ref<BoxMesh> mesh;
			mesh.instantiate();
			definition["mesh"] = mesh;
		}
		Ref<StandardMaterial3D> value;
		value.instantiate();
		definition[name] = value;
	} else {
		Dictionary component_definition;
		if (name == "bone_2d") {
			component_definition["length"] = 80.0; component_definition["rest"] = Transform2D();
		} else if (name == "skeleton_2d") {
			PackedInt64Array bones;
			for (int i = 0; i < entities.size(); i++) { if (Dictionary(entities[i]).has("bone_2d")) { bones.push_back(i); } }
			if (bones.is_empty()) { status->set_text(String(U"请先创建骨骼实体并添加 Bone2D 组件。")); return; }
			component_definition["bones"] = bones; component_definition["bind_poses"] = Array();
		} else if (name == "sprite_frames") {
			Ref<SpriteFrames> frames; frames.instantiate(); Ref<GradientTexture2D> texture; texture.instantiate(); frames->add_frame("default",texture);
			component_definition["space"]=ECSUIComponents::has_ui(definition)?"ui":"world"; component_definition["centered"]=true; component_definition["offset"]=Vector2(); component_definition["flip_h"]=false; component_definition["flip_v"]=false; component_definition["modulate"]=Color(1,1,1,1); component_definition["z_index"]=0;
			component_definition["frames"]=frames; component_definition["animation"]="default"; component_definition["playing"]=true; component_definition["speed"]=1.0; component_definition["rect"]=Rect2(0,0,64,64);
		} else if (name == "gridmap_3d") {
			Ref<MeshLibrary> library; library.instantiate(); library->create_item(0); Ref<BoxMesh> mesh; mesh.instantiate(); library->set_item_mesh(0,mesh); component_definition["library"]=library; component_definition["cell_size"]=Vector3(2,2,2); component_definition["cells"]=Array();
		} else if (name == "theme") {
			Ref<Theme> theme; theme.instantiate(); component_definition["resource"]=theme; component_definition["type"]="Button";
			if (!ECSUIComponents::has_ui(definition)) { definition["ui_layout"]=ECSUIComponents::defaults("ui_layout"); }
		} else if (name == "replication") {
			component_definition["id"]="entity_"+itos(selected); component_definition["authority"]=1; component_definition["fields"]=PackedStringArray({"position","rotation","scale","active"});
		} else if (name == "tilemap_2d") {
            Ref<TileSet> tiles; tiles.instantiate(); component_definition["tile_set"]=tiles; component_definition["cells"]=PackedInt32Array(); component_definition["chunk_size"]=16; component_definition["z_index"]=0; component_definition["collision_enabled"]=true; component_definition["navigation_enabled"]=true;
		} else if (name == "polygon_2d") {
			component_definition["polygon"] = PackedVector2Array({Vector2(0,0),Vector2(160,0),Vector2(160,80),Vector2(0,80)});
			component_definition["uv"] = PackedVector2Array({Vector2(0,0),Vector2(1,0),Vector2(1,1),Vector2(0,1)});
			component_definition["skeleton"] = -1; component_definition["bones"] = PackedInt32Array(); component_definition["weights"] = PackedFloat32Array();
			component_definition["texture"] = Variant(); component_definition["color"] = Color(1,1,1); component_definition["z_index"] = 0;
		} else if (name == "physics_2d" || name == "area_2d") {
			Ref<RectangleShape2D> shape;
			shape.instantiate();
			component_definition["shape"] = shape;
			if (name == "physics_2d") {
				component_definition["mode"] = "static";
			}
		} else if (name == "physics" || name == "area") {
			Ref<BoxShape3D> shape;
			shape.instantiate();
			component_definition["shape"] = shape;
			if (name == "physics") {
				component_definition["mode"] = "static";
			}
		} else if (name == "animation") {
			Ref<Animation> clip;
			clip.instantiate();
			component_definition["clip"] = clip;
		} else if (name == "ui") {
			component_definition["kind"] = "panel";
			component_definition["rect"] = Rect2(0, 0, 180, 40);
		} else if (name == "audio") {
			Ref<AudioStreamWAV> stream;
			stream.instantiate();
			component_definition["stream"] = stream;
			component_definition["playing"] = false;
		} else if (name == "navigation") {
			Ref<NavigationMesh> mesh;
			mesh.instantiate();
			component_definition["mesh"] = mesh;
			component_definition["layers"] = 1;
		} else if (name == "particles") {
			component_definition["amount"] = 128;
			component_definition["lifetime"] = 2.0;
			component_definition["speed"] = 3.0;
			component_definition["spread"] = 1.5;
			component_definition["gravity"] = Vector3(0, -2, 0);
			component_definition["size"] = .08;
			component_definition["color"] = Color(1, .55, .12);
			component_definition["emitting"] = true;
			Ref<ECSWorld> defaults; defaults.instantiate(); uint64_t id=defaults->create_entity();
			defaults->set_particles(id,component_definition); component_definition=defaults->get_particles(id);
		} else if (name == "light") {
			component_definition["type"] = "omni";
		}
		definition[name] = component_definition;
	}
	commit(entities, String(U"添加 ECS 组件 ") + name);
}
void ECSSceneEditorPanel::remove_component() {
	if (scene.is_null()) {
		return;
	}
	Array entities = scene->get_entities();
	if (selected < 0 || selected >= entities.size()) {
		return;
	}
	String name = component->get_item_text(component->get_selected());
	if (name == "position" || ECSUIComponents::is_component(name)) {
		component_section_action(0, name);
		return;
	}

	Dictionary definition = entities[selected];
	if (!definition.erase(name)) {
		return;
	}
	if (name == "mesh") {
		definition.erase("material");
		definition.erase("skeleton");
	}
	if (name == "physics" || name == "physics_2d") {
		for (const String &kind : { String("pin_joint"), String("joint_3d"), String("joint_2d") }) {
			if ((kind == "joint_2d") != (name == "physics_2d")) {
				continue;
			}
			for (int i = 0; i < entities.size(); i++) {
				if (entities[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				Dictionary entity = entities[i];
				if (!entity.has(kind) || entity[kind].get_type() != Variant::DICTIONARY) {
					continue;
				}
				Dictionary joint = entity[kind];
				if (int64_t(joint.get("body_a", -1)) == selected || int64_t(joint.get("body_b", -1)) == selected) {
					entity.erase(kind);
				}
			}
		}
	}
	ecs_repair_skeletal_references(entities);
	commit(entities, String(U"移除 ECS 组件 ") + name);
}
void ECSSceneEditorPanel::begin_navigation_bake() {
	if (baking_scene.is_valid() || scene.is_null() || selected < 0) {
		return;
	}
	int64_t request = scene->start_navigation_bake(selected);
	if (!request) {
		status->set_text(String(U"无法开始烘焙：检查网格与导航参数，或等待上次后台任务结束。"));
		return;
	}
	baking_scene = scene;
	baking_request = request;
	baking_entity = selected;
	baking_revision = scene_revision;
	status->set_text(String(U"正在后台生成导航网格…"));
	set_process(true);
}
void ECSSceneEditorPanel::cancel_navigation_bake() {
	if (baking_scene.is_null()) {
		return;
	}
	baking_scene->cancel_navigation_bake(baking_request);
	baking_scene.unref();
	set_process(false);
	status->set_text(String(U"已取消结果发布；后台生成结束前不能重启同一场景的烘焙。"));
}
void ECSSceneEditorPanel::_notification(int what) {
	if (what == NOTIFICATION_THEME_CHANGED && tree) {
		if (scene_2d) {
			Ref<StyleBoxFlat> pressed;
			pressed.instantiate();
			pressed->set_bg_color(Color(0.20, 0.43, 0.70));
			pressed->set_corner_radius_all(3 * EDSCALE);
			pressed->set_content_margin(SIDE_LEFT, 8 * EDSCALE);
			pressed->set_content_margin(SIDE_RIGHT, 8 * EDSCALE);
			pressed->set_content_margin(SIDE_TOP, 4 * EDSCALE);
			pressed->set_content_margin(SIDE_BOTTOM, 4 * EDSCALE);
			scene_2d->add_theme_style_override("pressed", pressed);
			scene_2d->add_theme_style_override("hover_pressed", pressed);
			scene_2d->add_theme_color_override("font_pressed_color", Color(1, 1, 1));
			scene_2d->add_theme_color_override("font_hover_pressed_color", Color(1, 1, 1));
		}
		if (scene_grid) { scene_grid->set_button_icon(get_editor_theme_icon(SNAME("Grid"))); }
		tree->add_theme_icon_override("arrow", get_editor_theme_icon(SNAME("ECSTreeExpanded")));
		tree->add_theme_icon_override("arrow_collapsed", get_editor_theme_icon(SNAME("ECSTreeCollapsed")));
		Ref<StyleBoxFlat> selection;
		selection.instantiate();
		selection->set_bg_color(get_theme_color("highlight_color", "Editor"));
		tree->add_theme_style_override("selected", selection);
		tree->add_theme_style_override("selected_focus", selection);
	}

	if (what != NOTIFICATION_PROCESS || baking_scene.is_null()) {
		return;
	}
	if (scene != baking_scene || scene_revision != baking_revision) {
		cancel_navigation_bake();
		status->set_text(String(U"场景已改变，已丢弃旧烘焙结果。"));
		return;
	}
	Dictionary result = baking_scene->get_navigation_bake(baking_request);
	if (bool(result.get("running", false))) {
		return;
	}
	baking_scene.unref();
	set_process(false);
	Ref<NavigationMesh> mesh = result.get("mesh", Variant());
	if (mesh.is_null()) {
		status->set_text(String(U"导航烘焙失败或已取消，原资源未修改。"));
		return;
	}
	Array after = scene->get_entities();
	if (baking_entity < 0 || baking_entity >= after.size()) {
		return;
	}
	Dictionary entity = after[baking_entity];
	Dictionary navigation = entity.get("navigation", Dictionary());
	navigation["mesh"] = mesh;
	entity["navigation"] = navigation;
	commit(after, String(U"异步烘焙 ECS 导航网格"));
	status->set_text(String(U"导航烘焙完成，多边形数：") + itos(mesh->get_polygon_count()));
}
void ECSSceneEditorPanel::bake_navigation() {
	if (scene.is_null() || selected < 0 || selected >= scene->get_entities().size()) {
		return;
	}
	Ref<NavigationMesh> mesh = scene->bake_navigation(selected);
	if (mesh.is_null()) {
		status->set_text(String(U"烘焙失败：检查实体网格、父级变换和导航参数；原资源未修改。"));
		return;
	}
	Array after = scene->get_entities();
	Dictionary entity = after[selected];
	Dictionary navigation = entity.get("navigation", Dictionary());
	navigation["mesh"] = mesh;
	entity["navigation"] = navigation;
	commit(after, String(U"烘焙 ECS 导航网格"));
	status->set_text(String(U"导航烘焙完成，多边形数：") + itos(mesh->get_polygon_count()));
}
void ECSSceneEditorPanel::save() {
	if (scene.is_null() || scene->get_path().is_empty()) {
		status->set_text(String(U"先将 ECSScene 保存为项目内资源。"));
		return;
	}
	Error result = ResourceSaver::save(scene, scene->get_path());
	status->set_text(result == OK ? String(U"已保存：") + scene->get_path() : String(U"保存失败：") + itos(result));
}
void ECSSceneEditorPanel::run_self_test() {
	if(OS::get_singleton()->get_cmdline_user_args().find("--ecs-tilemap-only")) { bool ok=ECSWorld::tilemap_2d_self_test("res://TileMapDemo.tres"); if(ok) { Ref<ECSScene> example=ResourceLoader::load("res://TileMapDemo.tres","ECSScene",ResourceLoader::CACHE_MODE_IGNORE); edit_scene(example); ok=canvas->run_tilemap_self_test(example); } get_tree()->quit(ok?0:1); return; }
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-skeletal-2d-only")) {
		bool ok=ECSWorld::skeletal_2d_self_test("res://Skeleton2D.tres");
		get_tree()->quit(ok ? 0 : 1); return;
	}
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-particles-only")) {
		get_tree()->quit(ECSWorld::particles_self_test("res://AnimationParticles.tres") ? 0 : 1);
		return;
	}
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-activation-only")) {
		bool ok = ECSWorld::activation_self_test();
		Ref<ECSScene> test_scene; test_scene.instantiate();
		Array test_entities; Dictionary parent, child;
		parent["name"] = "Activation parent"; child["name"] = "Activation child"; child["parent"] = 0;
		test_entities.push_back(parent); test_entities.push_back(child); test_scene->set_entities(test_entities);
		edit_scene(test_scene);
		proxy->target(test_scene, 0, "@metadata");
		bool valid = false; proxy->set("active", false, &valid);
		ok &= valid && !bool(Dictionary(test_scene->get_entities()[0]).get("active", true));
		refresh();
		TreeItem *parent_row = tree->get_root()->get_first_child();
		TreeItem *child_row = parent_row ? parent_row->get_first_child() : nullptr;
		const Color disabled_color = get_theme_color("disabled_font_color", "Editor");
		ok &= parent_row && child_row && parent_row->get_custom_color(0) == disabled_color && child_row->get_custom_color(0) == disabled_color;
		ok &= parent_row && child_row && parent_row->get_icon_modulate(0).a < 0.5 && child_row->get_icon_modulate(0).a < 0.5;
		ok &= EditorUndoRedoManager::get_singleton()->undo();
		ok &= bool(Dictionary(test_scene->get_entities()[0]).get("active", true));
		refresh();
		parent_row = tree->get_root()->get_first_child();
		child_row = parent_row ? parent_row->get_first_child() : nullptr;
		ok &= parent_row && child_row && parent_row->get_icon_modulate(0).a == 1.0 && child_row->get_icon_modulate(0).a == 1.0;
		ok &= EditorUndoRedoManager::get_singleton()->redo();
		ok &= !bool(Dictionary(test_scene->get_entities()[0]).get("active", true));
		proxy->target(Ref<ECSScene>(), -1, "@metadata");
		edit_scene(Ref<ECSScene>());
		print_line(ok ? "ECS_ACTIVATION_PASS" : "ECS_ACTIVATION_FAILED");
		get_tree()->quit(ok ? 0 : 1);
		return;
	}
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-scene-modes-only")) {
		set_scene_2d(true);
		bool ok = canvas->is_visible() && !spatial->is_visible();
		set_scene_grid(false);
		ok &= !canvas->is_grid_visible() && spatial->is_grid_visible();
		set_scene_2d(false);
		ok &= !canvas->is_visible() && spatial->is_visible() && scene_grid->is_pressed();
		set_scene_grid(false);
		set_scene_2d(true);
		ok &= !scene_grid->is_pressed() && !spatial->is_grid_visible();
		set_scene_grid(true);
		set_scene_2d(false);
		set_scene_grid(true);
		print_line(ok ? "ECS_SCENE_MODES_PASS" : "ECS_SCENE_MODES_FAILED");
		get_tree()->quit(ok ? 0 : 1);
		return;
	}
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-ui-canvas-only")) {
		get_tree()->quit(canvas->run_self_test() ? 0 : 1);
		return;
	}
	auto check = [&](bool condition, const String &message) {
		if (!condition) {
			ERR_PRINT("ECS_AUTHORING_TEST_FAILED " + message);
			get_tree()->quit(1);
		}
		return condition;
	};
	Ref<ECSScene> resource;
	resource.instantiate();
	Dictionary parent;
	parent["position"] = Vector3(1, 2, 3);
	parent["parent"] = -1;
	Dictionary child;
	child["position"] = Vector3(4, 5, 6);
	child["parent"] = 0;
	Dictionary animation;
	PackedInt64Array targets;
	targets.push_back(0);
	animation["targets"] = targets;
	child["animation"] = animation;
	Array entities;
	entities.push_back(parent);
	entities.push_back(child);
	resource->set_entities(entities);
	edit_scene(resource);
	proxy->target(resource, 0);
	proxy->set("name", String(U"玩家"));
	if (!check(String(Dictionary(resource->get_entities()[0])["name"]) == String(U"玩家"), "entity name")) {
		return;
	}
	if (!check(tree->get_root()->get_first_child()->get_first_child() != nullptr, "entity hierarchy")) {
		return;
	}
	if (!check(!can_reparent_entity(0, 0) && !can_reparent_entity(0, 1) && !can_reparent_entity(1, 7), "hierarchy rejects self descendant and invalid parent")) {
		return;
	}
	reparent_entity(1, -1);
	if (!check(int(Dictionary(resource->get_entities()[1])["parent"]) == -1 && Vector3(Dictionary(resource->get_entities()[1])["position"]) == Vector3(4, 5, 6), "hierarchy detach preserves component data")) {
		return;
	}
	auto *hierarchy_undo = EditorUndoRedoManager::get_singleton();
	if (!check(hierarchy_undo->undo() && int(Dictionary(resource->get_entities()[1])["parent"]) == 0 && hierarchy_undo->redo() && int(Dictionary(resource->get_entities()[1])["parent"]) == -1, "hierarchy undo redo")) {
		return;
	}
	reparent_entity(1, 0);
	if (!check(int(Dictionary(resource->get_entities()[1])["parent"]) == 0 && tree->get_root()->get_first_child()->get_first_child() != nullptr, "hierarchy reattach rebuilds tree")) {
		return;
	}
	selected = 0;
	duplicate_entity();
	if (!check(resource->get_entities().size() == 3, "duplicate")) {
		return;
	}
	selected = 0;
	delete_entity();
	Dictionary detached = resource->get_entities()[0];
	if (!check(int(detached["parent"]) == -1 && !detached.has("animation"), "delete repairs references")) {
		return;
	}
	auto *undo = EditorUndoRedoManager::get_singleton();
	if (!check(undo->undo() && resource->get_entities().size() == 3, "undo deletion")) {
		return;
	}
	if (!check(undo->redo() && resource->get_entities().size() == 2, "redo deletion")) {
		return;
	}
	proxy->target(resource, 0);
	proxy->set("position", Vector3(9, 8, 7));
	if (!check(Vector3(Dictionary(resource->get_entities()[0])["position"]) == Vector3(9, 8, 7), "inspector property")) {
		return;
	}
	if (!check(undo->undo() && Vector3(Dictionary(resource->get_entities()[0])["position"]) == Vector3(4, 5, 6), "undo property")) {
		return;
	}
	selected = 0;
	add_entity(true);
	if (!check(Dictionary(resource->get_entities()[2]).has("ui_layout"), "UI authoring")) {
		return;
	}
	String path = "user://ecs-authoring-test-" + itos(OS::get_singleton()->get_ticks_usec()) + ".tres";
	if (!check(ResourceSaver::save(resource, path) == OK, "save")) {
		return;
	}
	Ref<ECSScene> loaded = ResourceLoader::load(path, "ECSScene", ResourceLoader::CACHE_MODE_IGNORE);
	if (!check(loaded.is_valid() && loaded->get_entities().size() == 3, "reload")) {
		return;
	}
	// Exercise every add/remove action, including the dictionary-backed components.
	Ref<ECSScene> components;
	components.instantiate();
	edit_scene(components);
	add_entity(false);
	for (int i = 0; i < component->get_item_count(); i++) {
		component->select(i);
		String name = component->get_item_text(i);
		if (name == "pin_joint" || name == "joint_2d" || name == "joint_3d") {
			continue;
		}
		add_component();
		if (!check(Dictionary(components->get_entities()[0]).has(name), "add " + name)) {
			return;
		}
		if (!check(components->instantiate().is_valid(), "instantiate " + name)) {
			return;
		}
		if (name == "ui_layout") {
			proxy->target(components, 0);
			proxy->set("ui_layout/rect", Rect2(10, 20, 100, 60));
			if (!check(Rect2(Dictionary(Dictionary(components->get_entities()[0])["ui_layout"])["rect"]) == Rect2(10, 20, 100, 60), "component field editing")) {
				return;
			}
			if (!check(undo->undo(), "component field undo")) {
				return;
			}
		}
		if (name == "animation") {
			proxy->target(components, 0);
			proxy->set("animation/speed", 2.0);
			if (!check(double(Dictionary(Dictionary(components->get_entities()[0])["animation"])["speed"]) == 2.0, "animation inspector speed")) {
				return;
			}
			if (!check(undo->undo() && !Dictionary(Dictionary(components->get_entities()[0])["animation"]).has("speed"), "animation inspector undo")) {
				return;
			}
			if (!check(undo->redo() && components->instantiate().is_valid(), "animation inspector redo runtime")) {
				return;
			}
		}
		remove_component();
		if (!check(!Dictionary(components->get_entities()[0]).has(name), "remove " + name)) {
			return;
		}
		if (!check(undo->undo() && Dictionary(components->get_entities()[0]).has(name), "undo remove " + name)) {
			return;
		}
		if (!check(undo->redo() && !Dictionary(components->get_entities()[0]).has(name), "redo remove " + name)) {
			return;
		}
	}
	for (const String &joint_kind : { String("pin_joint"), String("joint_2d"), String("joint_3d") }) {
		bool two_d = joint_kind == "joint_2d";
		String physics_kind = two_d ? "physics_2d" : "physics";
		Ref<ECSScene> joint_scene;
		joint_scene.instantiate();
		Array joint_entities;
		for (int i = 0; i < 3; i++) {
			Dictionary entity;
			if (i < 2) {
				Ref<BoxShape3D> shape;
				shape.instantiate();
				Dictionary physics;
				if (two_d) {
					Ref<RectangleShape2D> shape_2d;
					shape_2d.instantiate();
					physics["shape"] = shape_2d;
				} else {
					physics["shape"] = shape;
				}
				physics["mode"] = i == 0 ? "rigid" : "static";
				entity[physics_kind] = physics;
			}
			joint_entities.push_back(entity);
		}
		joint_scene->set_entities(joint_entities);
		edit_scene(joint_scene);
		selected = 2;
		for (int i = 0; i < component->get_item_count(); i++) {
			if (component->get_item_text(i) == joint_kind) {
				component->select(i);
			}
		}
		add_component();
		if (!check(Dictionary(joint_scene->get_entities()[2]).has(joint_kind) && joint_scene->instantiate().is_valid(), "joint authoring instantiate")) {
			return;
		}
		selected = 0;
		delete_entity();
		if (!check(!Dictionary(joint_scene->get_entities()[1]).has(joint_kind), "joint reference removal")) {
			return;
		}
		if (!check(undo->undo() && Dictionary(joint_scene->get_entities()[2]).has(joint_kind) && joint_scene->instantiate().is_valid(), "joint reference undo")) {
			return;
		}
		selected = 0;
		for (int i = 0; i < component->get_item_count(); i++) {
			if (component->get_item_text(i) == physics_kind) {
				component->select(i);
			}
		}
		remove_component();
		if (!check(!Dictionary(joint_scene->get_entities()[2]).has(joint_kind) && joint_scene->instantiate().is_valid(), "remove physics clears joint")) {
			return;
		}
		if (!check(undo->undo() && joint_scene->instantiate().is_valid(), "undo physics removal restores joint")) {
			return;
		}
	}
	Ref<ECSScene> bake_scene;
	bake_scene.instantiate();
	Array bake_entities;
	Dictionary bake_region, bake_surface;
	bake_entities.push_back(bake_region);
	Ref<PlaneMesh> surface;
	surface.instantiate();
	surface->set_size(Vector2(20, 20));
	bake_surface["mesh"] = surface;
	bake_entities.push_back(bake_surface);
	bake_scene->set_entities(bake_entities);
	edit_scene(bake_scene);
	selected = 0;
	bake_navigation();
	if (!check(Dictionary(bake_scene->get_entities()[0]).has("navigation"), "navigation bake button")) {
		return;
	}
	Ref<NavigationMesh> baked = Dictionary(Dictionary(bake_scene->get_entities()[0])["navigation"])["mesh"];
	if (!check(baked.is_valid() && baked->get_polygon_count() > 0, "navigation bake polygons")) {
		return;
	}
	if (!check(undo->undo() && !Dictionary(bake_scene->get_entities()[0]).has("navigation"), "navigation bake undo")) {
		return;
	}
	if (!check(undo->redo() && Dictionary(bake_scene->get_entities()[0]).has("navigation"), "navigation bake redo")) {
		return;
	}
	if (!check(canvas->run_self_test(), "UI canvas drag/resize/undo")) {
		return;
	}
	if (!check(spatial->run_self_test(), "3D transform undo redo")) {
		return;
	}
	Ref<ECSScene> menu_scene;
	menu_scene.instantiate();
	edit_scene(menu_scene);
	hierarchy_action(0);
	if (!check(menu_scene->get_entities().size() == 1 && selected == 0, "context create root and select")) {
		return;
	}
	context_entity = 0;
	hierarchy_action(2);
	if (!check(menu_scene->get_entities().size() == 2 && int(Dictionary(menu_scene->get_entities()[1])["parent"]) == 0 && selected == 1, "context create child and select")) {
		return;
	}
	component_filter("");
	if (!check(component_results->get_item_count() > 5 && component_results->get_item_icon(0).is_valid(), "component picker default list with icons")) {
		return;
	}
	int default_count = component_results->get_item_count();
	component_filter("   ");
	if (!check(component_results->get_item_count() == default_count, "component picker whitespace shows defaults")) {
		return;
	}
	component_filter("__no_matching_component__");
	if (!check(component_results->get_item_count() == 0 && component_picker_status->get_text() == TTR("No matching components"), "component picker no results feedback")) {
		return;
	}
	component_filter("vel");
	if (!check(component_results->get_item_count() == 1, "component search filters")) {
		return;
	}
	component_pick(0);
	if (!check(Dictionary(menu_scene->get_entities()[1]).has("velocity"), "component picker adds")) {
		return;
	}
	component_filter("vel");
	if (!check(component_results->get_item_count() == 0, "component picker excludes existing")) {
		return;
	}
	update_component_actions();
	inspector_action(101);
	if (!check(!Dictionary(menu_scene->get_entities()[1]).has("velocity") && undo->undo() && Dictionary(menu_scene->get_entities()[1]).has("velocity"), "remove component menu undo")) {
		return;
	}
	if (!check(undo->redo() && !Dictionary(menu_scene->get_entities()[1]).has("velocity"), "remove component menu redo")) {
		return;
	}
	context_entity = 1;
	hierarchy_action(3);
	if (!check(menu_scene->get_entities().size() == 3, "context duplicate")) {
		return;
	}
	context_entity = 2;
	hierarchy_action(4);
	if (!check(menu_scene->get_entities().size() == 2 && undo->undo() && menu_scene->get_entities().size() == 3, "context delete undo")) {
		return;
	}
	selected = 0;
	rebuild_component_sections();
	if (!check(component_views.size() == 1, "component cards match entity")) {
		return;
	}
	component_fold_changed(true, "position");
	component_signature = "";
	rebuild_component_sections();
	auto *fold = Object::cast_to<FoldableContainer>(component_sections->get_child(1));
	if (!check(fold && fold->is_folded(), "component fold persists across rebuild")) {
		return;
	}
	component_section_action(0, "position");
	if (!check(!Dictionary(menu_scene->get_entities()[0]).has("position") && undo->undo() && Dictionary(menu_scene->get_entities()[0]).has("position"), "component card menu remove undo")) {
		return;
	}
	component_fold_changed(false, "position");
	Ref<ECSWorld> material_world;
	material_world.instantiate();
	uint64_t material_entity = material_world->create_entity();
	Ref<CanvasItemMaterial> ui_material;
	ui_material.instantiate();
	Dictionary ui_definition;
	ui_definition["material"] = ui_material;
	if (!check(material_world->set_ui(material_entity, ui_definition) && Ref<Material>(material_world->get_ui(material_entity)["material"]) == ui_material, "UI material reference")) {
		return;
	}
	Ref<StandardMaterial3D> spatial_material;
	spatial_material.instantiate();
	ui_definition["material"] = spatial_material;
	if (!check(!material_world->set_ui(material_entity, ui_definition), "reject spatial material on UI")) {
		return;
	}
	ui_definition["material"] = Variant();
	if (!check(material_world->set_ui(material_entity, ui_definition) && !material_world->get_ui(material_entity).has("material"), "clear UI material override")) {
		return;
	}
	Ref<ECSScene> render_scene;
	render_scene.instantiate();
	Ref<BoxMesh> render_mesh;
	render_mesh.instantiate();
	Dictionary render_entity;
	render_entity["mesh"] = render_mesh;
	render_entity["material"] = spatial_material;
	Array render_entities;
	render_entities.push_back(render_entity);
	render_scene->set_entities(render_entities);
	Ref<ECSSceneEntityEditor> render_proxy;
	render_proxy.instantiate();
	render_proxy->target(render_scene, 0, "@renderer");
	render_proxy->set("material", Variant());
	if (!check(!Dictionary(render_scene->get_entities()[0]).has("material") && undo->undo() && Dictionary(render_scene->get_entities()[0]).has("material"), "renderer clear material override undo")) {
		return;
	}
	// Composed UI schemas must survive authoring, serialization and runtime creation.
	Ref<ECSScene> ui_components_scene;
	ui_components_scene.instantiate();
	Array ui_entities;
	for (const char *kind : { "panel", "label", "image", "button", "toggle", "text_field", "text_area", "slider", "progress", "scroll", "hbox", "vbox", "grid" }) {
		Dictionary entity = ECSUIComponents::preset(kind);
		entity["parent"] = -1;
		Dictionary compiled;
		if (!check(!entity.has("ui") && entity.has("ui_layout") && ECSUIComponents::compose(entity, compiled), String("UI preset compose ") + kind)) {
			return;
		}
		ui_entities.push_back(entity);
	}
	ui_components_scene->set_entities(ui_entities);
	if (!check(ui_components_scene->instantiate().is_valid(), "UI presets instantiate")) {
		return;
	}
	String ui_path = "user://ecs-ui-components-roundtrip.tres";
	if (!check(ResourceSaver::save(ui_components_scene, ui_path) == OK, "UI component save")) {
		return;
	}
	Ref<ECSScene> ui_loaded = ResourceLoader::load(ui_path, "ECSScene", ResourceLoader::CACHE_MODE_IGNORE);
	if (!check(ui_loaded.is_valid() && ui_loaded->get_entities().recursive_equal(ui_entities, 0) && ui_loaded->instantiate().is_valid(), "UI components roundtrip")) {
		return;
	}
	edit_scene(ui_components_scene);
	selected = 3;
	component_section_action(0, "ui_button");
	Dictionary ui_after = ui_components_scene->get_entities()[3];
	if (!check(!ui_after.has("ui_button") && ui_after.has("ui_text") && ui_after.has("ui_image") && undo->undo() && Dictionary(ui_components_scene->get_entities()[3]).has("ui_button"), "UI behavior removal keeps display and undo restores")) {
		return;
	}
	Dictionary conflicting = ECSUIComponents::preset("button");
	if (!check(!ECSUIComponents::add(conflicting, "ui_toggle"), "UI mutually exclusive interaction")) {
		return;
	}
	component_section_action(0, "ui_layout");
	if (!check(!Dictionary(ui_components_scene->get_entities()[3]).has("ui_text") && undo->undo() && ui_components_scene->instantiate().is_valid(), "UI layout cascade undo")) {
		return;
	}
	Ref<ECSScene> button_scene;
	button_scene.instantiate();
	Array button_entities;
	button_entities.push_back(ECSUIComponents::preset("button"));
	button_scene->set_entities(button_entities);
	Ref<ECSWorld> button_world = button_scene->instantiate();
	ECSUISystem button_system;
	button_system.attach(button_world, RID());
	for (bool down : { true, false }) {
		Ref<InputEventMouseButton> event;
		event.instantiate();
		event->set_button_index(MouseButton::LEFT);
		event->set_position(Vector2(30, 30));
		event->set_pressed(down);
		button_system.input(event, Size2(640, 480));
	}
	button_world->step(0);
	bool pressed_event = false;
	for (const Variant &event : button_world->get_ui_events()) {
		if (String(Dictionary(event)["type"]) == "pressed") {
			pressed_event = true;
		}
	}
	if (!check(pressed_event, "composed button dispatches pressed")) {
		return;
	}
	Dictionary aligned = ECSUIComponents::preset("panel");
	Dictionary aligned_layout = aligned["ui_layout"];
	aligned_layout["alignment"] = 5;
	aligned_layout["margins"] = Vector4(4, 6, 4, 6);
	Dictionary aligned_ui;
	if (!check(ECSUIComponents::compose(aligned, aligned_ui) && Vector4(aligned_ui["anchors"]) == Vector4(.5, .5, .5, .5) && Rect2(aligned_ui["rect"]) == Rect2(24, 26, 232, 48), "layout alignment and margins")) {
		return;
	}
	proxy->target(ui_components_scene, 3, "ui_text");
	bool property_valid = false;
	proxy->set("text", String(U"确认"), &property_valid);
	if (!check(property_valid && String(Dictionary(Dictionary(ui_components_scene->get_entities()[3])["ui_text"])["text"]) == String(U"确认") && undo->undo(), "text component inspector edit undo")) {
		return;
	}
	proxy->set("ui_layout/columns", 0, &property_valid);
	if (!check(!property_valid, "invalid layout edit rejected")) {
		return;
	}
	edit_scene(ui_components_scene);
	selected = 0;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			apply_anchor_preset(x, y);
			Dictionary anchor_entity = ui_components_scene->get_entities()[0];
			Dictionary anchor_layout = anchor_entity["ui_layout"];
			Vector4 expected(x == 3 ? 0 : x * .5f, y == 3 ? 0 : y * .5f, x == 3 ? 1 : x * .5f, y == 3 ? 1 : y * .5f);
			rebuild_component_sections();
			auto *preset_button = Object::cast_to<Button>(component_sections->find_child("ECSAnchorPreset", true, false));
			if (!check(preset_button && Vector4(preset_button->get_meta("anchors")) == expected && preset_button->get_button_icon().is_valid() && preset_button->get_text() != String(U"锚点预设…"), "anchor preset button reflects selection")) {
				return;
			}
			if (!check(Vector4(anchor_layout["anchors"]) == expected && int(anchor_layout["alignment"]) == 0 && ui_components_scene->instantiate().is_valid(), "anchor grid preset")) {
				return;
			}
		}
	}
	if (!check(undo->undo() && Vector4(Dictionary(Dictionary(ui_components_scene->get_entities()[0])["ui_layout"])["anchors"]) == Vector4(1, 0, 1, 1) && undo->redo(), "anchor preset undo redo")) {
		return;
	}
	if (!check(undo->undo(), "anchor feedback undo")) {
		return;
	}
	rebuild_component_sections();
	auto *feedback = Object::cast_to<Button>(component_sections->find_child("ECSAnchorPreset", true, false));
	if (!check(feedback && Vector4(feedback->get_meta("anchors")) == Vector4(1, 0, 1, 1) && undo->redo(), "anchor button refreshes after undo")) {
		return;
	}
	rebuild_component_sections();
	feedback = Object::cast_to<Button>(component_sections->find_child("ECSAnchorPreset", true, false));
	if (!check(feedback && Vector4(feedback->get_meta("anchors")) == Vector4(0, 0, 1, 1), "anchor button refreshes after redo")) {
		return;
	}
	print_line("ECS_ANCHOR_FEEDBACK_PASS 16 selections undo redo");
	print_line("ECS_ANCHOR_PRESETS_PASS 16 presets undo redo");
	print_line("ECS_UI_COMPONENTS_PASS presets composition roundtrip remove undo conflicts button_event alignment margins inspector");
	Dictionary custom_registry = ECSCustomComponents::registry();
	if (custom_registry.has("ecs:Examples.Movement")) {
		Ref<ECSScene> custom;
		custom.instantiate();
		Array data;
		Dictionary item;
		item["position"] = Vector3();
		item["parent"] = -1;
		data.push_back(item);
		custom->set_entities(data);
		edit_scene(custom);
		selected = 0;
		component->add_item("ecs:Examples.Movement");
		component->select(component->get_item_count() - 1);
		add_component();
		if (!check(Dictionary(custom->get_entities()[0]).has("ecs:Examples.Movement"), "custom Add Component")) {
			return;
		}
		proxy->target(custom, 0, "ecs:Examples.Movement");
		bool valid = false;
		proxy->set("Speed", 2.5, &valid);
		if (!check(valid && double(Dictionary(Dictionary(custom->get_entities()[0])["ecs:Examples.Movement"])["Speed"]) == 2.5, "custom Inspector field")) {
			return;
		}
		proxy->set("Direction", Vector3(1, 0, 0), &valid);
		if (!check(valid, "custom Inspector vector")) {
			return;
		}
		proxy->set("Speed", String("invalid"), &valid);
		if (!check(!valid, "custom rejects invalid field type")) {
			return;
		}
		rebuild_component_sections();
		Ref<ECSWorld> custom_world = custom->instantiate();
		if (!check(custom_world.is_valid() && custom_world->query(PackedStringArray({ "ecs:Examples.Movement" })).size() == 1, "custom native instantiate")) {
			return;
		}
		String path = "user://ecs-custom-roundtrip.tres";
		if (!check(ResourceSaver::save(custom, path) == OK, "custom save")) {
			return;
		}
		Ref<ECSScene> loaded = ResourceLoader::load(path, "ECSScene", ResourceLoader::CACHE_MODE_IGNORE);
		if (!check(loaded.is_valid() && loaded->get_entities().recursive_equal(custom->get_entities(), 0) && loaded->instantiate().is_valid(), "custom reload")) {
			return;
		}
		component_section_action(2, "ecs:Examples.Movement");
		component_section_action(1, "ecs:Examples.Movement");
		if (!check(double(Dictionary(Dictionary(custom->get_entities()[0])["ecs:Examples.Movement"])["Speed"]) == 0, "custom reset")) {
			return;
		}
		component_section_action(3, "ecs:Examples.Movement");
		if (!check(double(Dictionary(Dictionary(custom->get_entities()[0])["ecs:Examples.Movement"])["Speed"]) == 2.5, "custom copy paste")) {
			return;
		}
		component_section_action(0, "ecs:Examples.Movement");
		if (!check(!Dictionary(custom->get_entities()[0]).has("ecs:Examples.Movement") && undo->undo() && Dictionary(custom->get_entities()[0]).has("ecs:Examples.Movement") && undo->redo() && !Dictionary(custom->get_entities()[0]).has("ecs:Examples.Movement"), "custom remove undo redo")) {
			return;
		}
		print_line("ECS_CUSTOM_AUTHORING_PASS add inspector validation save reload remove undo redo");
	}
	Ref<ECSScene> transform_scene;
	transform_scene.instantiate();
	Array transform_entities;
	Dictionary transform_entity;
	transform_entity["rotation"] = Vector3(.1, .2, .3);
	transform_entity["velocity"] = Vector3(2, 0, 0);
	transform_entities.push_back(transform_entity);
	transform_scene->set_entities(transform_entities);
	edit_scene(transform_scene);
	selected = 0;
	rebuild_component_sections();
	if (!check(component_views.size() == 2, "one Transform card and separate Velocity")) {
		return;
	}
	proxy->target(transform_scene, 0, "@transform");
	bool transform_valid = false;
	if (!check(Vector3(proxy->get("scale")) == Vector3(1, 1, 1) && !Dictionary(transform_scene->get_entities()[0]).has("scale"), "legacy transform defaults without modifying data")) {
		return;
	}
	proxy->set("position", Vector3(3, 4, 5), &transform_valid);
	if (!check(transform_valid && transform_scene->instantiate().is_valid(), "edit missing transform field")) {
		return;
	}
	component_section_action(1, "position");
	Dictionary reset_transform = transform_scene->get_entities()[0];
	if (!check(Vector3(reset_transform["position"]) == Vector3() && Vector3(reset_transform["rotation"]) == Vector3() && Vector3(reset_transform["scale"]) == Vector3(1, 1, 1) && Vector3(reset_transform["velocity"]) == Vector3(2, 0, 0), "reset grouped transform keeps velocity")) {
		return;
	}
	component_section_action(0, "position");
	if (!check(!ecs_has_transform(transform_scene->get_entities()[0]) && Dictionary(transform_scene->get_entities()[0]).has("velocity") && undo->undo() && ecs_has_transform(transform_scene->get_entities()[0]), "group removal undo preserves velocity")) {
		return;
	}
	print_line("ECS_TRANSFORM_CARD_PASS legacy_defaults edit grouped_reset remove undo independent_velocity");
	print_line("ECS_AUTHORING_TEST_PASS duplicate delete references undo redo inspector ui save reload components instantiate");
	edit_scene(Ref<ECSScene>());
}
#endif

#ifdef TOOLS_ENABLED
void ECSSceneEditorPanel::run_visual_test() {
	if (OS::get_singleton()->get_cmdline_user_args().find("--ecs-3d-authoring-visual")) {
		set_scene_2d(false);
		spatial->start_visual_test();
	} else {
		show_rect_tool();
		canvas->start_visual_test();
	}
}
#endif
