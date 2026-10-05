// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "age_mesh.h"

class AgeMeshPageSet : public Resource {
    GDCLASS(AgeMeshPageSet, Resource);
    Array pages;
    PackedInt32Array roots;
    Array root_data;
protected:
    static void _bind_methods();
public:
    void set_pages(const Array &p_value) { pages = p_value.duplicate(true); emit_changed(); }
    Array get_pages() const { return pages.duplicate(true); }
    void set_roots(const PackedInt32Array &p_value) { roots = p_value; emit_changed(); }
    PackedInt32Array get_roots() const { return roots; }
    void set_root_data(const Array &p_value) { root_data = p_value.duplicate(); emit_changed(); }
    Array get_root_data() const { return root_data.duplicate(); }
    String get_validation_error() const;
    static Ref<AgeMeshPageSet> build(const Ref<AgeMeshData> &p_data, const String &p_directory, int p_clusters_per_page = 64);
};
