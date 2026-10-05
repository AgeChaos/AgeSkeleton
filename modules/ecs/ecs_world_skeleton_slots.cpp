#include "ecs_world.h"
#include "ecs_scene.h"

bool ECSWorld::remap_skeleton_slots(Dictionary &definition,const HashMap<uint64_t,uint64_t> &map) {
	if(definition.has("attachment_library")) {
		if(definition["attachment_library"].get_type()!=Variant::PACKED_INT64_ARRAY) { return false; }
		PackedInt64Array library=definition["attachment_library"];
		for(int i=0;i<library.size();i++) { const uint64_t *mapped=map.getptr(library[i]); if(!mapped) { return false; } library.set(i,*mapped); }
		definition["attachment_library"]=library;
	}
	Variant raw=definition.get("skins",Dictionary()); if(raw.get_type()!=Variant::DICTIONARY) { return false; }
	Dictionary skins=Dictionary(raw).duplicate(true);
	for(const Variant &skin_name:skins.keys()) {
		if(skins[skin_name].get_type()!=Variant::DICTIONARY) { return false; }
		Dictionary slots=skins[skin_name];
		for(const Variant &slot_name:slots.keys()) {
			if(slots[slot_name].get_type()!=Variant::DICTIONARY) { return false; }
			Dictionary attachments=slots[slot_name];
			for(const Variant &name:attachments.keys()) {
				if(attachments[name].get_type()!=Variant::INT) { return false; }
				const uint64_t *mapped=map.getptr(uint64_t(int64_t(attachments[name]))); if(!mapped) { return false; }
				attachments[name]=int64_t(*mapped);
			}
		}
	}
	if(definition.has("skins")) { definition["skins"]=skins; }
	return true;
}
bool ECSWorld::prepare_skeleton_slots(Dictionary &definition, HashMap<uint64_t,Dictionary> &attachments) const {
	Variant raw_slots=definition.get("slots",Array()),raw_skins=definition.get("skins",Dictionary()),raw_skin=definition.get("skin",String("default"));
	if(raw_slots.get_type()!=Variant::ARRAY || raw_skins.get_type()!=Variant::DICTIONARY || raw_skin.get_type()!=Variant::STRING) { return false; }
	Array slot_list=raw_slots; Dictionary skins=raw_skins; String skin=raw_skin;
	Variant raw_library=definition.get("attachment_library",PackedInt64Array());if(raw_library.get_type()!=Variant::PACKED_INT64_ARRAY) { return false; }
	for(int64_t id:PackedInt64Array(raw_library)) { if(!is_alive(id)) { return false; }Dictionary style;style["slot"]=String();style["visible"]=false;attachments[id]=style; }
	if(slot_list.size()>4096 || skins.size()>256 || (!skins.is_empty() && !skins.has(skin))) { return false; }
	HashMap<String,Dictionary> slots_by_name;
	for(int i=0;i<slot_list.size();i++) {
		if(slot_list[i].get_type()!=Variant::DICTIONARY) { return false; }
		Dictionary slot=slot_list[i];
		for(const Variant &key:slot.keys()) { if(!PackedStringArray({"name","attachment","color","dark","z_index","blend"}).has(String(key))) { return false; } }
		Variant name=slot.get("name",Variant()),attachment=slot.get("attachment",String()),color=slot.get("color",Color(1,1,1)),z=slot.get("z_index",i),blend=slot.get("blend",0);
		if(name.get_type()!=Variant::STRING || String(name).is_empty() || slots_by_name.has(name) || attachment.get_type()!=Variant::STRING || color.get_type()!=Variant::COLOR || z.get_type()!=Variant::INT || int64_t(z)<-4096 || int64_t(z)>4096 || blend.get_type()!=Variant::INT || int64_t(blend)<0 || int64_t(blend)>2) { return false; }
		Color tint=color; for(int c=0;c<4;c++) { if(!Math::is_finite(tint[c])) { return false; } }
		Variant dark=slot.get("dark",Color(0,0,0)); if(dark.get_type()!=Variant::COLOR) { return false; } Color shade=dark;for(int c=0;c<4;c++) { if(!Math::is_finite(shade[c])) { return false; } } slot["dark"]=dark;
		slot["attachment"]=attachment; slot["color"]=color; slot["z_index"]=z; slot["blend"]=blend;
		slots_by_name[name]=slot;
	}
	for(const Variant &skin_name:skins.keys()) {
		if(skin_name.get_type()!=Variant::STRING || String(skin_name).is_empty() || skins[skin_name].get_type()!=Variant::DICTIONARY) { return false; }
		Dictionary skin_slots=skins[skin_name];
		for(const Variant &slot_name:skin_slots.keys()) {
			if(!slots_by_name.has(slot_name) || skin_slots[slot_name].get_type()!=Variant::DICTIONARY) { return false; }
			Dictionary named=skin_slots[slot_name];
			for(const Variant &name:named.keys()) {
				if(name.get_type()!=Variant::STRING || String(name).is_empty() || named[name].get_type()!=Variant::INT) { return false; }
				uint64_t id=int64_t(named[name]); if(!is_alive(id)) { return false; }
				if(attachments.has(id) && !String(attachments[id]["slot"]).is_empty() && String(attachments[id]["slot"])!=String(slot_name)) { return false; }
				Dictionary style; style["slot"]=slot_name; style["visible"]=false; attachments[id]=style;
			}
		}
	}
	Variant raw_stack=definition.get("active_skins",PackedStringArray()); if(raw_stack.get_type()!=Variant::PACKED_STRING_ARRAY) { return false; }
	PackedStringArray stack=raw_stack; if(stack.size()>256) { return false; }
	Dictionary selected=Dictionary(skins.get(skin,Dictionary())).duplicate(true),fallback=skins.get("default",Dictionary());
	if(!stack.is_empty()) {
		selected=Dictionary(); HashSet<String> used;
		for(const String &layer:stack) {
			if(!skins.has(layer) || used.has(layer)) { return false; } used.insert(layer); Dictionary layer_slots=skins[layer];
			for(const Variant &slot_name:layer_slots.keys()) { Dictionary named=selected.get(slot_name,Dictionary()); named.merge(layer_slots[slot_name],true); selected[slot_name]=named; }
		}
	}
	Variant raw_placeholders=definition.get("placeholders",Dictionary()); if(raw_placeholders.get_type()!=Variant::DICTIONARY) { return false; }
	Dictionary placeholders=raw_placeholders;
	for(const Variant &slot_name:placeholders.keys()) {
		if(!slots_by_name.has(slot_name) || placeholders[slot_name].get_type()!=Variant::PACKED_STRING_ARRAY) { return false; }
		HashSet<String> names; for(const String &name:PackedStringArray(placeholders[slot_name])) { if(name.is_empty() || names.has(name)) { return false; } names.insert(name); }
	}
	for(const auto &entry:slots_by_name) {
		Dictionary slot=entry.value; String name=slot["attachment"];
		if(name.is_empty()) { continue; }
		Dictionary candidates=Dictionary(fallback.get(entry.key,Dictionary())).duplicate(); candidates.merge(selected.get(entry.key,Dictionary()),true);
		if(!candidates.has(name)) { continue; } // A skin may intentionally omit this slot.
		uint64_t id=int64_t(candidates[name]); Dictionary style=slot.duplicate(); style["slot"]=entry.key; style["visible"]=true; attachments[id]=style;
	}
	PackedInt64Array library;for(const auto &entry:attachments) { library.push_back(entry.key); }definition["attachment_library"]=library;
	definition["slots"]=slot_list; definition["skins"]=skins; definition["skin"]=skin;
	return true;
}

bool ECSWorld::set_skeleton_skin(uint64_t entity,const String &skin) {
	Dictionary definition=get_skeleton_2d(entity); if(definition.is_empty() || !Dictionary(definition.get("skins",Dictionary())).has(skin)) { return false; }
	Dictionary patch; patch["skin"]=skin; patch["active_skins"]=PackedStringArray(); return set_skeleton_2d(entity,patch);
}
bool ECSWorld::set_skeleton_slot_attachment(uint64_t entity,const String &name,const String &attachment) {
	Dictionary definition=get_skeleton_2d(entity); if(definition.is_empty()) { return false; }
	Array slots=definition.get("slots",Array());
	for(int i=0;i<slots.size();i++) {
		Dictionary slot=slots[i]; if(String(slot.get("name",String()))!=name) { continue; }
		if(!attachment.is_empty()) {
			Dictionary skins=definition.get("skins",Dictionary()),chosen=skins.get(definition.get("skin",String("default")),Dictionary()),fallback=skins.get("default",Dictionary());
			bool available=Dictionary(chosen.get(name,Dictionary())).has(attachment) || Dictionary(fallback.get(name,Dictionary())).has(attachment);
			PackedStringArray stack=definition.get("active_skins",PackedStringArray()); if(!stack.is_empty()) { available=Dictionary(fallback.get(name,Dictionary())).has(attachment); for(const String &layer:stack) { available |= Dictionary(Dictionary(skins[layer]).get(name,Dictionary())).has(attachment); } }
			Dictionary placeholders=definition.get("placeholders",Dictionary()); available |= PackedStringArray(placeholders.get(name,PackedStringArray())).has(attachment);
			if(!available) { return false; }
		}
		slot["attachment"]=attachment; Dictionary patch; patch["slots"]=slots; return set_skeleton_2d(entity,patch);
	}
	return false;
}
Dictionary ECSWorld::get_skeleton_attachment_style(uint64_t entity) const {
	ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,Dictionary());
	for(const auto &rig:skeletons_2d) { if(const Dictionary *style=rig.value.attachments.getptr(entity)) { return style->duplicate(); } }
	return Dictionary();
}
bool ECSWorld::is_skeleton_attachment_visible(uint64_t entity) const {
	if(!is_alive(entity)) { return false; }
	Dictionary style=get_skeleton_attachment_style(entity); return style.is_empty() || bool(style.get("visible",false));
}

bool ECSWorld::skeleton_slots_self_test() {
	Ref<ECSWorld> world; world.instantiate(); auto ids=world->create_entities(4);
	Dictionary bone; bone["length"]=100.; if(!world->set_bone_2d(ids[1],bone)) { return false; }
	Dictionary polygon; polygon["polygon"]=PackedVector2Array({Vector2(),Vector2(10,0),Vector2(0,10)});
	if(!world->set_polygon_2d(ids[2],polygon) || !world->set_polygon_2d(ids[3],polygon)) { return false; }
	Dictionary red_attachment,blue_attachment,red_slots,blue_slots,skins,slot,rig;
	red_attachment["body"]=ids[2]; blue_attachment["body"]=ids[3]; red_slots["body-slot"]=red_attachment; blue_slots["body-slot"]=blue_attachment;
	skins["default"]=red_slots; skins["blue"]=blue_slots; slot["name"]="body-slot";slot["attachment"]="body";
	rig["bones"]=PackedInt64Array({ids[1]}); rig["slots"]=Array({slot}); rig["skins"]=skins;
	if(!world->set_skeleton_2d(ids[0],rig) || !world->is_skeleton_attachment_visible(ids[2]) || world->is_skeleton_attachment_visible(ids[3])) { return false; }
	if(!world->set_skeleton_skin(ids[0],"blue") || world->is_skeleton_attachment_visible(ids[2]) || !world->is_skeleton_attachment_visible(ids[3])) { return false; }
	if(world->set_skeleton_skin(ids[0],"missing") || !world->is_skeleton_attachment_visible(ids[3])) { return false; }
	if(!world->set_skeleton_slot_attachment(ids[0],"body-slot","") || world->is_skeleton_attachment_visible(ids[3])) { return false; }
	if(world->set_skeleton_slot_attachment(ids[0],"body-slot","missing")) { return false; }
	if(!world->set_skeleton_slot_attachment(ids[0],"body-slot","body")) { return false; }
    Dictionary patch;patch["active_skins"]=PackedStringArray({"blue","default"});
    if(!world->set_skeleton_2d(ids[0],patch) || !world->is_skeleton_attachment_visible(ids[2]) || world->is_skeleton_attachment_visible(ids[3])) { return false; }
    patch["active_skins"]=PackedStringArray({"default","blue"});
    if(!world->set_skeleton_2d(ids[0],patch) || world->is_skeleton_attachment_visible(ids[2]) || !world->is_skeleton_attachment_visible(ids[3])) { return false; }
    Dictionary placeholders;placeholders["body-slot"]=PackedStringArray({"empty"});patch["placeholders"]=placeholders;
    if(!world->set_skeleton_2d(ids[0],patch) || !world->set_skeleton_slot_attachment(ids[0],"body-slot","empty") || world->is_skeleton_attachment_visible(ids[3])) { return false; }
    if(!world->set_skeleton_slot_attachment(ids[0],"body-slot","body")) { return false; }
    Dictionary invalid;invalid["active_skins"]=PackedStringArray({"missing"});if(world->set_skeleton_2d(ids[0],invalid)) { return false; }
	Ref<ECSScene> scene;scene.instantiate(); if(!scene->capture(world)) { return false; }
	Ref<ECSWorld> loaded=scene->instantiate(); if(loaded.is_null()) { return false; } auto restored=loaded->query(PackedStringArray(),true);
	if(loaded->is_skeleton_attachment_visible(restored[2]) || !loaded->is_skeleton_attachment_visible(restored[3])) { return false; }
	print_line("SKELETON_SLOTS_PASS skins attachments hidden atomic_invalid snapshot_roundtrip"); return true;
}
