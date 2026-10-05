#include "ecs_animation_graph.h"
#include "ecs_animation_path.h"
#include "ecs_scene.h"

bool ECSAnimationGraph::validate(const Dictionary &graph,const Ref<Animation> &layout) {
	if(layout.is_null() || graph.get("nodes",Variant()).get_type()!=Variant::ARRAY || graph.get("output",Variant()).get_type()!=Variant::INT) { return false; }
	Array nodes=graph["nodes"]; int64_t output=graph["output"]; if(nodes.is_empty() || nodes.size()>64 || output<0 || output>=nodes.size()) { return false; }
	Ref<ECSWorld> checker; checker.instantiate();
	int budget=0;
	Vector<int> visited; visited.resize(nodes.size()); visited.fill(0);
	auto visit=[&](auto &&self,int index)->bool {
		if(++budget>4096) { return false; }
		if(index<0 || index>=nodes.size() || visited[index]==1) { return false; } 
		if(nodes[index].get_type()!=Variant::DICTIONARY) { return false; } Dictionary node=nodes[index]; String type=node.get("type",String()); visited.write[index]=1;
		if(type=="clip") {
			Ref<Animation> clip=node.get("clip",Variant()); if(clip.is_null() || !Math::is_finite(clip->get_length()) || clip->get_length()<=0 || clip->get_track_count()!=layout->get_track_count()) { return false; }
			for(int t=0;t<clip->get_track_count();t++) {
				if(clip->track_get_type(t)!=layout->track_get_type(t) || clip->track_get_path(t)!=layout->track_get_path(t) || clip->track_get_key_count(t)==0) { return false; }
				auto kind=clip->track_get_type(t); if(kind!=Animation::TYPE_VALUE && kind!=Animation::TYPE_POSITION_3D && kind!=Animation::TYPE_ROTATION_3D && kind!=Animation::TYPE_SCALE_3D) { return false; }
				String path=ecs_animation_subnames(clip->track_get_path(t)); bool event=path.get_slice(":",0)=="event";
				for(int k=0;k<clip->track_get_key_count(t);k++) { Variant value=clip->track_get_key_value(t,k);
					if(event) { double at=clip->track_get_key_time(t,k); if(!Math::is_finite(at) || at<0 || at>clip->get_length() || !checker->valid_skeletal_animation_value(0,path,value)) { return false; } continue; }
					if(value.get_type()==Variant::COLOR) { Color color=value; if(!Math::is_finite(color.r) || !Math::is_finite(color.g) || !Math::is_finite(color.b) || !Math::is_finite(color.a)) { return false; } }
					if(value.get_type()==Variant::QUATERNION && !Quaternion(value).is_finite()) { return false; }
					if(kind==Animation::TYPE_VALUE && value.get_type()!=layout->track_get_key_value(t,0).get_type()) { return false; }
					 if(!Math::is_finite(clip->track_get_key_time(t,k))) { return false; } if((value.get_type()==Variant::VECTOR3 && !Vector3(value).is_finite()) || (value.get_type()==Variant::VECTOR2 && !Vector2(value).is_finite()) || (value.get_type()==Variant::FLOAT && !Math::is_finite(double(value)))) { return false; } if(kind==Animation::TYPE_VALUE && value.get_type()!=Variant::VECTOR3 && value.get_type()!=Variant::VECTOR2 && value.get_type()!=Variant::FLOAT && value.get_type()!=Variant::COLOR) { return false; } }
			}
		} else {
			if(type!="blend" && type!="add" && type!="time_scale" && type!="output" && type!="blend_space_2d" && type!="state_machine") { return false; }
			if(node.get("inputs",Variant()).get_type()!=Variant::PACKED_INT32_ARRAY) { return false; } PackedInt32Array inputs=node["inputs"];
			if(type=="state_machine") {
				if(inputs.size()<2 || inputs.size()>32) { return false; }
				for(const String &key:{String("state"),String("request"),String("previous")}) { Variant value=node.get(key,node.get("state",0)); if(value.get_type()!=Variant::INT || int64_t(value)<0 || int64_t(value)>=inputs.size()) { return false; } }
				for(const String &key:{String("duration"),String("elapsed")}) { Variant value=node.get(key,key=="duration"?.2:0.); if((value.get_type()!=Variant::FLOAT && value.get_type()!=Variant::INT) || !Math::is_finite(double(value)) || double(value)<0 || double(value)>3600) { return false; } }
			} else if(type=="blend_space_2d") {
				if(inputs.size()!=3 || node.get("points",Variant()).get_type()!=Variant::PACKED_VECTOR2_ARRAY || node.get("blend_position",Variant()).get_type()!=Variant::VECTOR2) { return false; }
				PackedVector2Array points=node["points"]; if(points.size()!=3 || !Vector2(node["blend_position"]).is_finite()) { return false; }
				for(const Vector2 &point:points) { if(!point.is_finite()) { return false; } }
				if(Math::is_zero_approx((points[1]-points[0]).cross(points[2]-points[0]))) { return false; }
			} else if(inputs.size()!=((type=="blend" || type=="add")?2:1)) { return false; }
			Variant raw_mask=node.get("track_weights",PackedFloat32Array());
			if(raw_mask.get_type()!=Variant::PACKED_FLOAT32_ARRAY) { return false; }
			PackedFloat32Array mask=raw_mask;
			if(!mask.is_empty() && (mask.size()!=layout->get_track_count() || (type!="blend" && type!="add"))) { return false; }
			for(float value:mask) { if(!Math::is_finite(value) || value<0 || value>1) { return false; } }
			Variant parameter=node.get(type=="time_scale"?"speed":"weight",type=="time_scale"?1.0:.5);
			if((parameter.get_type()!=Variant::FLOAT && parameter.get_type()!=Variant::INT) || !Math::is_finite(double(parameter)) || (type=="time_scale" ? Math::abs(double(parameter))>100 : double(parameter)<0 || double(parameter)>1)) { return false; }
			for(int input:inputs) { if(!self(self,input)) { return false; } }
			if(type=="add" && !self(self,inputs[1])) { return false; }
		}
		visited.write[index]=0; return true;
	};
	if(!visit(visit,int(output))) { return false; }
	for(int i=0;i<nodes.size();i++) { if(!visit(visit,i)) { return false; } }
	return true;
}
Variant ECSAnimationGraph::sample(const Dictionary &graph,int track,double time) {
	Array nodes=graph["nodes"];
	int leaf=int(graph["output"]);
	while(String(Dictionary(nodes[leaf])["type"])!="clip") { leaf=PackedInt32Array(Dictionary(nodes[leaf])["inputs"])[0]; }
	Ref<Animation> layout=Dictionary(nodes[leaf])["clip"];
	auto evaluate=[&](auto &&self,int index,double at)->Variant {
		Dictionary node=nodes[index]; String type=node["type"];
		if(type=="clip") {
			Ref<Animation> clip=node["clip"]; double length=clip->get_length(); if(clip->get_loop_mode()==Animation::LOOP_LINEAR) { at=Math::fposmod(at,length); } else if(clip->get_loop_mode()==Animation::LOOP_PINGPONG) { at=Math::pingpong(at,length); } else { at=CLAMP(at,0.0,length); }
			if(clip->track_get_type(track)==Animation::TYPE_POSITION_3D) { Vector3 v; clip->try_position_track_interpolate(track,at,&v); return v; }
			if(clip->track_get_type(track)==Animation::TYPE_SCALE_3D) { Vector3 v; clip->try_scale_track_interpolate(track,at,&v); return v; }
			if(clip->track_get_type(track)==Animation::TYPE_ROTATION_3D) { Quaternion v; clip->try_rotation_track_interpolate(track,at,&v); return v.get_euler(); }
			return clip->value_track_interpolate(track,at);
		}
		PackedInt32Array inputs=node["inputs"]; if(type=="time_scale") { return self(self,inputs[0],at*double(node.get("speed",1.0))); } if(type=="output") { return self(self,inputs[0],at); }
		if(type=="blend_space_2d") {
			PackedVector2Array points=node["points"]; Vector2 position=node["blend_position"],ab=points[1]-points[0],ac=points[2]-points[0],ap=position-points[0];
			double determinant=ab.cross(ac),wb=ap.cross(ac)/determinant,wc=ab.cross(ap)/determinant,wa=1-wb-wc;
			if(wa<0 || wb<0 || wc<0) {
				// Outside the triangle: use its closest edge, preserving continuity.
				double best=INFINITY; Vector3 weights;
				for(int edge=0;edge<3;edge++) { int next=(edge+1)%3; Vector2 line=points[next]-points[edge]; double t=CLAMP((position-points[edge]).dot(line)/line.length_squared(),0.,1.); double distance=position.distance_squared_to(points[edge]+line*t); if(distance<best) { best=distance; weights=Vector3(); weights[edge]=1-t; weights[next]=t; } }
				wa=weights.x; wb=weights.y; wc=weights.z;
			}
			Variant a=self(self,inputs[0],at),b=self(self,inputs[1],at),c=self(self,inputs[2],at);
			switch(a.get_type()) {
				case Variant::VECTOR3: {
					if(layout->track_get_type(track)==Animation::TYPE_ROTATION_3D || String(ecs_animation_subnames(layout->track_get_path(track)))=="rotation") { Quaternion first=Quaternion::from_euler(a); if(wa+wb>0) { first=first.slerp(Quaternion::from_euler(b),wb/(wa+wb)); } return first.slerp(Quaternion::from_euler(c),wc).get_euler(); }
					return Vector3(a)*wa+Vector3(b)*wb+Vector3(c)*wc;
				}
				case Variant::VECTOR2: return Vector2(a)*wa+Vector2(b)*wb+Vector2(c)*wc;
				case Variant::FLOAT: return double(a)*wa+double(b)*wb+double(c)*wc;
				case Variant::COLOR: return Color(a)*wa+Color(b)*wb+Color(c)*wc;
				default: return a;
			}
		}
		int left=inputs[0],right=inputs[1]; double w=node.get("weight",.5);
		if(type=="state_machine") {
			int state=node.get("state",0),previous=node.get("previous",state); double duration=node.get("duration",.2),elapsed=node.get("elapsed",0.);
			left=inputs[previous]; right=inputs[state]; w=duration<=0?1.:CLAMP(elapsed/duration,0.,1.);
		}
		Variant a=self(self,left,at),b=self(self,right,at); PackedFloat32Array mask=node.get("track_weights",PackedFloat32Array()); if(!mask.is_empty()) { w*=mask[track]; } if(a.get_type()!=b.get_type()) { return a; }
		Variant base; if(type=="add") { base=self(self,inputs[1],0); if(base.get_type()!=b.get_type()) { return a; } }
		switch(a.get_type()) {
			case Variant::VECTOR3: {
				// Rotation tracks must interpolate orientation, not Euler coordinates.
				
				if(layout->track_get_type(track)==Animation::TYPE_ROTATION_3D || String(ecs_animation_subnames(layout->track_get_path(track)))=="rotation") {
					Quaternion qa=Quaternion::from_euler(a),qb=Quaternion::from_euler(b);
					return type=="add"?(qa*Quaternion().slerp(Quaternion::from_euler(base).inverse()*qb,w)).get_euler():qa.slerp(qb,w).get_euler();
				}
				return type=="add"?Vector3(a)+(Vector3(b)-Vector3(base))*w:Vector3(a).lerp(Vector3(b),w); }
			case Variant::VECTOR2: return type=="add"?Vector2(a)+(Vector2(b)-Vector2(base))*w:Vector2(a).lerp(Vector2(b),w);
			case Variant::COLOR: return type=="add"?Color(a)+(Color(b)-Color(base))*w:Color(a).lerp(Color(b),w);
			case Variant::FLOAT: return type=="add"?double(a)+(double(b)-double(base))*w:Math::lerp(double(a),double(b),w);
			default: return a;
		}
	};
	return evaluate(evaluate,int(graph["output"]),time);
}
Array ECSAnimationGraph::events(const Dictionary &graph,double from,double to,bool include_start) {
	Array result,nodes=graph["nodes"];
	// Emit events from the dominant branch per track; shared leaves never double-fire.
	int leaf=int(graph["output"]); while(String(Dictionary(nodes[leaf])["type"])!="clip") { leaf=PackedInt32Array(Dictionary(nodes[leaf])["inputs"])[0]; }
	Ref<Animation> layout=Dictionary(nodes[leaf])["clip"];
	for(int track=0;track<layout->get_track_count();track++) {
		if(String(ecs_animation_subnames(layout->track_get_path(track))).get_slice(":",0)!="event") { continue; }
		int index=int(graph["output"]); double start=from,end=to;
		while(String(Dictionary(nodes[index])["type"])!="clip") {
			Dictionary node=nodes[index]; String type=node["type"]; PackedInt32Array inputs=node["inputs"]; int port=0;
			if(type=="time_scale") { double speed=node.get("speed",1.); start*=speed; end*=speed; }
			else if(type=="blend" || type=="add") { double weight=node.get("weight",.5); PackedFloat32Array mask=node.get("track_weights",PackedFloat32Array()); if(!mask.is_empty()) { weight*=mask[track]; } port=weight>=.5?1:0; }
			else if(type=="state_machine") { int state=node.get("state",0),previous=node.get("previous",state); double duration=node.get("duration",.2),elapsed=node.get("elapsed",0.); port=duration<=0 || elapsed>=duration*.5?state:previous; }
			else if(type=="blend_space_2d") { PackedVector2Array points=node["points"]; Vector2 position=node["blend_position"]; double distance=INFINITY; for(int i=0;i<points.size();i++) { double candidate=position.distance_squared_to(points[i]); if(candidate<distance) { distance=candidate; port=i; } } }
			index=inputs[port];
		}
		Ref<Animation> clip=Dictionary(nodes[index])["clip"];
		for(const Variant &raw:ECSWorld::sample_animation_events(clip,start,end,include_start)) { Dictionary event=raw; if(int(event["track"])==track) { result.push_back(event); } }
	}
	return result;
}
void ECSAnimationGraph::advance(Dictionary &graph,double delta) {
	Array nodes=graph["nodes"];
	for(const Variant &raw:nodes) { if(raw.get_type()!=Variant::DICTIONARY) { continue; } Dictionary node=raw; if(String(node.get("type",""))!="state_machine") { continue; }
		int state=node.get("state",0),request=node.get("request",state); double elapsed=node.get("elapsed",0.),duration=node.get("duration",.2);
		if(request!=state && elapsed>=duration) { node["previous"]=state; node["state"]=request; elapsed=0; }
		else if(!node.has("previous")) { node["previous"]=state; if(request!=state) { node["state"]=request; elapsed=0; } }
		node["elapsed"]=MIN(duration,elapsed+MAX(0.,delta));
	}
}
bool ECSAnimationGraph::test() {
	Ref<Animation> a,b; a.instantiate(); int track=a->add_track(Animation::TYPE_VALUE); a->track_set_path(track,NodePath(":position")); a->track_insert_key(track,0,Vector3(2,0,0)); b=a->duplicate(true); b->track_set_key_value(track,0,Vector3(10,0,0));
	Dictionary first,second,mix,graph; first["type"]="clip"; first["clip"]=a; second["type"]="clip"; second["clip"]=b; mix["type"]="blend"; mix["inputs"]=PackedInt32Array({0,1}); mix["weight"]=.25; graph["nodes"]=Array({first,second,mix}); graph["output"]=2;
	bool ok=validate(graph,a) && Vector3(sample(graph,0,0)).is_equal_approx(Vector3(4,0,0)); Dictionary animation,entity; animation["clip"]=a; animation["graph"]=graph; entity["animation"]=animation;
	Ref<ECSScene> scene; scene.instantiate(); scene->set_entities(Array({entity})); Ref<ECSWorld> world=scene->instantiate(); if(world.is_null()) { return false; }
	auto id=world->query(PackedStringArray())[0]; world->step(.1); ok &= world->get_vector(id,"position").is_equal_approx(Vector3(4,0,0));
	Dictionary update; update["time"]=0.; ok &= world->set_animation(id,update); world->step(.1); ok &= world->get_vector(id,"position").is_equal_approx(Vector3(4,0,0));
	mix["track_weights"]=PackedFloat32Array({0}); graph["nodes"]=Array({first,second,mix}); ok &= validate(graph,a) && Vector3(sample(graph,0,0))==Vector3(2,0,0);
	mix["track_weights"]=PackedFloat32Array({NAN}); ok &= !validate(graph,a); mix.erase("track_weights");
	Dictionary space; space["type"]="blend_space_2d"; space["inputs"]=PackedInt32Array({0,1,0}); space["points"]=PackedVector2Array({Vector2(),Vector2(1,0),Vector2(0,1)}); space["blend_position"]=Vector2(.25,.25); graph["nodes"]=Array({first,second,space});
	ok &= validate(graph,a) && Vector3(sample(graph,0,0)).is_equal_approx(Vector3(4,0,0));
	space["blend_position"]=Vector2(2,0); ok &= Vector3(sample(graph,0,0)).is_equal_approx(Vector3(10,0,0));
	space["points"]=PackedVector2Array({Vector2(),Vector2(),Vector2()}); ok &= !validate(graph,a);
	Dictionary machine; machine["type"]="state_machine"; machine["inputs"]=PackedInt32Array({0,1}); machine["state"]=0; machine["request"]=1; machine["duration"]=1.; graph["nodes"]=Array({first,second,machine});
	ok &= validate(graph,a); advance(graph,.5); ok &= Vector3(sample(graph,0,0)).is_equal_approx(Vector3(6,0,0)); advance(graph,.5); ok &= Vector3(sample(graph,0,0)).is_equal_approx(Vector3(10,0,0));
	int event_track=a->add_track(Animation::TYPE_VALUE); a->track_set_path(event_track,NodePath(":event:step")); a->value_track_set_update_mode(event_track,Animation::UPDATE_DISCRETE); Dictionary event; event["name"]="step"; a->track_insert_key(event_track,.25,event);
	b=a->duplicate(true); second["clip"]=b; mix["inputs"]=PackedInt32Array({0,1}); mix["weight"]=1.; graph["nodes"]=Array({first,second,mix}); ok &= validate(graph,a) && events(graph,0,.5,false).size()==1 && events(graph,.5,0,false).size()==1;
	mix["inputs"]=PackedInt32Array({2,1}); graph["nodes"]=Array({first,second,mix}); return ok && !validate(graph,a);
}
