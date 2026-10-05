#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "ecs_ai_value.h"
#include "scene/gui/check_box.h"

bool ECSAnimationEditor::configure_animation(const String &name, double length, bool loop, bool create, String &error) {
	if (scene.is_null() || owner->get_selected_id() < 0 || !Math::is_finite(length) || length < .01 || length > 3600) {
		error = String(U"动画时长必须为 0.01～3600 秒。");
		return false;
	}
	Array entities = scene->get_entities();
	Dictionary definition = Dictionary(Dictionary(entities[owner->get_selected_id()]).get("animation", Dictionary())).duplicate(true);
	Dictionary named = definition.get("states", Dictionary());
	Ref<Animation> base = definition.get("clip", Variant());
	if ((create && (name.is_empty() || named.has(name))) || (!create && !name.is_empty() && !named.has(name))) {
		error = String(U"新动画需要未使用的名称；编辑时需要选择已有动画。");
		return false;
	}
	Ref<Animation> clip = name.is_empty() || create ? base : Ref<Animation>(named[name]);
	if (clip.is_null()) {
		if (!create) { error = String(U"请先创建动画。"); return false; }
		clip.instantiate();
	} else {
		clip = clip->duplicate(true);
	}
	if (create) {
		Ref<ECSWorld> world=scene->instantiate(); if(world.is_null()) { error=String(U"场景校验失败。"); return false; } auto ids=world->query(PackedStringArray(),true);
		PackedInt64Array targets = definition.get("targets", PackedInt64Array());
		if (targets.is_empty()) { targets.resize(clip->get_track_count()); targets.fill(owner->get_selected_id()); }
		if (targets.size() != clip->get_track_count()) { error = String(U"动画轨道目标不一致。"); return false; }
		for (int t = 0; t < clip->get_track_count(); t++) {
			String field = clip->track_get_path(t).get_concatenated_subnames();
			if (clip->track_get_type(t) != Animation::TYPE_VALUE || (!PackedStringArray({"position", "rotation", "scale", "shear", "event"}).has(field) && !field.begins_with("slot:") && !field.begins_with("event:")) || targets[t] < 0 || targets[t] >= entities.size()) {
				error = String(U"此轨道暂不支持从设置姿态建立空白动画，请使用复制动画。"); return false;
			}
			while (clip->track_get_key_count(t)) { clip->track_remove_key(t, clip->track_get_key_count(t) - 1); }
			Variant rest = field.begins_with("slot:") ? world->get_skeleton_slot_value(ids[targets[t]],field) : Dictionary(entities[targets[t]]).get(field, field == "scale" ? Vector3(1, 1, 1) : Vector3());
			if(field.get_slice(":",0)!="event") { clip->track_insert_key(t, 0, rest); }
		}
		definition["targets"] = targets;
	} else {
		for (int t = 0; t < clip->get_track_count(); t++) {
			int count = clip->track_get_key_count(t);
			if (count && clip->track_get_key_time(t, count - 1) > length + .000001) {
				error = String(U"时长短于最后一个关键帧。请先移动或删除超出的关键帧，避免静默裁掉动作。"); return false;
			}
		}
	}
	clip->set_length(length);
	clip->set_loop_mode(loop ? Animation::LOOP_LINEAR : Animation::LOOP_NONE);
	if (!name.is_empty()) { named[name] = clip; }
	String active = definition.get("state", String());
	if (base.is_null() || name.is_empty() || active == name) {
		definition["clip"] = clip;
		if (base.is_null()) { definition["state"] = name; }
		else if (name.is_empty() && !active.is_empty()) { named[active] = clip; }
	}
	definition["states"] = named;
	Array candidate = entities.duplicate(true); Dictionary root = candidate[owner->get_selected_id()]; root["animation"] = definition;
	Ref<ECSScene> check; check.instantiate(); check->set_entities(candidate);
	if (check->instantiate().is_null()) { error = String(U"动画配置校验失败，未修改工程。"); return false; }
	stop_preview();
	commit_animation(definition, create ? String(U"新建空白骨骼动画") : String(U"设置动画时长与循环"), false);
	editing_state = name; refresh_tracks(); loop_start->set_value(0); loop_end->set_value(length * timeline_fps); loop_playback->set_pressed_no_signal(loop); seek(0);
	return true;
}
void ECSAnimationEditor::insert_channel_key(const String &field,const Variant &value) {
    if(!animation_mode || timeline_locked) { return; }
    Dictionary request; request["operation"]="key"; request["revision"]=skeleton_ai_revision; request["entity"]=owner->get_selected_id(); request["animation"]=editing_state; request["time"]=time->get_value(); request["field"]=field; request["value"]=ECSAIValue::encode(value);
    Dictionary reply=skeleton_ai_request(request); if(!bool(reply["ok"])) { feedback->set_text(reply.get("error",String())); }
}
#endif
