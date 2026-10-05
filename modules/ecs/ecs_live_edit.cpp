#ifdef TOOLS_ENABLED
#include "ecs_live_edit.h"
#include "core/io/resource.h"
#include "core/object/class_db.h"

Variant ECSLiveEdit::encode(const Variant &value, int depth) {
	if(depth>24) { return Variant(); }
	if(value.get_type()==Variant::OBJECT) {
		Ref<Resource> resource=value;
		if(resource.is_null()) { return Variant(); }
		Dictionary result,fields;
		result["_ecs_resource"]=resource->get_class();
		List<PropertyInfo> properties; resource->get_property_list(&properties);
		for(const PropertyInfo &property:properties) {
			if(ClassDB::get_property_getter(resource->get_class(),property.name)!=StringName() && ClassDB::get_property_setter(resource->get_class(),property.name)==StringName()) { continue; }
			if(!(property.usage&PROPERTY_USAGE_STORAGE) || (property.usage&PROPERTY_USAGE_READ_ONLY) || property.name=="script" || property.name=="resource_path" || property.name=="resource_scene_unique_id") { continue; }
			fields[property.name]=encode(resource->get(property.name),depth+1);
		}
		result["fields"]=fields;
		return result;
	}
	if(value.get_type()==Variant::DICTIONARY) {
		Dictionary source=value,result;
		for(const Variant &key:source.keys()) { result[key]=encode(source[key],depth+1); }
		return result;
	}
	if(value.get_type()==Variant::ARRAY) {
		Array source=value,result;
		for(const Variant &item:source) { result.push_back(encode(item,depth+1)); }
		return result;
	}
	return value;
}
Variant ECSLiveEdit::decode(const Variant &value,bool &ok,int depth) {
	if(!ok || depth>24) { ok=false; return Variant(); }
	if(value.get_type()==Variant::DICTIONARY) {
		Dictionary source=value,result;
		if(source.has("_ecs_resource")) {
			StringName type=source["_ecs_resource"];
			if(!ClassDB::is_parent_class(type,"Resource") || ClassDB::is_parent_class(type,"Script") || !ClassDB::can_instantiate(type)) { ok=false; return Variant(); }
			Ref<Resource> resource=Object::cast_to<Resource>(ClassDB::instantiate(type));
			if(resource.is_null() || source.get("fields",Variant()).get_type()!=Variant::DICTIONARY) { ok=false; return Variant(); }
			Dictionary fields=source["fields"];
			for(const Variant &key:fields.keys()) {
				if(String(key)=="script" || String(key)=="resource_path") { ok=false; return Variant(); }
				Variant field=decode(fields[key],ok,depth+1);
				if(!ok) { return Variant(); }
				bool valid=false; resource->set(key,field,&valid);
				if(!valid) { WARN_PRINT("ECS live resource decode rejected: "+String(type)+"/"+String(key)); ok=false; return Variant(); }
			}
			return resource;
		}
		for(const Variant &key:source.keys()) { result[key]=decode(source[key],ok,depth+1); }
		return result;
	}
	if(value.get_type()==Variant::ARRAY) {
		Array source=value,result;
		for(const Variant &item:source) { result.push_back(decode(item,ok,depth+1)); }
		return result;
	}
	if(value.get_type()==Variant::OBJECT || value.get_type()==Variant::CALLABLE || value.get_type()==Variant::RID) { ok=false; return Variant(); }
	return value;
}
#endif
