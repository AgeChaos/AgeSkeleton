#include "ecs_world.h"
#include "ecs_animation_path.h"
#include "ecs_scene.h"
#include "core/object/callable_mp.h"

Variant ECSWorld::get_skeleton_slot_value(uint64_t entity, const String &path) const {
	PackedStringArray parts = path.split(":");
	if (parts.size() != 3 || parts[0] != "slot") { return Variant(); }
	String name = parts[1].uri_decode(), field = parts[2];
	for (const Variant &raw : Array(get_skeleton_2d(entity).get("slots", Array()))) {
		Dictionary slot = raw;
		if (String(slot.get("name", String())) == name) { return slot.get(field, Variant()); }
	}
	return Variant();
}

bool ECSWorld::valid_skeletal_animation_value(uint64_t entity, const String &path, const Variant &value) const {
	if (path.get_slice(":",0) == "event") {
		if (value.get_type() != Variant::DICTIONARY) { return false; }
		Dictionary event = value;
		if (event.is_empty()) { return true; } // Empty tracks maintain a shared layout between clips.
		for (const Variant &key : event.keys()) { if (!PackedStringArray({"name", "int", "float", "string"}).has(String(key))) { return false; } }
		Variant name = event.get("name", Variant()), integer = event.get("int", 0), number = event.get("float", 0.0), text = event.get("string", String());
		return name.get_type() == Variant::STRING && !String(name).is_empty() && integer.get_type() == Variant::INT && (number.get_type() == Variant::FLOAT || number.get_type() == Variant::INT) && Math::is_finite(double(number)) && text.get_type() == Variant::STRING;
	}
	PackedStringArray parts = path.split(":");
	if (parts.size() != 3 || parts[0] != "slot" || get_skeleton_slot_value(entity, path).get_type() == Variant::NIL) { return false; }
	if (parts[2] == "attachment") { return value.get_type() == Variant::STRING; } // Missing attachments deliberately hide the slot in the selected skin.
	if (parts[2] == "z_index") { return value.get_type() == Variant::INT && int64_t(value) >= -4096 && int64_t(value) <= 4096; }
	if ((parts[2] == "color" || parts[2] == "dark") && value.get_type() == Variant::COLOR) {
		Color c = value; return Math::is_finite(c.r) && Math::is_finite(c.g) && Math::is_finite(c.b) && Math::is_finite(c.a);
	}
	return false;
}

bool ECSWorld::set_skeleton_slot_value(uint64_t entity, const String &path, const Variant &value) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!valid_skeletal_animation_value(entity, path, value)) { return false; }
	PackedStringArray parts = path.split(":");
	if (parts.size() != 3) { return false; }
	if (get_skeleton_slot_value(entity, path) == value) { return true; }
	Array slots = get_skeleton_2d(entity).get("slots", Array());
	for (const Variant &raw : slots) {
		Dictionary slot = raw;
		if (String(slot["name"]) == parts[1].uri_decode()) {
			slot[parts[2]] = value; Dictionary patch; patch["slots"] = slots; return set_skeleton_2d(entity, patch);
		}
	}
	return false;
}

bool ECSWorld::valid_skeletal_animation_track(uint64_t target, const Ref<Animation> &clip, int track) const {
	String path = ecs_animation_subnames(clip->track_get_path(track));
	if(path=="polygon:frame") {
		if(clip->track_get_type(track)!=Animation::TYPE_VALUE || get_polygon_2d(target).is_empty()) { return false; }
		for(int k=0;k<clip->track_get_key_count(track);k++) {
			Variant raw=clip->track_get_key_value(track,k); if(raw.get_type()!=Variant::DICTIONARY) { return false; } Dictionary frame=raw;
			if(frame.size()!=5 || frame.get("polygon",Variant()).get_type()!=Variant::PACKED_VECTOR2_ARRAY || frame.get("uv",Variant()).get_type()!=Variant::PACKED_VECTOR2_ARRAY || frame.get("color",Variant()).get_type()!=Variant::COLOR || frame.get("z_index",Variant()).get_type()!=Variant::INT || Ref<Texture2D>(frame.get("texture",Variant())).is_null()) { return false; }
			PackedVector2Array points=frame["polygon"],uv=frame["uv"]; if(points.size()!=4 || uv.size()!=4 || int64_t(frame["z_index"]) < -4096 || int64_t(frame["z_index"])>4096) { return false; }
			for(const Vector2 &point:points) { if(!point.is_finite()) { return false; } } for(const Vector2 &point:uv) { if(!point.is_finite()) { return false; } } Color color=frame["color"]; for(int i=0;i<4;i++) { if(!Math::is_finite(color[i])) { return false; } }
		}
		return true;
	}
	if (path.get_slice(":",0) != "event" && !path.begins_with("slot:")) { return true; }
	if (clip->track_get_type(track) != Animation::TYPE_VALUE) { return false; }
	if (path.get_slice(":",0) != "event" && get_skeleton_slot_value(target, path).get_type() == Variant::NIL) { return false; }
	for (int k = 0; k < clip->track_get_key_count(track); k++) {
		double time = clip->track_get_key_time(track, k);
		if (!Math::is_finite(time) || time < 0 || time > clip->get_length() + 0.000001 || !valid_skeletal_animation_value(target, path, clip->track_get_key_value(track, k))) { return false; }
	}
	return true;
}

Array ECSWorld::sample_animation_events(const Ref<Animation> &clip, double from, double to, bool include_start) {
	Array found;
	if (clip.is_null() || from == to || !Math::is_finite(from) || !Math::is_finite(to) || clip->get_length() <= 0) { return found; }
	struct Hit { double time; int track; int key; Dictionary value; };
	struct Order { bool operator()(const Hit &a, const Hit &b) const { return a.time < b.time || (a.time == b.time && (a.track < b.track || (a.track == b.track && a.key < b.key))); } };
	Vector<Hit> hits;
	bool forward = to > from; double low = MIN(from, to), high = MAX(from, to), length = clip->get_length();
	for (int t = 0; t < clip->get_track_count(); t++) {
		if (!clip->track_is_enabled(t) || clip->track_get_type(t) != Animation::TYPE_VALUE || String(ecs_animation_subnames(clip->track_get_path(t))).get_slice(":",0) != "event") { continue; }
		for (int k = 0; k < clip->track_get_key_count(t); k++) {
			Dictionary event = clip->track_get_key_value(t, k); if (event.is_empty()) { continue; }
			double at = clip->track_get_key_time(t, k);
			auto add = [&](double position) {
				if ((forward ? position > from && position <= to : position < from && position >= to) || (include_start && position == from)) { hits.push_back({position, t, k, event}); }
			};
			if (clip->get_loop_mode() == Animation::LOOP_NONE) { add(at); }
			else {
				double period = clip->get_loop_mode() == Animation::LOOP_PINGPONG ? length * 2 : length;
				// Bound catch-up after a pathological stall instead of allocating without limit.
				double first = Math::ceil((low - at) / period), last = Math::floor((high - at) / period);
				for (double cycle = MAX(first, last - 1023); cycle <= last; cycle++) { add(at + cycle * period); }
				if (clip->get_loop_mode() == Animation::LOOP_PINGPONG && at > 0 && at < length) {
					double mirrored = period - at; first = Math::ceil((low - mirrored) / period); last = Math::floor((high - mirrored) / period);
					for (double cycle = MAX(first, last - 1023); cycle <= last; cycle++) { add(mirrored + cycle * period); }
				}
			}
		}
	}
	hits.sort_custom<Order>();
	for (int i = 0; i < hits.size(); i++) {
		const Hit &hit = hits[forward ? i : hits.size() - i - 1]; Dictionary result = hit.value.duplicate(); result["track"] = hit.track; found.push_back(result);
	}
	return found;
}

namespace {
class AnimationEventProbe : public RefCounted {
	GDCLASS(AnimationEventProbe,RefCounted);
protected:
	static void _bind_methods() {}
public:
	Ref<ECSWorld> world;
	int count=0;
	bool remove=false;
	void receive(uint64_t entity,const String &name,const Dictionary &data) {
		if(name=="hit" && int64_t(data.get("int",0))==7) { count++; }
		if(remove) { world->destroy_entity(entity); }
	}
};
}

bool ECSWorld::skeleton_animation_channels_self_test() {
	Ref<ECSWorld> world; world.instantiate(); auto ids=world->create_entities(3);
	Dictionary bone; bone["length"]=10.; if(!world->set_bone_2d(ids[1],bone)) { return false; }
	Dictionary slot,named,skin,skins,rig;
	slot["name"]="weapon";slot["attachment"]="sword"; named["sword"]=ids[2]; skin["weapon"]=named; skins["default"]=skin;
	rig["bones"]=PackedInt64Array({ids[1]});rig["slots"]=Array({slot});rig["skins"]=skins;
	if(!world->set_skeleton_2d(ids[0],rig)) { return false; }
	Ref<Animation> clip;clip.instantiate();clip->set_length(1);
	for(const char *path:{"slot:weapon:attachment","slot:weapon:color","slot:weapon:z_index","event:hit"}) { int t=clip->add_track(Animation::TYPE_VALUE);clip->track_set_path(t,NodePath(String(".:")+path));if(t!=1) { clip->value_track_set_update_mode(t,Animation::UPDATE_DISCRETE); } }
	clip->track_insert_key(0,0,String("sword"));clip->track_insert_key(0,.5,String());clip->track_insert_key(0,.75,String("sword"));
	clip->track_insert_key(1,0,Color(1,1,1));clip->track_insert_key(1,.5,Color(1,0,0));
	clip->track_insert_key(2,0,0);clip->track_insert_key(2,.25,3);
	Dictionary event;event["name"]="hit";event["int"]=7;
	for(double t:{0.,.25,.75}) { clip->track_insert_key(3,t,event); }
	if(sample_animation_events(clip,0,.6,true).size()!=2 || sample_animation_events(clip,.8,.1).size()!=2 || !sample_animation_events(clip,.2,.2).is_empty()) { return false; }
	clip->set_loop_mode(Animation::LOOP_LINEAR); if(sample_animation_events(clip,0,2.1,true).size()!=7) { return false; }
	clip->set_loop_mode(Animation::LOOP_PINGPONG); if(sample_animation_events(clip,0,2.1,true).size()!=6) { return false; }
	clip->set_loop_mode(Animation::LOOP_NONE);
	Dictionary anim;anim["clip"]=clip;anim["targets"]=PackedInt64Array({ids[0],ids[0],ids[0],ids[0]});
	if(!world->set_animation(ids[0],anim)) { return false; }
	Ref<AnimationEventProbe> probe;probe.instantiate();probe->world=world;world->connect("animation_event",callable_mp(probe.ptr(),&AnimationEventProbe::receive));
	world->advance_animation_preview(.3);
	if(probe->count!=2 || int64_t(world->get_skeleton_slot_value(ids[0],"slot:weapon:z_index"))!=3 || !Color(world->get_skeleton_slot_value(ids[0],"slot:weapon:color")).is_equal_approx(Color(1,.4,.4))) { return false; }
	world->advance_animation_preview(.3);if(world->is_skeleton_attachment_visible(ids[2])) { return false; }
	world->advance_animation_preview(.2);if(!world->is_skeleton_attachment_visible(ids[2]) || probe->count!=3) { return false; }
	Ref<ECSScene> scene;scene.instantiate();if(!scene->capture(world) || scene->instantiate().is_null()) { return false; }
	Ref<Animation> bad=clip->duplicate(true);bad->track_set_key_value(1,0,String("bad"));Dictionary invalid=anim.duplicate();invalid["clip"]=bad;
	if(world->set_animation(ids[0],invalid)) { return false; }
	Dictionary restart;restart["time"]=0.;restart["playing"]=true;if(!world->set_animation(ids[0],restart)) { return false; }
	probe->remove=true;world->advance_animation_preview(.8);if(world->is_alive(ids[0]) || probe->count!=4) { return false; }
	print_line("SKELETON_CHANNELS_PASS attachment color order events forward reverse loop pingpong zero_seek snapshot invalid_atomic callback_destroy");return true;
}
