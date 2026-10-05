// Editor-only local automation for the skeletal authoring workspace.
#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "ecs_ai_value.h"
#include "ecs_spine_import.h"
#include "skeleton_runtime_export.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"
#include "core/object/callable_mp.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_preset.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/menu_button.h"

void ECSAnimationEditor::skeleton_ai_start() {
    if(!OS::get_singleton()->get_cmdline_user_args().find("--ecs-ai-editor") && !bool(ProjectSettings::get_singleton()->get_setting("ecs/editor/ai_tools",false))) { return; }
    skeleton_ai_directory="res://.godot/agechaos-skeleton-ai/"+itos(OS::get_singleton()->get_process_id());
    if(DirAccess::make_dir_recursive_absolute(skeleton_ai_directory)!=OK) { skeleton_ai_directory=String(); return; }
    Dictionary session; session["pid"]=OS::get_singleton()->get_process_id(); session["protocol"]=1; session["workspace"]="skeleton";
    session["project"]=ProjectSettings::get_singleton()->globalize_path("res://");
    Ref<FileAccess> file=FileAccess::open(skeleton_ai_directory.path_join("session.json"),FileAccess::WRITE);
    if(file.is_null()) { skeleton_ai_directory=String(); return; } file->store_string(JSON::stringify(session)); file.unref();
    Timer *timer=memnew(Timer); timer->set_wait_time(.1); add_child(timer); timer->connect("timeout",callable_mp(this,&ECSAnimationEditor::skeleton_ai_poll)); timer->start();
    print_line("SKELETON_AI_READY "+ProjectSettings::get_singleton()->globalize_path(skeleton_ai_directory));
}
void ECSAnimationEditor::skeleton_ai_stop() {
    if(!skeleton_ai_directory.is_empty()) { DirAccess::remove_absolute(skeleton_ai_directory.path_join("session.json")); skeleton_ai_directory=String(); }
}
void ECSAnimationEditor::skeleton_ai_poll() {
	if (skeleton_ai_directory.is_empty()) {
		return;
	}
	Ref<DirAccess> dir = DirAccess::open(skeleton_ai_directory);
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
		String path = skeleton_ai_directory.path_join(name);
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
				response = skeleton_ai_request(json.get_data());
				DirAccess::remove_absolute(path + ".processing");
			}
		}
		file.unref();
		if (FileAccess::exists(path)) {
			DirAccess::remove_absolute(path);
		}
		String output = skeleton_ai_directory.path_join(name.trim_suffix(".request.json") + ".response.json");
		file = FileAccess::open(output + ".tmp", FileAccess::WRITE);
		if (file.is_valid()) {
			file->store_string(JSON::stringify(response));
			file.unref();
			DirAccess::rename_absolute(output + ".tmp", output);
		}
	}
}


Dictionary ECSAnimationEditor::skeleton_ai_request(const Dictionary &request) {
    String op=request.get("operation","status"); Dictionary reply;
    auto finish=[&](bool ok,const String &error=String()) { reply["ok"]=ok; reply["operation"]=op; reply["revision"]=skeleton_ai_revision; if(!ok) { reply["error"]=error; } return reply; };
    auto project_path=[](const String &path) { return path.begins_with("res://") && !path.contains("..") && !path.contains("::") && !path.contains("\\") && !path.begins_with("res://.godot/"); };
    for(const char *name:{"revision","entity","parent","owner","track","key"}) {
        if(!request.has(name)) { continue; } Variant v=request[name];
        if((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) || !Math::is_finite(double(v)) || double(v)!=Math::floor(double(v)) || double(v)<-1 || double(v)>2147483647) { return finish(false,"Invalid integer: "+String(name)); }
    }
    Array entities=scene.is_valid()?scene->get_entities():Array();
    if(op=="capabilities") {
        reply["operations"]=PackedStringArray({"status","inspect","new","open","import_spine","add_bone","add_image","pose","bind","subdivide","paint_weights","rename","patch","component","tracks","edit_key","animation_manage","view","validate","export_presets","export","export_runtime","ui_tree","ui_action","skin","slot","wardrobe","animation","key","preview","events","save","screenshot","undo","redo"});
        Dictionary parameters;
        parameters["export_runtime"]="entity:skeleton index, directory:new output directory, fps:1..120 (default 30); sampled portable mesh clips";
        parameters["wardrobe"]="entity:skeleton index, group:skin folder, skin:full group/variant name; empty skin restores the default part";
        parameters["rename"]="entity:int, kind:bone|slot|skin, name:string, old_name:string (slot/skin)";
        parameters["paint_weights"]="entity:int, strokes:[{bone:entity index,center:Vector2,radius:positive number,strength:-1..1}]";
        parameters["component"]="entity:int, component:bone_2d|skeleton_2d|polygon_2d, fields:typed dictionary (merged)";
        parameters["patch"]="changes:[{entity:int,field:name|position|rotation|scale|shear|bone_2d|skeleton_2d|polygon_2d,value:typed value}]";
        parameters["bind"]="entity:image index, bones:optional array of bone entity indices";
        parameters["tracks"]="entity:skeleton index, animation:optional name";
        parameters["edit_key"]="entity:skeleton index, animation:name, track:int, key:int, action:delete|move|curve, time:seconds (move), interpolation:0..2 and transition:0.05..20 (curve)";
        parameters["animation_manage"]="entity:skeleton index, animation:name, action:rename|duplicate|delete, name:destination";
        reply["parameters"]=parameters;
        reply["component_fields"]=Dictionary(); Dictionary fields=reply["component_fields"];
        fields["skeleton_2d"]="bones,bind_poses,ik,skins,skin,slots,active_skins,placeholders,attachment_library";
        fields["polygon_2d"]="polygon,uv,triangles,texture,color,bones,weights,skeleton";
        reply["unavailable"]=PackedStringArray({"Bezier control handles","ghosting","pinned skins","desktop window management"});
        reply["weight_space"]="mesh_local"; reply["weight_influences"]=4; reply["paint_atomic"]=true;
        return finish(true);
    }
    if(op=="ui_tree") {
        Array controls; Vector<Node *> pending; pending.push_back(this); int visited=0;
        while(!pending.is_empty() && controls.size()<1024 && visited<8192) {
            Node *node=pending[pending.size()-1]; pending.remove_at(pending.size()-1); ++visited;
            for(int i=node->get_child_count()-1;i>=0;i--) { pending.push_back(node->get_child(i)); }
            Control *control=Object::cast_to<Control>(node); if(!control || !control->is_visible_in_tree()) { continue; }
            Dictionary item; item["path"]=String(get_path_to(node)); item["class"]=node->get_class(); item["tooltip"]=control->get_tooltip_text(); item["rect"]=ECSAIValue::encode(control->get_global_rect());
            if(Button *button=Object::cast_to<Button>(control)) { item["text"]=button->get_text(); item["disabled"]=button->is_disabled(); item["pressed"]=button->is_pressed(); }
            if(Label *label=Object::cast_to<Label>(control)) { item["text"]=label->get_text(); }
            if(LineEdit *line=Object::cast_to<LineEdit>(control)) { item["text"]=line->is_secret()?String():line->get_text(); item["editable"]=line->is_editable(); }
            if(SpinBox *spin=Object::cast_to<SpinBox>(control)) { item["value"]=spin->get_value(); item["min"]=spin->get_min(); item["max"]=spin->get_max(); }
            if(OptionButton *choice=Object::cast_to<OptionButton>(control)) { Array options; for(int i=0;i<choice->get_item_count();i++) { options.push_back(choice->get_item_text(i)); } item["items"]=options; item["selected"]=choice->get_selected(); }
            if(ItemList *list=Object::cast_to<ItemList>(control)) { Array entries; for(int i=0;i<list->get_item_count();i++) { Dictionary entry; entry["index"]=i; entry["text"]=list->get_item_text(i); entry["selected"]=list->is_selected(i); entries.push_back(entry); } item["items"]=entries; }
            if(TabBar *tabs=Object::cast_to<TabBar>(control)) { Array entries; for(int i=0;i<tabs->get_tab_count();i++) entries.push_back(tabs->get_tab_title(i));item["items"]=entries;item["selected"]=tabs->get_current_tab(); }
            if(TabContainer *tabs=Object::cast_to<TabContainer>(control)) { Array entries; for(int i=0;i<tabs->get_tab_count();i++) { entries.push_back(tabs->get_tab_title(i)); } item["items"]=entries; item["selected"]=tabs->get_current_tab(); }
            if(Tree *tree=Object::cast_to<Tree>(control)) { Array rows; Vector<TreeItem *> todo; if(tree->get_root()) { todo.push_back(tree->get_root()); } while(!todo.is_empty() && rows.size()<1024) { TreeItem *row=todo[0]; todo.remove_at(0); Dictionary entry; entry["index"]=rows.size(); entry["text"]=row->get_text(0); entry["selected"]=row->is_selected(0); entry["collapsed"]=row->is_collapsed(); rows.push_back(entry); for(TreeItem *child=row->get_first_child();child;child=child->get_next()) { todo.push_back(child); } } item["items"]=rows; }
            controls.push_back(item);
        }
        // PopupMenu is a Window rather than a Control. Include visible menus separately.
        Vector<Node *> menus; menus.push_back(this); int count=0;
        while(!menus.is_empty() && count++<8192) { Node *node=menus[menus.size()-1]; menus.remove_at(menus.size()-1); for(int i=0;i<node->get_child_count();i++) { menus.push_back(node->get_child(i)); }
            PopupMenu *menu=Object::cast_to<PopupMenu>(node); if(!menu || !menu->is_visible()) { continue; } Dictionary item; item["path"]=String(get_path_to(node)); item["class"]="PopupMenu"; Array entries;
            for(int i=0;i<menu->get_item_count();i++) { Dictionary entry; entry["index"]=i; entry["text"]=menu->get_item_text(i); entry["disabled"]=menu->is_item_disabled(i)||menu->is_item_separator(i); entries.push_back(entry); } item["items"]=entries; controls.push_back(item);
        }
        reply["controls"]=controls; reply["truncated"]=visited>=8192 || !pending.is_empty(); return finish(true);
    }
    if(op=="tracks") {
        int index=request.get("entity",-1); if(index<0 || index>=entities.size()) { return finish(false,"Invalid skeleton"); }
        Dictionary definition=Dictionary(entities[index]).get("animation",Dictionary()); String name=request.get("animation",String());
        Ref<Animation> clip=name.is_empty()?Ref<Animation>(definition.get("clip",Variant())):Ref<Animation>(Dictionary(definition.get("states",Dictionary())).get(name,Variant()));
        if(clip.is_null()) { return finish(false,"Unknown animation"); } Array tracks; PackedInt64Array targets=definition.get("targets",PackedInt64Array());
        for(int t=0;t<clip->get_track_count();t++) { Dictionary track; track["track"]=t; track["entity"]=t<targets.size()?targets[t]:index; track["field"]=clip->track_get_path(t).get_concatenated_subnames(); track["interpolation"]=int(clip->track_get_interpolation_type(t)); Array keys;
            for(int k=0;k<clip->track_get_key_count(t);k++) { Dictionary key; key["key"]=k; key["time"]=clip->track_get_key_time(t,k); key["transition"]=clip->track_get_key_transition(t,k); key["value"]=ECSAIValue::encode(clip->track_get_key_value(t,k)); keys.push_back(key); } track["keys"]=keys; tracks.push_back(track);
        } reply["tracks"]=tracks; reply["length"]=clip->get_length(); return finish(true);
    }
    if(op=="validate") {
        if(scene.is_null()) { return finish(false,"No skeletal project"); }
        Array meshes,warnings;
        for(int i=0;i<entities.size();i++) {
            Dictionary e=entities[i]; if(!e.has("polygon_2d")) { continue; }
            Dictionary mesh=e["polygon_2d"],row; PackedVector2Array points=mesh.get("polygon",PackedVector2Array());
            PackedFloat32Array weights=mesh.get("weights",PackedFloat32Array());
            row["entity"]=i; row["vertices"]=points.size(); row["bound"]=weights.size()==points.size()*4 && !weights.is_empty();
            double deviation=0; for(int v=0;v+3<weights.size();v+=4) { deviation=MAX(deviation,Math::abs(double(weights[v]+weights[v+1]+weights[v+2]+weights[v+3])-1.0)); }
            row["max_weight_sum_error"]=deviation; meshes.push_back(row);
            if(!bool(row["bound"])) { warnings.push_back("Unbound mesh: "+itos(i)); }
        }
        reply["meshes"]=meshes; reply["warnings"]=warnings;
        return finish(scene->instantiate().is_valid(),"Project cannot instantiate");
    }
    if(op=="export_presets") {
        Array presets; auto *exports=EditorExport::get_singleton();
        for(int i=0;i<exports->get_export_preset_count();i++) { Ref<EditorExportPreset> preset=exports->get_export_preset(i); Dictionary item; item["name"]=preset->get_name(); item["platform"]=preset->get_platform()->get_name(); presets.push_back(item); }
        reply["presets"]=presets; return finish(true);
    }
    if(op=="status" || op=="inspect") {
        reply["path"]=scene.is_valid()?scene->get_path():String(); reply["entity_count"]=entities.size(); reply["playing"]=playing; reply["time"]=time->get_value(); reply["mode"]=animation_mode?"animation":"setup";
        reply["timeline_fps"]=timeline_fps; reply["feedback"]=feedback->get_text(); reply["selected"]=target->get_selected_id(); reply["owner"]=owner->get_selected_id(); reply["animation"]=editing_state;
        if(owner->get_selected_id()>=0 && owner->get_selected_id()<entities.size()) {
            Dictionary definition=Dictionary(entities[owner->get_selected_id()]).get("animation",Dictionary());
            reply["animations"]=Dictionary(definition.get("states",Dictionary())).keys();
            Ref<Animation> selected=current_clip(); if(selected.is_valid()) { reply["length"]=selected->get_length(); reply["loop"]=selected->get_loop_mode()!=Animation::LOOP_NONE; }
        }
        if(op=="inspect") {
            int index=request.get("entity",-1); if(index<0 || index>=entities.size()) { return finish(false,"Invalid entity"); }
            reply["entity"]=ECSAIValue::encode(entities[index]); reply["editor_hidden"]=local_canvas->is_authoring_hidden(index); reply["editor_locked"]=local_canvas->is_authoring_locked(index);
        }
        Array summary; for(int i=0;i<entities.size();i++) { Dictionary e=entities[i],row; row["index"]=i; row["name"]=e.get("name",""); row["parent"]=e.get("parent",-1); row["kind"]=e.has("bone_2d")?"bone":e.has("polygon_2d")?"image":"skeleton"; summary.push_back(row); } reply["entities"]=summary;
        return finish(true);
    }
    if(!request.has("revision") || int64_t(request["revision"])!=skeleton_ai_revision) { return finish(false,"Stale or missing revision; query status again"); }
    if(op=="new") {
        if(scene.is_valid() && !bool(request.get("replace",false))) { return finish(false,"Use replace=true to replace the current workspace; save first"); }
        pending_project_operation=-1; project_action(0); return finish(true);
    }
    if(op=="open" || op=="import_spine") {
        if(scene.is_valid() && !bool(request.get("replace",false))) { return finish(false,"Use replace=true to replace the current workspace; save first"); }
        String path=request.get("path",""); if(!project_path(path)) { return finish(false,"Expected project resource path"); }
        Ref<ECSScene> loaded;
        if(op=="import_spine") { String report; loaded=ecs_import_spine_json(path,report); reply["report"]=report; }
        else { loaded=ResourceLoader::load(path,"ECSScene",ResourceLoader::CACHE_MODE_IGNORE); }
        if(loaded.is_null() || loaded->instantiate().is_null()) { return finish(false,"Invalid skeletal project"); }
        adopt_project(loaded); return finish(true);
    }
    if(scene.is_null()) { return finish(false,"No skeletal project"); }
    if(op=="export") {
        String name=request.get("preset",""); Ref<EditorExportPreset> preset; auto *exports=EditorExport::get_singleton();
        for(int i=0;i<exports->get_export_preset_count();i++) { if(exports->get_export_preset(i)->get_name()==name) { if(preset.is_valid()) { return finish(false,"Ambiguous preset name"); } preset=exports->get_export_preset(i); } }
        if(preset.is_null()) { return finish(false,"Unknown export preset; query export_presets"); }
        String error;
        if(!prepare_platform_export()) { return finish(false,"Cannot prepare export snapshot"); }
        ++skeleton_ai_revision;
        List<String> extensions=preset->get_platform()->get_binary_extensions(preset); if(extensions.is_empty()) { return finish(false,"No output format for preset"); }
        String directory=skeleton_ai_directory.path_join("export-"+itos(OS::get_singleton()->get_ticks_msec()));
        if(DirAccess::make_dir_recursive_absolute(directory)!=OK) { return finish(false,"Cannot create export directory"); }
        String path=ProjectSettings::get_singleton()->globalize_path(directory.path_join("Skeleton."+extensions.front()->get()));
        Error result=preset->get_platform()->export_project(preset,false,path);
        reply["path"]=path; reply["error_code"]=int(result); return finish(result==OK,result==OK?String():"Export failed; check platform templates and editor log");
    }
    if(op=="save") {
        String path=request.get("path",scene->get_path());
        if(!project_path(path) || path.get_extension()!="tres") { return finish(false,"Expected res://...tres"); }
        if(FileAccess::exists(path)) {
            if(path!=scene->get_path() && !bool(request.get("overwrite",false))) { return finish(false,"Destination exists; choose another path or set overwrite=true"); }
            if(DirAccess::copy_absolute(path,path+".ai-backup")!=OK) { return finish(false,"Cannot back up destination"); }
        }
        if(ResourceSaver::save(scene,path,ResourceSaver::FLAG_BUNDLE_RESOURCES)!=OK) { return finish(false,"Save failed"); }
        scene->set_path(path); ++skeleton_ai_revision; reply["path"]=path; return finish(true);
    }
    if(op=="undo" || op=="redo") {
        auto *manager=EditorUndoRedoManager::get_singleton(); UndoRedo *history=manager->get_history_undo_redo(manager->get_history_id_for_object(scene.ptr()));
        if(!history || !(op=="undo"?history->undo():history->redo())) { return finish(false,"No action available"); } edit_scene(scene,nullptr); return finish(true);
    }
    if(op=="screenshot") {
        String target=request.get("target",String());
        Viewport *capture=get_viewport();
        if(!target.is_empty()) {
            Window *dialog=target=="settings"?preferences_dialog:target=="export"?asset_export_dialog:target=="export_preview"?export_preview_dialog:nullptr;
            if(target=="tooltip") { for(int i=0;i<hierarchy->get_child_count();++i) { Window *window=Object::cast_to<Window>(hierarchy->get_child(i));if(window && window->is_visible()) { dialog=window;break; } } }
            if(!dialog || !dialog->is_visible()) { return finish(false,"Requested dialog is not visible"); }
            capture=dialog;
        }
        Ref<Image> image=capture->get_texture()->get_image(); if(image.is_null() || image->is_empty()) { return finish(false,"Screenshot unavailable (headless rendering)"); }
        String path=skeleton_ai_directory.path_join("preview.png"); if(image->save_png(path)!=OK) { return finish(false,"Screenshot save failed"); }
        reply["path"]=ProjectSettings::get_singleton()->globalize_path(path); return finish(true);
    }
    if(op=="skin" || op=="slot") {
        int entity=request.get("entity",-1);
        if(entity<0 || entity>=entities.size()) { return finish(false,"Invalid skeleton entity"); }
        Ref<ECSWorld> world=scene->instantiate(); if(world.is_null()) { return finish(false,"Invalid project"); }
        PackedInt64Array ids=world->query(PackedStringArray(),true);
        bool changed=op=="skin"?world->set_skeleton_skin(ids[entity],request.get("name",String())):world->set_skeleton_slot_attachment(ids[entity],request.get("slot",String()),request.get("attachment",String()));
        if(!changed) { return finish(false,"Unknown skin, slot or attachment; no change applied"); }
        Ref<ECSScene> snapshot;snapshot.instantiate();if(!snapshot->capture(world)) { return finish(false,"Cannot serialize skeleton references"); }
        Array after=entities.duplicate(true);Dictionary root=after[entity];root["skeleton_2d"]=Dictionary(snapshot->get_entities()[entity])["skeleton_2d"];
        commit_entities(after,String(U"AI 切换皮肤与附件"));select_target(entity);
        Array visible;for(int i=0;i<ids.size();i++) { if(Dictionary(entities[i]).has("polygon_2d") && world->is_skeleton_attachment_visible(ids[i])) { visible.push_back(i); } }reply["visible_attachments"]=visible;return finish(true);
    }
    if(op=="ui_action") {
        String path=request.get("path",String()),command=request.get("action",String());
        NodePath node_path(path); if(path.is_empty() || node_path.is_absolute() || path.contains("..") || node_path.get_subname_count()) { return finish(false,"Expected local path returned by ui_tree"); }
        Node *node=get_node_or_null(node_path); if(!node || (node!=this && !is_ancestor_of(node))) { return finish(false,"Control no longer exists; query ui_tree"); }
        if(command=="menu_select") {
            PopupMenu *menu=Object::cast_to<PopupMenu>(node); Variant index=request.get("index",-1);
            if(!menu || !menu->is_visible() || (index.get_type()!=Variant::INT && index.get_type()!=Variant::FLOAT) || !Math::is_finite(double(index)) || double(index)!=Math::floor(double(index)) || double(index)<0 || double(index)>=menu->get_item_count() || menu->is_item_disabled(int(index)) || menu->is_item_separator(int(index))) { return finish(false,"Invalid visible menu item"); }
            int id=menu->get_item_id(int(index)); menu->hide(); menu->emit_signal("id_pressed",id);
        } else {
            Control *control=Object::cast_to<Control>(node); if(!control || !control->is_visible_in_tree()) { return finish(false,"Control is not visible"); }
            if(command=="press") {
                Button *button=Object::cast_to<Button>(control); if(!button || button->is_disabled()) { return finish(false,"Button unavailable"); }
                if(MenuButton *menu=Object::cast_to<MenuButton>(button)) { menu->show_popup(); }
                else if(OptionButton *choice=Object::cast_to<OptionButton>(button)) { choice->show_popup(); }
                else { if(button->is_toggle_mode()) { button->set_pressed(!button->is_pressed()); } button->emit_signal("pressed"); }
            } else if(command=="text") {
                LineEdit *line=Object::cast_to<LineEdit>(control); Variant text=request.get("text",Variant()); if(!line || !line->is_editable() || text.get_type()!=Variant::STRING || String(text).length()>4096) { return finish(false,"Expected editable text field and text"); }
                line->set_text(text); line->emit_signal("text_changed",String(text)); if(bool(request.get("submit",false))) { line->emit_signal("text_submitted",String(text)); }
            } else if(command=="number") {
                SpinBox *spin=Object::cast_to<SpinBox>(control); Variant value=request.get("value",Variant()); if(!spin || !spin->is_editable() || (value.get_type()!=Variant::INT && value.get_type()!=Variant::FLOAT) || !Math::is_finite(double(value)) || double(value)<spin->get_min() || double(value)>spin->get_max()) { return finish(false,"Expected editable number within bounds"); } spin->set_value(value);
            } else if(command=="item_select" || command=="collapse" || command=="tree_button" || command=="click_item" || command=="hover_item" || command=="activate_item") {
                Variant index=request.get("index",-1); if((index.get_type()!=Variant::INT && index.get_type()!=Variant::FLOAT) || !Math::is_finite(double(index)) || double(index)!=Math::floor(double(index)) || double(index)<0 || double(index)>100000) { return finish(false,"Invalid item index"); }
                if(Tree *tree=Object::cast_to<Tree>(control)) {
                    Vector<TreeItem *> todo; if(tree->get_root()) { todo.push_back(tree->get_root()); } TreeItem *selected=nullptr; int current=0;
                    while(!todo.is_empty() && current<=int(index)) { TreeItem *row=todo[0]; todo.remove_at(0); if(current++==int(index)) { selected=row; break; } for(TreeItem *child=row->get_first_child();child;child=child->get_next()) { todo.push_back(child); } }
                    if(!selected) { return finish(false,"Tree item no longer exists"); }
                    if(command=="hover_item") {
                        for(TreeItem *parent=selected->get_parent();parent;parent=parent->get_parent()) { parent->set_collapsed(false); }
                        tree->scroll_to_item(selected,true);
                        Rect2 rect=tree->get_item_rect(selected,0); Vector2 point=rect.get_center();
                        Ref<InputEventMouseMotion> event;event.instantiate();event->set_position(tree->get_global_transform_with_canvas().xform(point));event->set_global_position(event->get_position());
                        tree->get_viewport()->warp_mouse(event->get_position());
                        tree->get_viewport()->push_input(event,true);tree->get_viewport()->show_tooltip(tree); reply["tooltip"]=tree->get_tooltip(point); reply["point"]=ECSAIValue::encode(point);
                    } else if(command=="activate_item") {
                        selected->select(0); tree->emit_signal("item_activated");
                    } else if(command=="click_item") {
                        for(TreeItem *parent=selected->get_parent();parent;parent=parent->get_parent()) { parent->set_collapsed(false); }
                        tree->scroll_to_item(selected,true);
                        Rect2 rect=tree->get_item_rect(selected,0);
                        Vector2 point=rect.get_center(); point.x=MAX(point.x,rect.get_end().x-24);
                        Ref<InputEventMouseButton> event; event.instantiate(); event->set_button_index(MouseButton::LEFT); event->set_position(point); event->set_pressed(true);
                        tree->gui_input(event); event->set_pressed(false); tree->gui_input(event);
                    } else if(command=="tree_button") {
                        Variant column=request.get("column",-1),button=request.get("button",0);
                        for(const Variant &v:{column,button}) { if((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) || !Math::is_finite(double(v)) || double(v)!=Math::floor(double(v)) || double(v)<0 || double(v)>1000) { return finish(false,"Invalid tree button"); } }
                        if(int(column)>=tree->get_columns() || int(button)>=selected->get_button_count(column) || selected->is_button_disabled(column,button)) { return finish(false,"Tree button unavailable"); }
                        tree->emit_signal("button_clicked",selected,int(column),selected->get_button_id(column,button),int(MouseButton::LEFT));
                    } else if(command=="collapse") { if(request.get("collapsed",Variant()).get_type()!=Variant::BOOL) { return finish(false,"Expected collapsed boolean"); } selected->set_collapsed(request["collapsed"]); }
                    else { if(!selected->is_selectable(0)) { return finish(false,"Tree item is not selectable"); } selected->select(0); tree->emit_signal("item_selected"); }
                } else if(command=="item_select") {
                    if(ItemList *list=Object::cast_to<ItemList>(control)) { if(int(index)>=list->get_item_count() || list->is_item_disabled(index) || !list->is_item_selectable(index)) { return finish(false,"Item unavailable"); } list->select(index); list->emit_signal("item_selected",int(index)); }
                    else if(TabBar *tabs=Object::cast_to<TabBar>(control)) { if(int(index)>=tabs->get_tab_count() || tabs->is_tab_disabled(index) || tabs->is_tab_hidden(index)) return finish(false,"Tab unavailable");tabs->set_current_tab(index); }
                    else if(TabContainer *tabs=Object::cast_to<TabContainer>(control)) { if(int(index)>=tabs->get_tab_count() || tabs->is_tab_disabled(index) || tabs->is_tab_hidden(index)) { return finish(false,"Tab unavailable"); } tabs->set_current_tab(index); }
                    else { return finish(false,"Expected Tree, ItemList or TabContainer"); }
                } else { return finish(false,"Expected Tree"); }
            } else if(command=="select") {
                OptionButton *choice=Object::cast_to<OptionButton>(control); Variant index=request.get("index",-1); if(!choice || choice->is_disabled() || (index.get_type()!=Variant::INT && index.get_type()!=Variant::FLOAT) || !Math::is_finite(double(index)) || double(index)!=Math::floor(double(index)) || double(index)<0 || double(index)>=choice->get_item_count() || choice->is_item_disabled(int(index))) { return finish(false,"Invalid selectable option"); } choice->select(index); choice->emit_signal("item_selected",int(index));
            } else { return finish(false,"Expected press, text, number, select or menu_select"); }
        }
        ++skeleton_ai_revision; reply["dispatched"]=true; reply["note"]="UI action dispatched; inspect status/ui_tree after deferred work to verify outcome"; return finish(true);
    }
    if(op=="view") {
        String mode=request.get("mode",animation_mode?"animation":"setup"); if(mode!="animation" && mode!="setup") { return finish(false,"Expected setup or animation mode"); }
        if(request.has("grid") && request["grid"].get_type()!=Variant::BOOL) { return finish(false,"Expected boolean grid"); }
        set_mode(mode=="animation"?1:0); if(request.has("grid")) { local_canvas->set_grid_visible(request["grid"]); } ++skeleton_ai_revision; return finish(true);
    }
    if(op=="export_runtime") {
        Variant rate=request.get("fps",30);
        if((rate.get_type()!=Variant::INT && rate.get_type()!=Variant::FLOAT) || !Math::is_finite(double(rate)) || double(rate)!=Math::floor(double(rate)) || double(rate)<1 || double(rate)>120) { return finish(false,"Expected integer fps 1..120"); }
        reply=export_skeleton_runtime(scene,request.get("entity",0),request.get("directory",String()),int(rate));
        return finish(reply.get("ok",false),reply.get("error",String()));
    }
    if(op=="wardrobe") {
        int rig=request.get("entity",-1); String group=request.get("group",String()),skin=request.get("skin",String());
        if(!apply_wardrobe_skin(rig,group,skin)) { return finish(false,"Expected an existing skin group and matching group/skin name; empty skin restores the default part"); }
        Dictionary definition=Dictionary(scene->get_entities()[rig])["skeleton_2d"]; reply["active_skins"]=definition.get("active_skins",PackedStringArray());
        Ref<ECSWorld> world=scene->instantiate(); PackedInt64Array ids=world->query(PackedStringArray(),true); Array visible;
        for(int i=0;i<ids.size();i++) { if(Dictionary(scene->get_entities()[i]).has("polygon_2d") && world->is_skeleton_attachment_visible(ids[i])) { visible.push_back(i); } }
        reply["visible_attachments"]=visible;
        return finish(true);
    }
    if(op=="component") {
        int index=request.get("entity",-1); String component=request.get("component",String()); bool valid=true;
        Variant fields=ECSAIValue::decode(request.get("fields",Variant()),valid);
        if(index<0 || index>=entities.size() || !PackedStringArray({"bone_2d","skeleton_2d","polygon_2d"}).has(component) || !Dictionary(entities[index]).has(component) || !valid || fields.get_type()!=Variant::DICTIONARY) { return finish(false,"Expected existing component and typed fields dictionary"); }
        Array after=entities.duplicate(true); Dictionary e=after[index],data=e[component]; data.merge(fields,true);
        Ref<ECSScene> check; check.instantiate(); check->set_entities(after); if(check->instantiate().is_null()) { return finish(false,"Invalid component or references; no changes applied"); }
        commit_entities(after,String(U"AI 编辑组件配置")); return finish(true);
    }
    if(op=="animation_manage" || op=="edit_key") {
        int index=request.get("entity",-1); if(index<0 || index>=entities.size()) { return finish(false,"Invalid skeleton"); }
        Array after=entities.duplicate(true); Dictionary e=after[index],definition=e.get("animation",Dictionary()),named=definition.get("states",Dictionary()); String name=request.get("animation",String()),command=request.get("action",String());
        Ref<Animation> source=name.is_empty()?Ref<Animation>(definition.get("clip",Variant())):Ref<Animation>(named.get(name,Variant()));
        if(source.is_null()) { return finish(false,"Unknown animation"); }
        if(op=="animation_manage") {
            String destination=String(request.get("name",String())).strip_edges();
            if(command=="rename" || command=="duplicate") {
                if(destination.is_empty() || destination.length()>128 || named.has(destination) || (command=="rename" && name.is_empty())) { return finish(false,"Invalid or duplicate animation name"); }
                named[destination]=source->duplicate(true); if(command=="rename") { named.erase(name); if(String(definition.get("state",String()))==name) { definition["state"]=destination; definition["clip"]=named[destination]; } }
            } else if(command=="delete") { if(name.is_empty()) { return finish(false,"Default animation cannot be deleted"); } named.erase(name); if(String(definition.get("state",String()))==name) { definition["state"]=String(); } }
            else { return finish(false,"Expected rename, duplicate or delete"); }
        } else {
            if(timeline_locked) { return finish(false,"Timeline is locked"); }
            int track=request.get("track",-1),key=request.get("key",-1); if(track<0 || track>=source->get_track_count() || key<0 || key>=source->track_get_key_count(track)) { return finish(false,"Invalid track/key"); }
            Ref<Animation> clip=source->duplicate(true);
            if(command=="delete") { clip->track_remove_key(track,key); }
            else if(command=="move") {
                Variant at=request.get("time",Variant()); if((at.get_type()!=Variant::INT && at.get_type()!=Variant::FLOAT) || !Math::is_finite(double(at)) || double(at)<0 || double(at)>3600) { return finish(false,"Invalid key time"); }
                int collision=clip->track_find_key(track,at,Animation::FIND_MODE_APPROX); if(collision>=0 && collision!=key) { return finish(false,"Destination key already exists"); }
                Variant value=clip->track_get_key_value(track,key); double transition=clip->track_get_key_transition(track,key); clip->track_remove_key(track,key); clip->track_insert_key(track,at,value,transition); clip->set_length(MAX(clip->get_length(),double(at)));
            } else if(command=="curve") {
                Variant interpolation=request.get("interpolation",1),ease=request.get("transition",1.0); String field=clip->track_get_path(track).get_concatenated_subnames();
                if((interpolation.get_type()!=Variant::INT && interpolation.get_type()!=Variant::FLOAT) || !Math::is_finite(double(interpolation)) || double(interpolation)!=Math::floor(double(interpolation)) || double(interpolation)<0 || double(interpolation)>2 || (ease.get_type()!=Variant::INT && ease.get_type()!=Variant::FLOAT) || !Math::is_finite(double(ease)) || double(ease)<.05 || double(ease)>20 || field.begins_with("event:") || field.ends_with(":attachment") || field.ends_with(":z_index")) { return finish(false,"Invalid curve or discrete channel"); }
                clip->track_set_interpolation_type(track,Animation::InterpolationType(int(interpolation))); clip->track_set_key_transition(track,key,ease);
            } else { return finish(false,"Expected delete, move or curve"); }
            if(!name.is_empty()) { named[name]=clip; }
            String active=definition.get("state",String()); if(name.is_empty() || active==name) { definition["clip"]=clip; if(!active.is_empty()) { named[active]=clip; } }
        }
        definition["states"]=named; e["animation"]=definition;
        Ref<ECSScene> check; check.instantiate(); check->set_entities(after); if(check->instantiate().is_null()) { return finish(false,"Invalid animation edit; no changes applied"); }
        commit_entities(after,String(U"AI 编辑动画与摄影表")); return finish(true);
    }
    if(op=="rename") {
        int index=request.get("entity",-1); String kind=request.get("kind","bone"),old=request.get("old_name",""),name=String(request.get("name","")).strip_edges();
        if(index<0 || index>=entities.size() || name.is_empty() || name.length()>128 || name.contains("\n")) { return finish(false,"Invalid entity or name"); }
        Array after=entities.duplicate(true); Dictionary e=after[index];
        if(kind=="bone") {
            if(!e.has("bone_2d")) { return finish(false,"Entity is not a bone"); }
            for(int i=0;i<after.size();i++) { if(i!=index && Dictionary(after[i]).get("name",String())==Variant(name)) { return finish(false,"Name already exists"); } }
            e["name"]=name;
        } else if(kind=="skin" || kind=="slot") {
            if(!e.has("skeleton_2d") || old.is_empty() || old==name) { return finish(false,"Expected skeleton and distinct old_name/name"); }
            Dictionary rig=e["skeleton_2d"],skins=rig.get("skins",Dictionary());
            if(kind=="skin") {
                if(old=="default" || !skins.has(old) || skins.has(name)) { return finish(false,"Unknown, reserved or duplicate skin"); }
                skins[name]=skins[old]; skins.erase(old); rig["skins"]=skins;
                if(String(rig.get("skin","default"))==old) { rig["skin"]=name; }
                PackedStringArray active=rig.get("active_skins",PackedStringArray()); for(int i=0;i<active.size();i++) { if(active[i]==old) { active.set(i,name); } } rig["active_skins"]=active;
            } else {
                Array slots=rig.get("slots",Array()); int found=-1;
                for(int i=0;i<slots.size();i++) { String n=Dictionary(slots[i]).get("name",""); if(n==name) { return finish(false,"Duplicate slot"); } if(n==old) { found=i; } }
                if(found<0) { return finish(false,"Unknown slot"); } Dictionary slot=slots[found]; slot["name"]=name;
                for(const Variant &key:skins.keys()) { Dictionary skin=skins[key]; if(skin.has(old)) { skin[name]=skin[old]; skin.erase(old); } }
                Dictionary placeholders=rig.get("placeholders",Dictionary()); if(placeholders.has(old)) { placeholders[name]=placeholders[old]; placeholders.erase(old); } rig["placeholders"]=placeholders;
                Dictionary animation=e.get("animation",Dictionary());
                auto rename_clip=[&](const Ref<Animation> &source) { if(source.is_null()) { return source; } Ref<Animation> clip=source->duplicate(true); String prefix="slot:"+old.uri_encode()+":";
                    for(int t=0;t<clip->get_track_count();t++) { String field=clip->track_get_path(t).get_concatenated_subnames(); if(field.begins_with(prefix)) { clip->track_set_path(t,NodePath(".:slot:"+name.uri_encode()+":"+field.substr(prefix.length()))); } } return clip; };
                if(animation.has("clip")) { animation["clip"]=rename_clip(animation["clip"]); }
                if(animation.has("secondary")) { Dictionary secondary=animation["secondary"]; if(secondary.has("clip")) { secondary["clip"]=rename_clip(secondary["clip"]); } }
                Dictionary states=animation.get("states",Dictionary()); for(const Variant &key:states.keys()) { states[key]=rename_clip(states[key]); } String active=animation.get("state",String()); if(!active.is_empty() && states.has(active)) { animation["clip"]=states[active]; } if(!animation.is_empty()) { e["animation"]=animation; }
            }
        } else { return finish(false,"Expected bone, slot or skin kind"); }
        Ref<ECSScene> check; check.instantiate(); check->set_entities(after); if(check->instantiate().is_null()) { return finish(false,"Invalid rename; no changes applied"); }
        commit_entities(after,String(U"AI 重命名骨骼、插槽或皮肤")); return finish(true);
    }
    if(op=="paint_weights") {
        int index=request.get("entity",-1);
        if(index<0 || index>=entities.size() || !Dictionary(entities[index]).has("polygon_2d") || request.get("strokes",Variant()).get_type()!=Variant::ARRAY) { return finish(false,"Expected mesh entity and strokes array"); }
        Array strokes=request["strokes"]; if(strokes.is_empty() || strokes.size()>256) { return finish(false,"Expected 1..256 strokes"); }
        Dictionary mesh=Dictionary(entities[index])["polygon_2d"]; mesh=mesh.duplicate(true);
        int rig=mesh.get("skeleton",-1); if(rig<0 || rig>=entities.size()) { return finish(false,"Bind the mesh first"); }
        PackedInt64Array bones=Dictionary(Dictionary(entities[rig]).get("skeleton_2d",Dictionary())).get("bones",PackedInt64Array());
        PackedVector2Array points=mesh.get("polygon",PackedVector2Array());
        if(PackedFloat32Array(mesh.get("weights",PackedFloat32Array())).size()!=points.size()*4) { return finish(false,"Mesh has no valid weights"); }
        for(const Variant &item:strokes) {
            if(item.get_type()!=Variant::DICTIONARY) { return finish(false,"Invalid stroke"); }
            Dictionary stroke=item; bool valid=true; Variant center=ECSAIValue::decode(stroke.get("center",Variant()),valid);
            Variant bone=stroke.get("bone",-1),radius=stroke.get("radius",0),strength=stroke.get("strength",0);
            for(const Variant &number:{bone,radius,strength}) { if((number.get_type()!=Variant::INT && number.get_type()!=Variant::FLOAT) || !Math::is_finite(double(number))) { return finish(false,"Non-finite or non-numeric stroke parameter"); } }
            if(!valid || center.get_type()!=Variant::VECTOR2 || !Vector2(center).is_finite() || double(bone)!=Math::floor(double(bone)) || double(bone)<0 || double(bone)>=entities.size() || bones.find(int64_t(bone))<0 || double(radius)<=0 || double(radius)>100000 || Math::abs(double(strength))>1) { return finish(false,"Invalid stroke center, bone, radius or strength"); }
            mesh=ECSUICanvasEditor::paint_weights(mesh,Transform3D(),center,radius,bones.find(int64_t(bone)),strength);
        }
        Array after=entities.duplicate(true); Dictionary e=after[index]; e["polygon_2d"]=mesh;
        Ref<ECSScene> check; check.instantiate(); check->set_entities(after); if(check->instantiate().is_null()) { return finish(false,"Invalid painted mesh; no changes applied"); }
        set_mode(0); commit_entities(after,String(U"AI 批量绘制蒙皮权重")); reply["strokes"]=strokes.size(); return finish(true);
    }
    if(op=="pose") {
        int index=request.get("entity",-1); String field=request.get("field","position");
        bool valid=true; Variant value=ECSAIValue::decode(request.get("value",Variant()),valid);
        if(!valid || value.get_type()!=Variant::VECTOR3 || !Vector3(value).is_finite()) { return finish(false,"Expected a finite Vector3"); }
        for(const char *flag:{"compensate_bones","compensate_images"}) { if(request.has(flag) && request[flag].get_type()!=Variant::BOOL) { return finish(false,"Compensation flags must be booleans"); } }
        String error; Array after=compensate_pose(entities,index,field,value,request.get("compensate_bones",false),request.get("compensate_images",false),error);
        if(after.is_empty()) { return finish(false,error); }
        Ref<ECSScene> check; check.instantiate(); check->set_entities(after);
        if(check->instantiate().is_null()) { return finish(false,"Invalid compensated pose; no changes applied"); }
        set_mode(0); commit_entities(after,String(U"AI 编辑姿态与补偿")); select_target(index); return finish(true);
    }
    if(op=="patch") {
        if(request.get("changes",Variant()).get_type()!=Variant::ARRAY) { return finish(false,"Expected changes array"); }
        Array changes=request["changes"],after=entities.duplicate(true); if(changes.is_empty() || changes.size()>512) { return finish(false,"Expected 1..512 changes"); }
        for(const Variant &entry:changes) {
            if(entry.get_type()!=Variant::DICTIONARY) { return finish(false,"Invalid patch entry"); }
            Dictionary change=entry; Variant index=change.get("entity",-1);
            if((index.get_type()!=Variant::FLOAT && index.get_type()!=Variant::INT) || !Math::is_finite(double(index)) || double(index)!=Math::floor(double(index)) || double(index)<0 || double(index)>=after.size()) { return finish(false,"Invalid patch entity"); }
            String field=change.get("field","");
            if(!PackedStringArray({"name","position","rotation","scale","shear","bone_2d","skeleton_2d","polygon_2d"}).has(field)) { return finish(false,"Unsupported skeletal field"); }
            bool ok=true; Variant value=ECSAIValue::decode(change.get("value",Variant()),ok); if(!ok) { return finish(false,"Invalid typed value"); }
            Dictionary e=after[int(index)]; e[field]=value;
        }
        Ref<ECSScene> check; check.instantiate(); check->set_entities(after); if(check->instantiate().is_null()) { return finish(false,"Invalid skeleton/mesh/weights; no changes applied"); }
        commit_entities(after,String(U"AI 编辑骨骼工程")); return finish(true);
    }
    if(op=="events") {
        int entity=request.get("entity",-1),rig=find_rig(entity); if(rig<0) { return finish(false,"Entity has no skeleton"); }
        Dictionary definition=Dictionary(entities[rig]).get("animation",Dictionary()); String name=request.get("animation",String());
        Ref<Animation> clip=name.is_empty()?Ref<Animation>(definition.get("clip",Variant())):Ref<Animation>(Dictionary(definition.get("states",Dictionary())).get(name,Variant()));
        double from=request.get("time",0.0),to=request.get("end_time",0.0);
        if(clip.is_null() || !Math::is_finite(from) || !Math::is_finite(to) || from<0 || to<0 || from>3600 || to>3600) { return finish(false,"Invalid animation or event interval"); }
        reply["events"]=ECSAIValue::encode(ECSWorld::sample_animation_events(clip,from,to,false)); return finish(true);
    }
    if(op=="animation") {
        int index=request.get("entity",-1), rig=find_rig(index);
        if(rig<0) { return finish(false,"Entity has no skeleton"); }
        Variant length=request.get("length",Variant()), loop=request.get("loop",Variant()), create=request.get("create_animation",false);
        if((length.get_type()!=Variant::INT && length.get_type()!=Variant::FLOAT) || loop.get_type()!=Variant::BOOL || create.get_type()!=Variant::BOOL) { return finish(false,"Expected numeric length and boolean loop/create_animation"); }
        owner->select(rig); set_mode(1); String error;
        if(!configure_animation(request.get("animation",String()),length,loop,create,error)) { return finish(false,error); }
        reply["animation"]=editing_state; reply["length"]=current_clip()->get_length(); reply["loop"]=current_clip()->get_loop_mode()!=Animation::LOOP_NONE; return finish(true);
    }
    if(!PackedStringArray({"select","add_bone","add_image","bind","subdivide","key","preview"}).has(op)) { return finish(false,"Unknown skeletal operation"); }
    int entity=request.get("entity",target->get_selected_id());
    if(entity<0 || entity>=entities.size()) { return finish(false,"Invalid entity"); }
    select_target(entity); int rig=find_rig(entity);
    if(op=="select") { ++skeleton_ai_revision; return finish(true); }
    if(op=="add_bone") {
        if(rig<0) { return finish(false,"Select a bone or skeleton first"); }
        String name=request.get("name","Bone"); double length=request.get("length",100.0);
        if(name.is_empty() || !Math::is_finite(length) || length<=0 || length>100000) { return finish(false,"Invalid bone name or length"); }
        if(entity!=rig && !Dictionary(entities[entity]).has("bone_2d")) { return finish(false,"Parent must be a bone or skeleton"); }
        Array after=entities.duplicate(true); Dictionary e,bone,root=after[rig],definition=root["skeleton_2d"];
        bone["length"]=length; e["bone_2d"]=bone; e["name"]=name; e["parent"]=entity;
        e["position"]=entity==rig?Vector3():Vector3(double(Dictionary(Dictionary(entities[entity])["bone_2d"]).get("length",100.0)),0,0);
        PackedInt64Array bones=definition["bones"]; bones.push_back(after.size()); definition["bones"]=bones; definition["bind_poses"]=Array(); after.push_back(e);
        Ref<ECSScene> check; check.instantiate(); check->set_entities(after); if(check->instantiate().is_null()) { return finish(false,"Bone could not be added"); }
        commit_entities(after,String(U"AI 添加骨骼")); select_target(after.size()-1); reply["entity"]=after.size()-1; return finish(true);
    }
    if(op=="add_image") {
        String path=request.get("path",""); if(!project_path(path) || !FileAccess::exists(path)) { return finish(false,"Expected existing project image"); }
        if(!PackedStringArray({"png","jpg","jpeg","webp"}).has(path.get_extension().to_lower())) { return finish(false,"Unsupported image format"); }
        set_mode(0); project_operation=7; project_file_selected(path);
        if(scene->get_entities().size()!=entities.size()+1) { return finish(false,"Image import failed"); }
        reply["entity"]=entities.size(); return finish(true);
    }
    if(op=="bind" || op=="subdivide") {
        if(!Dictionary(entities[entity]).has("polygon_2d")) { return finish(false,"Select an image"); }
        set_mode(0);
        if(op=="subdivide") { Dictionary divided=subdivide_mesh(Dictionary(entities[entity])["polygon_2d"]); if(divided.is_empty()) { return finish(false,"Subdivision limit reached"); } mesh_changed(entity,divided); return finish(true); }
        if(rig<0) { return finish(false,"Image needs a skeleton ancestor"); }
        PackedInt64Array bones=Dictionary(Dictionary(entities[rig])["skeleton_2d"])["bones"],chosen;
        if(request.has("bones")) {
            if(request["bones"].get_type()!=Variant::ARRAY) { return finish(false,"Expected bone entity array"); }
            Array requested=request["bones"]; for(const Variant &v:requested) {
                if((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) || !Math::is_finite(double(v)) || double(v)!=Math::floor(double(v)) || double(v)<0 || double(v)>=entities.size() || bones.find(int64_t(v))<0 || chosen.has(int64_t(v))) { return finish(false,"Invalid, duplicate or foreign bone"); } chosen.push_back(int64_t(v));
            }
        } else { chosen=bones; }
        if(chosen.is_empty()) { return finish(false,"Select at least one bone"); }
        image_selection=entity; binding_rig=rig; binding_bones->clear();
        for(int i=0;i<bones.size();i++) { binding_bones->add_item(itos(i)); binding_bones->set_item_metadata(i,i); if(chosen.has(bones[i])) { binding_bones->select(i,false); } }
        apply_binding(); return finish(Dictionary(Dictionary(scene->get_entities()[entity])["polygon_2d"]).has("weights"),"Binding failed");
    }
    if(op=="key" || op=="preview") {
        if(rig<0) { return finish(false,"Entity has no skeleton"); }
        double at=request.get("time",0.0); if(!Math::is_finite(at) || at<0 || at>3600) { return finish(false,"Time must be within 0..3600 seconds"); }
        String animation_name=request.get("animation",String());
        Dictionary source=Dictionary(entities[rig]).get("animation",Dictionary());
        Dictionary source_states=source.get("states",Dictionary());
        if(!animation_name.is_empty() && !source_states.has(animation_name) && (op=="preview" || !bool(request.get("create_animation",false)))) { return finish(false,"Unknown animation; use create_animation=true with key to create it"); }
        owner->select(rig); set_mode(1); editing_state=animation_name; refresh_tracks();
        if(op=="preview") {
            if(current_clip().is_null()) { return finish(false,"No animation"); }
            loop_start->set_value(0); loop_end->set_value(current_clip()->get_length()*timeline_fps); loop_playback->set_pressed_no_signal(current_clip()->get_loop_mode()!=Animation::LOOP_NONE);
            seek(at); playing=bool(request.get("playing",false)); playback_time=at; ++skeleton_ai_revision;
            Dictionary pose; for(const char *field:{"position","rotation","scale","shear"}) { pose[field]=ECSAIValue::encode(local_canvas->get_authoring_vector(entity,field)); } reply["pose"]=pose; Ref<ECSWorld> snapshot=scene->instantiate(); if(snapshot.is_valid()) { auto ids=snapshot->query(PackedStringArray(),true); snapshot->travel_animation(ids[rig],animation_name,0); Dictionary anim=snapshot->get_animation(ids[rig]); if(animation_name.is_empty()) { anim=Dictionary(Dictionary(entities[rig])["animation"]).duplicate(true); auto ts=PackedInt64Array(anim.get("targets",PackedInt64Array())); for(int i=0;i<ts.size();i++) { ts.set(i,ids[ts[i]]); } anim["targets"]=ts; } anim["time"]=at; anim["playing"]=true; if(snapshot->set_animation(ids[rig],anim)) { snapshot->advance_animation_preview(0); reply["slots"]=ECSAIValue::encode(snapshot->get_skeleton_2d(ids[rig]).get("slots",Array())); } } return finish(true);
        }
        String field=request.get("field","rotation"); int field_index=field=="position"?0:field=="rotation"?1:field=="scale"?2:field=="shear"?3:-1;
        bool ok=true; Variant value=ECSAIValue::decode(request.get("value",Variant()),ok);
        if(field=="event" && ok && value.get_type()==Variant::DICTIONARY) { field="event:"+String(Dictionary(value).get("name",String())).uri_encode(); }
        if(field.get_slice(":",0)=="event" && value.get_type()==Variant::DICTIONARY) { Dictionary event=value; Variant integer=event.get("int",0); if((integer.get_type()!=Variant::INT && integer.get_type()!=Variant::FLOAT) || !Math::is_finite(double(integer)) || double(integer)!=Math::floor(double(integer)) || Math::abs(double(integer))>2147483647) { return finish(false,"Invalid integer event payload"); } event["int"]=int64_t(integer); }
        if(field.ends_with(":z_index") && value.get_type()==Variant::FLOAT && Math::is_finite(double(value)) && double(value)==Math::floor(double(value)) && Math::abs(double(value))<=4096) { value=int64_t(value); }
        bool channel=field.get_slice(":",0)=="event" || field.begins_with("slot:");
        if(!ok || (!channel && (field_index<0 || value.get_type()!=Variant::VECTOR3 || !Vector3(value).is_finite()))) { return finish(false,"Expected a supported animation field and typed value"); }
        Ref<ECSWorld> validation; PackedInt64Array validation_ids;
        if(channel) { validation=scene->instantiate(); if(validation.is_null()) { return finish(false,"Invalid scene"); } validation_ids=validation->query(PackedStringArray(),true); if(!validation->valid_skeletal_animation_value(validation_ids[entity],field,value)) { return finish(false,"Invalid slot or event key value"); } }
        if(timeline_locked) { return finish(false,"Timeline is locked"); }
        Dictionary definition=source.duplicate(true),named;
        Ref<Animation> base=source.get("clip",Variant());
        if(base.is_null()) { base.instantiate(); base->set_length(4); base->set_loop_mode(Animation::LOOP_LINEAR); }
        else { base=base->duplicate(true); }
        for(const Variant &name:source_states.keys()) {
            Ref<Animation> original=source_states[name];
            if(original.is_null()) { return finish(false,"Invalid named animation"); }
            named[name]=original->duplicate(true);
        }
        if(!animation_name.is_empty() && !named.has(animation_name)) { named[animation_name]=base->duplicate(true); }
        PackedInt64Array targets=definition.get("targets",PackedInt64Array());
        if(targets.is_empty()) { targets.resize(base->get_track_count()); targets.fill(rig); }
        if(targets.size()!=base->get_track_count()) { return finish(false,"Invalid animation target layout"); }
        int track=-1;
        for(int t=0;t<base->get_track_count();t++) {
            if(base->track_get_type(t)==Animation::TYPE_VALUE && targets[t]==entity && base->track_get_path(t).get_concatenated_subnames()==field) { track=t; break; }
        }
        if(track<0) {
            Variant rest=field.get_slice(":",0)=="event"?Variant(Dictionary()):field.begins_with("slot:")?validation->get_skeleton_slot_value(validation_ids[entity],field):Dictionary(entities[entity]).get(field,field=="scale"?Vector3(1,1,1):Vector3());
            auto append=[&](const Ref<Animation> &clip) { int t=clip->add_track(Animation::TYPE_VALUE); clip->track_set_path(t,NodePath(".:"+field)); if(field.get_slice(":",0)=="event" || field.ends_with(":attachment") || field.ends_with(":z_index")) { clip->value_track_set_update_mode(t,Animation::UPDATE_DISCRETE); } if(field.get_slice(":",0)!="event") { clip->track_insert_key(t,0,rest); } };
            track=base->get_track_count(); append(base);
            for(const Variant &name:named.keys()) { Ref<Animation> clip=named[name]; if(clip->get_track_count()!=track) { return finish(false,"Named animation layout mismatch"); } append(clip); }
            targets.push_back(entity);
        }
        Ref<Animation> selected=animation_name.is_empty()?base:Ref<Animation>(named[animation_name]);
        selected->track_insert_key(track,at,value); if(at>selected->get_length()) { selected->set_length(at); }
        String active=definition.get("state",String());
        if(!animation_name.is_empty() && active==animation_name) { base=selected; }
        if(!active.is_empty()) { named[active]=base; }
        definition["clip"]=base; definition["states"]=named; definition["targets"]=targets;
        Array after=entities.duplicate(true); Dictionary root=after[rig]; root["animation"]=definition;
        Ref<ECSScene> check; check.instantiate(); check->set_entities(after);
        if(check->instantiate().is_null()) { return finish(false,"Invalid animation edit; no changes applied"); }
        commit_animation(definition,String(U"AI 编辑命名动画关键帧"),false);
        editing_state=animation_name; refresh_tracks(); select_key(track,current_clip()->track_find_key(track,at,Animation::FIND_MODE_APPROX)); seek(at); reply["animation"]=animation_name; return finish(true);
    }
    return finish(false,"Unknown skeletal operation");
}
#endif
