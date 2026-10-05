// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
#pragma once
#define JSON_NOEXCEPTION
#include "../../third_party/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "motion.hpp"

namespace ageskeleton {
struct Attachment { std::string name; int texture=0,offset=0,count=0,slot=-1; std::vector<int> triangles; std::vector<float> uv,color; };
struct Binding { int slot=0,attachment=0; std::string key; };
struct Skin { std::string name; std::vector<Binding> bindings; };
struct Frame { float time=0; std::vector<float> positions,colors,matrices,influence_positions; std::vector<std::string> keys; std::vector<int> orders; };
struct Clip { std::string name; float duration=0; bool loop=false; std::vector<Frame> frames; std::vector<RuntimeEvent> events; };
struct Data { Rig rig; int version=1,vertex_count=0; std::vector<std::string> textures,slots,default_skins; std::vector<Attachment> attachments; std::vector<Skin> skins; Frame rest; std::vector<Clip> clips; };
using Json=nlohmann::json;
inline bool text(const Json &j,const char *key,std::string &out) { auto v=j.find(key);if(v==j.end()||!v->is_string())return false;out=v->get<std::string>();return true; }
inline bool integer(const Json &j,const char *key,int &out) { auto v=j.find(key);if(v==j.end()||!v->is_number())return false;double n=v->get<double>();if(!std::isfinite(n)||n!=std::floor(n)||n < -2147483648.0||n>2147483647.0)return false;out=int(n);return true; }
inline bool number(const Json &j,const char *key,float &out) { auto v=j.find(key);if(v==j.end()||!v->is_number())return false;out=v->get<float>();return std::isfinite(out); }
inline bool floats(const Json &j,const char *key,std::vector<float> &out) { auto a=j.find(key);if(a==j.end()||!a->is_array())return false;for(const auto &v:*a){if(!v.is_number())return false;float f=v.get<float>();if(!std::isfinite(f))return false;out.push_back(f);}return true; }
inline bool ints(const Json &j,const char *key,std::vector<int> &out) { auto a=j.find(key);if(a==j.end()||!a->is_array())return false;for(const auto &v:*a){Json wrap;wrap["v"]=v;int n;if(!integer(wrap,"v",n))return false;out.push_back(n);}return true; }
inline bool strings(const Json &j,const char *key,std::vector<std::string> &out) {auto a=j.find(key);if(a==j.end()||!a->is_array())return false;for(const auto &v:*a){if(!v.is_string())return false;out.push_back(v.get<std::string>());}return true;}
inline bool frame(const Json &j,const Data &d,Frame &f) {return number(j,"time",f.time)&&f.time>=0&&floats(j,"positions",f.positions)&&f.positions.size()==size_t(d.vertex_count*2)&&floats(j,"colors",f.colors)&&f.colors.size()==d.slots.size()*4&&strings(j,"keys",f.keys)&&f.keys.size()==d.slots.size()&&ints(j,"orders",f.orders)&&f.orders.size()==d.slots.size()&&(d.version==1||(floats(j,"matrices",f.matrices)&&f.matrices.size()==d.rig.names.size()*6&&floats(j,"influencePositions",f.influence_positions)&&f.influence_positions.size()==size_t(d.vertex_count)*8));}
inline bool load(const std::string &source,Data &result,std::string &error) {
    auto fail=[&](const char *reason){error=reason;return false;};
    if(source.size()>256*1024*1024)return fail("Runtime JSON exceeds 256 MiB");
    Json j=Json::parse(source,nullptr,false);if(j.is_discarded()||!j.is_object())return fail("Invalid JSON");
    std::string format;int version=0,fps=0;Data d;
    if(!text(j,"format",format)||format!="ageskeleton.meshclip"||!integer(j,"version",version)||(version!=1&&version!=2))return fail("Unsupported format/version");
    if(!integer(j,"vertexCount",d.vertex_count)||d.vertex_count<1||d.vertex_count>100000||!integer(j,"fps",fps)||fps<1||fps>120)return fail("Invalid limits");
    if(!strings(j,"textures",d.textures)||!strings(j,"slots",d.slots)||!strings(j,"defaultSkins",d.default_skins))return fail("Missing arrays");
    d.version=version;
    if(version==2){if(!j.contains("rig")||!j["rig"].is_object())return fail("Missing runtime rig");const auto&r=j["rig"];
        if(!strings(r,"names",d.rig.names)||d.rig.names.empty()||d.rig.names.size()>256||!ints(r,"parents",d.rig.parents)||!floats(r,"lengths",d.rig.lengths)||d.rig.parents.size()!=d.rig.names.size()||d.rig.lengths.size()!=d.rig.names.size()||!ints(r,"vertexBones",d.rig.vertex_bones)||!floats(r,"weights",d.rig.weights)||d.rig.vertex_bones.size()!=size_t(d.vertex_count)*4||d.rig.weights.size()!=d.rig.vertex_bones.size())return fail("Invalid runtime rig");
        for(size_t i=0;i<d.rig.names.size();++i){if(d.rig.names[i].empty()||std::find(d.rig.names.begin(),d.rig.names.begin()+i,d.rig.names[i])!=d.rig.names.begin()+i||d.rig.lengths[i]<0)return fail("Invalid bone");int p=int(i),count=0;while(p>=0){if(size_t(p)>=d.rig.names.size()||++count>int(d.rig.names.size()))return fail("Cyclic bone hierarchy");p=d.rig.parents[p];if(p < -1)return fail("Invalid parent");}}
        for(size_t i=0;i<d.rig.weights.size();++i)if(d.rig.weights[i]<0||d.rig.weights[i]>1||d.rig.vertex_bones[i] < -1||d.rig.vertex_bones[i]>=int(d.rig.names.size())||(d.rig.vertex_bones[i]<0&&d.rig.weights[i]!=0))return fail("Invalid skin influence");
    }
    for(const auto &p:d.textures)if(p.empty()||p.find_first_of("/\\:")!=std::string::npos||p.find("..")!=std::string::npos)return fail("Invalid texture filename");
    for(size_t i=0;i<d.slots.size();++i)if(d.slots[i].empty()||std::find(d.slots.begin(),d.slots.begin()+i,d.slots[i])!=d.slots.begin()+i)return fail("Duplicate slot");
    for(const char *key:{"attachments","skins","clips"})if(!j.contains(key)||!j[key].is_array())return fail("Missing data array");
    int end=0;
    for(const auto &a:j["attachments"]){Attachment m;
        if(!text(a,"name",m.name)||!integer(a,"texture",m.texture)||!integer(a,"offset",m.offset)||!integer(a,"count",m.count)||!integer(a,"slot",m.slot)||m.offset!=end||m.count<3||m.count>d.vertex_count-end||m.texture<0||size_t(m.texture)>=d.textures.size()||m.slot < -1||(m.slot>=0&&size_t(m.slot)>=d.slots.size()))return fail("Invalid attachment");
        if(!floats(a,"uv",m.uv)||m.uv.size()!=size_t(m.count*2)||!floats(a,"color",m.color)||m.color.size()!=4||!ints(a,"triangles",m.triangles)||m.triangles.empty()||m.triangles.size()%3)return fail("Invalid mesh");
        for(int index:m.triangles)if(index<0||index>=m.count)return fail("Invalid triangle index");end+=m.count;d.attachments.push_back(std::move(m));
    }
    if(end!=d.vertex_count)return fail("Vertex count mismatch");
    for(const auto &entry:j["skins"]){Skin skin;
        if(!text(entry,"name",skin.name)||skin.name.empty()||!entry.contains("bindings")||!entry["bindings"].is_array())return fail("Invalid skin");
        for(const Skin &other:d.skins)if(other.name==skin.name)return fail("Duplicate skin");
        for(const auto &b:entry["bindings"]){Binding binding;
            if(!integer(b,"slot",binding.slot)||!integer(b,"attachment",binding.attachment)||!text(b,"key",binding.key)||binding.key.empty()||binding.slot<0||size_t(binding.slot)>=d.slots.size()||binding.attachment<0||size_t(binding.attachment)>=d.attachments.size()||d.attachments[binding.attachment].slot!=binding.slot)return fail("Invalid binding");skin.bindings.push_back(std::move(binding));
        }d.skins.push_back(std::move(skin));
    }
    for(const auto &name:d.default_skins)if(std::none_of(d.skins.begin(),d.skins.end(),[&](const Skin&s){return s.name==name;}))return fail("Missing default skin");
    if(!j.contains("rest")||!frame(j["rest"],d,d.rest))return fail("Invalid rest pose");
    for(const auto &c:j["clips"]){Clip clip;
        if(!text(c,"name",clip.name)||clip.name.empty()||!number(c,"duration",clip.duration)||clip.duration<=0||!c.contains("loop")||!c["loop"].is_boolean()||!c.contains("frames")||!c["frames"].is_array()||c["frames"].size()<2)return fail("Invalid clip");clip.loop=c["loop"].get<bool>();
        for(const Clip &other:d.clips)if(other.name==clip.name)return fail("Duplicate clip");
        float last=-1;for(const auto &f:c["frames"]){Frame out;if(!frame(f,d,out)||out.time<=last)return fail("Invalid animation frame");last=out.time;clip.frames.push_back(std::move(out));}
        if(version==2){if(!c.contains("events")||!c["events"].is_array()||c["events"].size()>100000)return fail("Missing events");float prior=-1;
            for(const auto&e:c["events"]){RuntimeEvent event;if(!text(e,"name",event.name)||event.name.empty()||!number(e,"time",event.time)||event.time<prior||event.time<0||event.time>clip.duration||!integer(e,"intValue",event.int_value)||!number(e,"floatValue",event.float_value)||!text(e,"stringValue",event.string_value))return fail("Invalid event");prior=event.time;clip.events.push_back(std::move(event));}
        }
        if(clip.frames.front().time!=0||std::abs(last-clip.duration)>.0001f)return fail("Clip endpoints mismatch");d.clips.push_back(std::move(clip));
    }
    result=std::move(d);error.clear();return true;
}

class Player {
    Data data_;MotionPose motion_;bool event_start_=false;int clip_=-1;std::vector<std::string> active_,overrides_;std::vector<bool> overridden_,hidden_;
    Frame pose_,source_pose_;int source_clip_=-1;float source_time_=0;
    double fade_duration_=0,fade_elapsed_=0;
    void cancel_fade(){fade_duration_=fade_elapsed_=0;source_clip_=-1;}
    static float clip_time(const Clip &c,double at){return float(c.loop?std::fmod(std::fmod(at,c.duration)+c.duration,c.duration):std::clamp(at,0.0,double(c.duration)));}
    static void blend_numbers(const std::vector<float>&a,const std::vector<float>&b,float t,std::vector<float>&out){out.resize(a.size());for(size_t i=0;i<a.size();++i)out[i]=t<=0?a[i]:t>=1?b[i]:a[i]+(b[i]-a[i])*t;}
    static void blend_pose(const Frame&a,const Frame&b,float t,bool target_keys,Frame&out){
        blend_numbers(a.positions,b.positions,t,out.positions);blend_numbers(a.colors,b.colors,t,out.colors);
        blend_numbers(a.matrices,b.matrices,t,out.matrices);blend_numbers(a.influence_positions,b.influence_positions,t,out.influence_positions);
        out.keys=target_keys?b.keys:a.keys;out.orders=target_keys?b.orders:a.orders;
    }
    void sample_pose(int clip,float at,Frame &out)const{
        const Frame *a=&data_.rest,*b=a;float alpha=0;
        if(clip>=0){const auto &frames=data_.clips[clip].frames;auto it=std::upper_bound(frames.begin(),frames.end(),at,[](float t,const Frame&f){return t<f.time;});size_t index=it==frames.begin()?0:size_t(it-frames.begin()-1);a=&frames[index];b=&frames[std::min(index+1,frames.size()-1)];alpha=b->time>a->time?(at-a->time)/(b->time-a->time):0;}
        blend_pose(*a,*b,alpha,false,out);
    }
    int skin_index(const std::string &name)const {for(size_t i=0;i<data_.skins.size();++i)if(data_.skins[i].name==name)return int(i);return -1;}
    int slot_index(const std::string &name)const {auto i=std::find(data_.slots.begin(),data_.slots.end(),name);return i==data_.slots.end()?-1:int(i-data_.slots.begin());}
    int resolve(const std::string &skin,int slot,const std::string &key,int prior)const {int index=skin_index(skin);if(index>=0)for(const auto &b:data_.skins[index].bindings)if(b.slot==slot&&b.key==key)prior=b.attachment;return prior;}
public:
    std::vector<float> positions,colors;std::vector<bool> visible;std::vector<int> orders;bool playing=false;float speed=1,time=0;
    std::vector<RuntimeEvent> events;
    const Data &data()const{return data_;}
    bool load_json(const std::string &json,std::string &error){Data next;if(!load(json,next,error))return false;data_=std::move(next);cancel_fade();pose_=data_.rest;source_pose_=data_.rest;motion_.reset(data_.rig);events.clear();event_start_=false;clip_=-1;time=0;playing=false;speed=1;active_=data_.default_skins;overrides_.assign(data_.slots.size(),"");overridden_.assign(data_.slots.size(),false);hidden_.assign(data_.slots.size(),false);positions.resize(data_.vertex_count*2);colors.resize(data_.attachments.size()*4);visible.resize(data_.attachments.size());orders.resize(data_.attachments.size());evaluate();return true;}
    bool play(const std::string &name,bool restart=true){for(size_t i=0;i<data_.clips.size();++i)if(data_.clips[i].name==name){cancel_fade();if(restart||clip_!=int(i)){time=0;event_start_=true;}events.clear();clip_=int(i);playing=true;evaluate();return true;}return false;}
    bool is_blending()const{return fade_duration_>0;}
    float blend_progress()const{return is_blending()?float(fade_elapsed_/fade_duration_):1.0f;}
    bool cross_fade(const std::string &name,double duration=.2,bool restart=true){
        if(!std::isfinite(duration)||duration<0)return false;
        int next=-1;for(size_t i=0;i<data_.clips.size();++i)if(data_.clips[i].name==name){next=int(i);break;}if(next<0)return false;
        if(duration==0)return play(name,restart);
        evaluate();source_pose_=pose_;source_clip_=!is_blending()&&playing?clip_:-1;source_time_=time;
        if(restart||next!=clip_){time=0;event_start_=true;}
        clip_=next;fade_duration_=duration;fade_elapsed_=0;events.clear();playing=true;evaluate();return true;
    }
    void stop(){cancel_fade();events.clear();event_start_=false;clip_=-1;playing=false;time=0;if(!positions.empty())evaluate();}
    bool seek(float at){if(!std::isfinite(at))return false;cancel_fade();events.clear();event_start_=false;time=clip_<0?0:std::clamp(at,0.0f,data_.clips[clip_].duration);if(!positions.empty())evaluate();return true;}
    bool update(double delta){
        events.clear();if(!std::isfinite(delta)||delta<0||!std::isfinite(speed))return false;if(!playing||clip_<0)return true;
        const Clip &c=data_.clips[clip_];double step=delta*speed,next=time+step;if(!std::isfinite(next))return false;
        double to=c.loop?next:std::clamp(next,0.0,double(c.duration));
        if(!collect_events(c.events,c.name,c.duration,c.loop,time,to,event_start_,events))return false;
        if(to!=time)event_start_=false;
        if(is_blending()){
            if(source_clip_>=0)source_time_=clip_time(data_.clips[source_clip_],source_time_+step);
            fade_elapsed_=std::min(fade_duration_,fade_elapsed_+std::abs(step));
            if(fade_elapsed_>=fade_duration_)cancel_fade();
        }
        time=clip_time(c,next);
        if(!c.loop&&!is_blending()&&((speed>0&&next>=c.duration)||(speed<0&&next<=0)))playing=false;
        evaluate();return true;
    }
    bool set_ik_target(const std::string&tip,float x,float y,int chain=2,float mix=1,int iterations=24,float tolerance=.1f){if(!motion_.set_target(data_.rig,tip,x,y,chain,mix,iterations,tolerance))return false;evaluate();return true;}
    bool clear_ik_target(const std::string&tip){if(!motion_.clear_target(data_.rig,tip))return false;evaluate();return true;}
    bool bone_tip(const std::string&name,std::array<float,2>&out)const{auto i=std::find(data_.rig.names.begin(),data_.rig.names.end(),name);if(i==data_.rig.names.end())return false;out=motion_.tip(data_.rig,int(i-data_.rig.names.begin()));return true;}

    bool set_skin(const std::string &name){if(skin_index(name)<0)return false;active_={name};evaluate();return true;}
    bool set_wardrobe(const std::string &group,const std::string &name){std::string prefix=group+"/";auto in_group=[&](const std::string &s){return s.compare(0,prefix.size(),prefix)==0;};if(group.empty()||group.find('/')!=std::string::npos||std::none_of(data_.skins.begin(),data_.skins.end(),[&](const Skin&s){return in_group(s.name);})||(!name.empty()&&(skin_index(name)<0||!in_group(name))))return false;active_.erase(std::remove_if(active_.begin(),active_.end(),in_group),active_.end());if(!name.empty())active_.push_back(name);evaluate();return true;}
    bool set_slot_visible(const std::string &name,bool show){int s=slot_index(name);if(s<0)return false;hidden_[s]=!show;evaluate();return true;}
    bool set_attachment(const std::string &slot,const std::string &key,bool restore=false){int s=slot_index(slot);if(s<0)return false;if(!restore&&!key.empty()){bool found=false;for(const auto &skin:data_.skins)for(const auto &b:skin.bindings)found|=b.slot==s&&b.key==key;if(!found)return false;}overrides_[s]=key;overridden_[s]=!restore;evaluate();return true;}
    void evaluate(){
        sample_pose(clip_,time,pose_);
        if(is_blending()){
            if(source_clip_>=0)sample_pose(source_clip_,source_time_,source_pose_);
            float weight=blend_progress();blend_pose(source_pose_,pose_,weight,weight>=.5f,pose_);
        }
        positions=pose_.positions;
        motion_.apply(data_.rig,pose_.matrices,pose_.matrices,pose_.influence_positions,pose_.influence_positions,0,positions);
        for(size_t i=0;i<data_.attachments.size();++i){const auto &m=data_.attachments[i];int s=m.slot;visible[i]=s<0;orders[i]=s<0?int(i):pose_.orders[s];for(int k=0;k<4;++k)colors[i*4+k]=m.color[k]*(s<0?1:pose_.colors[s*4+k]);}
        for(size_t s=0;s<data_.slots.size();++s){if(hidden_[s])continue;const auto &key=overridden_[s]?overrides_[s]:pose_.keys[s];if(key.empty())continue;int index=resolve("default",int(s),key,-1);for(const auto &skin:active_)index=resolve(skin,int(s),key,index);if(index>=0)visible[index]=true;}
    }
};
}
