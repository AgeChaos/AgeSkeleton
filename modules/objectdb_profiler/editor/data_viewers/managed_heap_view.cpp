#include "managed_heap_view.h"
#include "core/object/callable_mp.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tree.h"
#include <algorithm>
#include <vector>

SnapshotManagedHeapView::SnapshotManagedHeapView() {
    set_name(String(U"C# 引用与保留根"));
    auto *box = memnew(VBoxContainer);
    add_child(box); box->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
    summary = memnew(Label);
    summary->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    box->add_child(summary);
    search = memnew(LineEdit);
    search->set_placeholder(String(U"搜索类型或对象 ID；@rooted 有根，@unrooted 无已知根，@survivor 跨快照存活"));
    box->add_child(search);
    search->connect("text_changed", callable_mp(this, &SnapshotManagedHeapView::populate));
    auto *split = memnew(VSplitContainer);
    split->set_v_size_flags(SIZE_EXPAND_FILL); box->add_child(split);
    objects_tree = memnew(Tree);
    objects_tree->set_v_size_flags(SIZE_EXPAND_FILL);
    objects_tree->set_columns(4); objects_tree->set_hide_root(true);
    objects_tree->set_column_titles_visible(true);
    objects_tree->set_column_title(0, String(U"托管类型"));
    objects_tree->set_column_title(1, "ID");
    objects_tree->set_column_title(2, String(U"自身字节"));
    objects_tree->set_column_title(3, String(U"保留状态"));
    objects_tree->set_select_mode(Tree::SELECT_ROW);
    split->add_child(objects_tree);
    objects_tree->connect("item_selected", callable_mp(this, &SnapshotManagedHeapView::selected));
    details = memnew(RichTextLabel);
    details->set_v_size_flags(SIZE_EXPAND_FILL);
    details->set_selection_enabled(true);
    split->add_child(details);
    details->connect("meta_clicked", callable_mp(this, &SnapshotManagedHeapView::link_clicked));
    summary->set_text(String(U"运行游戏后创建快照，查看 C# 强引用和保留根。"));
}
void SnapshotManagedHeapView::clear_snapshot() {
    snapshot_data = diff_data = nullptr;
    objects.clear(); incoming.clear(); root_labels.clear(); reachable.clear(); survivors.clear(); heap.clear();
    comparable = false;
    objects_tree->clear(); details->clear();
    summary->set_text(String(U"运行游戏后创建快照，查看 C# 强引用和保留根。"));
}
void SnapshotManagedHeapView::show_snapshot(GameStateSnapshot *data, GameStateSnapshot *diff) {
    clear_snapshot(); snapshot_data = data; diff_data = diff;
    heap = data->snapshot_context.get("leanclr_heap", Dictionary());
    Array rows = heap.get("objects", Array());
    int64_t bytes = 0;
    for (int i = 0; i < rows.size(); ++i) {
        Dictionary row = rows[i]; int64_t id = row["id"];
        objects[id] = row; bytes += int64_t(row["bytes"]);
        Array edges = row["references"];
        for (int n = 0; n < edges.size(); ++n) {
            Dictionary edge = edges[n], reverse;
            reverse["from"] = id; reverse["label"] = edge["label"];
            incoming[int64_t(edge["to"])].push_back(reverse);
        }
    }
    Array roots = heap.get("roots", Array());
    std::vector<int64_t> queue;
    for (int i = 0; i < roots.size(); ++i) {
        Dictionary root = roots[i]; int64_t id = root["to"];
        root_labels[id].push_back(root["label"]);
        if (!reachable.has(id)) { reachable.insert(id); queue.push_back(id); }
    }
    for (size_t i = 0; i < queue.size(); ++i) {
        auto *row = objects.getptr(queue[i]); if (!row) continue;
        Array edges = (*row)["references"];
        for (int n = 0; n < edges.size(); ++n) {
            Dictionary edge = edges[n]; int64_t to = edge["to"];
            if (!reachable.has(to)) { reachable.insert(to); queue.push_back(to); }
        }
    }
    int64_t old_bytes = 0, old_count = 0;
    if (diff) {
        Dictionary old = diff->snapshot_context.get("leanclr_heap", Dictionary());
        comparable = bool(heap.get("complete", false)) && bool(old.get("complete", false)) && heap.has("session") && heap["session"] == old.get("session", "");
        if (comparable) {
            Array old_rows = old.get("objects", Array()); old_count = old_rows.size();
            for (int i = 0; i < old_rows.size(); ++i) {
                Dictionary row = old_rows[i]; int64_t id = row["id"];
                old_bytes += int64_t(row["bytes"]);
                if (objects.has(id)) survivors.insert(id);
            }
        }
    }
    String text = vformat(String(U"对象 %d，自身字节 %d，有已知根 %d。状态：%s。"), objects.size(), bytes, reachable.size(), String(heap.get("status", "no managed snapshot")));
    if (comparable) text += vformat(String(U" 相对比较快照：对象 %+d，字节 %+d，共同存活 %d。"), int64_t(objects.size()) - old_count, bytes - old_bytes, survivors.size());
    else if (diff) text += String(U" 不同运行会话或不完整快照，不作对象存活比较。");
    text += String(U"\n有根不等于泄漏；无根可能待 GC。栈/线程静态根为保守扫描；弱句柄不是强引用。快照未强制 GC。");
    summary->set_text(text);
    populate(search->get_text());
}
void SnapshotManagedHeapView::populate(const String &filter) {
    objects_tree->clear();
    auto *root = objects_tree->create_item();
    std::vector<int64_t> ids;
    const String query = filter.replace("@unrooted", "").replace("@rooted", "").replace("@survivor", "").strip_edges();
    for (const auto &entry : objects) {
        bool rooted = reachable.has(entry.key);
        if (filter.contains("@rooted") && !rooted) continue;
        if (filter.contains("@unrooted") && rooted) continue;
        if (filter.contains("@survivor") && !survivors.has(entry.key)) continue;
        if (!query.is_empty() && String(entry.value["type"]).findn(query) < 0 && itos(entry.key).find(query) < 0) continue;
        ids.push_back(entry.key);
    }
    std::sort(ids.begin(), ids.end(), [&](int64_t a, int64_t b) { return int64_t(objects[a]["bytes"]) > int64_t(objects[b]["bytes"]); });
    for (size_t i = 0; i < ids.size() && i < 3000; ++i) {
        int64_t id = ids[i]; const auto &row = objects[id];
        auto *item = objects_tree->create_item(root);
        item->set_text(0, row["type"]); item->set_metadata(0, id);
        item->set_text(1, itos(id)); item->set_text(2, itos(row["bytes"]));
        item->set_text(3, (reachable.has(id) ? String(U"有根") : String(U"无已知根")) + (survivors.has(id) ? String(U" / 跨快照存活") : ""));
    }
    if (ids.size() > 3000) {
        auto *item = objects_tree->create_item(root); item->set_text(0, String(U"仅显示最大的 3000 个匹配对象，请缩小搜索范围。")); item->set_selectable(0, false);
    }
}
void SnapshotManagedHeapView::selected() {
    auto *item = objects_tree->get_selected();
    if (item && item->get_metadata(0).get_type() == Variant::INT) show_object(item->get_metadata(0));
}
void SnapshotManagedHeapView::link_clicked(const Variant &id) { show_object(id); }
void SnapshotManagedHeapView::show_object(int64_t id) {
    auto *object = objects.getptr(id); if (!object) return;
    details->clear();
    auto link = [&](int64_t other) {
        details->push_meta(other);
        details->add_text("#" + itos(other) + " " + (objects.has(other) ? String(objects[other]["type"]) : String(U"未采集")));
        details->pop();
    };
    link(id); details->add_text("\n" + itos((*object)["bytes"]) + String(U" 字节（自身大小，非独占保留大小）\n\n引用出去：\n"));
    Array edges = (*object)["references"];
    for (int i = 0; i < edges.size() && i < 200; ++i) {
        Dictionary e = edges[i]; details->add_text(String(e["label"]) + " → "); link(e["to"]); details->add_text("\n");
    }
    if (edges.size() > 200) details->add_text(String(U"仅显示前 200 条出边。\n"));
    details->add_text(String(U"\n被谁引用：\n"));
    Array back = (incoming.has(id) ? incoming[id] : Array());
    for (int i = 0; i < back.size() && i < 200; ++i) {
        Dictionary e = back[i]; link(e["from"]); details->add_text(" — " + String(e["label"]) + "\n");
    }
    if (back.size() > 200) details->add_text(String(U"仅显示前 200 条入边。\n"));
    details->add_text(String(U"\n保留根路径（每个到达的根对象取一条最短路径）：\n"));
    std::vector<int64_t> queue{id}; HashSet<int64_t> seen; seen.insert(id);
    HashMap<int64_t, int64_t> next;
    HashMap<int64_t, String> labels;
    int paths = 0; bool limited = false;
    for (size_t cursor = 0; cursor < queue.size(); ++cursor) {
        if (cursor >= 10000 || paths >= 32) { limited = true; break; }
        int64_t current = queue[cursor];
        if (root_labels.has(current)) {
            Array roots = root_labels[current];
            for (int r = 0; r < roots.size() && paths < 32; ++r) {
                ++paths; details->add_text("\n" + String(roots[r]) + "\n");
                int64_t step = current; int depth = 0;
                while (true) {
                    link(step);
                    if (step == id) break;
                    if (++depth >= 128) { details->add_text(String(U" … 路径显示达到 128 层，请沿入边继续查看")); limited = true; break; }
                    details->add_text(" — " + labels[step] + " →\n"); step = next[step];
                }
                details->add_text("\n");
            }
            if (roots.size() > 32) limited = true;
        }
        Array parents = (incoming.has(current) ? incoming[current] : Array());
        for (int n = 0; n < parents.size(); ++n) {
            Dictionary edge = parents[n]; int64_t parent = edge["from"];
            if (!seen.has(parent)) { seen.insert(parent); next[parent] = current; labels[parent] = edge["label"]; queue.push_back(parent); }
        }
    }
    if (!paths) details->add_text(String(U"未找到已采集根路径；可能待回收，或采集不完整，不能据此判定泄漏。\n"));
    if (limited) details->add_text(String(U"路径显示已达上限（32 条、10000 个祖先或 128 层），可点击引用继续追踪。\n"));
    if (survivors.has(id)) details->add_text(String(U"\n此对象在两次快照都存在。检查根路径是否符合预期；持续保留不自动等于泄漏。\n"));
}

// Exercises the actual editor model against a known runtime graph, without synthesizing addresses.
bool SnapshotManagedHeapView::validate_fixture(const Dictionary &first, const Dictionary &second) {
    Ref<GameStateSnapshot> a; a.instantiate(); a->snapshot_context["leanclr_heap"] = first;
    Ref<GameStateSnapshot> b; b.instantiate(); b->snapshot_context["leanclr_heap"] = second;
    show_snapshot(b.ptr(), a.ptr());
    bool ok = comparable && !survivors.is_empty();
    int64_t leaf = 0, listener = 0;
    for (const auto &entry : objects) {
        String type = entry.value["type"];
        if (type.ends_with(": Profiling.Leaf")) leaf = entry.key;
        if (type.ends_with(": Profiling.Listener")) listener = entry.key;
    }
    ok &= leaf != 0 && listener != 0;
    if (leaf) {
        show_object(leaf);
        String text = details->get_parsed_text();
        ok &= text.contains("static ") && text.contains("Profiling.Game");
    }
    if (listener) {
        link_clicked(listener);
        ok &= details->get_parsed_text().contains("Game.Signal");
    }
    populate("@survivor Profiling.Leaf");
    ok &= objects_tree->get_root()->get_child_count() == 2;
    Dictionary other = second.duplicate(true); other["session"] = "other-process";
    b->snapshot_context["leanclr_heap"] = other; show_snapshot(b.ptr(), a.ptr());
    ok &= !comparable && survivors.is_empty();
    clear_snapshot();
    ok &= objects.is_empty() && details->get_parsed_text().is_empty();
    return ok;
}
