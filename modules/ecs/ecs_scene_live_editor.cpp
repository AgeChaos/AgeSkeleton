#ifdef TOOLS_ENABLED
#include "ecs_scene_editor.h"
#include "ecs_live_edit.h"
#include "core/object/callable_mp.h"
#include "editor/debugger/editor_debugger_plugin.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/gui/check_box.h"
#include "scene/gui/line_edit.h"

void ECSSceneEditorPanel::set_runtime_session(const Ref<EditorDebuggerSession> &session) {
	runtime_session=session;
	if(runtime_scene.is_valid() || scene.is_null()) { return; }
	bool ok=true;
	Array entities=ECSLiveEdit::decode(ECSLiveEdit::encode(scene->get_entities()),ok);
	if(!ok) { status->set_text(String(U"运行检查器初始化失败：无法复制资源。")); return; }
	runtime_scene.instantiate(); runtime_scene->set_entities(entities); runtime_scene->set_custom_schemas(scene->get_custom_schemas());
	runtime_copied.clear(); runtime_copied_index=-1;
	runtime_actions->show(); component_signature="";
	component_add_button->set_disabled(true);
	entity_name->set_editable(false);
	refresh();
}
void ECSSceneEditorPanel::setup_runtime_proxy(const Ref<ECSSceneEntityEditor> &p) {
	p->set_runtime_writer(runtime_session.is_valid()?callable_mp(this,&ECSSceneEditorPanel::runtime_set_property):Callable());
}
bool ECSSceneEditorPanel::runtime_set_property(int index,const StringName &property,const Variant &value) {
	if(runtime_updating) { return false; }
	if(runtime_session.is_null() || !runtime_session->is_active()) { return false; }
	Array args; args.push_back(++runtime_request_id); args.push_back(index); args.push_back(String(property)); args.push_back(ECSLiveEdit::encode(value)); args.push_back(false);
	runtime_session->send_message("ecs:live_set",args);
	runtime_last_result.clear(); runtime_last_result["request"]=runtime_request_id; runtime_last_result["pending"]=true;
	return true;
}
void ECSSceneEditorPanel::runtime_transform_changed(int index,const Transform3D &transform) {
	if(runtime_session.is_null() || !runtime_session->is_active()) { return; }
	Array args; args.push_back(++runtime_request_id); args.push_back(index); args.push_back("transform"); args.push_back(transform); args.push_back(true);
	runtime_session->send_message("ecs:live_set",args);
}
void ECSSceneEditorPanel::runtime_edit_result(const Array &result) {
	if(result.size()!=3 || runtime_session.is_null()) { return; }
	runtime_last_result["request"]=result[0]; runtime_last_result["pending"]=false; runtime_last_result["ok"]=result[1]; runtime_last_result["error"]=result[2];
	runtime_results[itos(int64_t(result[0]))]=runtime_last_result.duplicate();
	if(runtime_results.size()>64) { runtime_results.erase(runtime_results.keys()[0]); }
	status->set_text(bool(result[1])?String(U"运行参数已更新；停止后恢复，保留修改请点击“应用到编辑场景”。"):String(U"运行修改失败：")+String(result[2]));
}
void ECSSceneEditorPanel::clear_runtime_resources() {
	for(const RuntimeResourceWatch &watch:runtime_resources) {
		if(watch.resource->is_connected("changed",watch.changed)) { watch.resource->disconnect("changed",watch.changed); }
	}
	runtime_resources.clear();
}
void ECSSceneEditorPanel::runtime_resource_changed(int index,const String &path) {
	if(runtime_updating) { return; }
	if(runtime_scene.is_null() || index<0 || index>=runtime_scene->get_entities().size()) { return; }
	Dictionary entity=runtime_scene->get_entities()[index];
	Variant value=entity.get(path.get_slice("/",0),Variant());
	if(path.contains("/") && value.get_type()==Variant::DICTIONARY) { value=Dictionary(value).get(path.get_slice("/",1),Variant()); }
	runtime_set_property(index,path,value);
}
void ECSSceneEditorPanel::update_runtime_inspector(int index,const Variant &encoded) {
	if(runtime_scene.is_null() || index<0 || index>=runtime_scene->get_entities().size() || encoded.get_type()!=Variant::DICTIONARY || Dictionary(encoded).is_empty()) { return; }
	bool ok=true; Dictionary definition=ECSLiveEdit::decode(encoded,ok);
	if(!ok) { status->set_text(String(U"无法解码运行组件数据。")); return; }
	Array entities=runtime_scene->get_entities();
	Dictionary previous=entities[index];
	// Keep unchanged resource instances and their Inspector editors stable.
	for(const Variant &key:definition.keys()) {
		Variant value=definition[key],old=previous.get(key,Variant());
		if(value.get_type()==Variant::OBJECT && ECSLiveEdit::encode(value)==ECSLiveEdit::encode(old)) { definition[key]=old; }
		else if(value.get_type()==Variant::DICTIONARY && old.get_type()==Variant::DICTIONARY) {
			Dictionary fields=value,old_fields=old;
			for(const Variant &field:fields.keys()) {
				if(fields[field].get_type()==Variant::OBJECT && ECSLiveEdit::encode(fields[field])==ECSLiveEdit::encode(old_fields.get(field,Variant()))) { fields[field]=old_fields[field]; }
			}
		}
	}
	entities[index]=definition; runtime_scene->set_entities(entities);
	spatial->update_runtime_definition(index,definition);
	canvas->update_runtime_definition(index,definition);
	if(index!=selected) { return; }
	runtime_updating=true;
	clear_runtime_resources();
	auto watch=[&](const Variant &value,const String &path) {
		if(value.get_type()!=Variant::OBJECT) { return; }
		Ref<Resource> resource=value; if(resource.is_null()) { return; }
		Callable changed=callable_mp(this,&ECSSceneEditorPanel::runtime_resource_changed).bind(index,path);
		if(!resource->is_connected("changed",changed)) { resource->connect("changed",changed); runtime_resources.push_back({resource,changed}); }
	};
	for(const Variant &key:definition.keys()) {
		watch(definition[key],key);
		if(definition[key].get_type()==Variant::DICTIONARY) { Dictionary fields=definition[key]; for(const Variant &field:fields.keys()) { watch(fields[field],String(key)+"/"+String(field)); } }
	}
	for(int i=0;i<component_views.size();i++) {
		List<PropertyInfo> properties; component_proxies[i]->get_property_list(&properties);
		for(const PropertyInfo &property:properties) { if(property.type!=Variant::NIL && property.type!=Variant::OBJECT) { component_views[i]->update_property(property.name); } }
	}
	entity_active->set_pressed_no_signal(definition.get("active",true));
	runtime_updating=false;
}
void ECSSceneEditorPanel::runtime_copy() {
	if(runtime_scene.is_null() || selected<0) { return; }
	bool ok=true;
	runtime_copied=ECSLiveEdit::decode(ECSLiveEdit::encode(runtime_scene->get_entities()[selected]),ok);
	if(!ok) { runtime_copied.clear(); return; }
	runtime_copied_index=selected;
	status->set_text(String(U"已复制所选实体运行值；停止后也可应用到编辑场景。"));
}
void ECSSceneEditorPanel::runtime_apply() {
	if(runtime_scene.is_valid()) { runtime_copy(); }
	if(scene.is_null() || runtime_copied.is_empty() || runtime_copied_index<0 || runtime_copied_index>=scene->get_entities().size()) { return; }
	Array before=scene->get_entities(),after=before.duplicate(true);
	after[runtime_copied_index]=runtime_copied.duplicate(true);
	auto *undo=EditorUndoRedoManager::get_singleton();
	undo->create_action(String(U"应用 ECS 运行参数到编辑场景"),UndoRedo::MERGE_DISABLE,scene.ptr());
	undo->add_do_method(scene.ptr(),"set_entities",after); undo->add_undo_method(scene.ptr(),"set_entities",before); undo->commit_action();
	status->set_text(String(U"运行值已应用，可撤销；保存场景后写入文件。"));
}
#endif
