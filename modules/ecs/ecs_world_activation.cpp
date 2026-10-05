// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/physics_2d/physics_server_2d.h"
#include "servers/navigation_3d/navigation_server_3d.h"
#include "servers/audio/audio_server.h"
#include "servers/rendering/rendering_server.h"

bool ECSWorld::is_active_self(uint64_t id) const {
 ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
 return is_alive(id) && slots[uint32_t(id)].active_self;
}
bool ECSWorld::is_active_in_hierarchy(uint64_t id) const {
 ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
 return is_alive(id) && slots[uint32_t(id)].active;
}
bool ECSWorld::set_active(uint64_t id, bool active) {
 ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
 if (!is_alive(id)) { return false; }
 if (slots[uint32_t(id)].active_self == active) { return true; }
 slots.write[uint32_t(id)].active_self = active;
 refresh_activation(uint32_t(id));
 return true;
}
void ECSWorld::refresh_activation(uint32_t root) {
 Vector<uint32_t> pending;
 pending.push_back(root);
 bool changed = false;
 for (int i = 0; i < pending.size(); i++) {
  uint32_t index = pending[i];
  const uint64_t *parent = parents.getptr(index);
  bool active = slots[index].active_self && (!parent || (is_alive(*parent) && slots[uint32_t(*parent)].active));
  if (active == slots[index].active) { continue; }
  slots.write[index].active = active;
  apply_activation(index);
  changed = true;
  if (const HashSet<uint32_t> *nested = children.getptr(index)) {
   for (uint32_t child : *nested) { pending.push_back(child); }
  }
 }
 if (changed) { ui_revision++; sync_joint_activation(); }
}
void ECSWorld::apply_activation(uint32_t index) {
 const bool active = slots[index].active;
 if (auto *server = PhysicsServer3D::get_singleton()) {
  if (PhysicsBody *body = physics_bodies.getptr(index)) { server->body_set_space(body->rid, active ? physics_space : RID()); }
  if (AreaState *area = areas.getptr(index)) { server->area_set_space(area->rid, active ? physics_space : RID()); }
 }
 if (auto *server = PhysicsServer2D::get_singleton()) {
  if (PhysicsBody2D *body = physics_bodies_2d.getptr(index)) { server->body_set_space(body->rid, active ? physics_space_2d : RID()); }
  if (AreaState2D *area = areas_2d.getptr(index)) { server->area_set_space(area->rid, active ? physics_space_2d : RID()); }
 }
 if (auto *server = NavigationServer3D::get_singleton()) {
  if (NavigationRegion *region = navigation_regions.getptr(index)) { server->region_set_enabled(region->rid, active); }
 }
 if (AudioState *audio = audio_sources.getptr(index)) {
  audio->motion_valid = false;
  if (audio->playback.is_valid()) { AudioServer::get_singleton()->set_playback_paused(audio->playback, !active || bool(audio->definition.get("paused", false))); }
 }
 if (LightState *light = lights.getptr(index)) { RenderingServer::get_singleton()->instance_set_visible(light->instance, active && bool(light->definition.get("enabled", true))); }
}
void ECSWorld::sync_joint_activation() {
 struct Restore { uint64_t id; Dictionary definition; int kind; };
 Vector<Restore> restore;
 int kind = 0;
 for (auto *table : { &pin_joints, &joints_3d, &joints_2d }) {
  for (auto &entry : *table) {
   PinJointState &joint = entry.value;
   bool active = slots[entry.key].active && is_active_in_hierarchy(joint.body_a) && is_active_in_hierarchy(joint.body_b);
   if (!active && !joint.suspended) {
    if (kind == 2) { PhysicsServer2D::get_singleton()->joint_disable_collisions_between_bodies(joint.rid, false); PhysicsServer2D::get_singleton()->joint_clear(joint.rid); }
    else { PhysicsServer3D::get_singleton()->joint_disable_collisions_between_bodies(joint.rid, false); PhysicsServer3D::get_singleton()->joint_clear(joint.rid); }
    joint.suspended = true;
   } else if (active && joint.suspended) {
    restore.push_back({ (uint64_t(slots[entry.key].generation) << 32) | entry.key, joint.definition.duplicate(true), kind });
    joint.suspended = false;
   }
  }
  kind++;
 }
 for (const Restore &item : restore) {
  if (item.kind == 0) { set_pin_joint(item.id, item.definition); }
  else if (item.kind == 1) { set_joint_3d(item.id, item.definition); }
  else { set_joint_2d(item.id, item.definition); }
 }
}

#include "ecs_scene.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
bool ECSWorld::activation_self_test() {
 Ref<ECSWorld> w; w.instantiate();
 uint64_t root = w->create_entity(), child = w->create_entity(), leaf = w->create_entity();
 w->set_parent(child, root); w->set_parent(leaf, child);
 w->set_vector(child, "position", Vector3()); w->set_vector(child, "velocity", Vector3(1,0,0));
 if (!w->set_active(root, false) || w->is_active_in_hierarchy(leaf) || !w->is_active_self(child)) { return false; }
 if (w->query(PackedStringArray()).size() != 0 || w->query(PackedStringArray(), true).size() != 3) { return false; }
 w->step(1); if (w->get_vector(child, "position") != Vector3()) { return false; }
 w->set_active(child, false); w->set_active(root, true);
 if (w->is_active_in_hierarchy(leaf)) { return false; }
 w->set_active(child, true); w->step(1);
 if (w->get_vector(child, "position") != Vector3(1,0,0)) { return false; }
 w->set_active(root, false); w->set_parent(child, 0);
 if (!w->is_active_in_hierarchy(leaf)) { return false; }
 w->set_parent(child, root); w->destroy_entity(root);
 if (!w->is_active_in_hierarchy(child) || !w->is_active_in_hierarchy(leaf)) { return false; }
 uint64_t replacement = w->create_entity();
 if (!w->is_active_self(replacement) || w->set_active(root, false)) { return false; }
 Dictionary ui; ui["kind"]="button"; ui["rect"]=Rect2(0,0,100,40);
 if (!w->set_ui(child, ui)) { return false; }
 w->queue_ui_event(child,"pressed",Variant()); w->set_active(child,false); w->step(0);
 if (!w->get_ui_events().is_empty()) { return false; }
 for (const ECSUIItem &item : w->get_ui_items()) { if (item.entity==child && item.style.visible) { return false; } }
 Ref<BoxShape3D> shape; shape.instantiate(); Dictionary body; body["shape"]=shape; body["mode"]="static";
 if (!w->set_physics(child,body)) { return false; }
 auto *p3=PhysicsServer3D::get_singleton(); RID rid=w->physics_bodies[uint32_t(child)].rid;
 if (p3->body_get_space(rid).is_valid()) { return false; }
 w->set_active(child,true); if (p3->body_get_space(rid)!=w->physics_space) { return false; }
 w->set_active(child,false); if (p3->body_get_space(rid).is_valid()) { return false; }
 Ref<RectangleShape2D> shape2; shape2.instantiate(); Dictionary body2; body2["shape"]=shape2; body2["mode"]="static";
 w->set_parent(leaf,0); if (!w->set_physics_2d(leaf,body2)) { return false; }
 RID rid2=w->physics_bodies_2d[uint32_t(leaf)].rid; w->set_active(leaf,false);
 if (PhysicsServer2D::get_singleton()->body_get_space(rid2).is_valid()) { return false; }
 w->set_active(leaf,true); if (PhysicsServer2D::get_singleton()->body_get_space(rid2)!=w->physics_space_2d) { return false; }
 Dictionary saved=w->serialize(); Ref<ECSScene> scene; scene.instantiate(); scene->set_entities(saved["entities"]); scene->set_layouts(saved["layouts"]);
 Ref<ECSWorld> copy=scene->instantiate();
 return copy.is_valid() && copy->query(PackedStringArray(),true).size()==3 && copy->query(PackedStringArray()).size()==2;
}
