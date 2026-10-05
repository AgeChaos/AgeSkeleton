#include "ecs_replication.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "modules/modules_enabled.gen.h"


void ECSReplication::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure_world", "world", "local_peer"), &ECSReplication::configure_world);
	ClassDB::bind_method(D_METHOD("apply_packet", "authenticated_sender", "packet"), &ECSReplication::apply_packet);
	ClassDB::bind_method(D_METHOD("get_entity","id"),&ECSReplication::get_entity);
	ClassDB::bind_method(D_METHOD("register_spawn_template","name","definition","authority","fields"),&ECSReplication::register_spawn_template);
	ClassDB::bind_method(D_METHOD("spawn_packet","id","template"),&ECSReplication::spawn_packet);
	ClassDB::bind_method(D_METHOD("despawn_packet","id"),&ECSReplication::despawn_packet);
	ClassDB::bind_method(D_METHOD("apply_lifecycle","sender","packet"),&ECSReplication::apply_lifecycle);
	ClassDB::bind_method(D_METHOD("send_lifecycle","packet","target"),&ECSReplication::send_lifecycle,DEFVAL(0));
	ClassDB::bind_method(D_METHOD("set_interpolation_duration","seconds"),&ECSReplication::set_interpolation_duration);
	ClassDB::bind_method(D_METHOD("advance_interpolation","delta"),&ECSReplication::advance_interpolation);
	ClassDB::bind_method(D_METHOD("begin_session","session"),&ECSReplication::begin_session);
	ClassDB::bind_method(D_METHOD("authorize_session","peer","session"),&ECSReplication::authorize_session);
	ClassDB::bind_method(D_METHOD("register_entity","id","entity","authority","fields"),&ECSReplication::register_entity);
	ClassDB::bind_method(D_METHOD("unregister_entity","id"),&ECSReplication::unregister_entity);
	ClassDB::bind_method(D_METHOD("set_peer","peer"),&ECSReplication::set_peer);
	ClassDB::bind_method(D_METHOD("send_snapshot","target"),&ECSReplication::send_snapshot,DEFVAL(0));
	ClassDB::bind_method(D_METHOD("poll"),&ECSReplication::poll);
	ClassDB::bind_method(D_METHOD("configure","world","scene","local_peer"),&ECSReplication::configure);
	ClassDB::bind_method(D_METHOD("snapshot"),&ECSReplication::snapshot);
	ClassDB::bind_method(D_METHOD("apply_snapshot","authenticated_sender","packet"),&ECSReplication::apply_snapshot);
}
bool ECSReplication::configure_world(const Ref<ECSWorld> &p_world, int p_local_peer) {
	if (p_world.is_null() || p_local_peer <= 0) {
		return false;
	}
	// Detach synchronization only; entity lifetime belongs to the caller.
	peer.unref();
	world = p_world;
	local_peer = p_local_peer;
	entries.clear();
	interpolations.clear();
	spawn_templates.clear();
	lifecycle_received.clear();
	spawned_ids.clear();
	retired_ids.clear();
	received.clear();
	sessions.clear();
	sequence = 0;
	session = 1;
	interpolation_duration = 0;
	return true;
}

bool ECSReplication::apply_packet(int p_authenticated_sender, const Dictionary &p_packet) {
	return p_packet.has("kind") ? apply_lifecycle(p_authenticated_sender, p_packet) : apply_snapshot(p_authenticated_sender, p_packet);
}

bool ECSReplication::configure(const Ref<ECSWorld> &p_world,const Ref<ECSScene> &p_scene,int p_peer) {
	if(p_world.is_null() || p_scene.is_null() || p_peer<=0) { return false; }
	Array definitions=p_scene->get_entities(); auto ids=p_world->query(PackedStringArray(),true); if(ids.size()<definitions.size()) { return false; }
	// Scene indices refer to initial allocation. Reject a recycled world.
	for(int i=0;i<definitions.size();i++) { if(uint64_t(ids[i])!=((uint64_t(1)<<32)|uint32_t(i))) { return false; } }
	HashMap<String,Entry> next;
	for(int i=0;i<definitions.size();i++) {
		Dictionary entity=definitions[i]; if(!entity.has("replication")) { continue; }
		if(entity["replication"].get_type()!=Variant::DICTIONARY) { return false; }
		Dictionary config=entity["replication"]; Variant raw_id=config.get("id",Variant()),raw_authority=config.get("authority",1),raw_fields=config.get("fields",PackedStringArray({"position","rotation","scale","active"}));
		if(raw_id.get_type()!=Variant::STRING || raw_authority.get_type()!=Variant::INT || int64_t(raw_authority)<=0 || int64_t(raw_authority)>INT32_MAX || raw_fields.get_type()!=Variant::PACKED_STRING_ARRAY) { return false; }
		String id=raw_id; PackedStringArray fields=raw_fields;
		if(id.is_empty() || id.length()>128 || next.has(id) || fields.size()>64) { return false; }
		HashSet<String> seen; for(const String &field:fields) { if((!PackedStringArray({"position","rotation","scale","active"}).has(field) && (!field.begins_with("component:ecs:") || p_world->get_component(ids[i],field.trim_prefix("component:")).is_empty())) || seen.has(field)) { return false; } seen.insert(field); }
		if(next.size()>=4096) { return false; }
		next[id]={uint64_t(ids[i]),int(raw_authority),fields};
	}
	configure_world(p_world, p_peer);
	entries = next;
	return true;
}
bool ECSReplication::set_interpolation_duration(double seconds) {
	if(!Math::is_finite(seconds) || seconds<0 || seconds>5) { return false; }
	advance_interpolation(5); interpolation_duration=seconds; return true;
}
void ECSReplication::advance_interpolation(double delta) {
	if(world.is_null() || !Math::is_finite(delta) || delta<0) { return; }
	Vector<String> finished;
	for(auto &item:interpolations) {
		Interpolation &state=item.value; if(!world->is_alive(state.entity)) { finished.push_back(item.key); continue; }
		state.elapsed=MIN(5.,state.elapsed+delta); double weight=interpolation_duration<=0?1.:CLAMP(state.elapsed/interpolation_duration,0.,1.);
		for(const Variant &raw:state.to.keys()) { String key=raw; Vector3 from=state.from[key],to=state.to[key]; Vector3 value=key=="rotation"?Quaternion::from_euler(from).slerp(Quaternion::from_euler(to),weight).get_euler():from.lerp(to,weight); world->set_vector(state.entity,key,value); }
		if(weight>=1) { finished.push_back(item.key); }
	}
	for(const String &id:finished) { interpolations.erase(id); }
}
bool ECSReplication::begin_session(int64_t value) {
	if(value<=session) { return false; } session=value; sequence=0; return true;
}
bool ECSReplication::authorize_session(int sender,int64_t value) {
	if(sender<=0 || sender==local_peer || value<=0 || (sessions.has(sender) && value<=sessions[sender]) || (!sessions.has(sender) && value<=1)) { return false; }
	bool known=false; for(const auto &prototype:spawn_templates) { known|=prototype.value.authority==sender; } for(const auto &entry:entries) { known|=entry.value.authority==sender; } if(!known) { return false; }
	sessions[sender]=value; received.erase(sender); lifecycle_received.erase(sender);
	for(const auto &entry:entries) { if(entry.value.authority==sender) { interpolations.erase(entry.key); } } return true;
}
bool ECSReplication::register_entity(const String &id,uint64_t entity,int authority,const PackedStringArray &fields) {
	if(world.is_null() || !world->is_alive(entity) || id.is_empty() || id.length()>128 || entries.has(id) || entries.size()>=4096 || authority<=0 || fields.size()>64) { return false; }
	HashSet<String> seen;
	for(const String &field:fields) {
		bool builtin=PackedStringArray({"position","rotation","scale","active"}).has(field);
		if(seen.has(field) || (!builtin && (!field.begins_with("component:") || !field.trim_prefix("component:").begins_with("ecs:") || world->get_component(entity,field.trim_prefix("component:")).is_empty()))) { return false; } seen.insert(field);
	}
	entries[id]={entity,authority,fields}; return true;
}
bool ECSReplication::unregister_entity(const String &id) { interpolations.erase(id); return entries.erase(id); }
bool ECSReplication::set_peer(const Ref<MultiplayerPeer> &value) {
	if(value.is_valid() && (world.is_null() || value->get_unique_id()!=local_peer)) { return false; }
	peer=value; return true;
}
Error ECSReplication::send_snapshot(int target) {
	if(peer.is_null() || peer->get_connection_status()!=MultiplayerPeer::CONNECTION_CONNECTED) { return ERR_UNCONFIGURED; }
	peer->set_target_peer(target); peer->set_transfer_mode(MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED);
	return peer->put_var(snapshot(),false);
}
int ECSReplication::poll() {
	if(peer.is_null()) { return 0; } peer->poll(); int accepted=0;
	for(int n=0;n<64 && peer->get_available_packet_count()>0;n++) {
		int sender=peer->get_packet_peer(); Variant packet;
		if(peer->get_var(packet,false)==OK && packet.get_type()==Variant::DICTIONARY && apply_packet(sender,packet)) { accepted++; }
	}
	return accepted;
}
Dictionary ECSReplication::snapshot() {
	Dictionary packet; if(world.is_null() || sequence==INT64_MAX) { return packet; } Array states;
	for(const auto &entry:entries) {
		if(entry.value.authority!=local_peer || !world->is_alive(entry.value.entity)) { continue; }
		Dictionary fields; for(const String &field:entry.value.fields) { fields[field]=field.begins_with("component:")?Variant(world->get_component(entry.value.entity,field.trim_prefix("component:"))):field=="active"?Variant(world->is_active_self(entry.value.entity)):Variant(world->get_vector(entry.value.entity,field)); }
		Dictionary state; state["id"]=entry.key; state["fields"]=fields; states.push_back(state);
	}
	packet["session"]=session; packet["version"]=1; packet["sequence"]=++sequence; packet["states"]=states; return packet;
}
bool ECSReplication::apply_snapshot(int sender,const Dictionary &packet) {
	if(world.is_null() || sender<=0 || sender==local_peer || packet.get("version",Variant()).get_type()!=Variant::INT || int64_t(packet["version"])!=1 || packet.get("sequence",Variant()).get_type()!=Variant::INT || packet.get("states",Variant()).get_type()!=Variant::ARRAY) { return false; }
	Variant epoch=packet.get("session",1);
	if(epoch.get_type()!=Variant::INT || int64_t(epoch)!=(sessions.has(sender)?sessions[sender]:1)) { return false; }
	int64_t serial=packet["sequence"]; if(serial<=0 || (received.has(sender) && serial<=received[sender])) { return false; }
	Array states=packet["states"]; if(states.size()>4096) { return false; } HashSet<String> seen;
	// Validate the complete packet before touching the world.
	for(const Variant &raw:states) {
		if(raw.get_type()!=Variant::DICTIONARY) { return false; } Dictionary state=raw;
		if(state.get("id",Variant()).get_type()!=Variant::STRING || state.get("fields",Variant()).get_type()!=Variant::DICTIONARY) { return false; }
		String id=state["id"]; const Entry *entry=entries.getptr(id);
		if(!entry || entry->authority!=sender || !world->is_alive(entry->entity) || seen.has(id)) { return false; } seen.insert(id);
		Dictionary fields=state["fields"]; for(const Variant &key:fields.keys()) {
			if(key.get_type()!=Variant::STRING || !entry->fields.has(key)) { return false; }
			Variant value=fields[key]; if(String(key).begins_with("component:")) {
				PackedByteArray current=world->get_component(entry->entity,String(key).trim_prefix("component:"));
				if(value.get_type()!=Variant::PACKED_BYTE_ARRAY || current.is_empty() || PackedByteArray(value).size()!=current.size()) { return false; }
			} else if(String(key)=="active") { if(value.get_type()!=Variant::BOOL) { return false; } } else if(value.get_type()!=Variant::VECTOR3 || !Vector3(value).is_finite()) { return false; }
		}
	}
	for(const Variant &raw:states) { Dictionary state=raw; String id=state["id"]; const Entry &entry=entries[id]; Dictionary fields=state["fields"]; Interpolation pending; pending.entity=entry.entity;
		for(const Variant &key:fields.keys()) { if(String(key).begins_with("component:")) { world->set_component(entry.entity,String(key).trim_prefix("component:"),fields[key]); } else if(String(key)=="active") { world->set_active(entry.entity,fields[key]); } else if(interpolation_duration>0) { pending.from[key]=world->get_vector(entry.entity,key); pending.to[key]=fields[key]; } else { world->set_vector(entry.entity,key,fields[key]); } }
		if(!pending.to.is_empty()) { interpolations[id]=pending; }
	}
	received[sender]=serial; return true;
}
bool ECSReplication::test() {
	// An external transport uses arbitrary live IDs, including recycled slots.
	{
		Ref<ECSWorld> left, right;
		left.instantiate();
		right.instantiate();
		left->destroy_entity(left->create_entity());
		uint64_t source = left->create_entity();
		uint64_t target = right->create_entity();
		Ref<ECSReplication> outgoing, incoming;
		outgoing.instantiate();
		incoming.instantiate();
		PackedStringArray fields({ "position", "active" });
		if (!outgoing->configure_world(left, 10) || !incoming->configure_world(right, 20) ||
				!outgoing->register_entity("unit", source, 10, fields) || !incoming->register_entity("unit", target, 10, fields)) {
			return false;
		}
		left->set_vector(source, "position", Vector3(4, 5, 6));
		Dictionary packet = outgoing->snapshot();
		if (incoming->apply_packet(30, packet) || !incoming->apply_packet(10, packet) || incoming->apply_packet(10, packet) ||
				right->get_vector(target, "position") != Vector3(4, 5, 6) || outgoing->send_snapshot() != ERR_UNCONFIGURED) {
			return false;
		}
		if (incoming->configure_world(right, 0) || incoming->get_entity("unit") != target ||
				!incoming->configure_world(right, 20) || incoming->get_entity("unit") != 0 || !right->is_alive(target)) {
			return false;
		}
	}
	Ref<ECSScene> scene; scene.instantiate(); Dictionary entity,config; config["id"]="player"; config["authority"]=1; config["fields"]=PackedStringArray({"position","active"}); entity["replication"]=config; scene->set_entities(Array({entity}));
	auto a=scene->instantiate(),b=scene->instantiate(); Ref<ECSReplication> sender,receiver; sender.instantiate(); receiver.instantiate(); if(!sender->configure(a,scene,1) || !receiver->configure(b,scene,2)) { return false; }
	auto aid=a->query(PackedStringArray())[0],bid=b->query(PackedStringArray())[0]; a->set_vector(aid,"position",Vector3(3,4,5)); Dictionary packet=sender->snapshot();
	bool ok=!receiver->apply_snapshot(3,packet) && receiver->apply_snapshot(1,packet) && b->get_vector(bid,"position")==Vector3(3,4,5) && !receiver->apply_snapshot(1,packet);
	a->set_vector(aid,"position",Vector3(8,9,10)); packet=sender->snapshot(); Array states=packet["states"]; Dictionary forged; forged["id"]="missing"; forged["fields"]=Dictionary(); states.push_back(forged); packet["states"]=states; a->set_vector(aid,"position",Vector3(8,9,10)); ok &= !receiver->apply_snapshot(1,packet) && b->get_vector(bid,"position")==Vector3(3,4,5);
	ok &= receiver->apply_snapshot(1,sender->snapshot());
	Dictionary old=sender->snapshot(); ok &= sender->begin_session(2); packet=sender->snapshot();
	ok &= !receiver->apply_snapshot(1,packet) && receiver->authorize_session(1,2) && receiver->apply_snapshot(1,packet) && !receiver->apply_snapshot(1,old);
	sender->configure(a,scene,1); receiver->configure(b,scene,2);
	ok &= receiver->set_interpolation_duration(1); Vector3 start=b->get_vector(bid,"position"); a->set_vector(aid,"position",start+Vector3(10,0,0)); ok &= receiver->apply_snapshot(1,sender->snapshot()) && b->get_vector(bid,"position")==start;
	receiver->advance_interpolation(.5); ok &= b->get_vector(bid,"position").is_equal_approx(start+Vector3(5,0,0)); receiver->advance_interpolation(.5); ok &= b->get_vector(bid,"position").is_equal_approx(start+Vector3(10,0,0)); receiver->set_interpolation_duration(0);
	a->register_component("ecs:test",4); b->register_component("ecs:test",4); PackedByteArray bytes; bytes.resize(4); bytes.fill(0); a->set_component(aid,"ecs:test",bytes); b->set_component(bid,"ecs:test",bytes);
	ok &= sender->register_entity("custom",aid,1,PackedStringArray({"component:ecs:test"})) && receiver->register_entity("custom",bid,1,PackedStringArray({"component:ecs:test"}));
	bytes.set(0,42); a->set_component(aid,"ecs:test",bytes); ok &= receiver->apply_snapshot(1,sender->snapshot()) && b->get_component(bid,"ecs:test")==bytes;
	ok &= sender->unregister_entity("custom") && receiver->unregister_entity("custom");
Dictionary prototype; prototype["position"]=Vector3(1,2,3); PackedStringArray spawn_fields({"position","active"});
	ok &= sender->register_spawn_template("unit",prototype,1,spawn_fields) && receiver->register_spawn_template("unit",prototype,1,spawn_fields);
	Dictionary spawn=sender->spawn_packet("unit-1","unit"); ok &= !spawn.is_empty() && !receiver->apply_lifecycle(3,spawn) && receiver->apply_lifecycle(1,spawn) && !receiver->apply_lifecycle(1,spawn);
	uint64_t remote=receiver->get_entity("unit-1"); ok &= remote && b->get_vector(remote,"position")==Vector3(1,2,3);
	a->set_vector(sender->get_entity("unit-1"),"position",Vector3(7,8,9)); ok &= receiver->apply_snapshot(1,sender->snapshot()) && b->get_vector(remote,"position")==Vector3(7,8,9);
	Dictionary despawn=sender->despawn_packet("unit-1"); ok &= receiver->apply_lifecycle(1,despawn) && !b->is_alive(remote) && !receiver->apply_lifecycle(1,spawn) && sender->spawn_packet("unit-1","unit").is_empty();
#ifdef MODULE_ENET_ENABLED
	Ref<MultiplayerPeer> server=Object::cast_to<MultiplayerPeer>(ClassDB::instantiate("ENetMultiplayerPeer")),client=Object::cast_to<MultiplayerPeer>(ClassDB::instantiate("ENetMultiplayerPeer")); if(server.is_null() || client.is_null()) { return false; } server->call("set_bind_ip","127.0.0.1");
	if(int(server->call("create_server",0,1))!=OK) { return false; } Ref<RefCounted> host=server->call("get_host"); if(host.is_null() || int(client->call("create_client","127.0.0.1",host->call("get_local_port")))!=OK) { return false; }
	for(int i=0;i<200 && client->get_connection_status()!=MultiplayerPeer::CONNECTION_CONNECTED;i++) { server->poll(); client->poll(); OS::get_singleton()->delay_usec(1000); }
	ok &= client->get_connection_status()==MultiplayerPeer::CONNECTION_CONNECTED;
	ok &= receiver->configure(b,scene,client->get_unique_id()) && sender->set_peer(server) && receiver->set_peer(client);
	a->set_vector(aid,"position",Vector3(21,22,23));
	for(int i=0;i<200 && b->get_vector(bid,"position")!=Vector3(21,22,23);i++) { sender->poll(); if(i%10==0) { sender->send_snapshot(); } receiver->poll(); OS::get_singleton()->delay_usec(1000); }
	ok &= b->get_vector(bid,"position")==Vector3(21,22,23); server->close(); client->close();
#endif
	return ok;
}
