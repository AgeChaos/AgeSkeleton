// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
namespace ageskeleton {
struct Rig {std::vector<std::string> names;std::vector<int> parents,vertex_bones;std::vector<float> lengths,weights;};
struct RuntimeEvent {std::string name,animation,string_value;float time=0,float_value=0;int int_value=0;};
inline bool collect_events(const std::vector<RuntimeEvent>&keys,const std::string &clip,double duration,bool loop,double from,double to,bool include_start,std::vector<RuntimeEvent>&out) {
    struct Hit {double at;int index;};std::vector<Hit> hits;out.clear();if(from==to)return true;
    bool forward=to>from;double low=std::min(from,to),high=std::max(from,to);
    for(size_t i=0;i<keys.size();++i){double at=keys[i].time;
        double first=loop?std::ceil((low-at)/duration):0,last=loop?std::floor((high-at)/duration):0;
        if(!std::isfinite(first)||!std::isfinite(last)||last-first>4096||std::abs(first)>9e15||std::abs(last)>9e15)return false;
        for(double k=first;k<=last;++k){double t=at+k*duration;if((forward?t>from&&t<=to:t<from&&t>=to)||(include_start&&t==from&&k==0&&k==0)){if(hits.size()>=4096)return false;hits.push_back({t,int(i)});}}
    }
    std::sort(hits.begin(),hits.end(),[&](const Hit&a,const Hit&b){return a.at!=b.at?(forward?a.at<b.at:a.at>b.at):(forward?a.index<b.index:a.index>b.index);});
    for(const auto &h:hits){out.push_back(keys[h.index]);out.back().animation=clip;}return true;
}
class MotionPose {
    struct Target {bool enabled=false;float x=0,y=0,mix=1,tolerance=.1f;int chain=2,iterations=24;};
    std::vector<Target> targets;std::vector<float> base,posed,backup;
    static float lerp(float a,float b,float t){return a+(b-a)*t;}
    bool child(const Rig&r,int node,int ancestor)const{for(int i=node;i>=0;i=r.parents[i])if(i==ancestor)return true;return false;}
    void rotate(const Rig&r,int bone,float angle){float c=std::cos(angle),s=std::sin(angle),x=posed[bone*6+4],y=posed[bone*6+5];for(size_t n=0;n<r.names.size();++n)if(child(r,int(n),bone)){int k=int(n)*6;for(int axis=0;axis<2;++axis){float a=posed[k+axis*2],b=posed[k+axis*2+1];posed[k+axis*2]=c*a-s*b;posed[k+axis*2+1]=s*a+c*b;}float a=posed[k+4]-x,b=posed[k+5]-y;posed[k+4]=x+c*a-s*b;posed[k+5]=y+s*a+c*b;}}
public:
    void reset(const Rig&r){targets.assign(r.names.size(),Target());base.resize(r.names.size()*6);posed.resize(base.size());backup.resize(base.size());}
    bool set_target(const Rig&r,const std::string&tip,float x,float y,int chain,float mix,int iterations,float tolerance){auto it=std::find(r.names.begin(),r.names.end(),tip);if(it==r.names.end()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(mix)||mix<0||mix>1||chain<1||chain>16||iterations<1||iterations>128||!std::isfinite(tolerance)||tolerance<0)return false;int index=int(it-r.names.begin()),p=index;for(int i=0;i<chain;++i){if(p<0)return false;p=r.parents[p];}targets[index]={true,x,y,mix,tolerance,chain,iterations};return true;}
    bool clear_target(const Rig&r,const std::string&tip){auto it=std::find(r.names.begin(),r.names.end(),tip);if(it==r.names.end())return false;targets[it-r.names.begin()].enabled=false;return true;}
    std::array<float,2> tip(const Rig&r,int bone)const{int k=bone*6;return {posed[k+4]+posed[k]*r.lengths[bone],posed[k+5]+posed[k+1]*r.lengths[bone]};}
    void apply(const Rig&r,const std::vector<float>&a,const std::vector<float>&b,const std::vector<float>&la,const std::vector<float>&lb,float t,std::vector<float>&positions){
        if(r.names.empty())return;for(size_t i=0;i<base.size();++i)base[i]=posed[i]=lerp(a[i],b[i],t);
        bool changed=false;
        for(size_t tip_index=0;tip_index<targets.size();++tip_index){const auto &target=targets[tip_index];if(!target.enabled||target.mix==0)continue;changed=true;std::array<int,16> chain;std::array<float,16> angles{};int p=int(tip_index);for(int j=0;j<target.chain;++j){chain[j]=p;p=r.parents[p];}backup=posed;
            for(int iteration=0;iteration<target.iterations;++iteration){auto end=tip(r,int(tip_index));if(std::hypot(end[0]-target.x,end[1]-target.y)<=target.tolerance)break;for(int j=0;j<target.chain;++j){int k=chain[j]*6;end=tip(r,int(tip_index));float ax=end[0]-posed[k+4],ay=end[1]-posed[k+5],bx=target.x-posed[k+4],by=target.y-posed[k+5];if(ax*ax+ay*ay<1e-12f||bx*bx+by*by<1e-12f)continue;float angle=std::atan2(ax*by-ay*bx,ax*bx+ay*by);angles[j]+=angle;rotate(r,chain[j],angle);}}
            if(target.mix<1){posed=backup;for(int j=target.chain-1;j>=0;--j)rotate(r,chain[j],std::atan2(std::sin(angles[j]),std::cos(angles[j]))*target.mix);}
        }
        if(!changed)return;
        // Apply each influence's exact bone-space displacement to the sampled mesh.
        // With no override the original sampled result is preserved bit for bit.
        for(size_t i=0;i<r.vertex_bones.size();++i){int bone=r.vertex_bones[i];float w=r.weights[i];if(bone<0||w==0)continue;int k=bone*6;float x=lerp(la[i*2],lb[i*2],t),y=lerp(la[i*2+1],lb[i*2+1],t);size_t v=(i/4)*2;positions[v]+=w*((posed[k]-base[k])*x+(posed[k+2]-base[k+2])*y+posed[k+4]-base[k+4]);positions[v+1]+=w*((posed[k+1]-base[k+1])*x+(posed[k+3]-base[k+3])*y+posed[k+5]-base[k+5]);}
    }
};
}
