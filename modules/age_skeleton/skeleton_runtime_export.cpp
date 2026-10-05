#ifdef TOOLS_ENABLED
#include "skeleton_runtime_export.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/geometry_2d.h"
#include "scene/resources/animation.h"
#include "scene/resources/texture.h"
#include "scene/resources/image_texture.h"
#include "modules/ecs/ecs_sprite_atlas.h"
#include "modules/ecs/ecs_compact_skeleton.h"
#include "modules/ecs/ecs_shared_textures.h"

bool pack_skeleton_atlas_entities(Array &entities, String &error) {
    Vector<Ref<Texture2D>> sources; Vector<Ref<Image>> images;
    for(const Variant &v:entities) {
        Dictionary entity=v;if(!entity.has("polygon_2d")) continue;
        Dictionary mesh=entity["polygon_2d"];Ref<Texture2D> texture=mesh.get("texture",Variant());
        if(texture.is_null()) { error="Attachment has no texture";return false; }
        for(const Vector2 &uv:PackedVector2Array(mesh.get("uv",PackedVector2Array()))) {
            if(!uv.is_finite() || uv.x<0 || uv.y<0 || uv.x>1 || uv.y>1) { error="Atlas export requires UVs within the source image (0..1)";return false; }
        }
        if(!sources.has(texture)) {sources.push_back(texture);images.push_back(texture->get_image());}
    }
    if(sources.size()<=1) return true; // Already a single native page; preserve its original extent.
    SpriteAtlasImages atlas;if(SpriteAtlasBuilder::pack_images(images,atlas)!=OK) {error=atlas.error;return false;}
    Vector<Ref<ImageTexture>> pages;for(const Ref<Image> &image:atlas.pages) pages.push_back(ImageTexture::create_from_image(image));
    for(int i=0;i<entities.size();++i) {
        Dictionary entity=entities[i];if(!entity.has("polygon_2d")) continue;
        Dictionary mesh=entity["polygon_2d"];int source=sources.find(Ref<Texture2D>(mesh["texture"]));int page=atlas.page_indices[source];
        Rect2 region=atlas.regions[source];Vector2 size=pages[page]->get_size();PackedVector2Array uv=mesh["uv"];
        for(int v=0;v<uv.size();++v) uv.set(v,(region.position+uv[v]*region.size)/size);
        mesh["uv"]=uv;mesh["texture"]=pages[page];
    }
    return true;
}

Dictionary export_skeleton_runtime(const Ref<ECSScene> &scene, int rig_index, const String &directory, int fps) {
	Dictionary reply;
	auto fail=[&](const String &reason) { reply["ok"]=false; reply["error"]=reason; return reply; };
	if(scene.is_null() || rig_index<0 || rig_index>=scene->get_entities().size() || fps<1 || fps>120) { return fail("Invalid skeleton or sample rate (1..120)"); }
	Array entities=scene->get_entities().duplicate(true); Dictionary root=entities[rig_index];
	String atlas_error;if(!pack_skeleton_atlas_entities(entities,atlas_error)) { return fail(atlas_error); }
	if(!root.has("skeleton_2d")) { return fail("Select a skeleton entity"); }
	int rigs=0; for(const Variant &e:entities) { if(Dictionary(e).has("skeleton_2d")) { ++rigs; } }
	if(rigs!=1) { return fail("Portable runtime exports one skeleton per project"); }
	Dictionary definition=root["skeleton_2d"], animation=root.get("animation",Dictionary());
	Dictionary named=animation.get("states",Dictionary());
	if(named.is_empty() && animation.has("clip")) { named["default"]=animation["clip"]; }
	Array slots=definition.get("slots",Array()), attachments, textures, skins;
	HashMap<int,int> mesh_indices;
	Vector<Ref<Texture2D>> texture_refs;
	int vertex_count=0;
	for(int i=0;i<entities.size();++i) {
		Dictionary entity=entities[i]; if(!entity.has("polygon_2d")) { continue; }
		Dictionary mesh=entity["polygon_2d"]; Ref<Texture2D> texture=mesh.get("texture",Variant());
		PackedVector2Array points=mesh.get("polygon",PackedVector2Array()),uv=mesh.get("uv",PackedVector2Array());
		if(texture.is_null() || points.size()<3 || uv.size()!=points.size()) { return fail("Every exported attachment needs a texture and matching UVs"); }
		PackedInt32Array indices=mesh.get("triangles",PackedInt32Array()); if(indices.is_empty()) { indices=Geometry2D::triangulate_polygon(points); }
		if(indices.is_empty() || indices.size()%3) { return fail("Invalid mesh triangulation"); }
		for(int index:indices) { if(index<0 || index>=points.size()) { return fail("Invalid vertex index"); } }
		int tex=texture_refs.find(texture); if(tex<0) { tex=texture_refs.size(); texture_refs.push_back(texture); textures.push_back(vformat("atlas_%03d.png",tex)); }
		Dictionary a; a["name"]=entity.get("name",String()); a["texture"]=tex; a["offset"]=vertex_count; a["count"]=points.size(); a["triangles"]=indices; a["slot"]=-1;
		Array coords; for(const Vector2 &v:uv) { coords.push_back(v.x); coords.push_back(v.y); } a["uv"]=coords;
		Color color=mesh.get("color",Color(1,1,1)); a["color"]=Array({color.r,color.g,color.b,color.a});
		mesh_indices[i]=attachments.size(); attachments.push_back(a); vertex_count+=points.size();
	}
	if(attachments.is_empty() || vertex_count>100000) { return fail("Expected 1..100000 vertices"); }
	Dictionary source_skins=definition.get("skins",Dictionary());
	for(const Variant &skin_name:source_skins.keys()) {
		Dictionary skin; skin["name"]=skin_name; Array bindings; Dictionary source=source_skins[skin_name];
		for(int s=0;s<slots.size();++s) {
			Dictionary slot=slots[s], entries=source.get(slot["name"],Dictionary());
			for(const Variant &key:entries.keys()) {
				int entity=entries[key]; const int *index=mesh_indices.getptr(entity); if(!index) { return fail("Skin references a non-mesh attachment"); }
				Dictionary binding; binding["slot"]=s; binding["key"]=key; binding["attachment"]=*index; bindings.push_back(binding);
				Dictionary a=attachments[*index]; a["slot"]=s;
			}
		}
		skin["bindings"]=bindings; skins.push_back(skin);
	}
	for(const Variant &v:slots) {
		Dictionary slot=v; Color dark=slot.get("dark",Color(0,0,0));
		if(int(slot.get("blend",0))!=0 || dark.r!=0 || dark.g!=0 || dark.b!=0) { return fail("Portable runtime supports normal alpha blending, without two-color tint"); }
	}
	int total_frames=1;
	for(const Variant &key:named.keys()) {
		Ref<Animation> clip=named[key]; if(clip.is_null() || clip->get_length()<=0 || clip->get_length()>300) { return fail("Invalid clip duration; maximum 300 seconds"); }
		if(clip->get_loop_mode()==Animation::LOOP_PINGPONG) { return fail("Bake ping-pong as a linear clip before exporting"); }
		total_frames+=int(Math::ceil(clip->get_length()*fps))+1;
		for(int t=0;t<clip->get_track_count();++t) {
			String field=clip->track_get_path(t).get_concatenated_subnames();
			if(field.begins_with("event:") && clip->track_is_enabled(t)) {
                for(int k=0;k<clip->track_get_key_count(t);++k) {Dictionary event=clip->track_get_key_value(t,k);if(event.is_empty())continue;
                    String event_name=event.get("name",String());if(event_name.is_empty()||clip->track_get_key_time(t,k)<0||clip->track_get_key_time(t,k)>clip->get_length())return fail("Invalid runtime event name or time");int64_t value=event.get("int",0);double number=event.get("float",0.);if(value < -2147483648LL || value>2147483647LL || !Math::is_finite(float(number))) return fail("Portable event payload exceeds int32/float32 range");
                }
            }
		}
	}
	if(int64_t(total_frames)*vertex_count>8000000) { return fail("Sample budget exceeded (8 million vertex samples); lower fps or split clips"); }
	String path=ProjectSettings::get_singleton()->globalize_path(directory);
	if(!path.is_absolute_path() || DirAccess::exists(path) || FileAccess::exists(path)) { return fail("Use a new output directory; existing directories are never overwritten"); }
	// All sampling uses isolated worlds; exporting never seeks or mutates the editor scene.
	Ref<ECSWorld> setup=scene->instantiate(); if(setup.is_null()) { return fail("Invalid scene"); }
	PackedInt64Array setup_ids=setup->query(PackedStringArray(),true);
	Transform3D inverse=setup->get_global_transform(setup_ids[rig_index]).affine_inverse();
    auto transform2=[](const Transform3D &v){return Transform2D(Vector2(v.basis[0][0],v.basis[1][0]),Vector2(v.basis[0][1],v.basis[1][1]),Vector2(v.origin.x,v.origin.y));};
    PackedInt64Array bone_entities=definition["bones"];if(bone_entities.is_empty()||bone_entities.size()>256)return fail("Expected 1..256 runtime bones");Array bone_names,parents,lengths,vertex_bones,weights;
    for(int i=0;i<bone_entities.size();++i){int entity=bone_entities[i];Dictionary bone=entities[entity];String name=bone.get("name",String());if(name.is_empty()||bone_names.has(name))return fail("Runtime IK requires unique non-empty bone names");bone_names.push_back(name);lengths.push_back(Dictionary(bone["bone_2d"]).get("length",0.));int p=bone.get("parent",-1);while(p>=0 && bone_entities.find(p)<0)p=Dictionary(entities[p]).get("parent",-1);parents.push_back(bone_entities.find(p));}
    if(int64_t(total_frames)*(vertex_count*10+bone_entities.size()*6)>32000000) return fail("Runtime IK sample budget exceeded; lower fps or split clips");
    for(int i=0;i<entities.size();++i){if(!mesh_indices.has(i))continue;Dictionary entity=entities[i],mesh=entity["polygon_2d"];PackedVector2Array points=mesh["polygon"];bool weighted=int(mesh.get("skeleton",-1))==rig_index;
        PackedInt32Array indices=mesh.get("bones",PackedInt32Array());PackedFloat32Array values=mesh.get("weights",PackedFloat32Array());int parent=entity.get("parent",-1);while(parent>=0 && bone_entities.find(parent)<0)parent=Dictionary(entities[parent]).get("parent",-1);int rigid=bone_entities.find(parent);
        if(weighted && (indices.size()!=points.size()*4 || values.size()!=indices.size()))return fail("Invalid mesh skin influences");
        for(int v=0;v<points.size();++v)for(int k=0;k<4;++k){if(weighted&&(indices[v*4+k]<-1||indices[v*4+k]>=bone_entities.size()||!Math::is_finite(values[v*4+k])||values[v*4+k]<0||values[v*4+k]>1))return fail("Invalid mesh skin influence");vertex_bones.push_back(weighted?indices[v*4+k]:rigid);weights.push_back(weighted?values[v*4+k]:(k==0&&rigid>=0?1.:0.));}
    }
    Dictionary runtime_rig;runtime_rig["names"]=bone_names;runtime_rig["parents"]=parents;runtime_rig["lengths"]=lengths;runtime_rig["vertexBones"]=vertex_bones;runtime_rig["weights"]=weights;
    bool sample_valid=true;
	auto sample=[&](const Ref<ECSWorld> &world,const PackedInt64Array &ids,double time) {
		Dictionary frame; frame["time"]=time; Array positions,keys,colors,orders,matrices,influence_positions;
		for(int i=0;i<entities.size();++i) { if(!mesh_indices.has(i)) { continue; }
			for(const Vector2 &p:world->get_deformed_polygon_2d(ids[i])) { Vector3 v=inverse.xform(Vector3(p.x,p.y,0)); positions.push_back(v.x); positions.push_back(v.y); }
		}
        Transform2D local_root=transform2(world->get_global_transform(ids[rig_index])).affine_inverse();Array bind=world->get_skeleton_2d(ids[rig_index])["bind_poses"];
        for(int bone:bone_entities){Transform2D m=transform2(inverse*world->get_global_transform(ids[bone]));for(int axis=0;axis<3;++axis){matrices.push_back(m[axis].x);matrices.push_back(m[axis].y);}}
        int influence=0;
        for(int i=0;i<entities.size();++i){if(!mesh_indices.has(i))continue;Dictionary mesh=world->get_polygon_2d(ids[i]);PackedVector2Array points=mesh["polygon"];if(points.size()!=int(Dictionary(attachments[mesh_indices[i]])["count"])) {sample_valid=false;return frame;}Transform2D mesh_world=transform2(world->get_global_transform(ids[i]));bool weighted=int64_t(mesh.get("skeleton",0))==ids[rig_index];
            for(const Vector2 &point:points){Vector2 global=mesh_world.xform(point);for(int k=0;k<4;++k){int bone=vertex_bones[influence++];Vector2 value;
                if(bone>=0){if(weighted)value=Transform2D(bind[bone]).affine_inverse().xform(local_root.xform(global));else {Transform2D bone_world=transform2(world->get_global_transform(ids[bone_entities[bone]]));if(Math::is_zero_approx(bone_world.determinant()))sample_valid=false;else value=bone_world.affine_inverse().xform(global);}}
                influence_positions.push_back(value.x);influence_positions.push_back(value.y);
            }}
        }
        frame["matrices"]=matrices;frame["influencePositions"]=influence_positions;
		Array current=world->get_skeleton_2d(ids[rig_index]).get("slots",Array());
		for(int i=0;i<current.size();++i) { Dictionary s=current[i]; keys.push_back(s.get("attachment",String())); Color c=s.get("color",Color(1,1,1)); for(int k=0;k<4;++k) { colors.push_back(c[k]); } orders.push_back(s.get("z_index",i)); }
		frame["positions"]=positions; frame["keys"]=keys; frame["colors"]=colors; frame["orders"]=orders; return frame;
	};
	Dictionary data; data["format"]="ageskeleton.meshclip"; data["version"]=2;data["rig"]=runtime_rig; data["name"]=root.get("name",String("Skeleton")); data["fps"]=fps; data["vertexCount"]=vertex_count; data["textures"]=textures; data["attachments"]=attachments; data["skins"]=skins;
	PackedStringArray active=definition.get("active_skins",PackedStringArray()); if(active.is_empty()) { active.push_back(definition.get("skin",String("default"))); } data["defaultSkins"]=active;
	Array slot_names; for(const Variant &v:slots) { slot_names.push_back(Dictionary(v)["name"]); } data["slots"]=slot_names;
	data["rest"]=sample(setup,setup_ids,0); Array clips;
	for(const Variant &key:named.keys()) {
		Ref<Animation> clip=named[key]; Ref<Animation> sampled=clip->duplicate(); sampled->set_loop_mode(Animation::LOOP_NONE);
		Ref<ECSWorld> world=scene->instantiate(); PackedInt64Array ids=world->query(PackedStringArray(),true);
		Dictionary state=world->get_animation(ids[rig_index]); state["clip"]=sampled; state["states"]=Dictionary(); state["state"]=String();
		int count=int(Math::ceil(clip->get_length()*fps)); Array frames;
		for(int f=0;f<=count;++f) {
			double at=MIN(double(f)/fps,clip->get_length()); state["time"]=at; state["playing"]=true;
			if(!world->set_animation(ids[rig_index],state)) { return fail("Cannot evaluate animation"); } world->advance_animation_preview(0);world->solve_ik_2d();
			frames.push_back(sample(world,ids,at));
		}
        Array events;
        struct EventKey {double time;int track,key;Dictionary value;};Vector<EventKey> sorted_events;
        for(int t=0;t<clip->get_track_count();++t){if(!clip->track_is_enabled(t)||!String(clip->track_get_path(t).get_concatenated_subnames()).begins_with("event:"))continue;for(int k=0;k<clip->track_get_key_count(t);++k){Dictionary event=clip->track_get_key_value(t,k);if(!event.is_empty())sorted_events.push_back({clip->track_get_key_time(t,k),t,k,event});}}
        struct EventOrder {bool operator()(const EventKey&a,const EventKey&b)const{return a.time!=b.time?a.time<b.time:a.track!=b.track?a.track<b.track:a.key<b.key;}};sorted_events.sort_custom<EventOrder>();
        for(const EventKey&e:sorted_events){Dictionary event;event["name"]=e.value["name"];event["time"]=e.time;event["intValue"]=e.value.get("int",0);event["floatValue"]=e.value.get("float",0.);event["stringValue"]=e.value.get("string",String());events.push_back(event);}
		Dictionary out; out["name"]=key; out["duration"]=clip->get_length(); out["loop"]=clip->get_loop_mode()!=Animation::LOOP_NONE; out["frames"]=frames;out["events"]=events; clips.push_back(out);
	}
	data["clips"]=clips;
    if(!sample_valid)return fail("Runtime IK requires fixed mesh topology and invertible bone transforms");
	if(DirAccess::make_dir_recursive_absolute(path)!=OK) { return fail("Cannot create output directory"); }
	for(int i=0;i<texture_refs.size();++i) {
		Ref<Image> image=texture_refs[i]->get_image(); if(image.is_null() || image->is_empty()) { return fail("Texture readback failed"); }
		if(image->is_compressed() && image->decompress()!=OK) { return fail("Cannot decompress texture"); }
		if(image->save_png(path.path_join(textures[i]))!=OK) { return fail("Cannot save texture PNG"); }
	}
	Ref<FileAccess> file=FileAccess::open(path.path_join("skeleton.ageskel.json"),FileAccess::WRITE); if(file.is_null()) { return fail("Cannot write runtime JSON"); }
	file->store_string(JSON::stringify(data)); if(file->get_error()!=OK) { return fail("Runtime JSON write failed"); }
	reply["ok"]=true; reply["path"]=path.path_join("skeleton.ageskel.json"); reply["vertices"]=vertex_count; reply["clips"]=clips.size(); reply["frames"]=total_frames; reply["atlas_pages"]=texture_refs.size(); return reply;
}
// Exercise atlas -> compact ECS resource -> shared-texture serialization -> playback.
bool skeleton_runtime_atlas_self_test() {
#define ATLAS_CHECK(c) ERR_FAIL_COND_V_MSG(!(c),false,"Skeleton atlas regression: " #c)
    Ref<ECSScene> source=ResourceLoader::load("user://skeleton-authoring-fixture.tres","ECSScene",ResourceLoader::CACHE_MODE_IGNORE);
    ATLAS_CHECK(source.is_valid());
    Array entities=source->get_entities().duplicate(true);
    int mesh_index=-1;
    for(int i=0;i<entities.size();++i) if(Dictionary(entities[i]).has("polygon_2d")) {mesh_index=i;break;}
    ATLAS_CHECK(mesh_index>=0);
    Dictionary extra=Dictionary(entities[mesh_index]).duplicate(true),mesh=extra["polygon_2d"];
    Ref<Image> image=Image::create_empty(17,23,false,Image::FORMAT_RGBA8);image->fill(Color(.7,.3,.2,1));
    mesh["texture"]=ImageTexture::create_from_image(image);entities.push_back(extra);
    Array original=entities.duplicate(true);String error;
    ATLAS_CHECK(pack_skeleton_atlas_entities(entities,error));
    Ref<Texture2D> first=Dictionary(Dictionary(entities[mesh_index])["polygon_2d"])["texture"];
    Ref<Texture2D> second=Dictionary(Dictionary(entities[entities.size()-1])["polygon_2d"])["texture"];
    ATLAS_CHECK(first==second);
    Ref<ECSScene> packed;packed.instantiate();packed->set_entities(ecs_pack_skeleton_entities(entities));
    ATLAS_CHECK(packed->get_entities().size()==1);
    const String path="user://atlas-regression/Skeleton.res";
    ATLAS_CHECK(ECSSharedTextures::save(packed,path,error)==OK);
    Ref<ECSScene> loaded=ResourceLoader::load(path,"ECSScene",ResourceLoader::CACHE_MODE_IGNORE);
    ATLAS_CHECK(loaded.is_valid() && loaded->get_entities().size()==1);
    Ref<ECSCompactSkeleton> before;before.instantiate();Ref<ECSCompactSkeleton> after;after.instantiate();
    ATLAS_CHECK(before->load(Dictionary(ecs_pack_skeleton_entities(original)[0])["skeleton_2d"]));
    ATLAS_CHECK(after->load(Dictionary(loaded->get_entities()[0])["skeleton_2d"]));
    Ref<Texture2D> shared=after->parts[mesh_index].polygon["texture"];
    ATLAS_CHECK(shared.is_valid() && shared->get_path().contains("SharedTextures") && shared->get_size()==first->get_size());
    for(const Variant &clip:before->states.keys()) {
        ATLAS_CHECK(before->play(clip,true,0) && after->play(clip,true,0));
        for(int frame=0;frame<120;++frame) {
            bool done=false;before->step(1./60.,done);after->step(1./60.,done);
            for(int part=0;part<before->parts.size();++part) {
                PackedVector2Array a=before->polygon_points(part),b=after->polygon_points(part);
                ATLAS_CHECK(a.size()==b.size());for(int v=0;v<a.size();++v) ATLAS_CHECK(a[v].is_equal_approx(b[v]));
            }
        }
    }
    SpriteAtlasImages invalid;Vector<Ref<Image>> oversize;oversize.push_back(Image::create_empty(32,32,false,Image::FORMAT_RGBA8));
    ATLAS_CHECK(SpriteAtlasBuilder::pack_images(oversize,invalid,32,2)==ERR_PARAMETER_RANGE_ERROR);
    print_line("SKELETON_NATIVE_ATLAS_PASS pack compact shared_texture_reload animation_positions oversize_guard");return true;
#undef ATLAS_CHECK
}

#endif
