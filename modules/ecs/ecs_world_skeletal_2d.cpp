// AgeChaos native ECS 2D skeletons. No Skeleton2D/Bone2D nodes are created.
#include "ecs_world.h"
#include "ecs_scene.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_loader.h"
#include "scene/resources/image_texture.h"
#include "core/math/geometry_2d.h"
#include "scene/resources/texture.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/canvas_item_material.h"
#include "scene/resources/shader.h"
#include "scene/resources/material.h"
#include "servers/rendering/rendering_server.h"

namespace {
Transform2D ecs_transform_2d(const Transform3D &t) {
	return Transform2D(Vector2(t.basis[0][0],t.basis[1][0]),Vector2(t.basis[0][1],t.basis[1][1]),Vector2(t.origin.x,t.origin.y));
}
bool ecs_valid_keys(const Dictionary &data,const PackedStringArray &allowed) {
	for(const Variant &key:data.keys()) { if((key.get_type()!=Variant::STRING && key.get_type()!=Variant::STRING_NAME) || !allowed.has(key)) { return false; } }
	return true;
}
}
Dictionary ECSWorld::get_bone_2d(uint64_t id) const { ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,Dictionary()); const Dictionary *data=is_alive(id)?bones_2d.getptr(uint32_t(id)):nullptr; return data?data->duplicate(true):Dictionary(); }
Dictionary ECSWorld::get_skeleton_2d(uint64_t id) const { ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,Dictionary()); const Skeleton2DState *state=is_alive(id)?skeletons_2d.getptr(uint32_t(id)):nullptr; return state?state->definition.duplicate(true):Dictionary(); }
Dictionary ECSWorld::get_polygon_2d(uint64_t id) const { ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,Dictionary()); const Polygon2DState *state=is_alive(id)?polygons_2d.getptr(uint32_t(id)):nullptr; return state?state->definition.duplicate(true):Dictionary(); }
bool ECSWorld::set_bone_2d(uint64_t id,const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,false);
	if(!is_alive(id)) { return false; }
	Dictionary data=get_bone_2d(id); data.merge(definition,true);
	if(!ecs_valid_keys(data,{"length","rest"})) { return false; }
	Variant length=data.get("length",80.0),rest=data.get("rest",Transform2D());
	if((length.get_type()!=Variant::FLOAT && length.get_type()!=Variant::INT) || !Math::is_finite(double(length)) || double(length)<0 || rest.get_type()!=Variant::TRANSFORM2D || !Transform2D(rest).is_finite() || Math::is_zero_approx(Transform2D(rest).determinant())) { return false; }
	data["length"]=double(length); data["rest"]=rest; bones_2d[uint32_t(id)]=data;
	uint8_t marker=1; store_ui_column(uint32_t(id),"bone_2d",&marker); return true;
}
bool ECSWorld::set_skeleton_2d(uint64_t id,const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,false);
	if(!is_alive(id)) { return false; }
	Dictionary data=get_skeleton_2d(id); data.merge(definition,true);
	if(!ecs_valid_keys(data,{"bones","bind_poses","ik","slots","skins","skin","active_skins","placeholders","attachment_library"}) || data.get("bones",Variant()).get_type()!=Variant::PACKED_INT64_ARRAY) { return false; }
	PackedInt64Array bones=data["bones"];
	if(bones.is_empty() || bones.size()>256) { return false; }
	HashSet<uint64_t> unique;
	Transform2D root=ecs_transform_2d(get_global_transform(id));
	if(Math::is_zero_approx(root.determinant())) { return false; }
	for(uint64_t bone:bones) { if(!is_alive(bone) || !bones_2d.has(uint32_t(bone)) || unique.has(bone)) { return false; } unique.insert(bone); }
	Array poses;
	if(data.has("bind_poses")) { if(data["bind_poses"].get_type()!=Variant::ARRAY) { return false; } poses=data["bind_poses"]; }
	if(poses.is_empty()) { for(uint64_t bone:bones) { poses.push_back(root.affine_inverse()*ecs_transform_2d(get_global_transform(bone))); } }
	if(poses.size()!=bones.size()) { return false; }
	for(const Variant &pose:poses) { if(pose.get_type()!=Variant::TRANSFORM2D || !Transform2D(pose).is_finite() || Math::is_zero_approx(Transform2D(pose).determinant())) { return false; } }
	for(const auto &polygon:polygons_2d) {
		if(uint64_t(int64_t(polygon.value.definition["skeleton"]))!=id) { continue; }
		for(int index:PackedInt32Array(polygon.value.definition["bones"])) { if(index>=bones.size()) { return false; } }
	}
	if(data.has("ik")) {
		if(data["ik"].get_type()!=Variant::ARRAY) { return false; }
		Array constraints=data["ik"]; if(constraints.size()>32) { return false; }
		for(const Variant &entry:constraints) {
			if(entry.get_type()!=Variant::DICTIONARY) { return false; } Dictionary c=entry;
			if(!ecs_valid_keys(c,{"chain","target","target_bone","enabled","iterations","tolerance"}) || c.get("chain",Variant()).get_type()!=Variant::PACKED_INT32_ARRAY || c.get("target",Variant()).get_type()!=Variant::VECTOR2 || !Vector2(c["target"]).is_finite()) { return false; }
			PackedInt32Array chain=c["chain"]; int iterations=c.get("iterations",24); double tolerance=c.get("tolerance",.1);
			if(chain.is_empty() || chain.size()>16 || iterations<1 || iterations>128 || !Math::is_finite(tolerance) || tolerance<=0 || c.get("enabled",true).get_type()!=Variant::BOOL) { return false; }
			int target_bone=c.get("target_bone",-1); if(target_bone<-1 || target_bone>=bones.size()) { return false; }
			HashSet<int> seen; for(int j=0;j<chain.size();j++) { int index=chain[j]; if(index<0 || index>=bones.size() || seen.has(index) || (j>0 && get_parent(bones[index])!=uint64_t(bones[chain[j-1]]))) { return false; } seen.insert(index); }
			if(target_bone>=0) { uint64_t target=bones[target_bone]; while(target) { if(chain.find(bones.find(target))>=0) { return false; } target=get_parent(target); } }
		}
	}
	HashMap<uint64_t,Dictionary> attachments;
	if(!prepare_skeleton_slots(data,attachments)) { return false; }
	data["bind_poses"]=poses.duplicate();
	auto *rs=RenderingServer::get_singleton();
	if(!skeletons_2d.has(uint32_t(id))) { Skeleton2DState state; state.skeleton=rs->skeleton_create(); skeletons_2d[uint32_t(id)]=state; }
	Skeleton2DState &state=skeletons_2d[uint32_t(id)]; state.definition=data; state.attachments=attachments;
	rs->skeleton_allocate_data(state.skeleton,bones.size(),true);
	for(auto &polygon:polygons_2d) { if(uint64_t(int64_t(polygon.value.definition["skeleton"]))==id) { rs->canvas_item_attach_skeleton(polygon.value.item,state.skeleton); } }
	uint8_t marker=1; store_ui_column(uint32_t(id),"skeleton_2d",&marker); return true;
}
bool ECSWorld::set_polygon_2d(uint64_t id,const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,false);
	if(!is_alive(id)) { return false; }
	Dictionary data=get_polygon_2d(id); data.merge(definition,true);
	if(!ecs_valid_keys(data,{"polygon","uv","texture","color","skeleton","bones","weights","z_index","triangles"})) { return false; }
	Variant polygon=data.get("polygon",Variant()),uv=data.get("uv",PackedVector2Array()),color=data.get("color",Color(1,1,1)),skeleton=data.get("skeleton",int64_t(0)),bone_indices=data.get("bones",PackedInt32Array()),weights=data.get("weights",PackedFloat32Array()),texture=data.get("texture",Variant()),z=data.get("z_index",0);
	if(polygon.get_type()!=Variant::PACKED_VECTOR2_ARRAY || uv.get_type()!=Variant::PACKED_VECTOR2_ARRAY || color.get_type()!=Variant::COLOR || skeleton.get_type()!=Variant::INT || bone_indices.get_type()!=Variant::PACKED_INT32_ARRAY || weights.get_type()!=Variant::PACKED_FLOAT32_ARRAY || z.get_type()!=Variant::INT || int64_t(z)<-4096 || int64_t(z)>4096 || (texture.get_type()!=Variant::NIL && texture.get_type()!=Variant::OBJECT)) { return false; }
	Ref<Texture2D> image=texture;
	if(!texture.is_null() && image.is_null()) { return false; }
	PackedVector2Array points=polygon,texcoords=uv;
	if(points.size()<3 || points.size()>16384 || (!texcoords.is_empty() && texcoords.size()!=points.size())) { return false; }
	for(const Vector2 &point:points) { if(!point.is_finite()) { return false; } }
	for(const Vector2 &point:texcoords) { if(!point.is_finite()) { return false; } }
	Color tint=color; for(int i=0;i<4;i++) { if(!Math::is_finite(tint[i])) { return false; } }
	Vector<int> triangles;
	if(data.has("triangles")) {
		if(data["triangles"].get_type()!=Variant::PACKED_INT32_ARRAY) { return false; } triangles=data["triangles"];
		if(triangles.size()%3 || triangles.size()>points.size()*12) { return false; }
		for(int index:triangles) { if(index<0 || index>=points.size()) { return false; } }
	}
	if(triangles.is_empty()) { triangles=Geometry2D::triangulate_polygon(points); }
	if(triangles.is_empty()) { return false; }
	PackedInt32Array indices=bone_indices; PackedFloat32Array influences=weights;
	uint64_t rig=int64_t(skeleton);
	if(rig) {
		if(!is_alive(rig) || !skeletons_2d.has(uint32_t(rig)) || indices.size()!=points.size()*4 || influences.size()!=indices.size()) { return false; }
		int count=PackedInt64Array(skeletons_2d[uint32_t(rig)].definition["bones"]).size();
		for(int i=0;i<points.size();i++) {
			double sum=0;
			for(int j=0;j<4;j++) { int n=i*4+j; if(indices[n]<0 || indices[n]>=count || !Math::is_finite(influences[n]) || influences[n]<0) { return false; } sum+=influences[n]; }
			if(sum<=0) { return false; }
			for(int j=0;j<4;j++) { influences.set(i*4+j,influences[i*4+j]/sum); }
		}
	} else if(!indices.is_empty() || !influences.is_empty()) { return false; }
	data["uv"]=uv; data["color"]=color; data["texture"]=texture; data["skeleton"]=skeleton; data["bones"]=indices; data["weights"]=influences; data["z_index"]=z;
	auto *rs=RenderingServer::get_singleton();
	Array arrays; arrays.resize(RSE::ARRAY_MAX);
	arrays[RSE::ARRAY_VERTEX]=points; arrays[RSE::ARRAY_INDEX]=triangles;
	if(!texcoords.is_empty()) { arrays[RSE::ARRAY_TEX_UV]=texcoords; }
	if(rig) { arrays[RSE::ARRAY_BONES]=indices; arrays[RSE::ARRAY_WEIGHTS]=influences; }
	RenderingServerTypes::SurfaceData surface;
	if(rig) {
		Transform2D base=ecs_transform_2d(get_global_transform(rig)); if(Math::is_zero_approx(base.determinant())) { return false; }
		Transform2D relative=base.affine_inverse()*ecs_transform_2d(get_global_transform(id));
		surface.mesh_to_skeleton_xform.basis.rows[0][0]=relative.columns[0][0]; surface.mesh_to_skeleton_xform.basis.rows[1][0]=relative.columns[0][1];
		surface.mesh_to_skeleton_xform.basis.rows[0][1]=relative.columns[1][0]; surface.mesh_to_skeleton_xform.basis.rows[1][1]=relative.columns[1][1];
		surface.mesh_to_skeleton_xform.origin=Vector3(relative.get_origin().x,relative.get_origin().y,0);
	}
	if(rs->mesh_create_surface_data_from_arrays(&surface,RSE::PRIMITIVE_TRIANGLES,arrays,Array(),Dictionary(),RSE::ARRAY_FLAG_USE_2D_VERTICES)!=OK) { return false; }
	if(!polygons_2d.has(uint32_t(id))) { Polygon2DState state; state.item=rs->canvas_item_create(); state.mesh=rs->mesh_create(); polygons_2d[uint32_t(id)]=state; }
	Polygon2DState &state=polygons_2d[uint32_t(id)]; state.definition=data;
	rs->canvas_item_clear(state.item); rs->mesh_clear(state.mesh); rs->mesh_add_surface(state.mesh,surface);
	rs->canvas_item_attach_skeleton(state.item,rig?skeletons_2d[uint32_t(rig)].skeleton:RID());
	rs->canvas_item_add_mesh(state.item,state.mesh,Transform2D(),tint,image.is_valid()?image->get_rid():RID());
	rs->canvas_item_set_z_index(state.item,int(z));
	uint8_t marker=1; store_ui_column(uint32_t(id),"polygon_2d",&marker); return true;
}
void ECSWorld::solve_ik_2d() {
	ERR_FAIL_COND(Thread::get_caller_id()!=owner_thread);
	for(auto &entry:skeletons_2d) {
		uint64_t rig=(uint64_t(slots[entry.key].generation)<<32)|entry.key; if(!is_active_in_hierarchy(rig)) { continue; }
		PackedInt64Array bones=entry.value.definition["bones"]; Array constraints=entry.value.definition.get("ik",Array());
		for(const Variant &value:constraints) {
			Dictionary c=value; if(!bool(c.get("enabled",true))) { continue; } PackedInt32Array chain=c["chain"]; bool valid=true;
			for(int index:chain) { if(index<0 || index>=bones.size() || !is_alive(bones[index]) || !bones_2d.has(uint32_t(bones[index]))) { valid=false; break; } }
			if(!valid) { continue; }
			Vector2 local=c["target"]; Vector3 target=get_global_transform(rig).xform(Vector3(local.x,local.y,0)); int target_bone=c.get("target_bone",-1); if(target_bone>=0) { if(!is_alive(bones[target_bone])) { continue; } target=get_global_transform(bones[target_bone]).origin; }
			uint64_t tip=bones[chain[chain.size()-1]]; double length=get_bone_2d(tip)["length"];
			for(int iteration=0;iteration<int(c.get("iterations",24));iteration++) {
				Vector3 end=get_global_transform(tip).xform(Vector3(length,0,0)); if(end.distance_to(target)<=double(c.get("tolerance",.1))) { break; }
				for(int j=chain.size()-1;j>=0;j--) {
					uint64_t bone=bones[chain[j]], parent=get_parent(bone); Transform3D base=parent?get_global_transform(parent):Transform3D(); if(Math::is_zero_approx(base.basis.determinant())) { continue; }
					Transform3D inv=base.affine_inverse(); Vector3 origin=get_vector(bone,"position"), a=inv.xform(get_global_transform(tip).xform(Vector3(length,0,0)))-origin,b=inv.xform(target)-origin;
					if(Vector2(a.x,a.y).length_squared()<1e-10 || Vector2(b.x,b.y).length_squared()<1e-10) { continue; }
					Vector3 rotation=get_vector(bone,"rotation"); rotation.z+=Vector2(a.x,a.y).angle_to(Vector2(b.x,b.y)); set_vector(bone,"rotation",rotation);
				}
			}
		}
	}
}
void ECSWorld::sync_skeletal_2d(RID viewport) {
	sync_tilemaps_2d(viewport);
	solve_ik_2d();
	ERR_FAIL_COND(Thread::get_caller_id()!=owner_thread);
	auto *rs=RenderingServer::get_singleton();
	if(!skeleton_canvas.is_valid()) { if(polygons_2d.is_empty()) { return; } skeleton_canvas=rs->canvas_create(); }
	if(skeleton_viewport!=viewport) {
		if(skeleton_viewport.is_valid()) { rs->viewport_remove_canvas(skeleton_viewport,skeleton_canvas); }
		rs->viewport_attach_canvas(viewport,skeleton_canvas); skeleton_viewport=viewport;
	}
	for(auto &entry:skeletons_2d) {
		uint64_t id=(uint64_t(slots[entry.key].generation)<<32)|entry.key;
		Transform2D root=ecs_transform_2d(get_global_transform(id));
		if(Math::is_zero_approx(root.determinant())) { continue; }
		PackedInt64Array bones=entry.value.definition["bones"]; Array poses=entry.value.definition["bind_poses"];
		rs->skeleton_set_base_transform_2d(entry.value.skeleton,root);
		for(int i=0;i<bones.size();i++) {
			Transform2D transform=is_alive(bones[i])?root.affine_inverse()*ecs_transform_2d(get_global_transform(bones[i]))*Transform2D(poses[i]).affine_inverse():Transform2D();
			rs->skeleton_bone_set_transform_2d(entry.value.skeleton,i,transform);
		}
	}
	for(auto &entry:polygons_2d) {
		uint64_t id=(uint64_t(slots[entry.key].generation)<<32)|entry.key;
		uint64_t rig=int64_t(entry.value.definition["skeleton"]);
		bool visible=is_active_in_hierarchy(id) && is_skeleton_attachment_visible(id) && (!rig || (is_alive(rig) && skeletons_2d.has(uint32_t(rig)) && is_active_in_hierarchy(rig)));
		if(rig && visible) { for(uint64_t bone:PackedInt64Array(skeletons_2d[uint32_t(rig)].definition["bones"])) { if(!is_alive(bone) || !bones_2d.has(uint32_t(bone))) { visible=false; break; } } }
		rs->canvas_item_set_parent(entry.value.item,skeleton_canvas);
		rs->canvas_item_set_transform(entry.value.item,ecs_transform_2d(get_global_transform(id)));
		Dictionary style=get_skeleton_attachment_style(id);
		Color tint(1,1,1);
		int z=entry.value.definition.get("z_index",0),blend=0;
		if(!style.is_empty()) { tint*=Color(style.get("color",Color(1,1,1))); z=style.get("z_index",z); blend=style.get("blend",0); }
		rs->canvas_item_set_modulate(entry.value.item,tint);
		rs->canvas_item_set_z_index(entry.value.item,z);
		if(blend>0 && skeleton_slot_materials[blend].is_null()) { Ref<CanvasItemMaterial> material; material.instantiate(); material->set_blend_mode(blend==1?CanvasItemMaterial::BLEND_MODE_ADD:CanvasItemMaterial::BLEND_MODE_MUL); skeleton_slot_materials[blend]=material; }
		Color dark=style.get("dark",Color(0,0,0));
		Ref<Texture2D> polygon_texture = entry.value.definition.get("texture", Variant());
		Ref<AtlasTexture> atlas_texture = polygon_texture;
		if(dark.r!=0 || dark.g!=0 || dark.b!=0 || atlas_texture.is_valid()) {
			Ref<ShaderMaterial> material=entry.value.tint_material;
			if(material.is_null() || int(material->get_meta("slot_blend",-1))!=blend) {
				Ref<Shader> shader;shader.instantiate();String mode=blend==1?"blend_add":blend==2?"blend_mul":"blend_mix";
				shader->set_code("shader_type canvas_item; render_mode "+mode+"; uniform vec4 dark: source_color=vec4(0.0); uniform vec4 atlas_map=vec4(1.0,1.0,0.0,0.0); uniform vec4 atlas_clip=vec4(-1e20,-1e20,1e20,1e20); varying vec4 light; void vertex(){light=COLOR;} void fragment(){if(any(lessThan(UV,atlas_clip.xy))||any(greaterThan(UV,atlas_clip.zw))){discard;} vec4 tex=texture(TEXTURE,UV*atlas_map.xy+atlas_map.zw);COLOR=vec4(tex.rgb*light.rgb+(vec3(1.0)-tex.rgb)*dark.rgb,tex.a*light.a);}");
				material.instantiate();material->set_shader(shader);material->set_meta("slot_blend",blend);entry.value.tint_material=material;
			}
			Vector2 uv_scale(1, 1), uv_offset;
			Vector2 clip_min(-1e20, -1e20), clip_max(1e20, 1e20);
			while (atlas_texture.is_valid() && atlas_texture->get_atlas().is_valid()) {
				Vector2 original_size = atlas_texture->get_size(), base_size = atlas_texture->get_atlas()->get_size();
				Rect2 region = atlas_texture->get_region(), margin = atlas_texture->get_margin();
				if (region.size.x == 0) { region.size.x = base_size.x; }
				if (region.size.y == 0) { region.size.y = base_size.y; }
				Vector2 low = (margin.position / original_size - uv_offset) / uv_scale;
				Vector2 high = ((margin.position + region.size) / original_size - uv_offset) / uv_scale;
				clip_min = clip_min.max(low); clip_max = clip_max.min(high);
				uv_offset = (uv_offset * original_size + region.position - margin.position) / base_size;
				uv_scale *= original_size / base_size;
				atlas_texture = atlas_texture->get_atlas();
			}
			material->set_shader_parameter("atlas_map", Vector4(uv_scale.x, uv_scale.y, uv_offset.x, uv_offset.y));
			material->set_shader_parameter("atlas_clip", Vector4(clip_min.x, clip_min.y, clip_max.x, clip_max.y));
			material->set_shader_parameter("dark",dark);rs->canvas_item_set_material(entry.value.item,material->get_rid());
		} else { rs->canvas_item_set_material(entry.value.item,blend>0?skeleton_slot_materials[blend]->get_rid():RID()); }
		rs->canvas_item_set_visible(entry.value.item,visible);
	}
}
PackedVector2Array ECSWorld::get_deformed_polygon_2d(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,PackedVector2Array());
	Dictionary data=get_polygon_2d(id); if(data.is_empty()) { return PackedVector2Array(); }
	PackedVector2Array points=data["polygon"]; uint64_t rig=int64_t(data["skeleton"]);
	Transform2D polygon_transform=ecs_transform_2d(get_global_transform(id));
	if(!rig) { for(int i=0;i<points.size();i++) { points.set(i,polygon_transform.xform(points[i])); } return points; }
	if(!is_alive(rig) || !skeletons_2d.has(uint32_t(rig))) { return PackedVector2Array(); }
	Dictionary skeleton=get_skeleton_2d(rig); PackedInt64Array bones=skeleton["bones"]; Array poses=skeleton["bind_poses"];
	PackedInt32Array indices=data["bones"]; PackedFloat32Array weights=data["weights"];
	Transform2D root=ecs_transform_2d(get_global_transform(rig)); if(Math::is_zero_approx(root.determinant())) { return PackedVector2Array(); }
	for(int i=0;i<points.size();i++) {
		Vector2 p=root.affine_inverse().xform(polygon_transform.xform(points[i])),output;
		for(int j=0;j<4;j++) { int b=indices[i*4+j]; if(b<0 || b>=bones.size() || !is_alive(bones[b]) || !bones_2d.has(uint32_t(bones[b]))) { return PackedVector2Array(); } output+=ecs_transform_2d(get_global_transform(bones[b])).xform(Transform2D(poses[b]).affine_inverse().xform(p))*weights[i*4+j]; }
		points.set(i,output);
	}
	return points;
}
bool ECSWorld::remove_skeletal_2d(uint64_t id,const StringName &kind) {
	ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,false);
	if(!is_alive(id)) { return false; } uint32_t index=uint32_t(id); auto *rs=RenderingServer::get_singleton();
	if(kind=="bone_2d") { if(!bones_2d.erase(index)) { return false; } }
	else if(kind=="skeleton_2d") { auto *state=skeletons_2d.getptr(index); if(!state) { return false; } for(auto &polygon:polygons_2d) { if(uint64_t(int64_t(polygon.value.definition["skeleton"]))==id) { rs->canvas_item_attach_skeleton(polygon.value.item,RID()); rs->canvas_item_set_visible(polygon.value.item,false); } } rs->free_rid(state->skeleton); skeletons_2d.erase(index); }
	else if(kind=="polygon_2d") { auto *state=polygons_2d.getptr(index); if(!state) { return false; } rs->free_rid(state->item); rs->free_rid(state->mesh); polygons_2d.erase(index); }
	else { return false; }
	remove_row(pools[kind],index); return true;
}
void ECSWorld::clear_skeletal_2d() {
	ERR_FAIL_COND(Thread::get_caller_id()!=owner_thread);
	auto *rs=RenderingServer::get_singleton();
	for(const auto &entry:polygons_2d) { rs->free_rid(entry.value.item); rs->free_rid(entry.value.mesh); }
	for(const auto &entry:skeletons_2d) { rs->free_rid(entry.value.skeleton); }
	if(skeleton_canvas.is_valid()) { rs->free_rid(skeleton_canvas); skeleton_canvas=RID(); skeleton_viewport=RID(); }
	polygons_2d.clear(); skeletons_2d.clear(); bones_2d.clear();
}

bool ECSWorld::skeletal_2d_self_test(const String &path) {
#define S2_CHECK(condition) ERR_FAIL_COND_V_MSG(!(condition), false, "ECS skeletal 2D check failed: " #condition)
	Ref<ECSWorld> world; world.instantiate(); PackedInt64Array ids=world->create_entities(4);
	world->set_vector(ids[0],"position",Vector3(240,300,0));
	world->set_parent(ids[1],ids[0]); world->set_parent(ids[2],ids[1]); world->set_parent(ids[3],ids[0]);
	world->set_vector(ids[2],"position",Vector3(100,0,0));
	Dictionary bone; bone["length"]=100.0;
	S2_CHECK(world->set_bone_2d(ids[1],bone) && world->set_bone_2d(ids[2],bone));
	Dictionary rig; rig["bones"]=PackedInt64Array({ids[1],ids[2]});
	S2_CHECK(world->set_skeleton_2d(ids[0],rig));
	Dictionary mesh; mesh["polygon"]=PackedVector2Array({Vector2(0,-30),Vector2(100,-30),Vector2(200,-30),Vector2(200,30),Vector2(100,30),Vector2(0,30)});
	mesh["uv"]=PackedVector2Array({Vector2(0,0),Vector2(.5,0),Vector2(1,0),Vector2(1,1),Vector2(.5,1),Vector2(0,1)});
	mesh["skeleton"]=ids[0];
	mesh["triangles"]=PackedInt32Array({0,1,5,1,4,5,1,2,4,2,3,4});
	PackedInt32Array indices; PackedFloat32Array weights;
	for(int i=0;i<6;i++) { bool middle=i==1 || i==4; int b=(i==2 || i==3)?1:0; for(int j=0;j<4;j++) { indices.push_back(middle?(j==1?1:0):(j==0?b:0)); weights.push_back(middle?(j<2?.5:0):(j==0?1:0)); } }
	mesh["bones"]=indices; mesh["weights"]=weights;
	Ref<Image> image=Image::create_empty(128,32,false,Image::FORMAT_RGBA8);
	for(int y=0;y<32;y++) { for(int x=0;x<128;x++) { bool stripe=(x/8+y/8)%2; image->set_pixel(x,y,stripe?Color(.12,.75,1):Color(.02,.25,.65)); } }
	mesh["texture"]=ImageTexture::create_from_image(image);
	S2_CHECK(world->set_polygon_2d(ids[3],mesh));
	PackedVector2Array rest=world->get_deformed_polygon_2d(ids[3]);
	S2_CHECK(rest.size()==6 && rest[2].is_equal_approx(Vector2(440,270)));
	world->set_vector(ids[2],"rotation",Vector3(0,0,Math::PI*.5));
	PackedVector2Array bent=world->get_deformed_polygon_2d(ids[3]);
	S2_CHECK(bent[2].is_equal_approx(Vector2(370,400)) && bent[0].is_equal_approx(rest[0]));
	Dictionary invalid; PackedFloat32Array bad=weights; bad.set(0,-1); invalid["weights"]=bad;
	S2_CHECK(!world->set_polygon_2d(ids[3],invalid));
	S2_CHECK(world->get_deformed_polygon_2d(ids[3])[2].is_equal_approx(bent[2]));
	Dictionary fewer; fewer["bones"]=PackedInt64Array({ids[1]}); fewer["bind_poses"]=Array();
	S2_CHECK(!world->set_skeleton_2d(ids[0],fewer));
	world->set_vector(ids[2],"rotation",Vector3());
	Ref<Animation> clip; clip.instantiate(); clip->set_length(4); clip->set_loop_mode(Animation::LOOP_LINEAR);
	int track=clip->add_track(Animation::TYPE_VALUE); clip->track_set_path(track,NodePath(".:rotation"));
	clip->track_insert_key(track,0,Vector3()); clip->track_insert_key(track,1,Vector3(0,0,Math::PI*.5)); clip->track_insert_key(track,2,Vector3()); clip->track_insert_key(track,3,Vector3(0,0,-Math::PI*.35)); clip->track_insert_key(track,4,Vector3());
	Ref<Animation> idle=clip->duplicate(true);
	for(int k=0;k<idle->track_get_key_count(0);k++) { idle->track_set_key_value(0,k,Vector3()); }
	Dictionary states; states["wave"]=clip; states["idle"]=idle;
	Dictionary animation; animation["clip"]=clip; animation["targets"]=PackedInt64Array({ids[2]}); animation["states"]=states; animation["state"]="wave";
	S2_CHECK(world->set_animation(ids[0],animation)); world->step_animations(1);
	S2_CHECK(world->get_deformed_polygon_2d(ids[3])[2].is_equal_approx(bent[2]));
	S2_CHECK(world->travel_animation(ids[0],"idle",.5)); world->step_animations(.5);
	S2_CHECK(world->get_vector(ids[2],"rotation").is_equal_approx(Vector3()));
	Dictionary secondary; secondary["clip"]=clip; secondary["weight"]=.5; secondary["time"]=1.0; secondary["speed"]=0.0;
	Dictionary blend; blend["secondary"]=secondary;
	S2_CHECK(world->set_animation(ids[0],blend)); world->step_animations(0);
	S2_CHECK(Math::is_equal_approx(world->get_vector(ids[2],"rotation").z,real_t(Math::PI*.25)));
	world->remove_animation(ids[0]); world->set_vector(ids[2],"rotation",Vector3());
	S2_CHECK(world->set_animation(ids[0],animation));
	Ref<ECSScene> scene; scene.instantiate(); S2_CHECK(scene->capture(world));
	Array entities=scene->get_entities(); const char *names[]={"Skeleton2D","Upper Bone","Lower Bone","Skinned Polygon"};
	for(int i=0;i<entities.size();i++) { Dictionary entity=entities[i]; entity["name"]=names[i]; }
	scene->set_entities(entities);
	S2_CHECK(ResourceSaver::save(scene,path)==OK);
	Ref<ECSScene> loaded=ResourceLoader::load(path,"ECSScene",ResourceLoader::CACHE_MODE_IGNORE); S2_CHECK(loaded.is_valid());
	Ref<ECSWorld> restored=loaded->instantiate(); S2_CHECK(restored.is_valid());
	PackedInt64Array polygons=restored->query(PackedStringArray({"polygon_2d"})); S2_CHECK(polygons.size()==1);
	restored->step_animations(1); S2_CHECK(restored->get_deformed_polygon_2d(polygons[0])[2].is_equal_approx(bent[2]));
	world->remove_animation(ids[0]); world->set_vector(ids[1],"rotation",Vector3()); world->set_vector(ids[2],"rotation",Vector3());
	Dictionary constraint; constraint["chain"]=PackedInt32Array({0,1}); constraint["target"]=Vector2(100,100); constraint["iterations"]=64;
	Dictionary ik; ik["ik"]=Array({constraint}); S2_CHECK(world->set_skeleton_2d(ids[0],ik)); world->solve_ik_2d();
	S2_CHECK(world->get_global_transform(ids[2]).xform(Vector3(100,0,0)).distance_to(Vector3(340,400,0))<.2);
	Dictionary bad_constraint=constraint.duplicate(); bad_constraint["chain"]=PackedInt32Array({1,0}); ik["ik"]=Array({bad_constraint}); S2_CHECK(!world->set_skeleton_2d(ids[0],ik));
	Ref<ECSScene> ik_capture; ik_capture.instantiate(); S2_CHECK(ik_capture->capture(world)); Ref<ECSWorld> ik_roundtrip=ik_capture->instantiate(); S2_CHECK(ik_roundtrip.is_valid()); ik_roundtrip->solve_ik_2d();
	constraint["target"]=Vector2(500,0); ik["ik"]=Array({constraint}); S2_CHECK(world->set_skeleton_2d(ids[0],ik)); world->solve_ik_2d(); S2_CHECK(world->get_vector(ids[1],"rotation").is_finite());
	world->destroy_entity(ids[2]); S2_CHECK(world->get_deformed_polygon_2d(ids[3]).is_empty());
	uint64_t reused=world->create_entity(); S2_CHECK(reused!=uint64_t(ids[2])); S2_CHECK(world->get_deformed_polygon_2d(ids[3]).is_empty());
	world->remove_component(ids[0],"skeleton_2d"); S2_CHECK(world->get_deformed_polygon_2d(ids[3]).is_empty());
	print_line("ECS_SKELETAL_2D_PASS ik_reachable unreachable invalid_chain rest skinning invalid_atomic shrink_rejected keyframes state_blend secondary_blend serialization stale_handles demo_saved");
	return true;
#undef S2_CHECK
}


#ifdef TOOLS_ENABLED
void ECSWorld::hide_authoring_polygon(uint64_t entity) {
    if(!is_alive(entity)) { return; }
    auto *state=polygons_2d.getptr(uint32_t(entity));
    if(state) { RenderingServer::get_singleton()->canvas_item_set_visible(state->item,false); }
}
#endif
