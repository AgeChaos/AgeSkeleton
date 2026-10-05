#include "ecs_replication.h"

uint64_t ECSReplication::get_entity(const String &id) const { const Entry *entry=entries.getptr(id); return entry && world.is_valid() && world->is_alive(entry->entity)?entry->entity:0; }

bool ECSReplication::register_spawn_template(const String &name,const Dictionary &definition,int authority,const PackedStringArray &fields) {
	if(world.is_null() || name.is_empty() || name.length()>128 || authority<=0 || spawn_templates.has(name) || spawn_templates.size()>=256 || fields.size()>4) { return false; }
	for(const Variant &key:definition.keys()) {
		if(key.get_type()!=Variant::STRING || !PackedStringArray({"name","position","rotation","scale","velocity","active","mesh","material","physics","physics_2d","particles","polygon_2d","ui"}).has(key)) { return false; }
	}
	HashSet<String> seen;
	for(const String &field:fields) { if(!PackedStringArray({"position","rotation","scale","active"}).has(field) || seen.has(field)) { return false; } seen.insert(field); }
	if(definition.has("polygon_2d")) { Dictionary polygon=definition["polygon_2d"]; if(int64_t(polygon.get("skeleton",-1))!=-1) { return false; } }
	Ref<ECSScene> check; check.instantiate(); check->set_entities(Array({definition})); if(check->instantiate().is_null()) { return false; }
	spawn_templates[name]={definition.duplicate(true),authority,fields}; return true;
}
bool ECSReplication::spawn_local(const String &id,const String &name,int sender) {
	const SpawnTemplate *prototype=spawn_templates.getptr(name);
	if(!prototype || prototype->authority!=sender || id.is_empty() || id.length()>128 || entries.has(id) || retired_ids.has(id) || entries.size()>=4096) { return false; }
	const Dictionary &data=prototype->definition; uint64_t entity=world->create_entity(); bool ok=true;
	for(const String &field:{String("position"),String("rotation"),String("scale"),String("velocity")}) { if(data.has(field)) { ok &= world->set_vector(entity,field,data[field]); } }
	if(data.has("mesh")) { ok &= world->set_mesh(entity,data["mesh"],data.get("material",Variant())); }
	if(data.has("ui")) { ok &= world->set_ui(entity,data["ui"]); }
	if(data.has("physics")) { ok &= world->set_physics(entity,data["physics"]); }
	if(data.has("physics_2d")) { ok &= world->set_physics_2d(entity,data["physics_2d"]); }
	if(data.has("particles")) { ok &= world->set_particles(entity,data["particles"]); }
	if(data.has("polygon_2d")) { Dictionary polygon=Dictionary(data["polygon_2d"]).duplicate(true); polygon["skeleton"]=int64_t(0); ok &= world->set_polygon_2d(entity,polygon); }
	if(data.has("active")) { world->set_active(entity,data["active"]); }
	if(!ok || !register_entity(id,entity,sender,prototype->fields)) { world->destroy_entity(entity); return false; }
	spawned_ids.insert(id); return true;
}
Dictionary ECSReplication::spawn_packet(const String &id,const String &name) {
	Dictionary packet; if(sequence==INT64_MAX || !spawn_local(id,name,local_peer)) { return packet; }
	packet["version"]=1; packet["kind"]="spawn"; packet["session"]=session; packet["sequence"]=++sequence; packet["id"]=id; packet["template"]=name; return packet;
}
Dictionary ECSReplication::despawn_packet(const String &id) {
	Dictionary packet; const Entry *entry=entries.getptr(id);
	if(!entry || entry->authority!=local_peer || !spawned_ids.has(id) || sequence==INT64_MAX || retired_ids.size()>=65536) { return packet; }
	world->destroy_entity(entry->entity); unregister_entity(id); spawned_ids.erase(id); retired_ids.insert(id);
	packet["version"]=1; packet["kind"]="despawn"; packet["session"]=session; packet["sequence"]=++sequence; packet["id"]=id; return packet;
}
bool ECSReplication::apply_lifecycle(int sender,const Dictionary &packet) {
	if(world.is_null() || sender<=0 || sender==local_peer || packet.get("version",Variant()).get_type()!=Variant::INT || int64_t(packet["version"])!=1 || packet.get("session",Variant()).get_type()!=Variant::INT || int64_t(packet["session"])!=(sessions.has(sender)?sessions[sender]:1) || packet.get("sequence",Variant()).get_type()!=Variant::INT || packet.get("kind",Variant()).get_type()!=Variant::STRING || packet.get("id",Variant()).get_type()!=Variant::STRING) { return false; }
	int64_t serial=packet["sequence"]; if(serial<=0 || (lifecycle_received.has(sender) && serial<=lifecycle_received[sender])) { return false; }
	String kind=packet["kind"],id=packet["id"];
	if(kind=="spawn") {
		if(packet.get("template",Variant()).get_type()!=Variant::STRING || !spawn_local(id,packet["template"],sender)) { return false; }
	} else if(kind=="despawn") {
		const Entry *entry=entries.getptr(id); if(!entry || entry->authority!=sender || !spawned_ids.has(id) || retired_ids.size()>=65536) { return false; }
		world->destroy_entity(entry->entity); unregister_entity(id); spawned_ids.erase(id); retired_ids.insert(id);
	} else { return false; }
	lifecycle_received[sender]=serial; return true;
}
Error ECSReplication::send_lifecycle(const Dictionary &packet,int target) {
	if(peer.is_null() || peer->get_connection_status()!=MultiplayerPeer::CONNECTION_CONNECTED) { return ERR_UNCONFIGURED; }
	if(packet.is_empty() || !packet.has("kind")) { return ERR_INVALID_PARAMETER; }
	peer->set_target_peer(target); peer->set_transfer_mode(MultiplayerPeer::TRANSFER_MODE_RELIABLE); return peer->put_var(packet,false);
}
