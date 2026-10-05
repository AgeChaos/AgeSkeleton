#ifdef TOOLS_ENABLED
#include "ecs_spine_channels.h"

namespace {
double curve(const Dictionary &frame, double time) {
	Variant value = frame.get("curve", Variant());
	if (value.get_type() == Variant::STRING) { return 0; }
	if (value.get_type() != Variant::ARRAY) { return time; }
	Array c = value; double lo = 0, hi = 1, u = time;
	for (int i=0;i<24;i++) { u=(lo+hi)*.5; double x=3*(1-u)*(1-u)*u*double(c[0])+3*(1-u)*u*u*double(c[2])+u*u*u; if(x<time) { lo=u; } else { hi=u; } }
	return 3*(1-u)*(1-u)*u*double(c[1])+3*(1-u)*u*u*double(c[3])+u*u*u;
}
}

bool ecs_spine_channel_length(const Dictionary &animation, const Array &slots, double &length, String &error) {
	auto fail=[&](const String &message) { error=message; return false; };
	auto frames=[&](const Variant &raw) {
		if(raw.get_type()!=Variant::ARRAY || Array(raw).size()>100000) { return false; }
		double previous=-1;
		for(const Variant &item:Array(raw)) {
			if(item.get_type()!=Variant::DICTIONARY) { return false; } Dictionary frame=item; Variant t=frame.get("time",0.0);
			if((t.get_type()!=Variant::FLOAT && t.get_type()!=Variant::INT) || !Math::is_finite(double(t)) || double(t)<previous || double(t)<0 || double(t)>3600) { return false; }
			previous=t; length=MAX(length,double(t)); Variant interpolation=frame.get("curve",Variant());
			if(interpolation.get_type()==Variant::STRING && String(interpolation)!="stepped") { return false; }
			if(interpolation.get_type()==Variant::ARRAY) { Array c=interpolation; if(c.size()!=4) { return false; } for(const Variant &v:c) { if((v.get_type()!=Variant::FLOAT && v.get_type()!=Variant::INT) || !Math::is_finite(double(v))) { return false; } } }
			else if(interpolation.get_type()!=Variant::NIL && interpolation.get_type()!=Variant::STRING) { return false; }
		}
		return true;
	};
	Variant raw=animation.get("slots",Dictionary()); if(raw.get_type()!=Variant::DICTIONARY) { return fail("Invalid slot timelines"); }
	Dictionary timelines=raw;
	for(const Variant &name:timelines.keys()) {
		bool exists=false; for(const Variant &slot:slots) { exists|=Dictionary(slot).get("name",String())==name; }
		if(!exists || timelines[name].get_type()!=Variant::DICTIONARY) { return fail("Missing animation slot"); }
		Dictionary channels=timelines[name];
		for(const Variant &field:channels.keys()) { if((String(field)!="attachment" && String(field)!="color" && String(field)!="twoColor") || !frames(channels[field])) { return fail("Unsupported or invalid slot timeline: "+String(field)); } }
	}
	if(!frames(animation.get("events",Array())) || !frames(animation.get("drawOrder",animation.get("draworder",Array())))) { return fail("Invalid events or draw order timeline"); }
	return true;
}

bool ecs_spine_import_channels(const Dictionary &animation, const Array &slots, const Dictionary &defaults, const PackedStringArray &event_names, const Ref<Animation> &clip, String &error) {
	auto fail=[&](const String &message) { error=message; return false; };
	Dictionary timelines=animation.get("slots",Dictionary()); Vector<int> order_tracks;
	for(int i=0;i<slots.size();i++) {
		Dictionary slot=slots[i]; String name=slot["name"]; Dictionary channels=timelines.get(name,Dictionary());
		for(const char *field:{"attachment","color","dark","z_index"}) {
			int track=clip->add_track(Animation::TYPE_VALUE); clip->track_set_path(track,NodePath(".:slot:"+name.uri_encode()+":"+field));
			clip->track_insert_key(track,0,slot[field]);
			if(String(field)!="color" && String(field)!="dark") { clip->value_track_set_update_mode(track,Animation::UPDATE_DISCRETE); }
			if(String(field)=="z_index") { order_tracks.push_back(track); continue; }
			Array frames=channels.get(field,Array()); String color_key="color"; if((String(field)=="color" || String(field)=="dark") && channels.has("twoColor")) { frames=channels["twoColor"]; color_key=String(field)=="dark"?"dark":"light"; }
			if(String(field)=="attachment") {
				for(const Variant &raw:frames) { Dictionary frame=raw; Variant attachment=frame.get("name",Variant()); if(attachment.get_type()!=Variant::NIL && attachment.get_type()!=Variant::STRING) { return fail("Invalid attachment key"); } clip->track_insert_key(track,frame.get("time",0.0),attachment.get_type()==Variant::NIL?String():String(attachment)); }
			} else if(!frames.is_empty()) {
				for(const Variant &raw:frames) { Variant color=Dictionary(raw).get(color_key,Variant()); if(color.get_type()!=Variant::STRING || !Color::html_is_valid(color)) { return fail("Invalid slot color key"); } }
				int count=int(Math::ceil(clip->get_length()*60))+1; if(count>216001 || int64_t(count)*slots.size()>2000000) { return fail("Slot color sampling exceeds budget"); }
				for(int k=0;k<count;k++) {
					double t=MIN(clip->get_length(),k/60.0); if(t<double(Dictionary(frames[0]).get("time",0.0))) { continue; }
					int n=0; while(n+1<frames.size() && double(Dictionary(frames[n+1]).get("time",0.0))<=t) { n++; }
					Dictionary a=frames[n]; Color color=Color::html(a[color_key]);
					if(n+1<frames.size()) { Dictionary b=frames[n+1]; double start=a.get("time",0.0),end=b.get("time",0.0); color=color.lerp(Color::html(b[color_key]),end>start?curve(a,(t-start)/(end-start)):0); }
					clip->track_insert_key(track,t,color);
				}
			}
		}
	}
	for(const Variant &raw:Array(animation.get("drawOrder",animation.get("draworder",Array())))) {
		Dictionary frame=raw; Variant offsets_raw=frame.get("offsets",Array()); if(offsets_raw.get_type()!=Variant::ARRAY) { return fail("Invalid draw order offsets"); }
		Vector<int> order,unchanged; order.resize(slots.size()); order.fill(-1); int cursor=0;
		for(const Variant &entry:Array(offsets_raw)) {
			if(entry.get_type()!=Variant::DICTIONARY) { return fail("Invalid draw order offset"); } Dictionary offset=entry; String name=offset.get("slot",String()); int index=-1;
			for(int i=0;i<slots.size();i++) { if(String(Dictionary(slots[i])["name"])==name) { index=i; break; } }
			Variant shift=offset.get("offset",0); if((shift.get_type()!=Variant::INT && shift.get_type()!=Variant::FLOAT) || !Math::is_finite(double(shift)) || double(shift)!=Math::floor(double(shift)) || Math::abs(double(shift))>slots.size() || index<cursor) { return fail("Invalid or unordered draw order slot"); }
			while(cursor<index) { unchanged.push_back(cursor++); }
			int destination=index+int(shift); if(destination<0 || destination>=order.size() || order[destination]!=-1) { return fail("Draw order outside slot range or collision"); } order.write[destination]=cursor++;
		}
		while(cursor<slots.size()) { unchanged.push_back(cursor++); }
		for(int i=order.size()-1;i>=0;i--) { if(order[i]<0) { order.write[i]=unchanged[unchanged.size()-1]; unchanged.resize(unchanged.size()-1); } }
		for(int i=0;i<order.size();i++) { clip->track_insert_key(order_tracks[order[i]],frame.get("time",0.0),i); }
	}
	for(const String &name:event_names) {
		int track=clip->add_track(Animation::TYPE_VALUE); clip->track_set_path(track,NodePath(".:event:"+name.uri_encode())); clip->value_track_set_update_mode(track,Animation::UPDATE_DISCRETE);
		Variant base=defaults.get(name,Dictionary()); if(base.get_type()!=Variant::DICTIONARY) { return fail("Invalid event defaults"); }
		if(Dictionary(base).has("audio")) { return fail("Audio event attachments are not supported"); }
		for(const Variant &raw:Array(animation.get("events",Array()))) {
			Dictionary frame=raw; if(String(frame.get("name",String()))!=name) { continue; }
			Variant integer=frame.get("int",Dictionary(base).get("int",0)); if((integer.get_type()!=Variant::INT && integer.get_type()!=Variant::FLOAT) || !Math::is_finite(double(integer)) || double(integer)!=Math::floor(double(integer)) || Math::abs(double(integer))>2147483647) { return fail("Invalid integer event value"); }
			Dictionary event; event["name"]=name; event["int"]=int64_t(integer); event["float"]=frame.get("float",Dictionary(base).get("float",0.0)); event["string"]=frame.get("string",Dictionary(base).get("string",String()));
			double at=frame.get("time",0.0); if(clip->track_find_key(track,at,Animation::FIND_MODE_EXACT)>=0) { return fail("Duplicate event name at the same time"); } clip->track_insert_key(track,at,event);
		}
	}
	return true;
}
#endif
