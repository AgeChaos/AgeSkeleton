#ifdef TOOLS_ENABLED
#include "ecs_spine_import.h"

namespace {
bool resolve_mesh(Dictionary &skins, const String &skin_name, const String &slot_name, const String &attachment_name, HashSet<String> &visiting, String &error) {
	String key=skin_name+"/"+slot_name+"/"+attachment_name;
	if(visiting.has(key) || visiting.size()>64) { error="Linked mesh cycle: "+key; return false; }
	if(!skins.has(skin_name) || skins[skin_name].get_type()!=Variant::DICTIONARY) { error="Linked mesh skin missing: "+skin_name; return false; }
	Dictionary skin=skins[skin_name];
	if(!skin.has(slot_name) || skin[slot_name].get_type()!=Variant::DICTIONARY) { error="Linked mesh slot missing: "+slot_name; return false; }
	Dictionary slot=skin[slot_name];
	if(!slot.has(attachment_name) || slot[attachment_name].get_type()!=Variant::DICTIONARY) { error="Linked mesh parent missing: "+attachment_name; return false; }
	Dictionary source=slot[attachment_name]; String type=source.get("type","region");
	if(type!="linkedmesh") { if(type!="mesh") { error="Linked mesh parent is not a mesh"; return false; } return true; }
	visiting.insert(key);
	String parent_skin=source.get("skin",String("default")),parent=source.get("parent",String());
	if(!resolve_mesh(skins,parent_skin,slot_name,parent,visiting,error)) { return false; }
	Dictionary geometry=Dictionary(Dictionary(skins[parent_skin])[slot_name])[parent];
	Dictionary resolved=source.duplicate(true);
	for(const char *field:{"vertices","uvs","triangles","hull","edges","width","height"}) { if(geometry.has(field)) { resolved[field]=geometry[field]; } }
	resolved["type"]="mesh"; resolved["path"]=source.get("path",attachment_name);
	slot[attachment_name]=resolved; visiting.erase(key);return true;
}
}

bool ecs_spine_resolve_linked_meshes(Dictionary &skins,String &error) {
	HashSet<String> visiting;
	for(const Variant &skin_name:skins.keys()) {
		if(skins[skin_name].get_type()!=Variant::DICTIONARY) { error="Invalid skin"; return false; } Dictionary skin=skins[skin_name];
		for(const Variant &slot_name:skin.keys()) {
			if(skin[slot_name].get_type()!=Variant::DICTIONARY) { error="Invalid slot"; return false; } Dictionary slot=skin[slot_name];
			for(const Variant &name:slot.keys()) {
				if(slot[name].get_type()!=Variant::DICTIONARY) { error="Invalid attachment"; return false; }
				if(String(Dictionary(slot[name]).get("type","region"))=="linkedmesh" && !resolve_mesh(skins,skin_name,slot_name,name,visiting,error)) { return false; }
			}
		}
	}
	return true;
}
#endif
