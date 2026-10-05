#pragma once
#include "scene/resources/animation.h"
#include "core/templates/hash_set.h"

struct SkeletonTimelineGroup {
	String id;
	String label;
	String icon;
	Vector<int> tracks;
};

struct SkeletonTimelineRow {
	int group = -1;
	int track = -1; // Group summaries deliberately are not editable keys.
};

struct SkeletonTimelineLayout {
	Vector<SkeletonTimelineGroup> groups;
	Vector<SkeletonTimelineRow> rows;

	void build(const Ref<Animation> &clip, const PackedInt64Array &targets, const Array &entities,
			int owner, const HashSet<String> &folded) {
		groups.clear();
		rows.clear();
		if (clip.is_null()) { return; }
		HashMap<String, int> indices;
		for (int track = 0; track < clip->get_track_count(); ++track) {
			String path = clip->track_get_path(track).get_concatenated_subnames();
			int entity = track < targets.size() ? int(targets[track]) : owner;
			String id, label, icon;
			if (path.begins_with("event:")) {
				id = "events"; label = String(U"事件"); icon = "sheet";
			} else if (path.begins_with("slot:")) {
				id = "slot:" + itos(entity) + ":" + path.get_slice(":", 1);
				label = path.get_slice(":", 1).uri_decode(); icon = "slot";
			} else {
				id = "entity:" + itos(entity);
				label = entity >= 0 && entity < entities.size() ? String(Dictionary(entities[entity]).get("name", "Entity")) : itos(entity);
				icon = "bone";
			}
			if (!indices.has(id)) {
				indices[id] = groups.size();
				SkeletonTimelineGroup group;
				group.id = id; group.label = label; group.icon = icon;
				groups.push_back(group);
			}
			groups.write[indices[id]].tracks.push_back(track);
		}
		for (int group = 0; group < groups.size(); ++group) {
			rows.push_back({ group, -1 });
			if (!folded.has(groups[group].id)) {
				for (int track : groups[group].tracks) { rows.push_back({ group, track }); }
			}
		}
	}

	int find_track(int track) const {
		for (int i = 0; i < rows.size(); ++i) { if (rows[i].track == track) { return i; } }
		return -1;
	}

	static String channel_label(const String &path) {
		if (path.begins_with("event:")) { return path.substr(6).uri_decode(); }
		String field = path.begins_with("slot:") ? path.get_slice(":", 2) : path;
		if (field == "position") { return String(U"移动"); }
		if (field == "rotation") { return String(U"旋转"); }
		if (field == "scale") { return String(U"缩放"); }
		if (field == "shear") { return String(U"倾斜"); }
		if (field == "attachment") { return String(U"附件"); }
		if (field == "color") { return String(U"颜色"); }
		if (field == "dark") { return String(U"暗部颜色"); }
		if (field == "z_index") { return String(U"绘制顺序"); }
		return field;
	}
};
