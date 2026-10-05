#include "modules/ecs/ecs_compact_skeleton.h"
#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "modules/ecs/ecs_shared_textures.h"
#include "ecs_spine_import.h"
#include "core/object/callable_mp.h"
#include "core/io/resource_loader.h"
#include "core/config/project_settings.h"
#include "editor/editor_node.h"
#include "core/io/resource_saver.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_file_dialog.h"
#include "scene/gui/check_box.h"
#include "scene/gui/item_list.h"
#include "scene/gui/foldable_container.h"
#include "core/math/geometry_2d.h"
#include "scene/gui/dialogs.h"
#include "scene/resources/image_texture.h"
void ECSAnimationEditor::configure_brush() {
	local_canvas->configure_weight_brush(!animation_mode && brush_enabled->is_pressed()?brush_bone->get_selected_id():-1,brush_radius->get_value(),brush_strength->get_value());
}
void ECSAnimationEditor::commit_entities(const Array &entities,const String &label) {
	Ref<ECSScene> check; check.instantiate(); check->set_entities(entities); if(check->instantiate().is_null()) { feedback->set_text(String(U"配置无效，未修改工程。")); return; }
	auto *undo=EditorUndoRedoManager::get_singleton(); undo->create_action(label,UndoRedo::MERGE_DISABLE,scene.ptr()); undo->add_do_method(scene.ptr(),"set_entities",entities); undo->add_undo_method(scene.ptr(),"set_entities",scene->get_entities()); undo->commit_action(); edit_scene(scene,canvas);
}
void ECSAnimationEditor::adopt_project(const Ref<ECSScene> &value) {
	stop_preview(); if(independent_project && scene.is_valid() && scene->is_connected("changed",callable_mp(this,&ECSAnimationEditor::project_changed))) { scene->disconnect("changed",callable_mp(this,&ECSAnimationEditor::project_changed)); }
	independent_project=false; edit_scene(value,nullptr); independent_project=true;
	scene->connect("changed",callable_mp(this,&ECSAnimationEditor::project_changed));
	feedback->set_text(String(U"独立骨骼工程 · ")+(scene->get_path().is_empty()?String(U"未保存"):scene->get_path()));
}
void ECSAnimationEditor::project_changed() { edit_scene(scene,nullptr); }
void ECSAnimationEditor::confirm_project_action() { int operation=pending_project_operation; pending_project_operation=-1; project_action(operation); }
void ECSAnimationEditor::project_action(int operation) {
	if(operation==11) { open_preferences(); return; }
	if(operation==8) { open_asset_export(); return; }
	if(operation==8) {
		if(scene.is_null()) { feedback->set_text(String(U"请先新建或打开骨骼工程。")); return; }
		String error;
	}
	if(operation==9) { return; }
	if((operation==0 || operation==1 || operation==6) && scene.is_valid() && pending_project_operation!=-1) { pending_project_operation=operation; replace_dialog->popup_centered(); return; }
	pending_project_operation=0;
	if(operation==0) {
		Ref<ECSScene> created; created.instantiate(); Array entities; Dictionary root,bone,rig,definition;
		root["name"]="Skeleton2D"; root["position"]=Vector3(); rig["bones"]=PackedInt64Array({1}); root["skeleton_2d"]=rig;
		bone["name"]="Root Bone"; bone["parent"]=0; definition["length"]=100.0; bone["bone_2d"]=definition; entities.push_back(root); entities.push_back(bone); created->set_entities(entities); adopt_project(created); owner->select(0); select_target(1); return;
	}
	if(operation==2 && scene.is_valid() && !scene->get_path().is_empty()) { Error error=ResourceSaver::save(scene,scene->get_path()); feedback->set_text(error==OK?String(U"工程已保存：")+scene->get_path():String(U"保存失败：")+itos(error)); return; }
	if(operation==4 || operation==5) {
		if(scene.is_null()) { return; } Array entities=scene->get_entities().duplicate(true); Dictionary bone,def; def["length"]=100.0; bone["bone_2d"]=def; bone["name"]="Bone "+itos(entities.size());
		if(operation==4) { int root_index=entities.size(); Dictionary root,rig; root["name"]="Skeleton "+itos(root_index); root["position"]=Vector3(); rig["bones"]=PackedInt64Array({root_index+1}); root["skeleton_2d"]=rig; bone["parent"]=root_index; entities.push_back(root); entities.push_back(bone); }
		else { int root_index=owner->get_selected_id(),parent=target->get_selected_id(); if(root_index<0 || parent<0) { return; } Dictionary root=entities[root_index]; if(!root.has("skeleton_2d")) { feedback->set_text(String(U"顶部先选择骨架。")); return; } Dictionary rig=root["skeleton_2d"]; PackedInt64Array bones=rig["bones"]; if(parent!=root_index && !bones.has(parent)) { feedback->set_text(String(U"父骨骼不属于当前骨架。")); return; } bone["parent"]=parent; if(parent!=root_index) { Dictionary parent_bone=Dictionary(entities[parent]).get("bone_2d",Dictionary()); bone["position"]=Vector3(double(parent_bone.get("length",100.0)),0,0); } bones.push_back(entities.size()); rig["bones"]=bones; rig["bind_poses"]=Array(); entities.push_back(bone); }
		commit_entities(entities,String(U"添加骨架或骨骼")); return;
	}
	project_operation=operation; project_dialog->clear_filters(); project_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	if(operation==8) {
		project_dialog->set_title(String(U"导出骨骼资源"));
		project_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
		project_dialog->add_filter("*.res",String(U"AgeChaos 骨骼资源（内含动画和图片）"));
		project_dialog->set_current_file("Skeleton.ecsrig.res");
	} else if(operation==6) { project_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE); project_dialog->add_filter("*.json",String(U"Spine JSON")); }
	else if(operation==7) { project_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE); project_dialog->add_filter("*.png,*.jpg,*.webp",String(U"骨骼图片")); }
	else { project_dialog->set_file_mode(operation==1?EditorFileDialog::FILE_MODE_OPEN_FILE:EditorFileDialog::FILE_MODE_SAVE_FILE); project_dialog->add_filter("*.tres",String(U"AgeChaos 骨骼工程")); if(operation!=1) { project_dialog->set_current_file("Skeleton.ecsrig.tres"); } }
	project_dialog->popup_centered_ratio(.7);
}
void ECSAnimationEditor::project_file_selected(const String &path) {
	if(project_operation==8) {
		if(scene.is_null()) { return; }
		String error;
		if(path.get_extension().to_lower()!="res") { feedback->set_text(String(U"骨骼资源请使用 .res 扩展名。")); return; }
		Ref<ECSScene> exported; exported.instantiate(); Array entities=scene->get_entities().duplicate(true);
        Array packed=ecs_pack_skeleton_entities(entities);
        if(packed.is_empty()) {feedback->set_text(String(U"骨骼资源包含不支持的游戏组件，未导出。"));return;}
        Ref<ECSCompactSkeleton> check;check.instantiate();
        if(!check->load(Dictionary(packed[0])["skeleton_2d"])) {feedback->set_text(String(U"骨骼数据无效，未导出。"));return;}
        exported->set_entities(packed);
		Error result=ECSSharedTextures::save(exported,path,error);
		feedback->set_text(result==OK?String(U"骨骼资源已导出（骨架、动画及共享纹理引用）：")+path:String(U"导出失败：")+itos(result)+" "+error);
		return;
	}
	if(project_operation==6) { String report; Ref<ECSScene> imported=ecs_import_spine_json(path,report); if(imported.is_valid()) { adopt_project(imported); } feedback->set_text(report); return; }
	if(project_operation==1) { Ref<ECSScene> loaded=ResourceLoader::load(path,"ECSScene",ResourceLoader::CACHE_MODE_IGNORE); if(loaded.is_null() || loaded->instantiate().is_null()) { feedback->set_text(String(U"不是有效的 AgeChaos 骨骼工程。")); return; } adopt_project(loaded); return; }
	if(project_operation==7) {
		if(scene.is_null()) { return; } Ref<Image> image=Image::load_from_file(path); if(image.is_null()) { feedback->set_text(String(U"图片加载失败。")); return; }
		Array entities=scene->get_entities().duplicate(true); Dictionary e,p; e["name"]=path.get_file(); e["parent"]=target->get_selected_id(); float w=image->get_width(),h=image->get_height(); p["polygon"]=PackedVector2Array({Vector2(-w/2,-h/2),Vector2(w/2,-h/2),Vector2(w/2,h/2),Vector2(-w/2,h/2)}); p["uv"]=PackedVector2Array({Vector2(0,0),Vector2(1,0),Vector2(1,1),Vector2(0,1)}); p["texture"]=ImageTexture::create_from_image(image); e["polygon_2d"]=p; entities.push_back(e); int added=entities.size()-1; commit_entities(entities,String(U"添加骨骼图片")); select_target(added); return;
	}
	if(scene.is_null()) { return; } Error error=ResourceSaver::save(scene,path); if(error==OK) { scene->set_path(path); feedback->set_text(String(U"工程已保存：")+path); } else { feedback->set_text(String(U"保存失败：")+itos(error)); }
}

int ECSAnimationEditor::find_rig(int entity) const {
	if(scene.is_null()) { return -1; }
	Array entities=scene->get_entities();
	for(int step=0;step<entities.size() && entity>=0 && entity<entities.size();step++) {
		Dictionary e=entities[entity]; if(e.has("skeleton_2d")) { return entity; }
		Dictionary mesh=e.get("polygon_2d",Dictionary()); int rig=mesh.get("skeleton",-1);
		if(rig>=0 && rig<entities.size() && Dictionary(entities[rig]).has("skeleton_2d")) { return rig; }
		entity=e.get("parent",-1);
	}
	int found=-1; for(int i=0;i<entities.size();i++) { if(Dictionary(entities[i]).has("skeleton_2d")) { if(found>=0) { return -1; } found=i; } }
	return found;
}
void ECSAnimationEditor::mesh_toggled(bool enabled) {
	if(enabled) { brush_enabled->set_pressed(false); }
	local_canvas->set_mesh_edit_mode(enabled && !animation_mode);
}
void ECSAnimationEditor::mesh_changed(int entity,const Dictionary &polygon) {
	if(animation_mode || scene.is_null() || entity<0 || entity>=scene->get_entities().size()) { return; }
	Array entities=scene->get_entities().duplicate(true); Dictionary e=entities[entity]; e["polygon_2d"]=polygon;
	commit_entities(entities,String(U"编辑图片网格"));
}
Dictionary ECSAnimationEditor::subdivide_mesh(const Dictionary &source) {
	Dictionary mesh=source.duplicate(true); PackedVector2Array points=mesh.get("polygon",PackedVector2Array()),uv=mesh.get("uv",PackedVector2Array());
	PackedInt32Array triangles=mesh.get("triangles",PackedInt32Array()),bones=mesh.get("bones",PackedInt32Array()); PackedFloat32Array weights=mesh.get("weights",PackedFloat32Array());
	if(triangles.is_empty()) { triangles=Geometry2D::triangulate_polygon(points); }
	if(points.size()+triangles.size()/3>16384 || triangles.size()%3) { return Dictionary(); }
	PackedInt32Array result;
	for(int t=0;t<triangles.size();t+=3) {
		int a=triangles[t],b=triangles[t+1],c=triangles[t+2],n=points.size();
		points.push_back((points[a]+points[b]+points[c])/3);
		if(!uv.is_empty()) { uv.push_back((uv[a]+uv[b]+uv[c])/3); }
		if(!weights.is_empty()) {
			HashMap<int,float> influence;
			for(int vertex:{a,b,c}) { for(int j=0;j<4;j++) { int bone=bones[vertex*4+j]; if(!influence.has(bone)) { influence[bone]=0; } influence[bone]+=weights[vertex*4+j]/3; } }
			int chosen[4]={0,0,0,0}; float values[4]={0,0,0,0};
			for(const KeyValue<int,float> &entry:influence) { for(int j=0;j<4;j++) { if(entry.value>values[j]) { for(int k=3;k>j;k--) { values[k]=values[k-1]; chosen[k]=chosen[k-1]; } values[j]=entry.value; chosen[j]=entry.key; break; } } }
			float sum=values[0]+values[1]+values[2]+values[3]; for(int j=0;j<4;j++) { bones.push_back(chosen[j]); weights.push_back(sum>0?values[j]/sum:(j==0?1:0)); }
		}
		for(int index:{a,b,n,b,c,n,c,a,n}) { result.push_back(index); }
	}
	mesh["polygon"]=points; mesh["uv"]=uv; mesh["triangles"]=result;
	if(!weights.is_empty()) { mesh["bones"]=bones; mesh["weights"]=weights; }
	return mesh;
}
void ECSAnimationEditor::image_action(int operation) {
	if(animation_mode) { return; }
	if(operation==0) { project_action(7); return; }
    if(operation==3) {
        for(Button *button:transform_tools) { button->set_pressed_no_signal(false); } if(canvas_tools[3]) { canvas_tools[3]->set_pressed_no_signal(false); }
        local_canvas->set_authoring_tool(1);
        mesh_edit->set_pressed(false);
        canvas_tools[0]->set_pressed_no_signal(false); canvas_tools[1]->set_pressed_no_signal(true); canvas_tools[2]->set_pressed_no_signal(false);
        brush_enabled->set_pressed(false);
        Object::cast_to<FoldableContainer>(setup_property_panel)->set_folded(false);
        pending_property_reveal=brush_enabled; property_reveal_frames=3;
    }
	int selected=target->get_selected_id();
	if(scene.is_null() || selected<0 || !Dictionary(scene->get_entities()[selected]).has("polygon_2d")) { feedback->set_text(String(U"先在画布或层级树中选择图片。")); return; }
	image_selection=selected;
	Dictionary mesh=Dictionary(scene->get_entities()[selected])["polygon_2d"];
	if(operation==1) { Dictionary divided=subdivide_mesh(mesh); if(divided.is_empty()) { feedback->set_text(String(U"网格已达到细分上限。")); return; } mesh_changed(selected,divided); mesh_edit->set_pressed(true); feedback->set_text(String(U"拖动顶点；双击三角形内部加点；右键顶点删除；Esc 取消拖动。")); return; }
	if(operation==3) {
		
		if(int(mesh.get("skeleton",-1))<0) { feedback->set_text(String(U"先点击“绑定骨骼”，选择参与蒙皮的骨骼。")); return; }
		int rig_index=mesh.get("skeleton",-1);
        Array entities=scene->get_entities();
        if(rig_index>=entities.size()) { feedback->set_text(String(U"图片的骨架引用无效，请重新绑定。")); return; }
        PackedInt64Array bones=Dictionary(Dictionary(entities[rig_index]).get("skeleton_2d",Dictionary())).get("bones",PackedInt64Array());
        if(bones.is_empty()) { feedback->set_text(String(U"骨架没有可绘制权重的骨骼。")); return; }
        if(!bones.has(brush_bone->get_selected_id())) { int index=brush_bone->get_item_index(bones[0]); if(index<0) { return; } brush_bone->select(index); }
        mesh_edit->set_pressed(false); brush_enabled->set_pressed(true); configure_brush(); feedback->set_text(String(U"在层级树中点击骨骼后刷权重，Shift 减少权重。图片保持选中。")); return;
	}
	binding_rig=find_rig(selected); if(binding_rig<0) { feedback->set_text(String(U"图片没有唯一所属骨架，请先将图片放到所需骨架下。")); return; }
	binding_bones->clear(); Array entities=scene->get_entities(); PackedInt64Array bones=Dictionary(Dictionary(entities[binding_rig])["skeleton_2d"])["bones"];
	for(int i=0;i<bones.size();i++) { binding_bones->add_item(Dictionary(entities[bones[i]]).get("name","Bone")); binding_bones->set_item_metadata(i,i); binding_bones->select(i,false); }
	binding_dialog->popup_centered();
}
void ECSAnimationEditor::apply_binding() {
	if(scene.is_null() || animation_mode || image_selection<0 || binding_rig<0) { return; }
	PackedInt32Array selected=binding_bones->get_selected_items(); if(selected.is_empty()) { feedback->set_text(String(U"至少选择一根骨骼。")); return; }
	Ref<ECSWorld> world=scene->instantiate(); if(world.is_null()) { return; } PackedInt64Array ids=world->query(PackedStringArray(),true);
	if(image_selection>=ids.size() || binding_rig>=ids.size()) { return; }
	Dictionary rig=world->get_skeleton_2d(ids[binding_rig]); if(rig.is_empty()) { return; }
	PackedInt64Array bones=rig["bones"];
	Array entities=scene->get_entities().duplicate(true); Dictionary e=entities[image_selection],mesh=e["polygon_2d"];
	PackedVector2Array points=mesh["polygon"]; PackedInt32Array indices; PackedFloat32Array weights; Transform3D transform=world->get_global_transform(ids[image_selection]);
	for(const Vector2 &point:points) {
		Vector3 p=transform.xform(Vector3(point.x,point.y,0)); double best[4]={1e30,1e30,1e30,1e30}; int chosen[4]={0,0,0,0};
		for(int item:selected) { int index=binding_bones->get_item_metadata(item); if(index<0 || index>=bones.size()) { return; } Transform3D bone=world->get_global_transform(bones[index]); Vector3 end=bone.xform(Vector3(double(world->get_bone_2d(bones[index])["length"]),0,0)); Vector2 segment[2]={Vector2(bone.origin.x,bone.origin.y),Vector2(end.x,end.y)}; double distance=Geometry2D::get_closest_point_to_segment(Vector2(p.x,p.y),segment).distance_squared_to(Vector2(p.x,p.y)); for(int j=0;j<4;j++) { if(distance<best[j]) { for(int k=3;k>j;k--) { best[k]=best[k-1]; chosen[k]=chosen[k-1]; } best[j]=distance; chosen[j]=index; break; } } }
		double sum=0; for(int j=0;j<MIN(4,selected.size());j++) { sum+=1/MAX(1.0,best[j]); }
		for(int j=0;j<4;j++) { indices.push_back(chosen[j]); weights.push_back(j<selected.size()?(1/MAX(1.0,best[j]))/sum:0); }
	}
	mesh["skeleton"]=binding_rig; mesh["bones"]=indices; mesh["weights"]=weights;
	commit_entities(entities,String(U"绑定图片与骨骼")); select_target(image_selection);
	feedback->set_text(String(U"绑定完成。可细分网格、编辑顶点，再进入权重工具微调。"));
}

void ECSAnimationEditor::external_images_dropped(const PackedStringArray &paths) {
	if(!is_visible_in_tree() || animation_mode) { return; }
	for(const String &path:paths) { String extension=path.get_extension().to_lower(); if(extension!="png" && extension!="jpg" && extension!="jpeg" && extension!="webp") { feedback->set_text(String(U"这里只接受 PNG、JPEG 或 WebP 图片；骨骼工程请使用工程菜单打开。")); return; } }
	images_dropped(paths,local_canvas->get_canvas_mouse_position());
}
void ECSAnimationEditor::images_dropped(const PackedStringArray &paths,const Vector2 &position) {
	if(animation_mode || scene.is_null()) { return; }
	Array entities=scene->get_entities().duplicate(true); int parent=target->get_selected_id(); Ref<ECSWorld> world=scene->instantiate(); if(world.is_null()) { return; }
	PackedInt64Array ids=world->query(PackedStringArray(),true); Transform3D parent_transform;
	if(parent>=0 && parent<ids.size()) { parent_transform=world->get_global_transform(ids[parent]); } else { parent=-1; }
	if(Math::abs(parent_transform.basis.determinant())<.000001) { feedback->set_text(String(U"父实体缩放为零，无法放置图片。")); return; }
	for(const String &path:paths) {
		Ref<Image> image=Image::load_from_file(path); if(image.is_null()) { feedback->set_text(String(U"图片加载失败：")+path); return; }
		Dictionary e,p; float w=image->get_width(),h=image->get_height();
		e["name"]=path.get_file(); e["parent"]=parent; e["position"]=parent_transform.affine_inverse().xform(Vector3(position.x,position.y,0));
		p["polygon"]=PackedVector2Array({Vector2(-w/2,-h/2),Vector2(w/2,-h/2),Vector2(w/2,h/2),Vector2(-w/2,h/2)}); p["uv"]=PackedVector2Array({Vector2(0,0),Vector2(1,0),Vector2(1,1),Vector2(0,1)}); p["texture"]=ImageTexture::create_from_image(image); e["polygon_2d"]=p; entities.push_back(e);
	}
	int added=entities.size()-1; commit_entities(entities,String(U"拖入骨骼图片")); select_target(added);
}

void ECSAnimationEditor::start_independent_project(const Ref<ECSScene> &value) {
	if(value.is_valid()) { adopt_project(value); }
	else { pending_project_operation=-1; project_action(0); }
}
bool ECSAnimationEditor::prepare_platform_export() {
	if(scene.is_null() || scene->instantiate().is_null()) { feedback->set_text(String(U"没有有效骨骼工程可以发布。")); return false; }
	// Export a separate snapshot. Authoring files remain untouched.
	Ref<ECSScene> snapshot; snapshot.instantiate(); snapshot->set_entities(scene->get_entities().duplicate(true));
	const String path="res://SkeletonExport.tres";
	String shared_error;
	Error error=ECSSharedTextures::save(snapshot,path,shared_error);
	if(error!=OK) { feedback->set_text(String(U"无法写入发布快照：")+itos(error)); return false; }
	auto *settings=ProjectSettings::get_singleton();
	settings->set("ecs/run/scene",path);
	settings->set("application/run/main_loop_type","ECSMainLoop");
	error=settings->save();
	if(error!=OK) { feedback->set_text(String(U"无法保存发布配置：")+itos(error)); return false; }
	feedback->set_text(String(U"已生成发布快照。选择目标平台并配置对应的 AgeChaos 导出模板。"));
	return true;
}
#endif
