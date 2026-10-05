#pragma once
#include "ecs_scene.h"
#include "scene/main/multiplayer_peer.h"

// Transport-independent ECS replication. The authenticated sender comes from
// the multiplayer transport, never from the packet payload.
class ECSReplication : public RefCounted {
	GDCLASS(ECSReplication, RefCounted);
	Ref<ECSWorld> world;
	Ref<MultiplayerPeer> peer;
	struct Entry { uint64_t entity; int authority; PackedStringArray fields; };
	HashMap<String,Entry> entries;
	HashMap<int,int64_t> received;
	int local_peer=0;
	int64_t sequence=0;
	HashMap<int,int64_t> sessions;
	int64_t session=1;
	struct Interpolation { uint64_t entity; Dictionary from,to; double elapsed=0; };
	HashMap<String,Interpolation> interpolations;
	double interpolation_duration=0;
	struct SpawnTemplate { Dictionary definition; int authority; PackedStringArray fields; };
	HashMap<String,SpawnTemplate> spawn_templates;
	HashMap<int,int64_t> lifecycle_received;
	HashSet<String> retired_ids;
	HashSet<String> spawned_ids;
	bool spawn_local(const String &p_id,const String &p_template,int p_sender);
protected:
	static void _bind_methods();
public:
	// External transports can bind an existing world without scene allocation assumptions.
	bool configure_world(const Ref<ECSWorld> &p_world, int p_local_peer);
	bool apply_packet(int p_authenticated_sender, const Dictionary &p_packet);
	bool configure(const Ref<ECSWorld> &p_world, const Ref<ECSScene> &p_scene, int p_peer);
	bool set_peer(const Ref<MultiplayerPeer> &p_peer);
	Error send_snapshot(int p_target=0);
	int poll();
	uint64_t get_entity(const String &p_id) const;
	bool register_spawn_template(const String &p_name,const Dictionary &p_definition,int p_authority,const PackedStringArray &p_fields);
	Dictionary spawn_packet(const String &p_id,const String &p_template);
	Dictionary despawn_packet(const String &p_id);
	bool apply_lifecycle(int p_sender,const Dictionary &p_packet);
	Error send_lifecycle(const Dictionary &p_packet,int p_target=0);
	bool set_interpolation_duration(double p_seconds);
	void advance_interpolation(double p_delta);
	bool begin_session(int64_t p_session);
	bool authorize_session(int p_peer,int64_t p_session);
	bool register_entity(const String &p_id,uint64_t p_entity,int p_authority,const PackedStringArray &p_fields);
	bool unregister_entity(const String &p_id);
	Dictionary snapshot();
	bool apply_snapshot(int p_authenticated_sender, const Dictionary &p_packet);
	static bool test();
};
