#ifdef TOOLS_ENABLED
#include "ecs_spine_import.h"
#include "ecs_spine_channels.h"
#include "core/io/json.h"
#include "core/math/geometry_2d.h"
#include "core/io/file_access.h"
#include "scene/resources/image_texture.h"
namespace {
Ref<Image> atlas_image(const String &path,const String &region,String &error) {
	String atlas_path=path.get_basename()+".atlas"; if(!FileAccess::exists(atlas_path)) { return Ref<Image>(); }
	PackedStringArray lines=FileAccess::get_file_as_string(atlas_path).split("\n"); String page,current; bool new_page=true; Dictionary regions,entry;
	for(String line:lines) {
		line=line.strip_edges(); if(line.is_empty()) { new_page=true; continue; }
		int colon=line.find(":");
		if(colon<0) { if(new_page) { page=line; new_page=false; current=String(); } else { current=line; entry=Dictionary(); entry["page"]=page; regions[current]=entry; } }
		else if(!current.is_empty()) { entry[line.substr(0,colon).strip_edges()]=line.substr(colon+1).strip_edges(); }
	}
	if(!regions.has(region)) { return Ref<Image>(); } Dictionary r=regions[region];
	auto numbers=[&](const String &key) { PackedInt32Array result; for(const String &n:String(r.get(key,String())).split(",",false)) { result.push_back(n.strip_edges().to_int()); } return result; };
	PackedInt32Array xy=numbers("xy"),size=numbers("size"),bounds=numbers("bounds"),orig=numbers("orig"),offset=numbers("offset"),offsets=numbers("offsets");
	if(bounds.size()==4) { xy=PackedInt32Array({bounds[0],bounds[1]}); size=PackedInt32Array({bounds[2],bounds[3]}); }
	if(offsets.size()==4) { offset=PackedInt32Array({offsets[0],offsets[1]}); orig=PackedInt32Array({offsets[2],offsets[3]}); }
	if(xy.size()!=2 || size.size()!=2 || size[0]<=0 || size[1]<=0) { error="Invalid atlas region: "+region; return Ref<Image>(); }
	Ref<Image> image=Image::load_from_file(path.get_base_dir().path_join(r["page"])); if(image.is_null()) { error="Atlas page missing"; return image; }
	String rotation=r.get("rotate","false"); if(rotation!="false" && rotation!="true" && rotation!="0" && rotation!="90") { error="Unsupported atlas rotation"; return Ref<Image>(); }
	bool rotated=rotation=="true" || rotation=="90"; Rect2i rect(xy[0],xy[1],rotated?size[1]:size[0],rotated?size[0]:size[1]);
	if(rect.position.x<0 || rect.position.y<0 || rect.get_end().x>image->get_width() || rect.get_end().y>image->get_height()) { error="Atlas region outside page"; return Ref<Image>(); }
	image=image->get_region(rect); if(rotated) { image->rotate_90(CLOCKWISE); }
	if(orig.size()==2 && offset.size()==2) {
		if(orig[0]<1 || orig[1]<1 || orig[0]>16384 || orig[1]>16384 || offset[0]<0 || offset[1]<0 || offset[0]+image->get_width()>orig[0] || offset[1]+image->get_height()>orig[1]) { error="Invalid atlas trim offsets"; return Ref<Image>(); }
		Ref<Image> full=Image::create_empty(orig[0],orig[1],false,Image::FORMAT_RGBA8); image->convert(Image::FORMAT_RGBA8); full->fill(Color(0,0,0,0)); full->blit_rect(image,Rect2i(0,0,image->get_width(),image->get_height()),Vector2i(offset[0],orig[1]-offset[1]-image->get_height())); image=full;
	}
	return image;
}
// Preserve untrimmed attachment geometry and skinning while sharing texture pages.
void pack_attachment_textures(Array &entities, const Vector<int> &indices) {
	if (indices.is_empty()) { return; }
	Vector<Size2i> sizes;
	Vector<Ref<Image>> images;
	for (int index : indices) {
		Dictionary entity = entities[index], polygon = entity["polygon_2d"];
		Ref<Texture2D> texture = polygon["texture"];
		Ref<Image> image = texture->get_image();
		image->convert(Image::FORMAT_RGBA8);
		images.push_back(image);
		sizes.push_back(image->get_size() + Vector2i(4, 4));
	}
	Vector<Point2i> positions;
	Size2i size;
	Geometry2D::make_atlas(sizes, positions, size);
	// Bound pages for mobile/Web. Oversized individual images keep their texture.
	if (size.x > 2048 || size.y > 2048) {
		if (indices.size() == 1) { return; }
		Vector<int> first, second;
		for (int i = 0; i < indices.size(); i++) {
			if (i < indices.size() / 2) { first.push_back(indices[i]); }
			else { second.push_back(indices[i]); }
		}
		pack_attachment_textures(entities, first);
		pack_attachment_textures(entities, second);
		return;
	}
	Ref<Image> atlas = Image::create_empty(size.x, size.y, false, Image::FORMAT_RGBA8);
	atlas->fill(Color(0, 0, 0, 0));
	for (int i = 0; i < images.size(); i++) {
		Ref<Image> image = images[i];
		Point2i origin = positions[i] + Point2i(2, 2);
		int w = image->get_width(), h = image->get_height();
		atlas->blit_rect(image, Rect2i(0, 0, w, h), origin);
		// Two-pixel edge extrusion protects linear filtering at attachment borders.
		for (int y = -2; y < h + 2; y++) {
			for (int x = -2; x < w + 2; x++) {
				if (x >= 0 && x < w && y >= 0 && y < h) { continue; }
				atlas->set_pixel(origin.x + x, origin.y + y, image->get_pixel(CLAMP(x, 0, w - 1), CLAMP(y, 0, h - 1)));
			}
		}
	}
	Ref<ImageTexture> shared = ImageTexture::create_from_image(atlas);
	for (int i = 0; i < indices.size(); i++) {
		Dictionary entity = entities[indices[i]], polygon = entity["polygon_2d"];
		PackedVector2Array uv = polygon["uv"];
		for (int j = 0; j < uv.size(); j++) {
			uv.set(j, (Vector2(positions[i] + Point2i(2, 2)) + uv[j] * Vector2(images[i]->get_size())) / Vector2(size));
		}
		polygon["uv"] = uv;
		polygon["texture"] = shared;
	}
}
double sample_curve(const Dictionary &key,double t) {
	Variant curve=key.get("curve",Variant()); if(curve.get_type()==Variant::STRING && String(curve)=="stepped") { return 0; }
	if(curve.get_type()==Variant::FLOAT || curve.get_type()==Variant::INT) {
		Array points; points.push_back(curve); points.push_back(key.get("c2",0.0)); points.push_back(key.get("c3",1.0)); points.push_back(key.get("c4",1.0)); curve=points;
	}
	if(curve.get_type()!=Variant::ARRAY) { return t; } Array c=curve; if(c.size()!=4) { return t; }
	double lo=0,hi=1,u=t;
	for(int i=0;i<24;i++) { u=(lo+hi)*.5; double x=3*(1-u)*(1-u)*u*double(c[0])+3*(1-u)*u*u*double(c[2])+u*u*u; if(x<t) { lo=u; } else { hi=u; } }
	return 3*(1-u)*(1-u)*u*double(c[1])+3*(1-u)*u*u*double(c[3])+u*u*u;
}
Vector3 sample_frames(const Array &frames,double t,const String &field,const Vector3 &base) {
	if(frames.is_empty() || t<double(Dictionary(frames[0]).get("time",0.0))) { return base; }
	int index=0; while(index+1<frames.size() && double(Dictionary(frames[index+1]).get("time",0.0))<=t) { index++; }
	auto value=[&](const Dictionary &key) { if(field=="shear") { return Vector3(-Math::deg_to_rad(double(key.get("x",0.0))),-Math::deg_to_rad(double(key.get("y",0.0))),0); } if(field=="rotate") { return Vector3(0,0,-Math::deg_to_rad(double(key.get("angle",key.get("value",0.0))))); } return Vector3(double(key.get("x",field=="scale"?1.0:0.0)),double(key.get("y",field=="scale"?1.0:0.0))*(field=="translate"?-1:1),field=="scale"?1:0); };
	Dictionary a=frames[index]; Vector3 result=value(a);
	if(index+1<frames.size()) { Dictionary b=frames[index+1]; double start=a.get("time",0.0),end=b.get("time",0.0); double weight=end>start?sample_curve(a,CLAMP((t-start)/(end-start),0.0,1.0)):0; result=result.lerp(value(b),weight); }
	return field=="scale"?base*result:base+result;
}
}
Ref<ECSScene> ecs_import_spine_json(const String &path,String &report) {
	auto fail=[&](const String &reason) { report=String(U"Spine 导入失败：")+reason; return Ref<ECSScene>(); };
	Ref<JSON> json; json.instantiate(); if(json->parse(FileAccess::get_file_as_string(path))!=OK || json->get_data().get_type()!=Variant::DICTIONARY) { return fail(String(U"JSON 格式无效。")); }
	Dictionary source=json->get_data(); Array bones=source.get("bones",Array()); if(bones.is_empty() || bones.size()>256) { return fail(String(U"骨骼数量须为 1～256。")); }
	for(const char *key:{"transform","path","physics"}) { if(source.has(key) && Array(source[key]).size()>0) { return fail(String(U"尚不支持约束：")+key); } }
	Array entities; Dictionary root,rig; root["name"]=path.get_file().get_basename(); root["position"]=Vector3(); PackedInt64Array bone_ids; HashMap<String,int> names; Vector<Transform2D> bind;
	entities.push_back(root);
	for(int i=0;i<bones.size();i++) {
		Dictionary b=bones[i]; String name=b.get("name",String()),parent=b.get("parent",String()); if(name.is_empty() || names.has(name) || (!parent.is_empty() && !names.has(parent))) { return fail(String(U"骨骼名称重复或父骨骼顺序无效。")); }
		if(String(b.get("transform","normal"))!="normal") { return fail(String(U"尚不支持特殊继承模式：")+name); }
		Dictionary e,definition; int parent_index=parent.is_empty()?0:names[parent]; Vector3 position(double(b.get("x",0.0)),-double(b.get("y",0.0)),0),rotation(0,0,-Math::deg_to_rad(double(b.get("rotation",0.0)))),scale(double(b.get("scaleX",1.0)),double(b.get("scaleY",1.0)),1);
		e["name"]=name; e["parent"]=parent_index; e["position"]=position; e["rotation"]=rotation; e["scale"]=scale; Vector3 shear(-Math::deg_to_rad(double(b.get("shearX",0.0))),-Math::deg_to_rad(double(b.get("shearY",0.0))),0); e["shear"]=shear; definition["length"]=double(b.get("length",0.0)); e["bone_2d"]=definition; entities.push_back(e); names[name]=i+1; bone_ids.push_back(i+1);
		Transform2D transform(Vector2(Math::cos(rotation.z+shear.x),Math::sin(rotation.z+shear.x))*scale.x,Vector2(-Math::sin(rotation.z+shear.y),Math::cos(rotation.z+shear.y))*scale.y,Vector2(position.x,position.y)); bind.push_back(parent_index?bind[parent_index-1]*transform:transform);
	}
	rig["bones"]=bone_ids; Array constraints; Array ik=source.get("ik",Array());
	for(const Variant &item:ik) { Dictionary c=item; if(double(c.get("mix",1.0))!=1 || bool(c.get("stretch",false)) || bool(c.get("compress",false)) || !bool(c.get("bendPositive",true))) { return fail(String(U"IK 混合、伸缩尚不支持。")); } Array chain_names=c.get("bones",Array()); PackedInt32Array chain; for(const Variant &name:chain_names) { if(!names.has(name)) { return fail("IK bone missing"); } chain.push_back(names[name]-1); } String target=c.get("target",String()); if(!names.has(target)) { return fail("IK target missing"); } Dictionary constraint; constraint["chain"]=chain; constraint["target"]=bind[names[target]-1].get_origin(); constraint["target_bone"]=names[target]-1; constraints.push_back(constraint); }
	if(!constraints.is_empty()) { rig["ik"]=constraints; }
	root["skeleton_2d"]=rig;
	Dictionary all_skins; Variant raw_skins=source.get("skins",Dictionary());
	if(raw_skins.get_type()==Variant::DICTIONARY) { all_skins=raw_skins; }
	else if(raw_skins.get_type()==Variant::ARRAY) {
		for(const Variant &item:Array(raw_skins)) { if(item.get_type()!=Variant::DICTIONARY) { return fail("Invalid skin"); } Dictionary skin=item; String name=skin.get("name",String()); if(name.is_empty() || all_skins.has(name)) { return fail("Invalid or duplicate skin name"); } all_skins[name]=skin.get("attachments",Dictionary()); }
	} else { return fail("Invalid skins"); }
	String linked_error; if(!ecs_spine_resolve_linked_meshes(all_skins,linked_error)) { return fail(linked_error); }
	String base=path.get_base_dir(); Dictionary metadata=source.get("skeleton",Dictionary()); String images=metadata.get("images","images"); Array slots=source.get("slots",Array()),slot_definitions; Dictionary skin_registry;
	HashSet<String> slot_names;
	for(int si=0;si<slots.size();si++) {
		Dictionary slot=slots[si]; String name=slot.get("name",String()),bone_name=slot.get("bone",String()),blend=slot.get("blend","normal");
		if(name.is_empty() || slot_names.has(name) || !names.has(bone_name)) { return fail("Invalid slot name or bone"); } slot_names.insert(name);
		if(blend!="normal" && blend!="additive" && blend!="multiply") { return fail(String(U"尚不支持插槽混合：")+blend); }
		Dictionary definition; definition["name"]=name; definition["bone"]=bone_ids.find(int64_t(names[bone_name])); definition["attachment"]=slot.get("attachment",String()); definition["color"]=Color::html(slot.get("color","ffffffff")); definition["dark"]=Color::html(slot.get("dark","000000")); definition["z_index"]=si; definition["blend"]=blend=="additive"?1:blend=="multiply"?2:0; slot_definitions.push_back(definition);
	}
	for(const Variant &skin_name:all_skins.keys()) {
		if(all_skins[skin_name].get_type()!=Variant::DICTIONARY) { return fail("Invalid skin attachment table"); }
		Dictionary skin=all_skins[skin_name],skin_slots; skin_registry[skin_name]=skin_slots;
		for(const Variant &slot_name:skin.keys()) { if(!slot_names.has(slot_name)) { return fail("Skin references missing slot"); } }
		for(int si=0;si<slots.size();si++) {
			Dictionary slot=slots[si]; String name=slot["name"],bone_name=slot["bone"];
			if(skin.get(name,Dictionary()).get_type()!=Variant::DICTIONARY) { return fail("Invalid slot attachments"); }
			Dictionary attachments=skin.get(name,Dictionary()),attachment_ids; skin_slots[name]=attachment_ids;
			for(const Variant &attachment_key:attachments.keys()) {
				String attachment_name=attachment_key; if(attachments[attachment_key].get_type()!=Variant::DICTIONARY) { return fail("Invalid attachment"); }
				Dictionary attachment=attachments[attachment_key]; String type=attachment.get("type","region"),image_name=attachment.get("path",attachment_name);
				if(type!="region" && type!="mesh") { return fail(String(U"尚不支持附件类型：")+type); }
		String image_path=base.path_join(images).path_join(image_name+".png"); if(!FileAccess::exists(image_path)) { image_path=base.path_join(image_name+".png"); }
		String atlas_error; Ref<Image> image=FileAccess::exists(image_path)?Image::load_from_file(image_path):atlas_image(path,image_name,atlas_error);
		if(image.is_null()) { return fail(atlas_error.is_empty()?String(U"找不到 PNG 图片或同名 .atlas 图集区域：")+image_name:atlas_error); }
		Dictionary e,p; e["name"]=name+" / "+attachment_name; e["parent"]=names[bone_name]; PackedVector2Array points,uv; PackedInt32Array triangles;
		if(type=="region") {
			double w=attachment.get("width",image->get_width()),h=attachment.get("height",image->get_height()); points=PackedVector2Array({Vector2(-w/2,-h/2),Vector2(w/2,-h/2),Vector2(w/2,h/2),Vector2(-w/2,h/2)}); uv=PackedVector2Array({Vector2(0,0),Vector2(1,0),Vector2(1,1),Vector2(0,1)});
			e["position"]=Vector3(double(attachment.get("x",0.0)),-double(attachment.get("y",0.0)),0); e["rotation"]=Vector3(0,0,-Math::deg_to_rad(double(attachment.get("rotation",0.0)))); e["scale"]=Vector3(double(attachment.get("scaleX",1.0)),double(attachment.get("scaleY",1.0)),1);
		} else {
			Array vertices=attachment.get("vertices",Array()),texcoords=attachment.get("uvs",Array()),faces=attachment.get("triangles",Array()); if(texcoords.size()%2 || texcoords.size()<6) { return fail("Invalid mesh UV"); }
			for(int i=0;i<texcoords.size();i+=2) { uv.push_back(Vector2(texcoords[i],texcoords[i+1])); }
			if(vertices.size()==texcoords.size()) { for(int i=0;i<vertices.size();i+=2) { points.push_back(Vector2(double(vertices[i]),-double(vertices[i+1]))); } }
			else { e["parent"]=0; PackedInt32Array indices; PackedFloat32Array weights; int cursor=0;
				for(int i=0;i<uv.size();i++) { if(cursor>=vertices.size()) { return fail("Truncated weighted mesh"); } int count=vertices[cursor++]; if(count<1 || count>4 || cursor+count*4>vertices.size()) { return fail(String(U"每顶点仅支持 1～4 个骨骼权重。")); } Vector2 point; double sum=0;
					for(int j=0;j<4;j++) { int bone=0; double weight=0; Vector2 local; if(j<count) { bone=vertices[cursor++]; local.x=vertices[cursor++]; local.y=-double(vertices[cursor++]); weight=vertices[cursor++]; if(bone<0 || bone>=bones.size() || !Math::is_finite(weight) || weight<0) { return fail("Invalid mesh weights"); } point+=bind[bone].xform(local)*weight; sum+=weight; } indices.push_back(bone); weights.push_back(weight); }
					if(sum<=0) { return fail("Empty mesh weights"); } points.push_back(point/sum);
				} if(cursor!=vertices.size()) { return fail("Trailing mesh vertex data"); } p["skeleton"]=0; p["bones"]=indices; p["weights"]=weights;
			}
			for(const Variant &face:faces) { triangles.push_back(face); } p["triangles"]=triangles;
		}
		p["polygon"]=points; p["uv"]=uv; p["texture"]=ImageTexture::create_from_image(image); p["z_index"]=si; p["color"]=Color::html(attachment.get("color","ffffffff")); e["name"]=String(skin_name)+" / "+name+" / "+attachment_name; e["polygon_2d"]=p; attachment_ids[attachment_name]=entities.size(); entities.push_back(e);
	}
        }
    }
    PackedInt64Array attachment_library;for(int i=bones.size()+1;i<entities.size();i++) { attachment_library.push_back(i); }rig["attachment_library"]=attachment_library;rig["slots"]=slot_definitions; rig["skins"]=skin_registry; rig["skin"]=skin_registry.has("default")?String("default"):skin_registry.is_empty()?String("default"):String(skin_registry.keys()[0]);
	Dictionary animations=source.get("animations",Dictionary()),states; PackedInt64Array targets;
	for(int i=0;i<bones.size();i++) { for(int j=0;j<4;j++) { targets.push_back(i+1); } }
	PackedStringArray event_names;
    for(const Variant &key:animations.keys()) { if(animations[key].get_type()!=Variant::DICTIONARY) { return fail("Invalid animation"); } Variant events=Dictionary(animations[key]).get("events",Array()); if(events.get_type()!=Variant::ARRAY) { return fail("Invalid event list"); } for(const Variant &raw:Array(events)) { if(raw.get_type()!=Variant::DICTIONARY) { return fail("Invalid event"); } Variant name=Dictionary(raw).get("name",Variant()); if(name.get_type()!=Variant::STRING || String(name).is_empty()) { return fail("Missing event name"); } if(!event_names.has(name)) { event_names.push_back(name); } } }
    if(event_names.size()>256) { return fail("Too many event tracks"); }
    for(int i=0;i<slot_definitions.size()*4+event_names.size();i++) { targets.push_back(0); }
    int total_samples=0;
	for(const Variant &name:animations.keys()) {
		Dictionary animation=animations[name]; for(const Variant &key:animation.keys()) { if(String(key)!="bones" && String(key)!="slots" && String(key)!="events" && String(key)!="drawOrder" && String(key)!="draworder") { return fail(String(U"尚不支持动画轨道：")+String(key)+String(U"（未导入，避免丢失效果）")); } }
		Dictionary timelines=animation.get("bones",Dictionary()); double length=0;
		for(const Variant &bone_name:timelines.keys()) { if(!names.has(bone_name)) { return fail("Animation bone missing"); } Dictionary channels=timelines[bone_name]; for(const Variant &field:channels.keys()) { if(String(field)!="rotate" && String(field)!="translate" && String(field)!="scale" && String(field)!="shear") { return fail("Unsupported bone channel: "+String(field)); } Array frames=channels[field]; double previous=-1; for(const Variant &frame:frames) { Dictionary key=frame; double t=key.get("time",0.0); if(!Math::is_finite(t) || t<previous || t<0 || t>3600) { return fail("Invalid animation time"); } previous=t; length=MAX(length,t); Variant curve=key.get("curve",Variant()); if(curve.get_type()!=Variant::NIL && curve.get_type()!=Variant::STRING && curve.get_type()!=Variant::FLOAT && curve.get_type()!=Variant::INT && (curve.get_type()!=Variant::ARRAY || Array(curve).size()!=4)) { return fail("Unsupported curve encoding"); } } } }
		String channel_error; if(!ecs_spine_channel_length(animation,slot_definitions,length,channel_error)) { return fail(channel_error); }
		Ref<Animation> clip; clip.instantiate(); clip->set_meta("agechaos_timeline_fps",60); clip->set_length(MAX(.01,length)); clip->set_loop_mode(Animation::LOOP_LINEAR);
		for(int i=0;i<bones.size();i++) {
			Dictionary e=entities[i+1],b=bones[i],channels=timelines.get(b.get("name",String()),Dictionary()); const char *fields[]={"translate","rotate","scale","shear"},*properties[]={"position","rotation","scale","shear"};
			for(int j=0;j<4;j++) { int track=clip->add_track(Animation::TYPE_VALUE); clip->track_set_path(track,NodePath(String(".:")+properties[j])); Array frames=Array(channels.get(fields[j],Array())).duplicate(true); Vector3 setup=e[properties[j]];
				// Spine rotate timelines interpolate the shortest angular delta. Keep baked
				// samples unwrapped too, otherwise a 0/360 seam reintroduces full spins.
				if(j==1 && !frames.is_empty()) {
					double previous=0,unwrapped=0;
					for(int f=0;f<frames.size();f++) {
						Dictionary key=frames[f]; double angle=key.get("angle",key.get("value",0.0));
						unwrapped=f==0?Math::wrapf(angle,-180.0,180.0):unwrapped+Math::wrapf(angle-previous,-180.0,180.0);
						previous=angle; key["angle"]=unwrapped;
					}
				}
				int samples=frames.is_empty()?1:MAX(2,int(Math::ceil(length*60))+1); total_samples+=samples; if(total_samples>2000000) { return fail("Animation import exceeds 2 million samples"); }
				for(int k=0;k<samples;k++) { double t=samples==1?0:MIN(length,k/60.0); clip->track_insert_key(track,t,sample_frames(frames,t,fields[j],setup)); }
			}
		}
		if(!ecs_spine_import_channels(animation,slot_definitions,source.get("events",Dictionary()),event_names,clip,channel_error)) { return fail(channel_error); }
		states[name]=clip;
	}
	if(!states.is_empty()) { Dictionary animation; Variant first=states.keys()[0]; animation["clip"]=states[first]; animation["state"]=first; animation["states"]=states; animation["targets"]=targets; root["animation"]=animation; }
	Vector<int> attachment_indices;
	for (int i = 0; i < entities.size(); i++) { if (Dictionary(entities[i]).has("polygon_2d")) { attachment_indices.push_back(i); } }
	pack_attachment_textures(entities, attachment_indices);
	Ref<ECSScene> result; result.instantiate(); result->set_entities(entities); if(result->instantiate().is_null()) { return fail(String(U"转换后的骨架校验失败。")); }
	report=String(U"Spine JSON 已导入：")+itos(bones.size())+String(U" 根骨骼，")+itos(states.size())+String(U" 个动画。曲线按 60 Hz 烘焙，请另存为 AgeChaos 工程。支持独立 PNG 和同名 .atlas；不包含 .skel / .spine 二进制兼容。 "); return result;
}
#endif
