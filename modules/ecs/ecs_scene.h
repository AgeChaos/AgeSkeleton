// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "ecs_world.h"

#include "core/io/resource.h"
#include "scene/resources/3d/navigation_mesh_source_geometry_data_3d.h"
#include "scene/resources/environment.h"

// Entity references in files are local array indices, never runtime handles.
class ECSScene : public Resource {
	GDCLASS(ECSScene, Resource);
	Array entities;
	Ref<NavigationMesh> pending_navigation_mesh;
	int64_t navigation_bake_id = 0;
	bool navigation_bake_canceled = false;
	Ref<NavigationMesh> prepare_navigation(int p_region, const PackedInt32Array &p_sources, Ref<NavigationMeshSourceGeometryData3D> &r_geometry) const;
	Dictionary layouts;
	Dictionary custom_schemas;
	Transform3D camera_transform = Transform3D(Basis(), Vector3(0, 10, 10)).looking_at(Vector3(), Vector3(0, 1, 0));
	float camera_fov = 60;
	Ref<Environment> environment;

protected:
	static void _bind_methods();

public:
	void set_entities(const Array &p_entities) {
		entities = p_entities.duplicate(true);
		emit_changed();
	}
	Array get_entities() const { return entities.duplicate(true); }
	void set_custom_schemas(const Dictionary &value) {
		custom_schemas = value.duplicate(true);
		emit_changed();
	}
	Dictionary get_custom_schemas() const { return custom_schemas.duplicate(true); }
	void set_layouts(const Dictionary &p_layouts) {
		layouts = p_layouts.duplicate(true);
		emit_changed();
	}
	Dictionary get_layouts() const { return layouts.duplicate(true); }
	void set_camera_transform(const Transform3D &p_transform) {
		ERR_FAIL_COND(!p_transform.is_finite());
		camera_transform = p_transform;
		emit_changed();
	}
	Transform3D get_camera_transform() const { return camera_transform; }
	void set_camera_fov(float p_fov) {
		ERR_FAIL_COND(!Math::is_finite(p_fov) || p_fov < 1 || p_fov > 179);
		camera_fov = p_fov;
		emit_changed();
	}
	float get_camera_fov() const { return camera_fov; }
	void set_environment(const Ref<Environment> &p_environment) {
		environment = p_environment;
		emit_changed();
	}
	Ref<Environment> get_environment() const { return environment; }
	Ref<ECSWorld> instantiate() const;
	Ref<NavigationMesh> bake_navigation(int p_region, const PackedInt32Array &p_sources = PackedInt32Array()) const;
	int64_t start_navigation_bake(int p_region, const PackedInt32Array &p_sources = PackedInt32Array());
	Dictionary get_navigation_bake(int64_t p_request) const;
	bool cancel_navigation_bake(int64_t p_request);
	bool capture(const Ref<ECSWorld> &p_world);
};
