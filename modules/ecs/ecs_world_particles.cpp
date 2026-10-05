// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "servers/rendering/rendering_server.h"
#include "core/os/os.h"
#include "scene/resources/curve.h"
#include "scene/resources/gradient.h"
#include "scene/resources/texture.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/environment.h"

bool ECSWorld::set_particles(uint64_t id, const Dictionary &definition) {
 ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
 if (!is_alive(id)) { return false; }
 Dictionary data = get_particles(id); data.merge(definition, true);
 for (const Variant &key : data.keys()) {
  if (key.get_type()!=Variant::STRING && key.get_type()!=Variant::STRING_NAME) { return false; }
  String field = key;
  if (field != "amount" && field != "lifetime" && field != "speed" && field != "spread" && field != "gravity" && field != "size" && field != "color" && field != "emitting" && field != "shape" && field != "radius" && field != "extents" && field != "seed" && field != "one_shot" && field != "size_curve" && field != "color_ramp" && field != "material" && field != "texture" && field != "budget" && field != "event" && field != "platform_budgets") { return false; }
 }
 Variant amount = data.get("amount", 128), emitting = data.get("emitting", true), gravity = data.get("gravity", Vector3(0,-2,0)), color = data.get("color", Color(1,.55,.12));
 if (amount.get_type() != Variant::INT || int64_t(amount) < 1 || int64_t(amount) > 10000 || emitting.get_type() != Variant::BOOL || gravity.get_type() != Variant::VECTOR3 || !Vector3(gravity).is_finite() || color.get_type() != Variant::COLOR) { return false; }
 Color tint = color;
 if (!Math::is_finite(tint.r) || !Math::is_finite(tint.g) || !Math::is_finite(tint.b) || !Math::is_finite(tint.a)) { return false; }
 const char *names[] = {"lifetime", "speed", "spread", "size"};
 const double defaults[] = {2,3,1.5,.08}, minima[] = {.01,0,0,.001}, maxima[] = {120,1000,1000,100};
 for (int i=0;i<4;i++) {
  Variant value = data.get(names[i], defaults[i]);
  if ((value.get_type()!=Variant::FLOAT && value.get_type()!=Variant::INT) || !Math::is_finite(double(value)) || double(value)<minima[i] || double(value)>maxima[i]) { return false; }
  data[names[i]]=double(value);
 }
 data["amount"]=amount; data["emitting"]=emitting; data["gravity"]=gravity; data["color"]=color;
 Variant shape=data.get("shape","cone"), seed=data.get("seed",1), once=data.get("one_shot",false), radius=data.get("radius",.2), extents=data.get("extents",Vector3(.2,.2,.2)), budget=data.get("budget",10000), event=data.get("event","play");
 if(shape.get_type()!=Variant::STRING || (String(shape)!="cone" && String(shape)!="sphere" && String(shape)!="box" && String(shape)!="point") || seed.get_type()!=Variant::INT || int64_t(seed)<0 || int64_t(seed)>UINT32_MAX || once.get_type()!=Variant::BOOL || (radius.get_type()!=Variant::INT && radius.get_type()!=Variant::FLOAT) || !Math::is_finite(double(radius)) || double(radius)<0 || double(radius)>1000 || extents.get_type()!=Variant::VECTOR3 || !Vector3(extents).is_finite() || budget.get_type()!=Variant::INT || int64_t(budget)<1 || int64_t(budget)>10000 || event.get_type()!=Variant::STRING) { return false; }
 Variant budgets=data.get("platform_budgets",Dictionary());
 if(budgets.get_type()!=Variant::DICTIONARY) { return false; }
 for(const Variant &platform:Dictionary(budgets).keys()) {
  if(platform.get_type()!=Variant::STRING) { return false; }
  Variant limit=Dictionary(budgets)[platform]; if(limit.get_type()!=Variant::INT || int64_t(limit)<1 || int64_t(limit)>10000) { return false; }
 }
 data["platform_budgets"]=Dictionary(budgets).duplicate(true);
 int effective_budget=int(budget);
 String platform=OS::get_singleton()->get_name().to_lower();
 if(Dictionary(budgets).has(platform)) { effective_budget=MIN(effective_budget,int(Dictionary(budgets)[platform])); }
 Vector3 extent=extents; if(extent.x<0 || extent.y<0 || extent.z<0 || extent.x>1000 || extent.y>1000 || extent.z>1000) { return false; }
 for(const String &field:{String("size_curve"),String("color_ramp"),String("material"),String("texture")}) {
  Variant value=data.get(field,Variant()); if(value.get_type()!=Variant::NIL && value.get_type()!=Variant::OBJECT) { return false; }
  Object *object=value.get_type()==Variant::OBJECT?(Object*)value:nullptr;
  if(object && ((field=="size_curve" && !Object::cast_to<Curve>(object)) || (field=="color_ramp" && !Object::cast_to<Gradient>(object)) || (field=="material" && !Object::cast_to<Material>(object)) || (field=="texture" && !Object::cast_to<Texture2D>(object)))) { return false; }
  data[field]=value;
 }
 data["shape"]=shape; data["seed"]=seed; data["one_shot"]=once; data["radius"]=double(radius); data["extents"]=extents; data["budget"]=budget; data["event"]=event;
 ParticleState *existing = particles.getptr(uint32_t(id));
 const bool rebuild = !existing || existing->points.size()!=MIN(int(amount),effective_budget) || double(existing->definition["lifetime"])!=double(data["lifetime"]);
 if (rebuild) {
  remove_particles(id);
  ParticleState state; state.random=uint32_t(id)+1; state.points.resize(MIN(int(amount),effective_budget));
  state.render_buffer.resize(state.points.size()*16);
  for(int i=0;i<state.points.size();i++) { state.points.write[i].age=-double(data["lifetime"])*i/state.points.size()-.000001; }
  Ref<SphereMesh> mesh; mesh.instantiate(); mesh->set_radius(.5); mesh->set_height(1); mesh->set_radial_segments(6); mesh->set_rings(2); state.mesh=mesh;
  Ref<StandardMaterial3D> material; material.instantiate(); material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED); material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR,true); material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA); state.material=material;
  auto *rs=RenderingServer::get_singleton(); state.multimesh=rs->multimesh_create();
  rs->multimesh_allocate_data(state.multimesh,MIN(int(amount),effective_budget),RSE::MULTIMESH_TRANSFORM_3D,true);
  rs->multimesh_set_mesh(state.multimesh,mesh->get_rid()); rs->multimesh_set_visible_instances(state.multimesh,0);
  state.instance=rs->instance_create(); rs->instance_set_base(state.instance,state.multimesh); rs->instance_geometry_set_material_override(state.instance,material->get_rid());
  particles[uint32_t(id)]=state;
 }
 ParticleState &state=particles[uint32_t(id)];
 Ref<Material> custom=data["material"]; Ref<Texture2D> texture=data["texture"];
 if(custom.is_valid()) { state.material=custom; }
 else {
  Ref<StandardMaterial3D> mat; mat.instantiate(); mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED); mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR,true); mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA); mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO,texture); if(texture.is_valid()) { mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED); } state.material=mat;
 }
 if(texture.is_valid()) { Ref<QuadMesh> quad; quad.instantiate(); state.mesh=quad; }
 else { Ref<SphereMesh> mesh; mesh.instantiate(); mesh->set_radius(.5); mesh->set_height(1); mesh->set_radial_segments(6); mesh->set_rings(2); state.mesh=mesh; }
 RenderingServer::get_singleton()->multimesh_set_mesh(state.multimesh,state.mesh->get_rid());
 RenderingServer::get_singleton()->instance_geometry_set_material_override(state.instance,state.material->get_rid());
 state.definition=data.duplicate(true);
 uint8_t marker=1; store_ui_column(uint32_t(id),"particles",&marker);
 return true;
}
Dictionary ECSWorld::get_particles(uint64_t id) const {
 ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,Dictionary());
 const ParticleState *state=is_alive(id)?particles.getptr(uint32_t(id)):nullptr;
 return state?state->definition.duplicate(true):Dictionary();
}
bool ECSWorld::remove_particles(uint64_t id) {
 ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,false);
 if (!is_alive(id)) { return false; }
 ParticleState *state=particles.getptr(uint32_t(id)); if (!state) { return false; }
 auto *rs=RenderingServer::get_singleton(); rs->free_rid(state->instance); rs->free_rid(state->multimesh);
 particles.erase(uint32_t(id)); remove_row(pools["particles"],uint32_t(id)); return true;
}
void ECSWorld::clear_particles() {
 auto *rs=RenderingServer::get_singleton();
 for (const auto &entry:particles) { rs->free_rid(entry.value.instance); rs->free_rid(entry.value.multimesh); }
 particles.clear();
}
void ECSWorld::step_particles(double delta) {
 const uint64_t started=OS::get_singleton()->get_ticks_usec();
 for (auto &entry:particles) {
  if (!slots[entry.key].active || entry.value.paused) { continue; }
  ParticleState &state=entry.value; state.time+=delta;
  double lifetime=state.definition["lifetime"], speed=state.definition["speed"], spread=state.definition["spread"], radius=state.definition["radius"];
  bool once=state.definition["one_shot"], emitting=state.definition["emitting"]; String shape=state.definition["shape"]; Vector3 gravity=state.definition["gravity"], extent=state.definition["extents"];
  const uint32_t base_seed=uint32_t(int64_t(state.definition["seed"]));
  ParticlePoint *points=state.points.ptrw();
  for(int i=0;i<state.points.size();i++) {
   ParticlePoint &point=points[i];
   double local=state.time-(once?0:lifetime*i/state.points.size());
   if(local<0 || (once && local>=lifetime)) { point.age=lifetime; continue; }
   const uint64_t cycle=uint64_t(local/lifetime);
   if(!emitting) { point.age+=delta; if(point.age<lifetime) { point.position+=point.velocity*delta+gravity*(delta*delta*.5); point.velocity+=gravity*delta; } continue; }
   uint32_t seed=base_seed ^ (uint32_t(i)*747796405u) ^ (uint32_t(cycle)*2891336453u);
   auto random=[&seed]() { seed+=0x9e3779b9u; uint32_t value=seed; value=(value^(value>>16))*0x85ebca6bu; value=(value^(value>>13))*0xc2b2ae35u; value^=value>>16; return double(value)/4294967295.0; };
   double z=random()*2-1, angle=random()*Math::TAU;
   Vector3 direction(Math::sqrt(1-z*z)*Math::cos(angle),z,Math::sqrt(1-z*z)*Math::sin(angle));
   Vector3 origin;
   if(shape=="sphere") { origin=direction*radius*Math::pow(random(),1.0/3.0); point.velocity=direction*speed; }
   else {
    if(shape=="box") { origin=Vector3(random()*2-1,random()*2-1,random()*2-1)*extent; }
    else if(shape=="cone") { origin=Vector3(direction.x,0,direction.z)*radius; }
    point.velocity=Vector3(direction.x*spread,speed,direction.z*spread);
   }
   point.age=Math::fposmod(local,lifetime);
   point.position=origin+point.velocity*point.age+gravity*(point.age*point.age*.5);
   point.velocity+=gravity*point.age;
  }
 }
 particles_step_usec=OS::get_singleton()->get_ticks_usec()-started;
}
bool ECSWorld::control_particles(uint64_t id,const String &action,double time) {
 ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,false);
 ParticleState *state=is_alive(id)?particles.getptr(uint32_t(id)):nullptr;
 if(!state || !Math::is_finite(time) || time<0 || time>120) { return false; }
 if(action=="pause") { state->paused=true; }
 else if(action=="play") { state->paused=false; state->definition["emitting"]=true; }
 else if(action=="stop") { state->definition["emitting"]=false; }
 else if(action=="restart" || action=="seek") {
  state->time=action=="seek"?time:0; state->paused=false; state->definition["emitting"]=true;
  step_particles(0); state->paused=action=="seek";
 } else { return false; }
 return true;
}
Dictionary ECSWorld::get_particles_statistics() const {
 ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,Dictionary());
 Dictionary result; int alive=0, capacity=0, visible_emitters=0;
 for(const auto &entry:particles) { capacity+=entry.value.points.size(); if(!slots[entry.key].active) { continue; } visible_emitters++; double lifetime=entry.value.definition["lifetime"]; for(const ParticlePoint &point:entry.value.points) { if(point.age>=0 && point.age<lifetime) { alive++; } } }
 result["emitters"]=particles.size(); result["active_emitters"]=visible_emitters; result["live_particles"]=alive; result["capacity"]=capacity; result["simulation_usec"]=int64_t(particles_step_usec); result["upload_usec"]=int64_t(particles_upload_usec); result["draw_call_upper_estimate"]=visible_emitters; result["cpu_particle_bytes"]=int64_t(capacity*sizeof(ParticlePoint)); result["backend"]="cpu_multimesh";
 return result;
}
bool ECSWorld::trigger_effect(uint64_t root,const String &event) {
 ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,false);
 if(!is_alive(root)) { return false; }
 for(uint64_t id:query(PackedStringArray(),true)) {
  uint64_t cursor=id; while(cursor && cursor!=root) { cursor=get_parent(cursor); }
  if(cursor!=root || !is_active_in_hierarchy(id)) { continue; }
  if(ParticleState *state=particles.getptr(uint32_t(id))) { if(String(state->definition["event"])==event) { control_particles(id,"restart"); } }
  if(audio_sources.has(uint32_t(id))) { Dictionary audio; audio["playing"]=true; audio["position"]=0.0; set_audio(id,audio); }
  if(lights.has(uint32_t(id))) { Dictionary light; light["enabled"]=true; set_light(id,light); }
 }
 return true;
}
void ECSWorld::sync_particles(RID scenario) {
 ERR_FAIL_COND(Thread::get_caller_id()!=owner_thread);
 const uint64_t started=OS::get_singleton()->get_ticks_usec();
 auto *rs=RenderingServer::get_singleton();
 for (auto &entry:particles) {
  ParticleState &state=entry.value;
  rs->instance_set_scenario(state.instance,scenario); rs->instance_set_visible(state.instance,slots[entry.key].active);
  uint64_t id=(uint64_t(slots[entry.key].generation)<<32)|entry.key;
  rs->instance_set_transform(state.instance,calculate_global_transform(id));
  int visible=0; double lifetime=state.definition["lifetime"], size=state.definition["size"]; Color color=state.definition["color"];
  Ref<Curve> curve=state.definition["size_curve"]; Ref<Gradient> ramp=state.definition["color_ramp"];
  float *buffer=state.render_buffer.ptrw();
  for(const ParticlePoint &point:state.points) {
   if(point.age<0 || point.age>=lifetime) { continue; }
   double fraction=point.age/lifetime, fade=1-fraction; Color tint=color; tint.a*=fade;
   double particle_size=size*(curve.is_valid()?MAX(0.0f,curve->sample_baked(fraction)):1.0f);
   if(ramp.is_valid()) { tint=color*ramp->get_color_at_offset(fraction); }
   // MultiMesh uses three transform rows followed by four color floats.
   float *row=buffer+visible*16;
   row[0]=particle_size; row[1]=0; row[2]=0; row[3]=point.position.x;
   row[4]=0; row[5]=particle_size; row[6]=0; row[7]=point.position.y;
   row[8]=0; row[9]=0; row[10]=particle_size; row[11]=point.position.z;
   row[12]=tint.r; row[13]=tint.g; row[14]=tint.b; row[15]=tint.a;
   visible++;
  }
  if(visible>0) { rs->multimesh_set_buffer(state.multimesh,state.render_buffer); }
  rs->multimesh_set_visible_instances(state.multimesh,visible);
 }
 particles_upload_usec=OS::get_singleton()->get_ticks_usec()-started;
}

#include "ecs_scene.h"
#ifdef TOOLS_ENABLED
Dictionary ECSWorld::particle_preview(uint64_t id) const {
 Dictionary data;
 const ParticleState *state=is_alive(id)?particles.getptr(uint32_t(id)):nullptr;
 if(!state) { return data; }
 int visible=0; double lifetime=state->definition["lifetime"];
 for(const ParticlePoint &point:state->points) { if(point.age>=0 && point.age<lifetime) { visible++; } }
 data["buffer"]=state->render_buffer;
 data["visible"]=visible;
 return data;
}
void ECSWorld::apply_particle_preview(uint64_t id,const Dictionary &data,RID scenario,const Transform3D &transform,bool visible) {
 ParticleState *state=is_alive(id)?particles.getptr(uint32_t(id)):nullptr;
 if(!state) { return; }
 auto *rs=RenderingServer::get_singleton();
 rs->instance_set_scenario(state->instance,scenario);
 rs->instance_set_transform(state->instance,transform);
 rs->instance_set_visible(state->instance,visible && !data.is_empty());
 if(data.is_empty() || data.get("buffer",Variant()).get_type()!=Variant::PACKED_FLOAT32_ARRAY) { return; }
 PackedFloat32Array buffer=data["buffer"];
 int count=data.get("visible",0);
 if(buffer.size()%16 || buffer.size()>160000 || count<0 || count>buffer.size()/16) { return; }
 if(state->render_buffer.size()!=buffer.size()) {
  rs->multimesh_allocate_data(state->multimesh,buffer.size()/16,RSE::MULTIMESH_TRANSFORM_3D,true);
 }
 state->render_buffer=buffer;
 rs->multimesh_set_buffer(state->multimesh,buffer);
 rs->multimesh_set_visible_instances(state->multimesh,count);
}
#endif
#include "core/io/resource_saver.h"
#include "core/io/resource_loader.h"
bool ECSWorld::particles_self_test(const String &path) {
 Ref<ECSWorld> world; world.instantiate(); uint64_t id=world->create_entity();
 Dictionary definition; definition["amount"]=64;
 if(!world->set_particles(id,definition) || world->query(PackedStringArray({"particles"})).size()!=1) { return false; }
 world->step(.5);
 const ParticlePoint before=world->particles[uint32_t(id)].points[0];
 if(before.age<=0 || before.position.is_zero_approx()) { return false; }
 world->set_active(id,false); world->step(1);
 if(world->particles[uint32_t(id)].points[0].position!=before.position) { return false; }
 world->set_active(id,true); world->step(.1);
 if(world->particles[uint32_t(id)].points[0].position==before.position) { return false; }
 if(!world->control_particles(id,"seek",.7)) { return false; }
 Vector3 fixed=world->particles[uint32_t(id)].points[0].position;
 world->step(.2); if(world->particles[uint32_t(id)].points[0].position!=fixed) { return false; }
 world->control_particles(id,"restart"); world->control_particles(id,"seek",.7);
 if(world->particles[uint32_t(id)].points[0].position!=fixed) { return false; }
 world->control_particles(id,"play");
 Dictionary invalid; invalid["amount"]=-1;
 if(world->set_particles(id,invalid) || int(world->get_particles(id)["amount"])!=64) { return false; }
 Dictionary off; off["emitting"]=false; world->set_particles(id,off); world->step(3);
 for(const ParticlePoint &point:world->particles[uint32_t(id)].points) { if(point.age<2) { return false; } }
 Dictionary serialized=world->serialize(); Ref<ECSScene> roundtrip; roundtrip.instantiate(); roundtrip->set_entities(serialized["entities"]); roundtrip->set_layouts(serialized["layouts"]);
 Ref<ECSWorld> restored=roundtrip->instantiate();
 if(restored.is_null() || restored->query(PackedStringArray({"particles"})).size()!=1 || !world->remove_component(id,"particles") || !world->query(PackedStringArray({"particles"})).is_empty()) { return false; }
 world->set_particles(id,definition); world->destroy_entity(id); if(!world->particles.is_empty()) { return false; }
 Ref<ECSScene> scene; scene.instantiate(); Array entities;
 Ref<StandardMaterial3D> material; material.instantiate(); material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED); material->set_albedo(Color(.12,.65,1));
 Ref<BoxMesh> box; box.instantiate(); box->set_size(Vector3(.7,.7,.7));
 Ref<Animation> animation; animation.instantiate(); animation->set_length(4); animation->set_loop_mode(Animation::LOOP_LINEAR);
 int track=animation->add_track(Animation::TYPE_POSITION_3D);
 animation->position_track_insert_key(track,0,Vector3(-2,0,0)); animation->position_track_insert_key(track,2,Vector3(2,1,0)); animation->position_track_insert_key(track,4,Vector3(-2,0,0));
 int scale_track=animation->add_track(Animation::TYPE_SCALE_3D);
 animation->scale_track_insert_key(scale_track,0,Vector3(1,1,1)); animation->scale_track_insert_key(scale_track,2,Vector3(1.5,1.5,1.5)); animation->scale_track_insert_key(scale_track,4,Vector3(1,1,1));
 Dictionary moving, anim; anim["clip"]=animation;
 Array events; Dictionary event; event["time"]=2.0; event["event"]="burst"; events.push_back(event); anim["effect_events"]=events; anim["playing"]=true; anim["speed"]=1.0;
 moving["name"]=String(U"动画方块：往返与缩放"); moving["position"]=Vector3(-2,0,0); moving["mesh"]=box; moving["material"]=material; moving["animation"]=anim; entities.push_back(moving);
 Dictionary emitter; emitter["name"]=String(U"金色粒子喷泉"); emitter["position"]=Vector3(0,-1,0); emitter["particles"]=Dictionary();
 world->create_entity(); uint64_t temp=world->create_entity(); world->set_particles(temp,Dictionary()); emitter["particles"]=world->get_particles(temp); entities.push_back(emitter);
 Dictionary attached; attached["name"]=String(U"跟随动画的蓝色粒子"); attached["parent"]=0; attached["position"]=Vector3(); Dictionary blue=Dictionary(emitter["particles"]).duplicate(true); blue["color"]=Color(.1,.65,1); blue["amount"]=64; blue["speed"]=1.0; attached["particles"]=blue; entities.push_back(attached);
 Dictionary fire; fire["name"]=String(U"火焰示例"); fire["position"]=Vector3(-3,-1,0);
 Dictionary flame=Dictionary(emitter["particles"]).duplicate(true); flame["amount"]=192; flame["lifetime"]=1.4; flame["speed"]=2.0; flame["spread"]=.35; flame["gravity"]=Vector3(0,.3,0); flame["size"]=.24;
 Ref<Image> image=Image::create_empty(32,32,false,Image::FORMAT_RGBA8);
 for(int y=0;y<32;y++) { for(int x=0;x<32;x++) { double r=Vector2((x-15.5)/15.5,(y-15.5)/15.5).length(); double alpha=MAX(0.0,1-r*r); image->set_pixel(x,y,Color(1,1,1,alpha*alpha)); } }
 Ref<ImageTexture> soft=ImageTexture::create_from_image(image); flame["texture"]=soft; flame["size"]=.45;
 Ref<Curve> curve; curve.instantiate(); curve->add_point(Vector2(0,.2)); curve->add_point(Vector2(.2,1)); curve->add_point(Vector2(1,0)); flame["size_curve"]=curve;
 Ref<Gradient> gradient; gradient.instantiate(); gradient->set_color(0,Color(1,.9,.2,1)); gradient->set_color(1,Color(.8,.05,.01,0)); flame["color_ramp"]=gradient;
 fire["particles"]=flame; entities.push_back(fire);
 Dictionary burst; burst["name"]=String(U"动画事件爆炸"); burst["parent"]=0; burst["position"]=Vector3();
 Dictionary explosion=Dictionary(emitter["particles"]).duplicate(true); explosion["shape"]="sphere"; explosion["one_shot"]=true; explosion["amount"]=160; explosion["speed"]=4.0; explosion["lifetime"]=.8; explosion["gravity"]=Vector3(); explosion["event"]="burst"; explosion["emitting"]=false; explosion["texture"]=soft; explosion["size"] = .15; explosion["color_ramp"]=gradient; burst["particles"]=explosion; entities.push_back(burst);
 Ref<Environment> environment; environment.instantiate(); environment->set_background(Environment::BG_COLOR); environment->set_bg_color(Color(.025,.035,.055)); scene->set_environment(environment);
 scene->set_entities(entities); scene->set_camera_transform(Transform3D(Basis(),Vector3(0,2,9)).looking_at(Vector3(0,.5,0),Vector3(0,1,0)));
 if(ResourceSaver::save(scene,path)!=OK) { return false; }
 Ref<ECSScene> loaded=ResourceLoader::load(path,"ECSScene",ResourceLoader::CACHE_MODE_IGNORE);
 if(loaded.is_null()) { return false; }
 Ref<ECSWorld> demo=loaded->instantiate(); if(demo.is_null()) { return false; }
 PackedInt64Array animated=demo->query(PackedStringArray({"animation"})); if(animated.size()!=1) { return false; }
 demo->step(2);
 if(!demo->get_vector(animated[0],"position").is_equal_approx(Vector3(2,1,0))) { return false; }
 PackedInt64Array emitters=demo->query(PackedStringArray({"particles"}));
 if(emitters.size()!=4) { return false; }
 bool triggered=false; for(uint64_t emitter_id:emitters) { Dictionary d=demo->get_particles(emitter_id); if(String(d["event"])=="burst") { triggered=bool(d["emitting"]); } }
 if(!triggered) { return false; }
 Dictionary limit; limit["amount"]=100; limit["budget"]=10; world->set_particles(temp,limit);
 if(world->particles[uint32_t(temp)].points.size()!=10) { return false; }
 print_line("ECS_PARTICLES_PASS simulate inactive resume invalid atomic stop serialize remove destroy demo_animation deterministic_seek pause animation_event budget");
 return true;
}
