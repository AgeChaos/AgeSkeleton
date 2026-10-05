// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
#include "ageskeleton/meshclip.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
void check(bool value,const char *name){if(!value){std::cerr<<name<<" FAILED\n";std::exit(1);}}
void near(float value,float expected,const char *name){check(std::abs(value-expected)<.0002f,name);}
int main(int argc,char **argv){
    check(argc==2,"fixture");std::ifstream f(argv[1]);std::string src((std::istreambuf_iterator<char>(f)),{}),error;ageskeleton::Player p;
    check(p.load_json(src,error),error.c_str());p.play("Source");p.update(.25);near(p.positions[0],2.5f,"source");
    check(p.cross_fade("Destination",1),"start blend");near(p.positions[0],2.5f,"start continuity");check(p.is_blending()&&p.blend_progress()==0,"active");
    p.update(.25);near(p.positions[0],5.25f,"both clips advance");near(p.colors[1],.75f,"colors blend");near(p.colors[3],.875f,"alpha blend");check(p.visible[0]&&p.orders[0]==0,"source discrete keys");
    check(p.events.size()==2&&p.events[0].animation=="Destination","target events only");
    auto before=p.positions;float progress=p.blend_progress();p.playing=false;p.update(1);check(p.positions==before&&p.blend_progress()==progress&&p.events.empty(),"pause blend");p.playing=true;p.speed=0;p.update(1);check(p.positions==before&&p.blend_progress()==progress,"zero speed");p.speed=1;
    check(!p.cross_fade("Missing",1)&&!p.cross_fade("Third",NAN)&&!p.cross_fade("Third",-1)&&p.positions==before&&p.blend_progress()==progress,"invalid atomic");
    p.update(.25);near(p.positions[0],7.75f,"half blend");check(!p.visible[0]&&p.orders[0]==5,"target discrete keys");
    p.set_attachment("Slot","visible");check(p.visible[0],"attachment override during blend");p.set_slot_visible("Slot",false);check(!p.visible[0],"hide override during blend");p.set_slot_visible("Slot",true);p.set_attachment("Slot","",true);
    check(p.cross_fade("Third",1),"interrupt");near(p.positions[0],7.75f,"interrupt continuity");p.update(.5);near(p.positions[0],14.875f,"interrupt snapshot");p.update(.5);near(p.positions[0],22,"finish");check(!p.is_blending()&&p.blend_progress()==1,"complete");
    p.cross_fade("Source",1);p.seek(.5);check(!p.is_blending(),"seek cancels blend");near(p.positions[0],3,"seek target");p.cross_fade("Third",1);p.stop();check(!p.is_blending()&&p.positions==p.data().rest.positions,"stop clears");
    p.cross_fade("Destination",0);near(p.positions[0],12,"zero duration");check(!p.is_blending(),"instant");
    p.play("Source");p.seek(.75);p.cross_fade("Source",1,false);near(p.time,.75f,"same clip preserve time");p.speed=-1;p.update(.25);near(p.time,.5f,"reverse target");near(p.blend_progress(),.25f,"reverse progress");near(p.positions[0],3,"reverse pose");
    p.speed=1;p.play("Source");p.cross_fade("Once",2);p.update(1);check(p.playing&&p.is_blending(),"short nonloop holds until fade ends");p.update(1);check(!p.playing&&!p.is_blending(),"nonloop stops after fade");
    p.play("Source");p.cross_fade("Destination",1);p.update(.25);before=p.positions;progress=p.blend_progress();check(!p.update(2000)&&p.positions==before&&p.blend_progress()==progress&&p.events.empty(),"event failure leaves fade atomic");
    check(p.set_ik_target("Tip",4.25f,1,2,1,64,.0001f),"IK during blend");std::array<float,2> tip;p.bone_tip("Tip",tip);near(tip[0],4.25f,"blended IK x");near(tip[1],1,"blended IK y");p.clear_ik_target("Tip");check(p.positions==before,"clear IK returns blend");
    auto j=ageskeleton::Json::parse(src);j["version"]=1;j.erase("rig");check(p.load_json(j.dump(),error),"legacy");p.play("Source");p.cross_fade("Destination",1);p.update(.5);near(p.positions[0],7.5f,"legacy crossfade");
    std::cout<<"AGESKELETON_BLEND_NATIVE_PASS advancing_source interrupt pause reverse slots colors events IK legacy\n";
}
