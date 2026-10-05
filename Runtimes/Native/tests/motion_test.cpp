// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
#include "ageskeleton/meshclip.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
void check(bool value,const char *name){if(!value){std::cerr<<name<<" FAILED\n";std::exit(1);}}
void events(const ageskeleton::Player &p,const std::string &expected){std::string got;for(auto &e:p.events)got+=e.name+",";check(got==expected,("events "+got+" expected "+expected).c_str());}
int main(int argc,char **argv){
    check(argc==2,"fixture");std::ifstream f(argv[1]);std::string src((std::istreambuf_iterator<char>(f)),{}),error;ageskeleton::Player p;
    check(p.load_json(src,error),error.c_str());auto rest=p.positions;std::array<float,2> tip;
    check(p.set_ik_target("Tip",1,1,2,1,64,.0001f),"IK target");check(p.bone_tip("Tip",tip)&&std::hypot(tip[0]-1,tip[1]-1)<.001,"IK tip reaches target");
    check(std::hypot(p.positions[0]-1,p.positions[1]-1)<.001,"weighted vertex follows tip");
    check(p.clear_ik_target("Tip")&&p.positions==rest,"clear restores baseline exactly");
    check(p.set_ik_target("Tip",1,1,2,0)&&p.positions==rest,"zero mix");
    check(p.set_ik_target("Tip",1,1,2,.5f),"partial mix");check(p.positions[0]>1.1f&&p.positions[1]>.1f&&p.positions[1]<.99f,"partial pose");
    auto before=p.positions;check(!p.set_ik_target("Tip",NAN,1)&&p.positions==before,"NaN atomic");check(!p.set_ik_target("Tip",1,1,3)&&p.positions==before,"chain atomic");
    check(p.set_ik_target("Tip",100,100,2,1,64,.001f),"unreachable accepted");for(float v:p.positions)check(std::isfinite(v),"unreachable finite");p.clear_ik_target("Tip");
    p.play("Loop");p.update(0);events(p,"");p.update(.25);events(p,"start,quarter,");check(p.events[0].int_value==(-2147483647-1)&&p.events[0].string_value==u8"测试 event","typed payload");
    p.update(.75);events(p,"late,start,end,");p.update(0);events(p,"");
    p.seek(.5);p.speed=-1;p.update(.75);events(p,"quarter,end,start,late,");
    p.seek(0);p.speed=1;p.update(2.25);events(p,"quarter,late,start,end,quarter,late,start,end,quarter,");
    p.playing=false;p.update(1);events(p,"");p.playing=true;p.speed=0;p.update(1);events(p,"");
    p.speed=1;p.seek(0);float at=p.time;check(!p.update(2000)&&p.time==at&&p.events.empty(),"catch-up atomic");
    p.play("Once");p.update(2);events(p,"start,quarter,late,end,");check(!p.playing&&p.time==1,"nonloop endpoint");p.stop();events(p,"");
    auto j=ageskeleton::Json::parse(src);j["rig"]["parents"][0]=1;check(!p.load_json(j.dump(),error),"cycle rejected");
    j=ageskeleton::Json::parse(src);j["version"]=1;j.erase("rig");check(p.load_json(j.dump(),error)&&p.play("Loop")&&p.update(.25)&&p.events.empty(),"v1 compatible");check(!p.set_ik_target("Tip",1,1),"v1 no IK");
    std::cout<<"AGESKELETON_MOTION_NATIVE_PASS weighted_ik mix clear invalid events loops reverse pause seek payload legacy\n";
}
