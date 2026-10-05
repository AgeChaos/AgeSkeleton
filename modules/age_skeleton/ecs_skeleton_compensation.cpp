#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "scene/gui/button.h"

namespace {
Transform3D local_pose(const Dictionary &entity) {
	Vector3 shear=entity.get("shear",Vector3());
	Basis skew(Vector3(Math::cos(shear.x),Math::sin(shear.x),0),Vector3(-Math::sin(shear.y),Math::cos(shear.y),0),Vector3(0,0,1));
	return Transform3D((Basis::from_euler(entity.get("rotation", Vector3()))*skew).scaled_local(entity.get("scale", Vector3(1, 1, 1))), entity.get("position", Vector3()));
}
Transform2D planar(const Transform3D &value) {
	return Transform2D(Vector2(value.basis[0][0], value.basis[1][0]), Vector2(value.basis[0][1], value.basis[1][1]), Vector2(value.origin.x, value.origin.y));
}
bool ordered_entities(const Array &entities, Vector<int> &order) {
	Vector<bool> done; done.resize(entities.size()); done.fill(false);
	while (order.size() < entities.size()) {
		int count = order.size();
		for (int i = 0; i < entities.size(); i++) {
			if (done[i]) { continue; }
			int parent = Dictionary(entities[i]).get("parent", -1);
			if (parent < -1 || parent >= entities.size()) { return false; }
			if (parent >= 0 && !done[parent]) { continue; }
			done.write[i] = true; order.push_back(i);
		}
		if (order.size() == count) { return false; }
	}
	return true;
}
// The affine matrix mapping a mesh vertex to world space, including weighted skinning.
bool vertex_matrix(const Array &entities, const Vector<Transform3D> &global, int mesh_index, int vertex, Transform2D &result) {
	Dictionary mesh = Dictionary(entities[mesh_index])["polygon_2d"];
	int rig = mesh.get("skeleton", -1);
	result = planar(global[mesh_index]);
	if (rig < 0) { return true; }
	if (rig >= entities.size()) { return false; }
	Dictionary skeleton = Dictionary(entities[rig]).get("skeleton_2d", Dictionary());
	PackedInt64Array bones = skeleton.get("bones", PackedInt64Array());
	Array bind = skeleton.get("bind_poses", Array());
	PackedInt32Array indices = mesh.get("bones", PackedInt32Array());
	PackedFloat32Array weights = mesh.get("weights", PackedFloat32Array());
	if (indices.size() < (vertex + 1) * 4 || weights.size() != indices.size()) { return false; }
	Transform2D root = planar(global[rig]);
	if (Math::is_zero_approx(root.determinant())) { return false; }
	Transform2D polygon_to_root = root.affine_inverse() * result;
	result = Transform2D(Vector2(), Vector2(), Vector2());
	for (int slot = 0; slot < 4; slot++) {
		float weight = weights[vertex * 4 + slot];
		if (weight == 0) { continue; }
		int index = indices[vertex * 4 + slot];
		if (index < 0 || index >= bones.size() || bones[index] < 0 || bones[index] >= entities.size()) { return false; }
		Transform2D bone = planar(global[bones[index]]);
		if (!bind.is_empty() && index >= bind.size()) { return false; }
		Transform2D rest = bind.is_empty() ? root.affine_inverse() * bone : Transform2D(bind[index]);
		if (Math::is_zero_approx(rest.determinant())) { return false; }
		Transform2D skin = bone * rest.affine_inverse() * polygon_to_root;
		for (int axis = 0; axis < 3; axis++) { result[axis] += skin[axis] * weight; }
	}
	return result.is_finite();
}
}

Array ECSAnimationEditor::compensate_pose(const Array &before, int edited, const String &field, const Vector3 &value, bool bones, bool images, String &error) {
	error = String();
	if (edited < 0 || edited >= before.size() || !value.is_finite() || (field != "position" && field != "rotation" && field != "scale" && field != "shear")) {
		error = String(U"无效的姿态参数。"); return Array();
	}
	Array after = before.duplicate(true);
	// Legacy projects may omit explicit bind poses. Capture their original rest
	// transforms before editing, otherwise rebuilding the preview silently rebinds
	// the mesh to the new pose and only the bone appears to move.
	Ref<ECSWorld> bind_world;
	PackedInt64Array bind_ids;
	for(int i=0;i<before.size();i++) {
		Dictionary original=before[i];
		if(!original.has("skeleton_2d")) { continue; }
		Dictionary rig=original["skeleton_2d"];
		if(!Array(rig.get("bind_poses",Array())).is_empty()) { continue; }
		if(bind_world.is_null()) {
			Ref<ECSScene> rest; rest.instantiate(); rest->set_entities(before);
			bind_world=rest->instantiate(); if(bind_world.is_null()) { error=String(U"骨架绑定数据无效，未修改。"); return Array(); }
			bind_ids=bind_world->query(PackedStringArray(),true);
		}
		Dictionary entry=after[i], definition=entry["skeleton_2d"];
		definition["bind_poses"]=bind_world->get_skeleton_2d(bind_ids[i]).get("bind_poses",Array());
	}
	Dictionary changed = after[edited]; changed[field] = value;
	if (!bones && !images) { return after; }
	Vector<int> order;
	if (!ordered_entities(before, order)) { error = String(U"实体父子层级无效，未修改。"); return Array(); }
	Vector<Transform3D> old_global, new_global;
	old_global.resize(before.size()); new_global.resize(before.size());
	Vector<bool> descendant; descendant.resize(before.size()); descendant.fill(false);
	for (int i : order) {
		Dictionary original = before[i], current = after[i];
		int parent = original.get("parent", -1);
		Transform3D old_parent = parent < 0 ? Transform3D() : old_global[parent];
		Transform3D new_parent = parent < 0 ? Transform3D() : new_global[parent];
		old_global.write[i] = old_parent * local_pose(original);
		descendant.write[i] = parent >= 0 && (parent == edited || descendant[parent]);
		bool preserve = i != edited && descendant[i] && ((bones && original.has("bone_2d")) || (images && original.has("polygon_2d")));
		if (preserve) {
			if (Math::is_zero_approx(new_parent.basis.determinant())) { error = String(U"父项缩放为零，无法补偿；本次修改已取消。"); return Array(); }
			Transform3D local = new_parent.affine_inverse() * old_global[i];
            Vector3 scale = local.basis.get_scale();
            if (Math::is_zero_approx(scale.x * scale.y * scale.z)) { error = String(U"零缩放不能执行补偿。"); return Array(); }
            const Basis &basis=local.basis;
            bool planar_pose=Math::is_zero_approx(basis[2][0]) && Math::is_zero_approx(basis[2][1]) && Math::is_zero_approx(basis[0][2]) && Math::is_zero_approx(basis[1][2]);
            if(planar_pose) {
                Vector2 xaxis(basis[0][0],basis[1][0]),yaxis(basis[0][1],basis[1][1]);
                double angle=xaxis.angle(),skew=Math::wrapf(Math::atan2(-yaxis.x,yaxis.y)-angle,-Math::PI,Math::PI);
                current["rotation"]=Vector3(0,0,angle); current["scale"]=Vector3(xaxis.length(),yaxis.length(),basis[2][2]); current["shear"]=Vector3(0,skew,0);
            } else {
                Vector3 rotation=local.basis.get_euler_normalized();
                if(!Basis::from_euler(rotation).scaled_local(scale).is_equal_approx(local.basis)) { error=String(U"仅支持二维剪切补偿，不能处理混合三维剪切。"); return Array(); }
                current["rotation"]=rotation; current["scale"]=scale; current["shear"]=Vector3();
            }
            current["position"]=local.origin;
            if(!local_pose(current).is_equal_approx(local)) { error=String(U"补偿矩阵无法精确重建。"); return Array(); }
		}
		new_global.write[i] = new_parent * local_pose(current);
	}
	// An active IK solver would overwrite the compensated setup pose on the next frame.
	for (int i = 0; i < before.size(); i++) {
		Dictionary rig=Dictionary(before[i]).get("skeleton_2d",Dictionary());
		Array constraints=rig.get("ik",Array()); bool enabled=false;
		for(const Variant &entry:constraints) { enabled |= bool(Dictionary(entry).get("enabled",true)); }
		if(!enabled) { continue; }
		bool affected=!old_global[i].is_equal_approx(new_global[i]);
		for(int64_t bone:PackedInt64Array(rig.get("bones",PackedInt64Array()))) {
			if(bone>=0 && bone<before.size()) { affected |= !old_global[bone].is_equal_approx(new_global[bone]); }
		}
		if(affected) { error=String(U"该骨架有启用的 IK 约束；请先关闭约束再补偿编辑绑定姿态。"); return Array(); }
	}
	if (images) {
		for (int i = 0; i < before.size(); i++) {
			Dictionary original = before[i];
			if (i == edited || !original.has("polygon_2d")) { continue; }
			Dictionary polygon = original["polygon_2d"];
			PackedVector2Array points = polygon.get("polygon", PackedVector2Array());
			bool changed_points = false;
			for (int vertex = 0; vertex < points.size(); vertex++) {
				Transform2D old_map, new_map;
				if (!vertex_matrix(before, old_global, i, vertex, old_map) || !vertex_matrix(after, new_global, i, vertex, new_map)) {
					error = String(U"蒙皮数据无效，无法补偿图片。"); return Array();
				}
				if (old_map.is_equal_approx(new_map)) { continue; }
				if (Math::is_zero_approx(new_map.determinant())) { error = String(U"蒙皮变换退化，无法保持图片位置；本次修改已取消。"); return Array(); }
				Vector2 next = new_map.affine_inverse().xform(old_map.xform(points[vertex]));
				if (!next.is_finite()) { error = String(U"图片补偿结果无效。"); return Array(); }
				points.set(vertex, next); changed_points = true;
			}
			if (changed_points) { Dictionary current = after[i], mesh = current["polygon_2d"]; mesh["polygon"] = points; }
		}
	}
	return after;
}

Array ECSAnimationEditor::compensated_pose(int entity, const String &field, const Vector3 &value) {
	if (scene.is_null()) { return Array(); }
	String error;
	Array result = compensate_pose(scene->get_entities(), entity, field, value, compensation_tools[0]->is_pressed(), compensation_tools[1]->is_pressed(), error);
	if (!error.is_empty()) { feedback->set_text(error); }
	return result;
}

bool ECSAnimationEditor::run_compensation_self_test() {
	String error;
	Array before = scene->get_entities().duplicate(true);
	Array after = compensate_pose(before, 1, "rotation", Vector3(0, 0, .7), true, true, error);
	if (after.is_empty()) { return false; }
	auto instantiate = [](const Array &entities) { Ref<ECSScene> resource; resource.instantiate(); resource->set_entities(entities); return resource->instantiate(); };
	Ref<ECSWorld> old_world = instantiate(before), new_world = instantiate(after);
	if (old_world.is_null() || new_world.is_null()) { return false; }
	Array legacy=before.duplicate(true); Dictionary legacy_root=legacy[0],legacy_rig=legacy_root["skeleton_2d"]; legacy_rig.erase("bind_poses");
	Ref<ECSWorld> legacy_old=instantiate(legacy); auto legacy_ids=legacy_old->query(PackedStringArray(),true);
	PackedVector2Array legacy_points=legacy_old->get_deformed_polygon_2d(legacy_ids[3]);
	Array legacy_edit=compensate_pose(legacy,1,"rotation",Vector3(0,0,.8),false,false,error);
	if(legacy_edit.is_empty() || Array(Dictionary(Dictionary(legacy_edit[0])["skeleton_2d"])["bind_poses"]).is_empty()) { return false; }
	Ref<ECSWorld> legacy_new=instantiate(legacy_edit); auto legacy_new_ids=legacy_new->query(PackedStringArray(),true);
	if(legacy_new->get_deformed_polygon_2d(legacy_new_ids[3])==legacy_points) { return false; }
	print_line("SKELETON_LEGACY_BIND_POSE_PASS bone_rotation_deforms_image_without_rebinding");
	auto old_ids = old_world->query(PackedStringArray(), true), new_ids = new_world->query(PackedStringArray(), true);
	if (!old_world->get_global_transform(old_ids[2]).is_equal_approx(new_world->get_global_transform(new_ids[2]))) { return false; }
	PackedVector2Array old_points = old_world->get_deformed_polygon_2d(old_ids[3]), new_points = new_world->get_deformed_polygon_2d(new_ids[3]);
	if (old_points.size() != new_points.size()) { return false; }
	for (int i = 0; i < old_points.size(); i++) { if (!old_points[i].is_equal_approx(new_points[i])) { return false; } }
	if (!compensate_pose(before, 1, "scale", Vector3(0, 0, 1), true, true, error).is_empty()) { return false; }
	Array regular = compensate_pose(before, 1, "position", Vector3(10, 20, 0), false, false, error);
	if (regular.is_empty() || Vector3(Dictionary(regular[2]).get("position", Vector3())) != Vector3(Dictionary(before[2]).get("position", Vector3()))) { return false; }
	Array rigid=before.duplicate(true); Dictionary rigid_image=rigid[3]; Dictionary mesh=rigid_image["polygon_2d"]; mesh["skeleton"]=-1; mesh.erase("bones"); mesh.erase("weights"); rigid_image["parent"]=1;
	Array rigid_after=compensate_pose(rigid,1,"rotation",Vector3(0,0,.5),false,true,error);
	if(rigid_after.is_empty()) { return false; }
	Ref<ECSWorld> rigid_old=instantiate(rigid),rigid_new=instantiate(rigid_after);
	if(rigid_old.is_null() || rigid_new.is_null()) { return false; }
	auto rigid_old_ids=rigid_old->query(PackedStringArray(),true),rigid_new_ids=rigid_new->query(PackedStringArray(),true);
	if(!rigid_old->get_global_transform(rigid_old_ids[3]).is_equal_approx(rigid_new->get_global_transform(rigid_new_ids[3]))) { return false; }
	Array skew=before.duplicate(true); Dictionary skew_parent=skew[1]; skew_parent["scale"]=Vector3(2,1,1);
	Array skew_after=compensate_pose(skew,1,"rotation",Vector3(0,0,.7),true,false,error);
	if(skew_after.is_empty()) { return false; }
	Ref<ECSWorld> skew_old=instantiate(skew),skew_new=instantiate(skew_after);
	if(skew_old.is_null() || skew_new.is_null()) { return false; }
	auto so=skew_old->query(PackedStringArray(),true),sn=skew_new->query(PackedStringArray(),true);
	if(!skew_old->get_global_transform(so[2]).is_equal_approx(skew_new->get_global_transform(sn[2]))) { return false; }
	Array constrained=before.duplicate(true); Dictionary root=constrained[0],rig=root["skeleton_2d"],constraint;
	constraint["chain"]=PackedInt32Array({0,1}); constraint["target"]=Vector2(100,100); rig["ik"]=Array({constraint});
	if(!compensate_pose(constrained,1,"rotation",Vector3(0,0,.7),true,false,error).is_empty()) { return false; }
	bool old_mode=animation_mode; int selection=target->get_selected_id();
	set_mode(0); compensation_tools[0]->set_pressed(true); compensation_tools[1]->set_pressed(true);
	bool drag_ok=local_canvas->run_compensated_drag_self_test(scene);
	compensation_tools[0]->set_pressed(false); compensation_tools[1]->set_pressed(false);
	set_mode(old_mode?1:0); select_target(selection);
	if(!drag_ok) { ERR_PRINT("Compensation drag regression failed"); return false; }
	print_line("SKELETON_COMPENSATION_PASS child_world_pose weighted_vertices rigid_images shear_preserved ik_guard singular_rejected disabled_inheritance");
	return true;
}
#endif
