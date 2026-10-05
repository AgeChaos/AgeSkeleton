// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
#include "ageskeleton/batcher.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
void check(bool value,const char *name){if(!value){std::cerr<<name<<" FAILED\n";std::exit(1);}}
int main(int argc,char **argv){
    check(argc==2,"fixture argument");std::ifstream f(argv[1]);std::string source((std::istreambuf_iterator<char>(f)),{}),error;ageskeleton::Player p;
    check(p.load_json(source,error),error.c_str());check(p.play("Walk"),"play");p.seek(.35f);auto before=p.positions;
    check(p.set_wardrobe("Tops","Tops/Steel Armor"),"wardrobe");check(p.positions==before&&p.playing,"wardrobe preserves pose/playback");
    check(!p.set_wardrobe("Tops","Weapons/Short Sword"),"invalid group");
    check(p.set_slot_visible("Torso",false),"hide slot");for(size_t i=0;i<p.data().attachments.size();++i)if(p.data().attachments[i].slot==1)check(!p.visible[i],"hidden torso");
    p.set_slot_visible("Torso",true);check(p.set_attachment("Torso",""),"empty key");check(p.set_attachment("Torso","",true),"restore key");
    check(!p.seek(NAN)&&!p.update(-1),"invalid input");p.seek(0);p.update(100.1);check(p.time>=0&&p.time<1.2,"loop");p.playing=false;float time=p.time;p.update(1);check(p.time==time,"pause");
    check(!p.load_json("{}",error),"invalid data");check(p.positions.size()==before.size(),"load failure atomic");
    auto j=ageskeleton::Json::parse(source);j["attachments"][0]["triangles"][0]=100000;check(!p.load_json(j.dump(),error),"bad index");
    j=ageskeleton::Json::parse(source);j["clips"][0]["loop"]=false;check(p.load_json(j.dump(),error),"nonloop load");check(p.play("Idle"),"idle");p.update(10);check(!p.playing&&std::abs(p.time-2.4f)<.001,"nonloop end");
    p.stop();check(p.positions==p.data().rest.positions,"stop rest");
    j=ageskeleton::Json::parse(source);j["textures"]=ageskeleton::Json::array({"a.png","b.png"});
    for(auto &a:j["attachments"])a["texture"]=0;j["attachments"][1]["texture"]=1;
    check(p.load_json(j.dump(),error),"batch fixture");
    std::fill(p.visible.begin(),p.visible.end(),false);for(int i=0;i<3;++i){p.visible[i]=true;p.orders[i]=i;}
    ageskeleton::Batcher batch;check(batch.update(p)&&batch.batches.size()==3,"A B A preserves painter order");
    auto revision=batch.revision;p.positions[0]+=1;check(!batch.update(p)&&batch.revision==revision,"pose reuses topology");
    p.visible[1]=false;check(batch.update(p)&&batch.batches.size()==1,"hidden middle merges adjacent pages");
    check(batch.batches[0].positions.size()==size_t((p.data().attachments[0].count+p.data().attachments[2].count)*2),"combined vertices");
    for(int index:batch.batches[0].indices)check(index>=0&&size_t(index)<batch.batches[0].positions.size()/2,"offset indices");
    std::fill(p.visible.begin(),p.visible.end(),false);check(batch.update(p)&&batch.batches.empty(),"all hidden clears batches");
    std::cout<<"AGESKELETON_NATIVE_PASS load play seek loop pause wardrobe visibility invalid atomic stop\n";
}
