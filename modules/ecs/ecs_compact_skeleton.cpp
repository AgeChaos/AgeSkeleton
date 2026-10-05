#ifndef ECS_SKELETON_2D_DISABLED
#include "ecs_compact_skeleton.h"
#include "ecs_world.h"
#include "ecs_animation_path.h"
#include "core/math/geometry_2d.h"
#include "scene/resources/canvas_item_material.h"
#include "scene/resources/shader.h"
#include "scene/resources/atlas_texture.h"
#include "servers/rendering/rendering_server.h"

namespace {
Transform2D local_pose(const ECSCompactSkeleton::Part &p) {
    Basis basis=Basis::from_euler(p.rotation);
    basis*=Basis(Vector3(Math::cos(p.shear.x),Math::sin(p.shear.x),0),Vector3(-Math::sin(p.shear.y),Math::cos(p.shear.y),0),Vector3(0,0,1));
    basis.scale_local(p.scale);
    return Transform2D(Vector2(basis[0][0],basis[1][0]),Vector2(basis[0][1],basis[1][1]),Vector2(p.position.x,p.position.y));
}
Variant sample(const Ref<Animation> &clip,int track,double time) {
    switch(clip->track_get_type(track)) {
        case Animation::TYPE_POSITION_3D: { Vector3 v; if(clip->try_position_track_interpolate(track,time,&v)==OK) return v; break; }
        case Animation::TYPE_SCALE_3D: { Vector3 v; if(clip->try_scale_track_interpolate(track,time,&v)==OK) return v; break; }
        case Animation::TYPE_ROTATION_3D: { Quaternion q; if(clip->try_rotation_track_interpolate(track,time,&q)==OK) return q.get_euler(); break; }
        case Animation::TYPE_VALUE: return clip->value_track_interpolate(track,time);
        default: break;
    }
    return Variant();
}
String channel(const Ref<Animation> &clip,int i) {
    auto type=clip->track_get_type(i);
    if(type==Animation::TYPE_POSITION_3D) return "position";
    if(type==Animation::TYPE_ROTATION_3D) return "rotation";
    if(type==Animation::TYPE_SCALE_3D) return "scale";
    return ecs_animation_subnames(clip->track_get_path(i));
}
}

Array ecs_pack_skeleton_entities(const Array &input) {
    if(input.is_empty() || input.size()>65536 || input[0].get_type()!=Variant::DICTIONARY) return Array();
    Dictionary root=input[0];
    if(!root.has("skeleton_2d")) return Array();
    if(Dictionary(root["skeleton_2d"]).has("data")) return input.duplicate(true);
    Array data=input.duplicate(true);
    for(int i=0;i<data.size();i++) {
        if(data[i].get_type()!=Variant::DICTIONARY) return Array();
        Dictionary part=data[i];
        for(const Variant &key:part.keys()) {
            String k=key;
            // Captured ECS scenes include an empty custom-component dictionary.
            // It carries no gameplay state and is safe to omit from skeleton data.
            if(k=="components" && part[key].get_type()==Variant::DICTIONARY && Dictionary(part[key]).is_empty()) {part.erase(key);continue;}
            if(!PackedStringArray({"name","position","rotation","scale","shear","parent","active","bone_2d","polygon_2d","skeleton_2d","animation","ecs_uid","_ecs_uid"}).has(k)) return Array();
            if(i>0 && (k=="skeleton_2d" || k=="animation")) return Array();
        }
        if(i>0 && !part.has("bone_2d") && !part.has("polygon_2d")) return Array();
        if(i==0) {
            part["position"]=Vector3();part["rotation"]=Vector3();part["scale"]=Vector3(1,1,1);part["shear"]=Vector3();part.erase("parent");part["active"]=true;
        }
    }
    Dictionary packed=root.duplicate(true); packed.erase("animation");packed.erase("bone_2d");packed.erase("polygon_2d");
    Dictionary definition;definition["data"]=data;packed["skeleton_2d"]=definition;
    return Array({packed});
}

bool ECSCompactSkeleton::load(const Dictionary &value) {
    if(value.get("data",Variant()).get_type()!=Variant::ARRAY) return false;
    Array data=value["data"];
    if(data.is_empty() || data.size()>65536) return false;
    parts.resize(data.size());
    for(int i=0;i<data.size();i++) {
        if(data[i].get_type()!=Variant::DICTIONARY) return false;
        Dictionary d=data[i]; Part &p=parts.write[i]; p.name=d.get("name",String());p.parent=d.get("parent",-1);
        if((i==0 && p.parent!=-1) || (i>0 && (p.parent<0 || p.parent>=parts.size())) || p.parent==i) return false;
        for(const char *field:{"position","rotation","scale","shear"}) if(d.has(field) && (d[field].get_type()!=Variant::VECTOR3 || !Vector3(d[field]).is_finite())) return false;
        p.position=d.get("position",Vector3());p.rotation=d.get("rotation",Vector3());p.scale=d.get("scale",Vector3(1,1,1));p.shear=d.get("shear",Vector3());p.active=d.get("active",true);
        if(d.has("polygon_2d")) {
            if(d["polygon_2d"].get_type()!=Variant::DICTIONARY) return false;
            p.polygon=Dictionary(d["polygon_2d"]).duplicate(true);
            if(p.polygon.get("polygon",Variant()).get_type()!=Variant::PACKED_VECTOR2_ARRAY) return false;
            PackedVector2Array points=p.polygon["polygon"],uv=p.polygon.get("uv",PackedVector2Array());
            if(points.size()<3 || points.size()>16384 || (!uv.is_empty() && uv.size()!=points.size())) return false;
            for(Vector2 point:points) if(!point.is_finite()) return false;
            for(Vector2 point:uv) if(!point.is_finite()) return false;
            if(p.polygon.has("triangles")) {
                if(p.polygon["triangles"].get_type()!=Variant::PACKED_INT32_ARRAY) return false;
                PackedInt32Array triangles=p.polygon["triangles"];
                if(triangles.size()%3 || triangles.size()>points.size()*12) return false;
                for(int index:triangles) if(index<0 || index>=points.size()) return false;
            }
        }
    }
    // Topological order accepts parent-after-child files and rejects cycles.
    HashSet<int> visited;
    while(order.size()<parts.size()) {
        int before=order.size();
        for(int i=0;i<parts.size();i++) if(!visited.has(i) && (parts[i].parent<0 || visited.has(parts[i].parent))) {order.push_back(i);visited.insert(i);}
        if(order.size()==before) return false;
    }
    Dictionary root=data[0];
    if(root.get("skeleton_2d",Variant()).get_type()!=Variant::DICTIONARY) return false;
    rig=Dictionary(root["skeleton_2d"]).duplicate(true);
    if(rig.get("bones",Variant()).get_type()!=Variant::PACKED_INT64_ARRAY) return false;
    bones=rig["bones"]; if(bones.is_empty() || bones.size()>256) return false;
    HashSet<int> unique;
    for(int index:bones) {if(index<=0 || index>=parts.size() || unique.has(index) || !Dictionary(data[index]).has("bone_2d")) return false;unique.insert(index);}
    update_pose();
    bind_poses=rig.get("bind_poses",Array());
    if(bind_poses.is_empty()) for(int index:bones) bind_poses.push_back(parts[index].global);
    if(bind_poses.size()!=bones.size()) return false;
    for(Variant pose:bind_poses) if(pose.get_type()!=Variant::TRANSFORM2D || !Transform2D(pose).is_finite() || Math::is_zero_approx(Transform2D(pose).determinant())) return false;
    for(const Part &p:parts) if(!p.polygon.is_empty()) {
        int rig_index=p.polygon.get("skeleton",-1); if(rig_index!=-1 && rig_index!=0) return false;
        if(rig_index==0) {
            PackedInt32Array indices=p.polygon.get("bones",PackedInt32Array());PackedFloat32Array weights=p.polygon.get("weights",PackedFloat32Array());
            if(indices.size()!=PackedVector2Array(p.polygon["polygon"]).size()*4 || weights.size()!=indices.size()) return false;
            for(int i=0;i<indices.size();i++) if(indices[i]<0 || indices[i]>=bones.size() || weights[i]<0 || !Math::is_finite(weights[i])) return false;
            for(int i=0;i<weights.size();i+=4) if(weights[i]+weights[i+1]+weights[i+2]+weights[i+3]<=0) return false;
        }
    }
    constraints=rig.get("ik",Array());
    for(Variant raw:constraints) {
        if(raw.get_type()!=Variant::DICTIONARY) return false;Dictionary c=raw;
        PackedInt32Array chain=c.get("chain",PackedInt32Array());if(chain.is_empty() || chain.size()>16 || int(c.get("iterations",24))<1 || int(c.get("iterations",24))>128) return false;
        for(int i=0;i<chain.size();i++) if(chain[i]<0 || chain[i]>=bones.size() || (i>0 && parts[bones[chain[i]]].parent!=bones[chain[i-1]])) return false;
        int target=c.get("target_bone",-1);if(target<-1 || target>=bones.size()) return false;
    }
    definition=value.duplicate(true);
    if(value.has("skin")) rig["skin"]=value["skin"];
    if(value.has("slots")) rig["slots"]=value["slots"];
    Dictionary skins=rig.get("skins",Dictionary());
    for(const Variant &key:skins.keys()) {
        if(skins[key].get_type()!=Variant::DICTIONARY) return false;Dictionary slots=skins[key];
        for(const Variant &slot:slots.keys()) {if(slots[slot].get_type()!=Variant::DICTIONARY) return false;Dictionary attachments=slots[slot];
            for(const Variant &name:attachments.keys()) {int index=attachments[name];if(index<0 || index>=parts.size() || parts[index].polygon.is_empty()) return false;}
        }
    }
    animation=root.get("animation",Dictionary());
    if(!animation.is_empty() && !set_animation(value.get("animation",Dictionary()))) return false;
    return true;
}

Variant ECSCompactSkeleton::get_channel(int target,const String &path) const {
    if(path=="position") return parts[target].position;
    if(path=="rotation") return parts[target].rotation;
    if(path=="scale") return parts[target].scale;
    if(path=="shear") return parts[target].shear;
    if(path.begins_with("slot:")) {
        String name=path.get_slice(":",1),key=path.get_slice(":",2);
        for(Variant raw:Array(rig.get("slots",Array()))) {Dictionary slot=raw;if(String(slot.get("name",String()))==name) return slot.get(key,Variant());}
    }
    return Variant();
}
void ECSCompactSkeleton::set_channel(int target,const String &path,const Variant &value) {
    if(value.get_type()==Variant::VECTOR3) {
        if(path=="position") parts.write[target].position=value;
        else if(path=="rotation") parts.write[target].rotation=value;
        else if(path=="scale") parts.write[target].scale=value;
        else if(path=="shear") parts.write[target].shear=value;
    }
    if(path.begins_with("slot:")) {
        String name=path.get_slice(":",1),key=path.get_slice(":",2);
        for(Variant raw:Array(rig.get("slots",Array()))) {Dictionary slot=raw;if(String(slot.get("name",String()))==name) {slot[key]=value;break;}}
    }
}
bool ECSCompactSkeleton::set_animation(const Dictionary &patch) {
    Dictionary next=clip.is_valid()?get_animation():animation.duplicate(true);next.merge(patch,true);
    Ref<Animation> candidate=next.get("clip",Variant());
    if(candidate.is_null() || candidate->get_length()<=0 || !Math::is_finite(candidate->get_length()) || !next.get("graph",Variant()).is_null() || !next.get("secondary",Variant()).is_null() || !next.get("blend_space",Variant()).is_null()) return false;
    PackedInt64Array new_targets=next.get("targets",PackedInt64Array());
    if(new_targets.size()!=candidate->get_track_count()) return false;
    for(int i=0;i<new_targets.size();i++) {
        if(new_targets[i]<0 || new_targets[i]>=parts.size()) return false;
        String path=channel(candidate,i);
        if(!path.begins_with("event:") && get_channel(new_targets[i],path).get_type()==Variant::NIL) return false;
    }
    double new_time=next.get("time",0.),new_speed=next.get("speed",1.),duration=patch.get("blend_duration",0.);
    if(!Math::is_finite(new_time) || !Math::is_finite(new_speed) || !Math::is_finite(duration) || duration<0) return false;
    Vector<Variant> from;
    if(duration>0) for(int i=0;i<new_targets.size();i++) from.push_back(get_channel(new_targets[i],channel(candidate,i)));
    animation=next;clip=candidate;targets=new_targets;states=next.get("states",Dictionary());
    bool restart = patch.has("clip") || patch.has("time");
    time=candidate->get_loop_mode()==Animation::LOOP_NONE?CLAMP(new_time,0.,candidate->get_length()):Math::fposmod(new_time,candidate->get_length()*(candidate->get_loop_mode()==Animation::LOOP_PINGPONG?2:1));speed=new_speed;playing=next.get("playing",true);
    if(restart || patch.has("blend_duration")) {blend_duration=duration;blend_elapsed=0;blend_from=from;event_start=time==0;}
    sample_pose();
    return true;
}
Dictionary ECSCompactSkeleton::get_animation() const {Dictionary result=animation.duplicate(true);result["time"]=time;result["speed"]=speed;result["playing"]=playing;return result;}
bool ECSCompactSkeleton::play(const StringName &name,bool loop,double duration) {
    if(!states.has(name)) return false;
    Ref<Animation> selected=states[name];if(selected.is_null()) return false;
    if(selected->get_loop_mode()!=(loop?Animation::LOOP_LINEAR:Animation::LOOP_NONE)) {selected=selected->duplicate();selected->set_loop_mode(loop?Animation::LOOP_LINEAR:Animation::LOOP_NONE);}
    Dictionary update;update["clip"]=selected;update["state"]=name;update["time"]=0.;update["playing"]=true;update["blend_duration"]=duration;
    return set_animation(update);
}
Array ECSCompactSkeleton::step(double delta,bool &finished) {
    Array events;finished=false;
    if(clip.is_null() || !playing || delta<0 || !Math::is_finite(delta)) return events;
    double next=time+delta*speed;if(!Math::is_finite(next)) return events;
    events=ECSWorld::sample_animation_events(clip,time,next,event_start);event_start=false;
    double length=clip->get_length();
    if(clip->get_loop_mode()==Animation::LOOP_NONE) {finished=speed>0?next>=length:speed<0 && next<=0;time=CLAMP(next,0.,length);}
    else time=Math::fposmod(next,clip->get_loop_mode()==Animation::LOOP_PINGPONG?length*2:length);
    blend_elapsed=MIN(blend_duration,blend_elapsed+delta);
    sample_pose();
    if(finished) playing=false;
    return events;
}
void ECSCompactSkeleton::sample_pose() {
    if(clip.is_null()) {update_pose();solve_ik();return;}
    double length=clip->get_length();
    double at=clip->get_loop_mode()==Animation::LOOP_PINGPONG && time>length?2*length-time:time;
    double weight=blend_duration>0?blend_elapsed/blend_duration:1;
    for(int i=0;i<targets.size();i++) {
        if(!clip->track_is_enabled(i) || !clip->track_get_key_count(i) || channel(clip,i).begins_with("event:")) continue;
        Variant value=sample(clip,i,at);
        if(weight<1 && blend_from.size()==targets.size() && blend_from[i].get_type()==value.get_type()) {
            if(value.get_type()==Variant::VECTOR3) value=channel(clip,i)=="rotation"?Quaternion::from_euler(Vector3(blend_from[i])).slerp(Quaternion::from_euler(Vector3(value)),weight).get_euler():Vector3(blend_from[i]).lerp(Vector3(value),weight);
            else if(value.get_type()==Variant::COLOR) value=Color(blend_from[i]).lerp(Color(value),weight);
            else if(weight<.5) value=blend_from[i];
        }
        set_channel(targets[i],channel(clip,i),value);
    }
    update_pose();solve_ik();
}

void ECSCompactSkeleton::update_pose() {for(int i:order) parts.write[i].global=(parts[i].parent>=0?parts[parts[i].parent].global:Transform2D())*local_pose(parts[i]);}
void ECSCompactSkeleton::solve_ik() {
    Array source=definition["data"];
    for(Variant raw:constraints) {
        Dictionary c=raw;if(!bool(c.get("enabled",true))) continue;
        PackedInt32Array chain=c["chain"];int target_index=c.get("target_bone",-1);
        Vector2 target=target_index>=0?parts[bones[target_index]].global.get_origin():Vector2(c.get("target",Vector2()));
        int tip=bones[chain[chain.size()-1]];double length=Dictionary(Dictionary(source[tip])["bone_2d"]).get("length",0.);
        for(int iteration=0;iteration<int(c.get("iterations",24));iteration++) {
            if(parts[tip].global.xform(Vector2(length,0)).distance_to(target)<=double(c.get("tolerance",.1))) break;
            for(int j=chain.size()-1;j>=0;j--) {
                int index=bones[chain[j]],parent=parts[index].parent;Transform2D base=parent>=0?parts[parent].global:Transform2D();
                if(Math::is_zero_approx(base.determinant())) continue;
                Transform2D inv=base.affine_inverse();Vector2 origin(parts[index].position.x,parts[index].position.y);
                Vector2 a=inv.xform(parts[tip].global.xform(Vector2(length,0)))-origin,b=inv.xform(target)-origin;
                if(a.length_squared()<1e-10 || b.length_squared()<1e-10) continue;
                parts.write[index].rotation.z+=a.angle_to(b);update_pose();
            }
        }
    }
}
int ECSCompactSkeleton::find_bone(const String &name) const {int result=-1;for(int index:bones) if(parts[index].name==name) {if(result!=-1)return -1;result=index;}return result;}
bool ECSCompactSkeleton::set_skin(const String &skin) {if(!Dictionary(rig.get("skins",Dictionary())).has(skin)) return false;rig["skin"]=skin;return true;}
bool ECSCompactSkeleton::set_attachment(const String &name,const String &attachment) {
    Dictionary skins=rig.get("skins",Dictionary()),selected=skins.get(rig.get("skin",String("default")),Dictionary()),fallback=skins.get("default",Dictionary());
    if(!attachment.is_empty() && !Dictionary(selected.get(name,Dictionary())).has(attachment) && !Dictionary(fallback.get(name,Dictionary())).has(attachment)) return false;
    for(Variant raw:Array(rig.get("slots",Array()))) {Dictionary slot=raw;if(String(slot.get("name",String()))==name) {slot["attachment"]=attachment;return true;}}
    return false;
}
PackedVector2Array ECSCompactSkeleton::polygon_points(int index) const {
    const Part &part=parts[index];PackedVector2Array points=part.polygon.get("polygon",PackedVector2Array());
    bool weighted=int(part.polygon.get("skeleton",-1))==0;
    PackedInt32Array indices=part.polygon.get("bones",PackedInt32Array());PackedFloat32Array weights=part.polygon.get("weights",PackedFloat32Array());
    for(int i=0;i<points.size();i++) {
        Vector2 source=part.global.xform(points[i]);
        if(weighted) {Vector2 result;double total=0;for(int j=0;j<4;j++){int k=i*4+j;result+=parts[bones[indices[k]]].global.xform(Transform2D(bind_poses[indices[k]]).affine_inverse().xform(source))*weights[k];total+=weights[k];}points.set(i,total>0?result/total:source);}
        else points.set(i,source);
    }
    return points;
}

void ECSCompactSkeleton::draw(RID canvas,const Transform2D &root,bool visible) {
    auto *rs=RenderingServer::get_singleton();
    HashMap<int,Dictionary> styles;
    Dictionary skins=rig.get("skins",Dictionary()),selected=skins.get(rig.get("skin",String("default")),Dictionary()),fallback=skins.get("default",Dictionary());
    for(const Variant &skin_name:skins.keys()) {Dictionary slots=skins[skin_name];for(const Variant &slot:slots.keys()){Dictionary named=slots[slot];for(const Variant &name:named.keys()) styles[int(named[name])]=Dictionary();}}
    for(Variant raw:Array(rig.get("slots",Array()))) {
        Dictionary slot=raw;String name=slot.get("name",String()),attachment=slot.get("attachment",String());
        Dictionary candidates=Dictionary(fallback.get(name,Dictionary())).duplicate();candidates.merge(selected.get(name,Dictionary()),true);
        if(!attachment.is_empty() && candidates.has(attachment)) styles[int(candidates[attachment])]=slot;
    }
    for(int i=0;i<parts.size();i++) {
        Part &part=parts.write[i];if(part.polygon.is_empty()) continue;
        Dictionary style=styles.has(i)?styles[i]:Dictionary();bool show=visible && (!styles.has(i) || !style.is_empty());
        for(int parent=i;parent>=0;parent=parts[parent].parent) show &= parts[parent].active;
        // Do not create an empty item for an initially hidden attachment: its
        // first visible frame must still initialize the geometry commands.
        if(!show) {if(part.item.is_valid()) rs->canvas_item_set_visible(part.item,false);continue;}
        bool fresh=!part.item.is_valid();if(fresh) part.item=rs->canvas_item_create();
        rs->canvas_item_set_parent(part.item,canvas);rs->canvas_item_set_visible(part.item,true);
        bool weighted=int(part.polygon.get("skeleton",-1))==0;
        Ref<Texture2D> texture=part.polygon.get("texture",Variant());
        Color color=part.polygon.get("color",Color(1,1,1));
        if(fresh || weighted) {
            rs->canvas_item_clear(part.item);
            PackedVector2Array points=weighted?polygon_points(i):PackedVector2Array(part.polygon["polygon"]),uv=part.polygon.get("uv",PackedVector2Array());
            bool rectangle=!weighted && !part.polygon.has("triangles") && texture.is_valid() && points.size()==4 && uv.size()==4;
            if(rectangle) rectangle=points[1].is_equal_approx(Vector2(points[2].x,points[0].y)) && points[3].is_equal_approx(Vector2(points[0].x,points[2].y)) && points[2].x>points[0].x && points[2].y>points[0].y && uv[1].is_equal_approx(Vector2(uv[2].x,uv[0].y)) && uv[3].is_equal_approx(Vector2(uv[0].x,uv[2].y)) && uv[2].x>uv[0].x && uv[2].y>uv[0].y;
            part.atlas_rect=false;
            if(rectangle) {
                Rect2 destination(points[0],points[2]-points[0]),source(uv[0]*texture->get_size(),(uv[2]-uv[0])*texture->get_size());
                Ref<AtlasTexture> region=texture;
                bool valid=true;
                while(region.is_valid()) {
                    part.atlas_rect=true;
                    Rect2 mapped_destination,mapped_source;
                    if(!region->get_rect_region(destination,source,mapped_destination,mapped_source)) {valid=false;break;}
                    destination=mapped_destination;source=mapped_source;region=region->get_atlas();
                }
                if(valid) rs->canvas_item_add_texture_rect_region(part.item,destination,texture->get_rid(),source,color);
            }
            else {
                PackedInt32Array triangles=part.polygon.get("triangles",PackedInt32Array());if(triangles.is_empty()) triangles=Geometry2D::triangulate_polygon(PackedVector2Array(part.polygon["polygon"]));
                rs->canvas_item_add_triangle_array(part.item,triangles,points,Vector<Color>({color}),uv,Vector<int>(),Vector<float>(),texture.is_valid()?texture->get_rid():RID());
            }
        }
        rs->canvas_item_set_transform(part.item,root*(weighted?Transform2D():part.global));
        rs->canvas_item_set_modulate(part.item,style.get("color",Color(1,1,1)));
        rs->canvas_item_set_z_index(part.item,style.get("z_index",part.polygon.get("z_index",0)));
        int blend=style.get("blend",0);Color dark=style.get("dark",Color(0,0,0));
        Ref<AtlasTexture> atlas=texture;
        if(part.atlas_rect) atlas.unref(); // Rect geometry already maps and clips the region.
        bool custom=dark.r!=0 || dark.g!=0 || dark.b!=0 || atlas.is_valid();
        if(custom) {
            Ref<ShaderMaterial> material=part.material;
            if(material.is_null() || int(material->get_meta("blend",-1))!=blend) {
                Ref<Shader> shader;shader.instantiate();String mode=blend==1?"blend_add":blend==2?"blend_mul":"blend_mix";
                shader->set_code("shader_type canvas_item; render_mode "+mode+"; uniform vec4 dark: source_color=vec4(0.0); uniform vec4 atlas_map=vec4(1.0,1.0,0.0,0.0); uniform vec4 atlas_clip=vec4(-1e20,-1e20,1e20,1e20); varying vec4 light; void vertex(){light=COLOR;} void fragment(){if(any(lessThan(UV,atlas_clip.xy))||any(greaterThan(UV,atlas_clip.zw))){discard;} vec4 tex=texture(TEXTURE,UV*atlas_map.xy+atlas_map.zw);COLOR=vec4(tex.rgb*light.rgb+(vec3(1.0)-tex.rgb)*dark.rgb,tex.a*light.a);}");
                material.instantiate();material->set_shader(shader);material->set_meta("blend",blend);part.material=material;
            }
            Vector2 scale(1,1),offset,clip_min(-1e20,-1e20),clip_max(1e20,1e20);
            while(atlas.is_valid() && atlas->get_atlas().is_valid()) {
                Vector2 size=atlas->get_size(),base=atlas->get_atlas()->get_size();Rect2 region=atlas->get_region(),margin=atlas->get_margin();
                if(region.size.x==0) region.size.x=base.x;
                if(region.size.y==0) region.size.y=base.y;
                clip_min=clip_min.max((margin.position/size-offset)/scale);
                clip_max=clip_max.min(((margin.position+region.size)/size-offset)/scale);
                offset=(offset*size+region.position-margin.position)/base;scale*=size/base;atlas=atlas->get_atlas();
            }
            material->set_shader_parameter("atlas_clip",Vector4(clip_min.x,clip_min.y,clip_max.x,clip_max.y));
            material->set_shader_parameter("atlas_map",Vector4(scale.x,scale.y,offset.x,offset.y));material->set_shader_parameter("dark",dark);
        } else if(blend>0) {
            Ref<CanvasItemMaterial> material=part.material;
            if(material.is_null()) {material.instantiate();part.material=material;}
            material->set_blend_mode(blend==1?CanvasItemMaterial::BLEND_MODE_ADD:CanvasItemMaterial::BLEND_MODE_MUL);
        } else part.material.unref();
        rs->canvas_item_set_material(part.item,part.material.is_valid()?part.material->get_rid():RID());
    }
}
ECSCompactSkeleton::~ECSCompactSkeleton() {
    auto *rs=RenderingServer::get_singleton();if(!rs) return;
    for(const Part &part:parts) {if(part.item.is_valid()) rs->free_rid(part.item);if(part.mesh.is_valid()) rs->free_rid(part.mesh);}
    if(skeleton.is_valid()) rs->free_rid(skeleton);
}

#else
#include "ecs_compact_skeleton.h"
ECSCompactSkeleton::~ECSCompactSkeleton() = default;
Array ecs_pack_skeleton_entities(const Array &) { return Array(); }
#endif
