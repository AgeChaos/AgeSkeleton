#ifdef TOOLS_ENABLED
#include "ecs_main_loop.h"
#include "ecs_live_edit.h"
#include "ecs_ui_components.h"
#include "ecs_custom_components.h"
#include "scene/resources/material.h"

Dictionary ECSMainLoop::live_definition(int index) const {
	if(index<0 || index>=editor_scene_entities.size() || !world->is_alive(editor_scene_entities[index])) { return Dictionary(); }
	uint64_t id=editor_scene_entities[index];
	Dictionary result=Dictionary(scene_resource->get_entities()[index]).duplicate(true);
	result["active"]=world->is_active_self(id);
	for(const char *name:{"position","rotation","scale","velocity"}) {
		if(result.has(name) || String(name)!="velocity") { result[name]=world->get_vector(id,name); }
	}
	for(const char *name:{"particles","animation","physics","physics_2d","audio","light","camera","area","area_2d","pin_joint","joint_3d","joint_2d","navigation"}) {
		if(!result.has(name)) { continue; }
		String kind=name;
		Dictionary data=kind=="particles"?world->get_particles(id):kind=="animation"?world->get_animation(id):kind=="physics"?world->get_physics(id):kind=="physics_2d"?world->get_physics_2d(id):kind=="audio"?world->get_audio(id):kind=="light"?world->get_light(id):kind=="camera"?world->get_camera(id):kind=="area"?world->get_area(id):kind=="area_2d"?world->get_area_2d(id):kind=="pin_joint"?world->get_pin_joint(id):kind=="joint_3d"?world->get_joint_3d(id):kind=="joint_2d"?world->get_joint_2d(id):world->get_navigation(id);
		// References in authoring data are scene indices, not runtime handles.
		for(const char *key:{"targets","bones","body_a","body_b"}) {
			if(Dictionary(result[name]).has(key)) { data[key]=Dictionary(result[name])[key]; }
			else { data.erase(key); }
		}
		result[name]=data;
	}
	for(const char *name:{"bone_2d","skeleton_2d","polygon_2d"}) {
		if(!result.has(name)) { continue; }
		String kind=name; Dictionary data=kind=="bone_2d"?world->get_bone_2d(id):kind=="skeleton_2d"?world->get_skeleton_2d(id):world->get_polygon_2d(id);
		if(kind=="skeleton_2d") { data["bones"]=Dictionary(result[name])["bones"]; }
		if(kind=="polygon_2d") { data["skeleton"]=Dictionary(result[name]).get("skeleton",-1); }
		result[name]=data;
	}
	if(result.has("mesh")) { result["mesh"]=world->get_mesh(id); result["material"]=world->get_material(id); }
	// UI keeps the authored component split; runtime writes update it below.
	if(result.has("ui")) { result["ui"]=world->get_ui(id); }
	else if(result.has("ui_layout")) {
		Dictionary legacy; legacy["ui"]=world->get_ui(id);
		Dictionary ui=ECSUIComponents::migrate(legacy);
		for(const Variant &key:result.keys()) { if(ECSUIComponents::is_component(key) && ui.has(key)) { result[key]=ui[key]; } }
	}
	Dictionary schemas=scene_resource->get_custom_schemas().duplicate(true); schemas.merge(ECSCustomComponents::registry(),true);
	for(const Variant &key:result.keys()) {
		if(ECSCustomComponents::is_component(key) && schemas.has(key)) {
			Dictionary values; if(ECSCustomComponents::unpack(schemas[key],world->get_component(id,key),values)) { result[key]=values; }
		}
	}
	return result;
}
bool ECSMainLoop::live_set(int index,const String &property,const Variant &value,bool global) {
	if(index<0 || index>=editor_scene_entities.size()) { return false; }
	uint64_t id=editor_scene_entities[index];
	if(!world->is_alive(id)) { return false; }
	if(global) {
		if(property!="transform" || value.get_type()!=Variant::TRANSFORM3D || !Transform3D(value).is_finite()) { return false; }
		Transform3D local=value;
		uint64_t parent=world->get_parent(id);
		if(parent) {
			Transform3D parent_transform=world->get_global_transform(parent);
			if(Math::is_zero_approx(parent_transform.basis.determinant())) { return false; }
			local=parent_transform.affine_inverse()*local;
		}
		if(Math::is_zero_approx(local.basis.determinant())) { return false; }
		return world->set_vector(id,"position",local.origin) && world->set_vector(id,"rotation",local.basis.get_euler_normalized()) && world->set_vector(id,"scale",local.basis.get_scale());
	}
	if(property=="position" || property=="rotation" || property=="scale" || property=="velocity") {
		return value.get_type()==Variant::VECTOR3 && Vector3(value).is_finite() && world->set_vector(id,property,value);
	}
	if(property=="active") { return value.get_type()==Variant::BOOL && world->set_active(id,value); }
	Dictionary definition=live_definition(index);
	String group=property.get_slice("/",0),field=property.get_slice("/",1);
	if(property=="mesh" || property=="material") {
		if(value.get_type()!=Variant::OBJECT && value.get_type()!=Variant::NIL) { return false; }
		Ref<Mesh> mesh=property=="mesh"?Ref<Mesh>(value):world->get_mesh(id);
		Ref<Material> material=property=="material"?Ref<Material>(value):world->get_material(id);
		if(mesh.is_null() || (property=="material" && !value.is_null() && material.is_null())) { return false; }
		return world->set_mesh(id,mesh,material);
	}
	if(field.is_empty() || !definition.has(group) || definition[group].get_type()!=Variant::DICTIONARY || property.get_slice_count("/")!=2) { return false; }
	if(!Dictionary(definition[group]).has(field)) { return false; }
	Dictionary patch; patch[field]=value;
	if(ECSCustomComponents::is_component(group)) {
		Dictionary schemas=scene_resource->get_custom_schemas().duplicate(true); schemas.merge(ECSCustomComponents::registry(),true);
		if(!schemas.has(group)) { return false; }
		Dictionary values=definition[group]; values[field]=value; PackedByteArray bytes;
		return ECSCustomComponents::pack(schemas[group],values,bytes) && world->set_component(id,group,bytes);
	}
	if(group=="bone_2d") { return world->set_bone_2d(id,patch); }
	if(group=="skeleton_2d") { if(field=="bones") { return false; } return world->set_skeleton_2d(id,patch); }
	if(group=="polygon_2d") { if(field=="skeleton") { return false; } return world->set_polygon_2d(id,patch); }
	if(group=="particles") { return world->set_particles(id,patch); }
	if(group=="animation") { if(field=="targets") { return false; } return world->set_animation(id,patch); }
	if(group=="physics") { return world->set_physics(id,patch); }
	if(group=="physics_2d") { return world->set_physics_2d(id,patch); }
	if(group=="audio") { return world->set_audio(id,patch); }
	if(group=="light") { return world->set_light(id,patch); }
	if(group=="camera") { return world->set_camera(id,patch); }
	if(group=="area") { return world->set_area(id,patch); }
	if(group=="area_2d") { return world->set_area_2d(id,patch); }
	if(group=="joint_3d" || group=="joint_2d" || group=="pin_joint") {
		if(field=="body_a" || field=="body_b") { return false; }
		return group=="joint_3d"?world->set_joint_3d(id,patch):group=="joint_2d"?world->set_joint_2d(id,patch):world->set_pin_joint(id,patch);
	}
	if(group=="navigation") {
		Dictionary data=world->get_navigation(id); data[field]=value;
		return world->set_navigation(id,data.get("mesh",Variant()),data.get("layers",1));
	}
	if(group=="ui" || ECSUIComponents::is_component(group)) {
		Dictionary component=definition[group]; component[field]=value; definition[group]=component;
		Dictionary compiled;
		if(!ECSUIComponents::compose(definition,compiled) || !world->set_ui(id,compiled)) { return false; }
		// This is the runtime process's Resource; it is never saved to disk.
		Array definitions=scene_resource->get_entities().duplicate(true);
		Dictionary authored=definitions[index]; authored[group]=component; definitions[index]=authored;
		scene_resource->set_entities(definitions);
		return true;
	}
	return false;
}
#endif
