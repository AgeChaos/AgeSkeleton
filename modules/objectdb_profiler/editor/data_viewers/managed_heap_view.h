#pragma once
#include "snapshot_view.h"
#include "core/templates/hash_set.h"
class LineEdit;
class Label;
class RichTextLabel;
class SnapshotManagedHeapView : public SnapshotView {
    GDCLASS(SnapshotManagedHeapView, SnapshotView);
    Tree *objects_tree = nullptr;
    LineEdit *search = nullptr;
    Label *summary = nullptr;
    RichTextLabel *details = nullptr;
    Dictionary heap;
    HashMap<int64_t, Dictionary> objects;
    HashMap<int64_t, Array> incoming;
    HashMap<int64_t, Array> root_labels;
    HashSet<int64_t> reachable, survivors;
    bool comparable = false;
    void populate(const String &filter);
    void selected();
    void show_object(int64_t id);
    void link_clicked(const Variant &id);
public:
    SnapshotManagedHeapView();
    bool validate_fixture(const Dictionary &first, const Dictionary &second);
    void clear_snapshot() override;
    void show_snapshot(GameStateSnapshot *data, GameStateSnapshot *diff = nullptr) override;
};
